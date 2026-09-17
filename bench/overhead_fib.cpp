// Compiled WITH -finstrument-functions: the instrumented twin of the
// benchmark's plain fib, so the measured difference is the hook cost.

extern "C" {

__attribute__((noinline)) long cyclescope_bench_fib_instrumented(int n) {
  return n < 2 ? n
               : cyclescope_bench_fib_instrumented(n - 1) +
                     cyclescope_bench_fib_instrumented(n - 2);
}

}  // extern "C"
