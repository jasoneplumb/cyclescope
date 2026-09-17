// Compiled WITH -finstrument-functions (see tests/CMakeLists.txt); the
// hooks in cyclescope::instrument trace every call made here. extern "C"
// keeps the symbol names stable for the assertions.

#include <cyclescope/trace.hpp>

extern "C" {

// Regression shape: an instrumented translation unit instantiates the
// inline collector functions, the linker may select those copies
// program-wide, and a hook firing inside them must stand down instead of
// re-entering a lock this thread holds.
__attribute__((noinline)) int cyclescope_fixture_flush(const char* path) {
  return cyclescope::collector::instance().write_json(path) ? 1 : 0;
}

__attribute__((noinline)) void cyclescope_fixture_leaf(void) {
  asm volatile("");  // Keeps the call from being optimized away.
}

__attribute__((noinline)) void cyclescope_fixture_parent(void) {
  for (int i = 0; i < 3; ++i) {
    cyclescope_fixture_leaf();
  }
}

}  // extern "C"
