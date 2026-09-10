/*
 * qhexitp.c -- DOS-exit hang isolation probe, cells U + B (pure DJGPP).
 * (Task #32 SECONDARY, quit-hang investigation.)
 *
 * The SDL-free companion to qhexit.c (cell A). This binary contains NO SDL
 * and NO engine -- pure DJGPP libc + CWSDPMI -- so it isolates the uclock
 * exit-time PIT-restore as the load-bearing variable, with nothing else in
 * the picture.
 *
 *   QHEXITP.EXE U   -- cell U: call uclock() once to ARM the DJGPP PIT-ch0
 *                      reprogram (+ its exit-time restore), then walk the
 *                      DOS-exit unwind. uclock-ON.  Log -> QHEXITU.LOG.
 *   QHEXITP.EXE B   -- cell B: never call uclock; walk the same exit unwind.
 *                      uclock-OFF baseline.  Log -> QHEXITB.LOG.
 *   (default, no arg)  -> U.
 *
 * WHY SDL-FREE
 *   SDL_Init() arms uclock unconditionally (SDL.c:296 SDL_InitTicks ->
 *   SDL_GetPerformanceCounter -> uclock first-call; verified in vendored
 *   source). So an SDL-based "uclock-off" cell is impossible -- it always
 *   carries uclock. The only clean uclock on/off discriminator is SDL-free,
 *   which is what U vs B provide. bare uclock() is the SAME libc uclock SDL
 *   calls -> identical PIT reprogram -> identical exit-time restore; and
 *   SDL_Quit does not disarm uclock (DJGPP restores at exit), so the
 *   post-SDL_Quit uclock state == this bare-uclock-armed exit state. U
 *   faithfully reproduces the real uclock-exit-restore without SDL.
 *
 *   READ across the A/U/B family (one-way test; clean does NOT exonerate):
 *     U hang + B clean      = uclock-PIT-restore hangs in pure isolation
 *                             (SDL-free). DECISIVE for the uclock lead.
 *     A hang + U clean      = the wedge needs SDL's exit teardown, not uclock.
 *     A and U both hang     = uclock is the common cause; SDL not required.
 *     all clean             = INCONCLUSIVE (real hang may need engine state).
 *
 * MARKERS (same scheme as cell A, minus M1 which is SDL-specific). RAW write()
 * to a kept-open fd, never closed; fsync per line; BIOS-tick timestamps (NOT
 * uclock -- so cell B never touches uclock via the logger, and the discriminator
 * stays clean).
 *     M4  pre-return-from-main  -- last stmt before main() returns.
 *     -- main returns; libc runs atexit handlers LIFO --
 *     M2  atexit-runs-first     -- handler registered LAST -> runs FIRST.
 *     -- libc-internal atexit handlers run here (uclock PIT-restore in cell U --
 *     -- IF atexit-based) --
 *     M3  atexit-runs-last      -- handler registered FIRST -> runs LAST.
 *     -- libc finalization -> INT 21h 0x4C --
 *   DIAGNOSTIC READ (log ends abruptly):
 *     last = M4 -> hung entering libc atexit dispatch.
 *     last = M2 -> hung in a libc-internal atexit handler (cell U: uclock
 *                  PIT-restore is the prime suspect).
 *     last = M3 -> hung AFTER atexit, in libc finalization / INT 21h 0x4C.
 *     clean-exit tail + prompt returns -> NO HANG.
 *
 * 8.3 DOS filenames:
 *   Source: tests/probes/qhexitp.c  Binary: QHEXITP.EXE
 *   Logs:   QHEXITU.LOG (cell U) / QHEXITB.LOG (cell B)
 *
 * Pure DJGPP (no SDL link, no g_sfx_synth stub). minstack 512k (PROBES_MINSTK).
 *
 * License: MIT.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>     /* vsnprintf / snprintf only */
#include <stdlib.h>    /* atexit */
#include <string.h>
#include <unistd.h>    /* write, fsync */
#include <fcntl.h>     /* open, O_* */
#include <time.h>      /* uclock (cell U only) */
#include <sys/farptr.h>
#include <go32.h>

