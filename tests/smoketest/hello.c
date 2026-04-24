/*
 * hello.c — Phase 0 / 1 smoke test for the DOSKUTSU toolchain.
 *
 * Minimal DJGPP program: prints a known string and exits 0. Exercises
 * the full DJGPP + CWSDPMI + DOSBox-X pipeline without pulling in SDL.
 *
 * Expected stdout (exact byte match, checked by tests/run-smoke.sh):
 *
 *     DOSKUTSU smoketest: hello from DJGPP under DPMI
 *
 * Compiled via `make hello`; run under DOSBox-X via `make smoke-fast` or
 * `make smoke`.
 */

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    /* setvbuf to line-buffered so the message flushes before exit — DOS
     * stdout can be surprisingly slow and unbuffered, and a test runner
     * that captures stdout needs the line to actually land in the file. */
    setvbuf(stdout, NULL, _IOLBF, 256);

    printf("DOSKUTSU smoketest: hello from DJGPP under DPMI\n");

    return 0;
}
