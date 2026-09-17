# cyclescope design notes

cyclescope is a small C++20 scope and function tracing library. Threads
record complete events into per-thread buffers; the collector writes chrome
trace-event JSON that the Perfetto UI (ui.perfetto.dev) and chrome://tracing
load directly. Two front ends feed the same core: an explicit RAII scope
macro, and a compiler-instrumentation backend that traces every function
call in translation units built with -finstrument-functions.

## Core: per-thread buffers behind one collector

Each recording thread owns a buffer guarded by its own mutex, uncontended
except while a flush snapshots it, so recording threads never contend with
each other. Buffers are bounded (default 2^20 events per thread) and
overflow drops events with a count that `write_json` reports; a tracing
library that silently grows without bound or silently loses data is worse
than one that tells you which happened.

The registry keeps a buffer alive after its thread exits so the events
still reach the trace; `clear()` prunes dead buffers, and thread ids come
from a monotonic 64-bit counter so pruning never recycles them. `record()`
is `noexcept`: it is called from destructors, and an allocation failure
counts as a dropped event instead of terminating the program.

`write_json` snapshots the registry and every buffer first, then performs
all file I/O with no lock held, so a slow disk cannot stall recording
threads or thread registration. The JSON footer is written whenever the
header succeeded, keeping the on-disk file syntactically complete even
after a mid-write failure.

Event names are `const char*` with required static lifetime (string
literals, `__func__`, or interned strings). Storing pointers rather than
copies keeps the record path at one small struct append.

## The suppression flag: tracing must never trace itself

The deepest design constraint comes from how -finstrument-functions
interacts with inline functions and the one-definition rule. Every inline
collector function instantiated in an instrumented translation unit exists
there as an instrumented copy, and the linker may select that copy for the
whole program. Two failure modes follow:

- A hook fires inside `write_json` while the thread holds a buffer mutex;
  the hook calls `record()`, which relocks the same mutex. Self-deadlock.
- In unoptimized builds, an out-of-line instrumented copy of the guard
  object's own constructor fires a hook before the guard is set, and the
  hook constructs another guard. Unbounded recursion.

The defense is a thread-local suppression flag with three properties:
`record()`, `write_json()`, and `clear()` set it for their duration, both
hooks check it first and stand down, and the guard's constructor and
destructor are forced inline and marked non-instrumentable so no callable
copy of them can ever exist. A regression test compiles a fixture with
instrumentation and flushes from inside it, the exact deadlock shape,
bounded by a test timeout so a regression fails instead of hanging.

Teardown is the other lifecycle hazard: hooks and traced destructors can
run after static destruction has begun, so the collector singleton and the
name-interning table and mutex are deliberately immortal. The operating
system reclaims them at process exit; nothing a late hook touches is ever
a destroyed object. A trivially destructible thread-local flag likewise
marks TLS teardown, after which late events on that thread drop safely.

## Instrumentation backend

The hooks live in a separately compiled, uninstrumented static library.
Per-thread state is a fixed-depth (512) POD frame stack: no allocation
inside hooks, trivially destructible, safe during TLS teardown. Calls
deeper than the cap are counted and skipped, and an exit whose function
pointer does not match the frame it would close (longjmp, exceptions) is
discarded rather than miscounted.

Names resolve lazily: the first return from a given function looks it up
with dladdr, demangles when possible, and interns the string for the
process lifetime, so the steady-state cost per event is a mutex-guarded
hash lookup rather than symbol resolution. Symbols outside the dynamic
symbol table fall back to their address; executables on Linux should set
ENABLE_EXPORTS so dladdr can see their symbols. MSVC has no
-finstrument-functions, so the backend is not built there; the scope macro
front end remains fully portable.

## Emitter choice

Chrome trace-event JSON with `ph:"X"` complete events is the smallest
format that a first-class interactive viewer consumes with zero tooling on
our side: one JSON file, drag it into Perfetto. Timestamps and durations
are microseconds with nanosecond-resolution inputs. The format costs more
bytes per event than a binary protocol, which is acceptable at the event
rates a scope tracer produces; a binary emitter can be added behind the
same collector if a use case demands it.

## Measured overhead

One machine, one data point: arm64 macOS (8 logical cores), Apple clang,
Release, C++20. Single runs of `bench/overhead_bench`, which measures each
path against an untraced twin of the same work and refuses to report if
the instrumented computation diverges.

| path                        | cost per event | workload                          |
| --------------------------- | -------------: | --------------------------------- |
| scope macro (`CYCLESCOPE_SCOPE`) | 136 ns    | 1,000,000 scopes vs empty loop    |
| compiler instrumentation    |          86 ns | fib(26), 635,621 calls vs plain   |

Both paths land in the same order of magnitude: roughly a hundred
nanoseconds per event, dominated by the timestamp read, the uncontended
buffer mutex, and (for instrumentation) the interned-name lookup. That
budget suits functions of microseconds and up; instrumenting a
few-nanosecond leaf function multiplies its cost by an order of magnitude,
which is visible in the fib workload itself (traced runtime grows from
under a millisecond to tens of milliseconds). The practical guidance
follows directly: instrument coarse-grained code or selected translation
units, and use the scope macro where call-level granularity is too fine.

Reproduce with:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/bench/overhead_bench
./build/examples/cyclescope_demo   # writes demo.trace.json for Perfetto
```

## Limits, stated plainly

- Names must have static lifetime; dynamic names need interning by the
  caller.
- pid is fixed at 1; single-process traces are the target.
- Buffers of exited threads persist until `clear()`; long-running programs
  with thread churn should clear after each flush.
- Events recorded on a thread after its TLS teardown, or beyond a buffer's
  capacity, are dropped and counted, never silently lost.