/* ============================================================ */
/* RAW-fd logging (no stdio). fd opened once, NEVER closed, so   */
/* atexit handlers write during the exit unwind. fsync per line. */
/* BIOS 18.2 Hz tick timestamps (0040:006C) -- NOT uclock, so    */
/* cell B never arms the PIT via the logger.                     */
/* ============================================================ */

static int g_fd = -1;

static unsigned long bios_ticks(void)
{
    return _farpeekl(_dos_ds, 0x46CUL);
}

static void log_open(const char *name)
{
    g_fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (g_fd < 0) {
        char alt[24];
        snprintf(alt, sizeof alt, "C:\\%s", name);
        g_fd = open(alt, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }
}

static void rawlog(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf - 1) n = (int)sizeof buf - 1;
    buf[n++] = '\n';
    (void)!write(1, buf, n);
    if (g_fd >= 0) {
        (void)!write(g_fd, buf, n);
        fsync(g_fd);
    }
}

/* ============================================================ */
/* atexit markers (no fd close)                                  */
/* ============================================================ */

static const char *g_cell = "U";

/* Registered FIRST -> runs LAST. */
static void atexit_m3_last(void)
{
    rawlog("[M3] atexit-runs-last  : ticks=%lu -- libc-internal atexit handlers "
           "have run (cell %s).", bios_ticks(), g_cell);
    rawlog("=== QHEXIT%s clean exit: all atexit handlers ran; next is libc "
           "finalization -> INT 21h 0x4C. Prompt returning on g2k == this cell "
           "did NOT hang. ===",
           (g_cell[0] == 'U') ? "U" : "B");
}

/* Registered LAST -> runs FIRST. */
static void atexit_m2_first(void)
{
    rawlog("[M2] atexit-runs-first : ticks=%lu -- C-runtime atexit unwind entered "
           "(cell %s).", bios_ticks(), g_cell);
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    int uclock_on = 1;   /* default = cell U */
    if (argc > 1 && (argv[1][0] == 'B' || argv[1][0] == 'b'))
        uclock_on = 0;
    g_cell = uclock_on ? "U" : "B";

    log_open(uclock_on ? "QHEXITU.LOG" : "QHEXITB.LOG");

    rawlog("=== QHEXIT cell %s: SDL-free DOS-exit hang isolation (task #32) ===",
           g_cell);
    rawlog("Pure DJGPP, no SDL, no engine. %s",
           uclock_on ? "uclock ARMED (PIT-ch0 reprogram + exit-restore)."
                     : "uclock NOT armed (baseline; PIT untouched).");
    rawlog("Markers raw write()+fsync to a kept-open fd. BIOS-tick timestamps.");
    rawlog("");

    /* M3 handler registered FIRST -> runs LAST. */
    if (atexit(atexit_m3_last) != 0)
        rawlog("WARN: atexit(atexit_m3_last) registration FAILED");

    if (uclock_on) {
        /* The ONE thing that distinguishes U from B: arm uclock. First call
         * reprograms PIT channel 0 + registers DJGPP's exit-time restore. */
        uclock_t t = uclock();
        rawlog("[setup] uclock() armed; UCLOCKS_PER_SEC=%lu first-read=%lld",
               (unsigned long)UCLOCKS_PER_SEC, (long long)t);
    } else {
        rawlog("[setup] uclock() deliberately NOT called (baseline).");
    }

    /* A little wall-clock work so both cells spend similar time live. Spin on
     * the BIOS tick counter (~0.5 s); never touches uclock in either cell. */
    {
        unsigned long t0 = bios_ticks();
        volatile uint32_t sink = 0;
        while ((bios_ticks() - t0) < 9) {   /* ~9 ticks ~= 0.5 s @ 18.2 Hz */
            for (int i = 0; i < 100000; i++) sink += (uint32_t)i;
        }
        rawlog("[setup] ~0.5 s BIOS-tick spin done (sink=%lu).",
               (unsigned long)sink);
    }

    /* M2 handler registered LAST -> runs FIRST in the atexit chain. */
    if (atexit(atexit_m2_first) != 0)
        rawlog("WARN: atexit(atexit_m2_first) registration FAILED");

    rawlog("[M4] pre-return-from-main: ticks=%lu -- main() about to return; "
           "C-runtime atexit unwind begins next (cell %s).",
           bios_ticks(), g_cell);

    return 0;
}
