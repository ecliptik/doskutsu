/*
 * qhexit.c -- DOS-exit hang isolation probe, cell A (SDL-linked).
 * (Task #32 SECONDARY, quit-hang investigation.)
 *
 * QUESTION
 *   Does the DJGPP libc + CWSDPMI process-EXIT unwind (atexit handlers /
 *   uclock PIT-restore / INT 21h 0x4C) hang in ISOLATION once a real SDL
 *   init+quit (which arms uclock's exit-time PIT-restore) has run, with NO
 *   engine state and NO audio device present?
 *
 * WHY A STANDALONE PROBE
 *   Established this session (sdl-engine + nx-engine, code+log): SDL_Quit()
 *   provably RETURNS in every live-game run including the hung one (the
 *   engine's `post-SDL_Quit` marker logs even when it then hangs). So the #32
 *   hang is STRICTLY AFTER SDL_Quit returns -- in DJGPP/CWSDPMI process exit.
 *   The game logs also PROVE the SB16 is fully torn down before the hang
 *   (DSP-halt / irq-unhook / dma-mask all logged pre-hang), so at the hang
 *   the live low-level state is uclock's reprogrammed PIT (IRQ-0, restored by
 *   libc at exit) + CWSDPMI -- NOT the audio device. This cell reproduces
 *   exactly that: a real SDL_Init/SDL_Quit (which arms uclock via
 *   SDL_InitTicks -> SDL_GetPerformanceCounter -> uclock first-call, verified
 *   in vendor/SDL/src/SDL.c:296 + timer/dos/SDL_systimer.c) with NO audio
 *   open, then walk the DOS-exit unwind.
 *
 * CELL FAMILY (3 cells across 2 binaries; team-lead bundles the g2k iter)
 *   A (this file, SDL-linked)  -- SDL_Init(EVENTS) + SDL_GetTicks spin
 *                                 (uclock armed by SDL) + SDL_Quit + exit.
 *                                 Faithful "SDL-program exit, uclock armed,
 *                                 NOAUDIO." Closest to the real binary.
 *                                 Log -> QHEXITA.LOG.
 *   U (qhexitp.c, pure DJGPP)  -- uclock() armed directly + exit. uclock-ON,
 *                                 SDL-free.  Log -> QHEXITU.LOG.
 *   B (qhexitp.c, pure DJGPP)  -- no uclock + exit. uclock-OFF baseline,
 *                                 SDL-free.  Log -> QHEXITB.LOG.
 *   Reads: U-hang + B-clean = uclock-PIT-restore hangs in pure isolation
 *   (SDL-free; DECISIVE for the uclock lead). A-hang + U-clean = wedge needs
 *   SDL's exit teardown. A+U both hang = uclock is the common cause. all
 *   clean = INCONCLUSIVE (the real hang may need Pool-st15 engine audio
 *   state; a clean probe does NOT exonerate the exit path -- this is a
 *   one-way test, on record with team-lead + sdl-engine).
 *
 * MARKER SCHEME (mirrors nx-engine's in-situ engine 0187 brackets so the two
 *   logs cross-reference line-for-line). All markers are RAW write() to a
 *   kept-open fd -- NOT stdio -- so they bypass libc's own stdio-teardown
 *   atexit handler (which is itself part of the unwind under test); the fd is
 *   never closed, so M2/M3 still write during the exit unwind, and fsync after
 *   each write commits to disk so the LAST line survives a hang.
 *
 *     M1  post-SDL_Quit         -- right after SDL_Quit() returns (= engine
 *                                  `post-SDL_Quit`).
 *     M4  pre-return-from-main  -- last stmt before main() returns; C-runtime
 *                                  atexit unwind begins next.
 *     -- main returns; libc runs atexit handlers LIFO --
 *     M2  atexit-runs-first     -- handler registered LAST (after uclock
 *                                  engaged) -> runs FIRST. Proves the atexit
 *                                  chain was entered.
 *     -- libc-internal atexit handlers run here (registered during main, so   --
 *     -- LIFO-between M2 and M3). uclock's PIT-restore is the PRIME SUSPECT    --
 *     -- among them IF DJGPP registers it via atexit; unverified from         --
 *     -- precompiled libc.a -> treat the bracketing as a BONUS. The robust    --
 *     -- uclock discriminator is the U/B pair, not this bracket.              --
 *     M3  atexit-runs-last      -- handler registered FIRST (top of main,
 *                                  before uclock) -> runs LAST of ours, after
 *                                  those libc-internal handlers. Proves they
 *                                  completed.
 *     -- libc finalization -> INT 21h 0x4C -> back to COMMAND.COM --
 *
 *   DIAGNOSTIC READ of a g2k QHEXITA.LOG that ends abruptly:
 *     last = M1  -> hung between SDL_Quit return and main return (our code).
 *     last = M4  -> hung entering the libc atexit dispatch.
 *     last = M2  -> hung in a libc-internal atexit handler in the M2..M3
 *                   window (uclock PIT-restore is the prime suspect; confirm
 *                   with flush-instr + the U/B cells rather than assuming).
 *     last = M3  -> hung AFTER atexit, in libc finalization / INT 21h 0x4C /
 *                   CWSDPMI process-teardown.
 *     clean-exit tail present + prompt returns -> NO HANG (one-way test;
 *                   does NOT exonerate the exit path).
 *
 * 8.3 DOS filenames:
 *   Source: tests/probes/qhexit.c   Binary: QHEXIT.EXE   Log: QHEXITA.LOG
 *   BAT:    tests/probes/qhexit.bat (drives A + the qhexitp U/B cells)
 *
 * SDL3-linked (SDL_Init / SDL_GetTicks / SDL_Delay / SDL_Quit); minstack
 * 2048k (matches YIELD/IDLEPROB/MIXBENCH recipe).
 *
 * License: MIT.
 */

