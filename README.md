# cyclescope

[![CI](https://github.com/jasoneplumb/cyclescope/actions/workflows/ci.yml/badge.svg?branch=mainline)](https://github.com/jasoneplumb/cyclescope/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

A lightweight C++20 scope and function tracing library. Threads record complete events into their own buffers; one flush call writes a trace that loads directly in the [Perfetto UI](https://ui.perfetto.dev) or chrome://tracing.

## Modules

- `cyclescope/trace.hpp`: the core; per-thread bounded buffers, a Perfetto compatible trace-event JSON emitter, and the `CYCLESCOPE_SCOPE()` / `CYCLESCOPE_SCOPE_FUNC()` RAII macros
- `cyclescope::instrument`: function-level tracing for translation units compiled with `-finstrument-functions`, with lazy symbol resolution and demangling (not available with MSVC)
- `cyclescope/log.hpp`: editor-clickable `path(line): message` diagnostics with compile-time disables

## Quick start

```cmake
include(FetchContent)
FetchContent_Declare(cyclescope
  GIT_REPOSITORY https://github.com/jasoneplumb/cyclescope.git
  GIT_TAG mainline
)
FetchContent_MakeAvailable(cyclescope)
target_link_libraries(app PRIVATE cyclescope::cyclescope)
```

```cpp
#include <cyclescope/trace.hpp>

void render_frame() {
  CYCLESCOPE_SCOPE_FUNC();
  {
    CYCLESCOPE_SCOPE("shadow pass");
    // ...
  }
}

int main() {
  render_frame();
  cyclescope::collector::instance().write_json("app.trace.json");
}
```

To trace every function call in selected translation units instead, compile them with `-finstrument-functions` and link `cyclescope::instrument`; see `examples/demo.cpp`, which writes a trace of a recursive workload ready to drop into Perfetto.

## Building and testing

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Tests run under ThreadSanitizer with `-DCYCLESCOPE_SANITIZE=thread`; CI covers macOS and Linux plus a TSan job. `bench/overhead_bench` measures the per-event cost of both tracing paths against untraced twins of the same work.

## Design

Trade-off rationale, the suppression and teardown story, and measured overhead are in [docs/design.md](docs/design.md).

## License

[MIT](LICENSE)
