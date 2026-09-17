// Instrumented demo: every function call in this file becomes a trace
// event. Build is compiled with -finstrument-functions and linked against
// cyclescope::instrument; running it writes demo.trace.json, which loads
// in the Perfetto UI (ui.perfetto.dev) or chrome://tracing.

#include <cstdio>
#include <cyclescope/trace.hpp>

namespace {

__attribute__((noinline)) long fib(int n) {
  return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

__attribute__((noinline)) long work(void) {
  long total = 0;
  for (int i = 0; i < 18; ++i) {
    total += fib(i);
  }
  return total;
}

}  // namespace

int main() {
  const long total = work();
  std::uint64_t dropped = 0;
  const bool ok =
      cyclescope::collector::instance().write_json("demo.trace.json",
                                                   &dropped);
  std::printf("fib total %ld; trace %s, %llu dropped\n", total,
              ok ? "written to demo.trace.json" : "FAILED",
              static_cast<unsigned long long>(dropped));
  return ok ? 0 : 1;
}
