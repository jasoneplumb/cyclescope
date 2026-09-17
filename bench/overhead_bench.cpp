// Per-event cost of the two tracing paths, measured as the difference
// against an untraced twin of the same work. This translation unit is
// compiled WITHOUT instrumentation; the instrumented fib twin lives in
// overhead_fib.cpp.
//
// Usage: overhead_bench [scope_iterations] [fib_n]

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cyclescope/trace.hpp>

extern "C" long cyclescope_bench_fib_instrumented(int n);

namespace {

using bench_clock = std::chrono::steady_clock;

double seconds_since(bench_clock::time_point start) {
  return std::chrono::duration<double>(bench_clock::now() - start).count();
}

__attribute__((noinline)) long fib_plain(int n) {
  return n < 2 ? n : fib_plain(n - 1) + fib_plain(n - 2);
}

// Total calls made by the recursive fib: 2 * fib(n + 1) - 1.
long fib_call_count(int n) {
  long a = 0;
  long b = 1;
  for (int i = 0; i < n + 1; ++i) {
    const long next = a + b;
    a = b;
    b = next;
  }
  return 2 * b - 1;
}

void measure_scope_macro(std::int64_t iterations) {
  auto& collector = cyclescope::collector::instance();
  collector.clear();
  collector.set_capacity_per_thread(
      static_cast<std::size_t>(iterations) + 1024);

  const auto baseline_start = bench_clock::now();
  for (std::int64_t i = 0; i < iterations; ++i) {
    asm volatile("");
  }
  const double baseline = seconds_since(baseline_start);

  const auto traced_start = bench_clock::now();
  for (std::int64_t i = 0; i < iterations; ++i) {
    CYCLESCOPE_SCOPE("overhead");
  }
  const double traced = seconds_since(traced_start);

  std::printf("scope macro       %8.1f ns/event  (%lld events, %.3f s)\n",
              (traced - baseline) / static_cast<double>(iterations) * 1e9,
              static_cast<long long>(iterations), traced);
  collector.clear();
  collector.set_capacity_per_thread(std::size_t{1} << 20);
}

void measure_instrumentation(int fib_n) {
  auto& collector = cyclescope::collector::instance();
  collector.clear();
  const long calls = fib_call_count(fib_n);
  collector.set_capacity_per_thread(static_cast<std::size_t>(calls) + 1024);

  const auto plain_start = bench_clock::now();
  const long plain_result = fib_plain(fib_n);
  const double plain = seconds_since(plain_start);

  const auto traced_start = bench_clock::now();
  const long traced_result = cyclescope_bench_fib_instrumented(fib_n);
  const double traced = seconds_since(traced_start);

  if (plain_result != traced_result) {
    std::fprintf(stderr, "instrumented fib diverged\n");
    std::exit(1);
  }
  std::printf(
      "instrumentation   %8.1f ns/call   (%ld calls, plain %.3f s, traced "
      "%.3f s)\n",
      (traced - plain) / static_cast<double>(calls) * 1e9, calls, plain,
      traced);
  collector.clear();
  collector.set_capacity_per_thread(std::size_t{1} << 20);
}

}  // namespace

int main(int argc, char** argv) {
  const std::int64_t iterations =
      argc > 1 ? std::strtoll(argv[1], nullptr, 10) : 1000000;
  const int fib_n = argc > 2 ? std::atoi(argv[2]) : 26;
  if (iterations <= 0 || fib_n < 1 || fib_n > 40) {
    std::fprintf(stderr,
                 "usage: overhead_bench [iterations > 0] [1 <= fib_n <= 40]\n");
    return 1;
  }
  std::printf("overhead_bench: %lld scope iterations, fib(%d)\n",
              static_cast<long long>(iterations), fib_n);
  measure_scope_macro(iterations);
  measure_instrumentation(fib_n);
  return 0;
}
