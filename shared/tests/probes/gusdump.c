/*
 * gusdump.c -- standalone GF1 BOUNDED register-snapshot probe (GUS Campaign 3, #39).
 *
 * Task #4 secondary tool. Pure DJGPP; NO SDL/engine/C++. Single-pass snapshot of
 * the GF1 global + voice-0 registers so the operator can DIFF "after our
 * by-the-book init" against research-supplied known-good values (or against a
 * post-MIDIDEMO state). Helps localize the #39 init delta: a register that reads
 * DIFFERENT after MIDIDEMO than after our init is a prime suspect.
 *
 * SAFETY (diag-wedge rule -- memory/gus_campaign_picogus_diag_wedge.md):
 *   The PicoGUS wedges when a GF1 read is CONCURRENT WITH AN ACTIVE VOICE or
 *   when reads run in a hot loop. So this probe:
 *     - default (own-init mode): resets + brings up the GF1 with NO voice
 *       started, then reads each register ONCE (single pass, ~30 reads, small
 *       inter-read settle). No voice is ever active -> safe.
 *     - NOINIT=1 mode: snapshots WHATEVER a prior program left, WITHOUT its own
 *       reset. HAZARD: only run this AFTER MIDIDEMO has fully EXITED and the
 *       card is idle. Do NOT run it while MIDIDEMO is sustaining a note (that is
 *       the exact read-concurrent-with-active-voice wedge). Default OFF.
 *
 *   GUSDUMP                 own-init snapshot (safe)
 *   GUSDUMP BASE=240        override base (hex)
 *   GUSDUMP NOINIT=1        snapshot prior state (read hazard note above)
 *
 * Output: loud stdout banner + GUSDUMP.LOG (8.3, fopen-direct, CWD).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <dos.h>
#include <pc.h>

#define P2X0_MIXCTRL(b) ((b) + 0x000)
#define P2X6_IRQSTAT(b) ((b) + 0x006)
#define P2XB_IRQDMA(b)  ((b) + 0x00B)
#define P3X2_VOICESEL(b)((b) + 0x102)
#define P3X3_REGSEL(b)  ((b) + 0x103)
#define P3X4_DATALO(b)  ((b) + 0x104)
#define P3X5_DATAHI(b)  ((b) + 0x105)
#define REG_VOICES 0x0E
#define REG_RESET  0x4C

static FILE *g_log = NULL;

static void logln(const char *fmt, ...)
{
    va_list ap; char buf[256];
    va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    fputs(buf, stdout); fputc('\n', stdout); fflush(stdout);
    if (g_log) { fputs(buf, g_log); fputc('\n', g_log); fflush(g_log); }
}

static void udelay(unsigned us) { unsigned i; for (i = 0; i < us; i++) (void)inportb(0x80); }

static void gus_w8(int base, unsigned char reg, unsigned char val)
{
    outportb(P3X3_REGSEL(base), reg);
    outportb(P3X5_DATAHI(base), val);
}
static unsigned char gus_r8(int base, unsigned char reg)
{
    outportb(P3X3_REGSEL(base), reg);
    udelay(2);                       /* small settle; NOT a hot loop */
    return inportb(P3X5_DATAHI(base));
}
static unsigned short gus_r16(int base, unsigned char reg)
{
    unsigned char lo, hi;
    outportb(P3X3_REGSEL(base), reg);
    udelay(2);
    lo = inportb(P3X4_DATALO(base));
    hi = inportb(P3X5_DATAHI(base));
    return (unsigned short)(lo | (hi << 8));
}
static void gus_select_voice(int base, int v) { outportb(P3X2_VOICESEL(base), (unsigned char)v); }

static void gus_reset_bringup(int base)
{
    gus_w8(base, REG_RESET, 0x00); udelay(100);
    gus_w8(base, REG_RESET, 0x01); udelay(100);
    gus_w8(base, REG_VOICES, 0xC0 | (14 - 1)); /* 14-voice minimum */
    gus_w8(base, REG_RESET, 0x07); udelay(100); /* master+DAC+IRQ-enable */
    outportb(P2X0_MIXCTRL(base), 0x08);         /* line-out enabled, latches */
}