#include <SDL3/SDL.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>     /* vsnprintf only -- no FILE* used for the log */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>    /* write, fsync */
#include <fcntl.h>     /* open, O_* */
#include <sys/farptr.h>
#include <go32.h>

/* ============================================================ */
/* SDL DOS backend -> engine extern (g_dos_sfx_synth_active_count, patch */
/* SDL/0071) is supplied by the SHARED stub TU                    */
/* tests/probes/probe_sdl_stubs.c, linked into every SDL probe    */
/* via PROBES_SDL_LDLIBS (task #34). Do NOT define it here too --  */
/* that would double-define it against the shared TU.             */
/* ============================================================ */

/* ============================================================ */
/* RAW-fd logging (no stdio). The fd is opened once and NEVER    */
/* closed, so atexit handlers can still write during the exit    */
/* unwind. fsync after each line commits to disk so the last     */
/* line survives a hang. Timestamps come from the BIOS 18.2 Hz   */
/* tick counter at 0040:006C -- deliberately NOT uclock, so the  */
/* logger never perturbs the PIT facility under test.            */
/* ============================================================ */

static int g_fd = -1;

static unsigned long bios_ticks(void)
{
    return _farpeekl(_dos_ds, 0x46CUL);   /* BIOS timer tick count (18.2 Hz) */
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
    (void)!write(1, buf, n);            /* console echo */
    if (g_fd >= 0) {
        (void)!write(g_fd, buf, n);     /* forensic record */
        fsync(g_fd);                    /* commit to disk before next line */
    }
}

/* ============================================================ */
/* atexit markers. Registration order vs LIFO run order is the   */
/* instrument -- see MARKER SCHEME in the file header. Neither    */
/* handler closes g_fd.                                          */
/* ============================================================ */

/* Registered FIRST (top of main, before uclock engaged) -> runs LAST. */
static void atexit_m3_last(void)
{
    rawlog("[M3] atexit-runs-last  : ticks=%lu -- libc-internal atexit handlers "
           "(uclock PIT-restore among them IF atexit-based) have run.",
           bios_ticks());
    rawlog("=== QHEXITA clean exit: all atexit handlers ran; next is libc "
           "finalization -> INT 21h 0x4C. Prompt returning on g2k == the "
           "isolated exit path did NOT hang (one-way test). ===");
    /* Deliberately NO close(g_fd): nothing legitimately writes after this,
     * but leaving it open is harmless and keeps the never-close invariant. */
}

/* Registered LAST (after uclock engaged, just before main returns) -> runs FIRST. */
static void atexit_m2_first(void)
{
    rawlog("[M2] atexit-runs-first : ticks=%lu -- C-runtime atexit unwind "
           "entered (libc-internal handlers not yet run).", bios_ticks());
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    log_open("QHEXITA.LOG");

    rawlog("=== QHEXIT cell A: SDL-program DOS-exit hang isolation (task #32) ===");
    rawlog("MODE A: SDL_Init(EVENTS) + SDL_GetTicks spin (uclock armed by SDL) "
           "+ SDL_Quit + walk the exit unwind. NO audio device opened.");
    rawlog("Markers are raw write()+fsync to a kept-open fd (stdio-teardown-proof). "
           "BIOS-tick timestamps (NOT uclock, to avoid perturbing the PIT).");
    rawlog("");

    /* M3 handler registered FIRST -> runs LAST (after libc-internal handlers). */
    if (atexit(atexit_m3_last) != 0)
        rawlog("WARN: atexit(atexit_m3_last) registration FAILED");

    rawlog("[setup] SDL_Init(SDL_INIT_EVENTS)...");
    if (!SDL_Init(SDL_INIT_EVENTS)) {
        const char *err = SDL_GetError();
        rawlog("[setup] SDL_Init FAILED: %s -- exit path still under test.",
               err ? err : "(no error)");
    } else {
        rawlog("[setup] SDL_Init OK (SDL_InitTicks armed uclock's PIT reprogram).");
    }

    /* Belt-and-suspenders uclock engage via SDL: SDL_Init already armed it,
     * this just exercises the tick path the game uses. */
    {
        Uint64 t0 = SDL_GetTicks();
        uint32_t spins = 0;
        while ((SDL_GetTicks() - t0) < 400) { spins++; SDL_Delay(1); }
        rawlog("[setup] %lu SDL_GetTicks spins over ~400 ms; uclock PIT engaged.",
               (unsigned long)spins);
    }

    rawlog("[teardown] SDL_Quit()...");
    SDL_Quit();
    rawlog("[M1] post-SDL_Quit      : ticks=%lu -- control back in main(); "
           "SDL fully down (= engine `post-SDL_Quit`).", bios_ticks());

    /* M2 handler registered LAST -> runs FIRST in the atexit chain. */
    if (atexit(atexit_m2_first) != 0)
        rawlog("WARN: atexit(atexit_m2_first) registration FAILED");

    rawlog("[M4] pre-return-from-main: ticks=%lu -- main() about to return; "
           "C-runtime atexit unwind begins next.", bios_ticks());

    /* Return (not exit()) so the standard libc exit path runs exactly as the
     * engine's main() does: atexit LIFO (M2 .. libc-internal .. M3) then
     * __exit -> INT 21h 0x4C. */
    return 0;
}
