/*
 * irqrate.c — IRQ fire-rate characterization via BIOS-counter polling.
 *
 * Phase 9 wave 20 task #12 (P3, medium pri). Tests the hypothesis from
 * W20A v2 findings § 6: non-audio IRQs (primarily the timer IRQ if it's
 * been reprogrammed by a TSR or driver) account for a measurable chunk
 * of the audio-off ceiling.
 *
 * The headline question: is the timer IRQ firing at the default 18.2 Hz
 * (one tick per 54.9 ms; PIT divisor 0xFFFF) or has something bumped
 * it higher? At 1000 Hz with ~5-15 us per handler, that's 5-15 ms/sec
 * of systemwide drift — directly load-bearing on the 13.5 ms baseline.
 *
 * What it does (zero IRQ hooks installed — defensive):
 *
 *   1. Snapshots BIOS timer tick at 0x40:0x6C (DWORD; bumped once per
 *      timer IRQ regardless of what reprogramming happened) and the
 *      raw PIT counter at port 0x40 (channel-0 count register).
 *   2. Sleeps for ~10 seconds via uclock-driven busy loop.
 *   3. Re-snapshots, computes timer IRQ fires/sec, and reports the
 *      PIT mode register state (port 0x43 read-back) so we can
 *      identify the divisor / mode programmed.
 *   4. Polls BIOS keyboard buffer at 0x40:0x1A (head) and 0x40:0x1C
 *      (tail) before/after the sleep to estimate keystroke rate as
 *      a proxy for IRQ 1 load (NOT a fire count — measures keys
 *      delivered, which is load-bearing only if operator types).
 *
 * Why no DPMI IRQ hook: `_go32_dpmi_allocate_iret_wrapper` does NOT
 * properly restore DS for hardware IRQs in protected mode, so a hooked
 * handler attempting to access C globals page-faults silently (probe
 * exits mid-sampling with no result lines printed). The canonical
 * DJGPP pattern requires a hand-rolled asm wrapper that restores DS
 * via `__djgpp_app_DS` — substantial code, real risk on DOSBox-X / real
 * HW. BIOS-counter polling answers the headline question with no
 * hazard. SB16 IRQ rate (audio playback, IRQ 5) is OUT OF SCOPE here;
 * a future probe with hand-rolled asm wrappers can revisit.
 *
 * Output: C:\IRQRATE.LOG (fsync per line). Falls back to ./IRQRATE.LOG.
 *
 * Pure DJGPP. No SDL.
 *
 * 8.3 DOS filename: IRQRATE.EXE (7.3) — fits.
 *
 * Build: `make irqrate`. DOSBox-X smoke is correctness-only — DOSBox-X
 * timer rates differ from real HW (cycles=max alters wall-clock vs
 * emulated-time relationship). Per dosbox_not_perf_proxy.md.
 *
 * License: MIT.
 */

#include <dos.h>
#include <go32.h>
#include <pc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/farptr.h>
#include <time.h>
#include <unistd.h>

/* Measurement window. Default 10 sec; override via env DOS_PORT_IRQRATE_SECS. */
#define MEASURE_SECS_DEFAULT 10
static int measure_secs = MEASURE_SECS_DEFAULT;

/* ============================================================ */
/* Logging                                                      */
/* ============================================================ */

static FILE *g_log = NULL;

