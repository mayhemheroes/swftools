/*
 * swftools/mayhem/asan_options.c — ASan runtime option overrides.
 *
 * LeakSanitizer (LSan) is enabled by default when ASan is built with
 * -fsanitize=address on Linux. LSan works by ptrace-attaching to its own
 * threads at process exit to scan for leaks. Mayhem's coverage-collection mode
 * already runs the target under ptrace, and a Linux process can have only ONE
 * tracer. LSan's attach fails → it calls _exit(-1) before any edges are
 * recorded → 0-edge "Run Failed" even when the code path is reachable.
 *
 * Fix: bake detect_leaks=0 via __asan_default_options so LSan is compiled in
 * but never activated at runtime. The strong symbol (no __attribute__((weak)))
 * wins over the ASan runtime's own weak copy, so it cannot be overridden even
 * if a sanitized shared library is pulled in via --whole-archive.
 *
 * ASan + UBSan remain fully active for the bugs that matter (heap overflows,
 * use-after-free, undefined behaviour). Leak detection is not useful during
 * short-iteration fuzzing — leaks are not crashes and iterative targets leak
 * by design.
 */
const char *__asan_default_options(void)
{
    return "detect_leaks=0";
}
