// Compiler-instrumentation backend: implements the hooks that
// -finstrument-functions inserts at every function entry and exit, and
// records each call as a complete event in the cyclescope collector.
//
// This translation unit must be compiled WITHOUT -finstrument-functions
// (the build system guarantees it); the attribute on each hook is a second
// line of defense. Usage: compile the code to trace with
// -finstrument-functions and link cyclescope::instrument.
//
// Names resolve lazily: the first return from a given function looks it up
// with dladdr, demangles it when possible, and interns the string for the
// process lifetime (the collector stores name pointers, not copies).
// Symbols outside the dynamic symbol table fall back to their address; on
// Linux, executables should enable exports (CMake ENABLE_EXPORTS) so
// dladdr can see them.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cyclescope/trace.hpp>
#include <mutex>
#include <unordered_map>

#if !defined(_WIN32)
#include <dlfcn.h>
#endif
#if defined(__has_include)
#if __has_include(<cxxabi.h>)
#include <cxxabi.h>
#define CYCLESCOPE_HAS_CXXABI 1
#endif
#endif

namespace {

constexpr int kMaxDepth = 512;

struct frame {
  void* fn;
  std::uint64_t start_ns;
};

// Trivially destructible on purpose: hooks can run during TLS teardown,
// and POD thread_local storage stays readable there.
struct frame_stack {
  int depth = 0;
  int beyond = 0;  // Calls deeper than kMaxDepth, tracked but not recorded.
};

thread_local frame_stack tls_stack;
thread_local frame tls_frames[kMaxDepth];

const char* interned_name(void* fn) {
  // Deliberately immortal, mutex included: exit hooks can fire during
  // process teardown, after function-local statics with destructors are
  // gone, and locking a destroyed mutex throws from a noexcept context.
  static auto* mutex = new std::mutex();
  static auto* names = new std::unordered_map<void*, const char*>();

  std::lock_guard guard(*mutex);
  if (const auto it = names->find(fn); it != names->end()) {
    return it->second;
  }

  const char* resolved = nullptr;
#if !defined(_WIN32)
  Dl_info info;
  if (dladdr(fn, &info) != 0 && info.dli_sname != nullptr) {
#if defined(CYCLESCOPE_HAS_CXXABI)
    int status = -1;
    char* demangled =
        abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
    resolved = (status == 0 && demangled != nullptr)
                   ? demangled
                   : ::strdup(info.dli_sname);
#else
    resolved = ::strdup(info.dli_sname);
#endif
  }
#endif
  if (resolved == nullptr) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "fn@%p", fn);
    resolved = ::strdup(buffer);
  }
  names->emplace(fn, resolved);
  return resolved;
}

}  // namespace

extern "C" {

// Both hooks stand down while the suppress flag is set: it covers hook
// reentrancy AND collector internals. Without the latter, an instrumented
// copy of an inline collector function (the linker may pick one from an
// instrumented translation unit) fires a hook while this thread already
// holds a collector mutex, and record() self-deadlocks.
__attribute__((no_instrument_function)) void __cyg_profile_func_enter(
    void* fn, void* /*call_site*/) {
  if (cyclescope::detail::tls_suppress) {
    return;
  }
  cyclescope::detail::suppress_scope suppress;
  frame_stack& stack = tls_stack;
  if (stack.beyond > 0 || stack.depth == kMaxDepth) {
    ++stack.beyond;
  } else {
    tls_frames[stack.depth] = {fn, cyclescope::trace_now_ns()};
    ++stack.depth;
  }
}

__attribute__((no_instrument_function)) void __cyg_profile_func_exit(
    void* fn, void* /*call_site*/) {
  if (cyclescope::detail::tls_suppress) {
    return;
  }
  cyclescope::detail::suppress_scope suppress;
  frame_stack& stack = tls_stack;
  if (stack.beyond > 0) {
    --stack.beyond;
  } else if (stack.depth > 0) {
    --stack.depth;
    const frame entered = tls_frames[stack.depth];
    // Longjmp or exceptions can skip exits; only record matched frames.
    if (entered.fn == fn) {
      cyclescope::collector::instance().record(
          interned_name(fn), entered.start_ns,
          cyclescope::trace_now_ns() - entered.start_ns);
    }
  }
}

}  // extern "C"