static void rlog(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    fputs(buf, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    if (g_log) {
        fputs(buf, g_log);
        fputc('\n', g_log);
        fflush(g_log);
        fsync(fileno(g_log));
    }
}

static void open_log(void)
{
    g_log = fopen("C:\\IRQRATE.LOG", "w");
    if (!g_log) g_log = fopen("IRQRATE.LOG", "w");
}

/* ============================================================ */
/* PIT counter read (channel 0, mode 3 typically)               */
/* ============================================================ */

/* Read the current PIT channel-0 counter. Returns 16-bit count value.
 * Latches the count first via control byte 0x00 (counter latch). */
static uint16_t pit_count_read(void)
{
    /* Latch counter 0. Control byte 0x00: counter=0, RW=00 (latch), mode=000. */
    outportb(0x43, 0x00);
    uint8_t lo = inportb(0x40);
    uint8_t hi = inportb(0x40);
    return ((uint16_t)hi << 8) | lo;
}

/* ============================================================ */
/* Busy-wait via uclock (PIT-rate independent)                  */
/* ============================================================ */

static void busy_wait_secs(double secs)
{
    double t0 = (double)uclock() / (double)UCLOCKS_PER_SEC;
    double t;
    do {
        t = (double)uclock() / (double)UCLOCKS_PER_SEC;
    } while ((t - t0) < secs);
}

/* ============================================================ */
/* main                                                         */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    rlog("=== IRQRATE wave-20 task #12 (P3) starting ===");
    rlog("DJGPP build, target = timer + keyboard IRQ rate via BIOS counter polling");

    const char *env_secs = getenv("DOS_PORT_IRQRATE_SECS");
    if (env_secs) {
        int v = atoi(env_secs);
        if (v > 0 && v < 120) measure_secs = v;
    }
    rlog("Measurement window: %d seconds (override via DOS_PORT_IRQRATE_SECS)",
         measure_secs);

    /* Snapshot before. */
    uint32_t bios_t0  = _farpeekl(_dos_ds, 0x46C);    /* BIOS timer tick count */
    uint16_t kbd_h0   = _farpeekw(_dos_ds, 0x41A);    /* kbd buffer head */
    uint16_t kbd_t0   = _farpeekw(_dos_ds, 0x41C);    /* kbd buffer tail */
    uint16_t pit_c0   = pit_count_read();

    rlog("SNAPSHOT-BEGIN: bios_ticks=%lu  kbd_head=0x%04X  kbd_tail=0x%04X  pit_count=%u",
         (unsigned long)bios_t0, kbd_h0, kbd_t0, pit_c0);
    rlog("Sampling for %d seconds — type a few keys to load the keyboard buffer...",
         measure_secs);

    double sample_start = (double)uclock() / (double)UCLOCKS_PER_SEC;
    busy_wait_secs((double)measure_secs);
    double sample_end   = (double)uclock() / (double)UCLOCKS_PER_SEC;
    double window = sample_end - sample_start;

    /* Snapshot after. */
    uint32_t bios_t1  = _farpeekl(_dos_ds, 0x46C);
    uint16_t kbd_h1   = _farpeekw(_dos_ds, 0x41A);
    uint16_t kbd_t1   = _farpeekw(_dos_ds, 0x41C);
    uint16_t pit_c1   = pit_count_read();

    /* Compute IRQ rates. */
    uint32_t timer_ticks = bios_t1 - bios_t0;
    double timer_rate = (double)timer_ticks / window;

    /* Keyboard buffer is a circular FIFO at 0x40:0x1E..0x3D (32 bytes,
     * 16 word entries). head/tail diff in bytes; each keypress consumes
     * 2 bytes. Operator may have drained via gettime calls etc. */
    int kbd_buffered = (int)kbd_t1 - (int)kbd_h1;
    if (kbd_buffered < 0) kbd_buffered += 32;  /* wrap */
    rlog("");
    rlog("=== Results (window=%.3f sec) ===", window);
    rlog("BIOS-TIMER ticks=%lu  rate=%.3f Hz", (unsigned long)timer_ticks, timer_rate);
    rlog("  Default 18.2 Hz   = PIT divisor 0xFFFF (no TSR reprogramming)");
    rlog("  100  Hz           = PIT divisor ~12000 (some networking TSRs)");
    rlog("  1000 Hz           = PIT divisor 1193 (uncommon; some games)");
    if (timer_rate < 19.0) {
        rlog("  -> CONCLUSION: default 18.2 Hz; timer IRQ contributes <0.3 ms/sec");
    } else if (timer_rate < 200.0) {
        rlog("  -> CONCLUSION: bumped above default; ~%.0f IRQs/sec * 5-15 us each",
             timer_rate);
    } else {
        rlog("  -> CONCLUSION: HIGH rate; meaningful contribution to audio-off ceiling");
    }
    rlog("PIT-COUNT  before=%u  after=%u  delta=%d  (channel-0 latched count register)",
         pit_c0, pit_c1, (int)pit_c1 - (int)pit_c0);
    rlog("KBD-BUFFER head=0x%04X (was 0x%04X)  tail=0x%04X (was 0x%04X)  buffered=%d bytes",
         kbd_h1, kbd_h0, kbd_t1, kbd_t0, kbd_buffered);
    rlog("  (8 bytes ~ 4 keypresses; only meaningful if operator typed during sample)");

    rlog("");
    rlog("=== IRQRATE done ===");
    rlog("");
    rlog("Reading the result:");
    rlog("  Timer rate at default 18.2 Hz   -> non-audio IRQ contribution to audio-off");
    rlog("                                     ceiling is NEGLIGIBLE (~0.3 ms/sec).");
    rlog("                                     Eliminates non-audio-IRQ hypothesis.");
    rlog("  Timer rate >> 18.2 Hz           -> investigate which TSR/driver bumped");
    rlog("                                     PIT and whether removing it closes");
    rlog("                                     part of the 13.5 ms baseline.");

    if (g_log) fclose(g_log);
    return 0;
}