int main(int argc, char **argv)
{
    int base = 0x240, noinit = 0, i;
    const char *us;
    unsigned char irqstat;

    g_log = fopen("GUSDUMP.LOG", "w");

    us = getenv("ULTRASND");
    if (us && *us) { int p = 0; if (sscanf(us, "%x", &p) == 1 && p > 0) base = p; }
    for (i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "BASE=", 5) || !strncmp(argv[i], "base=", 5))
            base = (int)strtol(argv[i] + 5, NULL, 16);
        else if (!strncmp(argv[i], "NOINIT=", 7) || !strncmp(argv[i], "noinit=", 7))
            noinit = atoi(argv[i] + 7);
    }

    logln("==== GUSDUMP GF1 bounded register snapshot (#39) ====");
    logln("gusdump: base=0x%X mode=%s", base, noinit ? "NOINIT (prior state)" : "own-init");
    if (noinit) {
        logln("gusdump: HAZARD -- NOINIT reads prior GF1 state. Only valid if "
              "MIDIDEMO has fully EXITED and the card is idle (no active voice).");
    } else {
        logln("gusdump: bringing up GF1 (reset 0x00->0x01->0x07, voices=14, "
              "mix=0x08), NO voice started ...");
        gus_reset_bringup(base);
    }

    /* --- global state (bounded single read each) ------------------------- */
    irqstat = inportb(P2X6_IRQSTAT(base));
    logln("gusdump: [global] P2X6 board-IRQ-status = 0x%02X", irqstat);
    logln("gusdump: [global] reg 0x4C reset     = 0x%02X (bit0 master, bit1 DAC, bit2 IRQ-en)",
          gus_r8(base, REG_RESET));
    logln("gusdump: [global] reg 0x0E voices    = 0x%02X (active = (val&0x3F)+1 = %d)",
          gus_r8(base, REG_VOICES), (gus_r8(base, REG_VOICES) & 0x3F) + 1);
    logln("gusdump: [global] reg 0x41 dma-ctrl  = 0x%02X", gus_r8(base, 0x41));
    logln("gusdump: [global] reg 0x45 timer-ctrl= 0x%02X", gus_r8(base, 0x45));
    logln("gusdump: [global] reg 0x49 sampling  = 0x%02X", gus_r8(base, 0x49));

    /* --- voice 0 state via read-alias indices (reg | 0x80) --------------- */
    gus_select_voice(base, 0);
    udelay(4);
    logln("gusdump: [voice0] ctrl(0x80)    = 0x%02X (bit0 stopped, bit1 stop, "
          "bit2 16bit, bit3 loop)", gus_r8(base, 0x80));
    logln("gusdump: [voice0] freq(0x81)    = 0x%04X", gus_r16(base, 0x81));
    logln("gusdump: [voice0] start(0x82/83)= 0x%04X 0x%04X",
          gus_r16(base, 0x82), gus_r16(base, 0x83));
    logln("gusdump: [voice0] end(0x84/85)  = 0x%04X 0x%04X",
          gus_r16(base, 0x84), gus_r16(base, 0x85));
    logln("gusdump: [voice0] vol(0x89)     = 0x%04X", gus_r16(base, 0x89));
    logln("gusdump: [voice0] cur(0x8A/8B)  = 0x%04X 0x%04X",
          gus_r16(base, 0x8A), gus_r16(base, 0x8B));
    logln("gusdump: [voice0] pan(0x8C)     = 0x%02X", gus_r8(base, 0x8C));
    logln("gusdump: [voice0] volctrl(0x8D) = 0x%02X (bit0 stopped, bit1 stop)",
          gus_r8(base, 0x8D));

    if (!noinit) {
        gus_w8(base, REG_RESET, 0x00); /* park chip in reset on the way out */
    }
    logln("gusdump: RESULT=DONE base=0x%X mode=%s -- diff these vs known-good "
          "(research) or post-MIDIDEMO NOINIT snapshot.",
          base, noinit ? "noinit" : "own-init");
    if (g_log) fclose(g_log);
    return 0;
}
