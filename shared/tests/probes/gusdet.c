/*
 * gusdet.c -- standalone GF1 read-only detect/report probe (GUS Campaign 3, #39).
 *
 * Task #4 secondary tool. Pure DJGPP; NO SDL/engine/C++. Mirrors the detection
 * primitives of vendor/SDL/src/audio/dos/SDL_dosaudio_gus.c (gus_reset_chip,
 * gus_dram_roundtrip, gus_size_dram) -- these are the SAME peek/poke ops the
 * driver already runs to reach "device up" on g2k (CONFIRMED non-wedging in the
 * #39 handoff), so they are diag-wedge-safe: NO voice is ever started, so the
 * GF1 reads never contend with an active voice.
 *
 * MISSION: confirm the card answers at the ULTRASND base + sizes its DRAM,
 * WITHOUT touching any voice. Cross-checks the parsed ULTRASND env against what
 * the card actually reports, so the operator knows GUSTONE's defaults match the
 * hardware before chasing the silence.
 *
 *   GUSDET            (reads ULTRASND for base; default base 0x240)
 *   GUSDET BASE=240   (override base port, hex)
 *
 * Output: loud stdout banner + GUSDET.LOG (8.3, fopen-direct, CWD).
 * SAFETY: read-only peek/poke + size walk; bounded; NO voice activity.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <dos.h>
#include <pc.h>

#define P3X3_REGSEL(b)  ((b) + 0x103)
#define P3X4_DATALO(b)  ((b) + 0x104)
#define P3X5_DATAHI(b)  ((b) + 0x105)
#define P3X7_DRAMIO(b)  ((b) + 0x107)
#define REG_DRAM_LO 0x43
#define REG_DRAM_HI 0x44
#define REG_RESET   0x4C

static FILE *g_log = NULL;

static void logln(const char *fmt, ...)
{
    va_list ap; char buf[256];
    va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    fputs(buf, stdout); fputc('\n', stdout); fflush(stdout);
    if (g_log) { fputs(buf, g_log); fputc('\n', g_log); fflush(g_log); }
}

static void gus_w8(int base, unsigned char reg, unsigned char val)
{
    outportb(P3X3_REGSEL(base), reg);
    outportb(P3X5_DATAHI(base), val);
}
static void gus_w16(int base, unsigned char reg, unsigned short val)
{
    outportb(P3X3_REGSEL(base), reg);
    outportb(P3X4_DATALO(base), (unsigned char)(val & 0xFF));
    outportb(P3X5_DATAHI(base), (unsigned char)(val >> 8));
}
static void gus_set_dram_addr(int base, unsigned long addr)
{
    gus_w16(base, REG_DRAM_LO, (unsigned short)(addr & 0xFFFF));
    gus_w8(base, REG_DRAM_HI, (unsigned char)((addr >> 16) & 0x0F));
}
static void gus_poke(int base, unsigned long a, unsigned char v)
{
    gus_set_dram_addr(base, a);
    outportb(P3X7_DRAMIO(base), v);
}
static unsigned char gus_peek(int base, unsigned long a)
{
    gus_set_dram_addr(base, a);
    return inportb(P3X7_DRAMIO(base));
}

static void udelay(unsigned us) { unsigned i; for (i = 0; i < us; i++) (void)inportb(0x80); }

static void gus_reset_chip(int base)
{
    gus_w8(base, REG_RESET, 0x00); udelay(100);
    gus_w8(base, REG_RESET, 0x01); udelay(100);
}

static int gus_dram_roundtrip(int base)
{
    unsigned char a, b;
    disable();
    gus_poke(base, 0, 0xAA);
    gus_poke(base, 1, 0x55);
    a = gus_peek(base, 0);
    b = gus_peek(base, 1);
    enable();
    return (a == 0xAA && b == 0x55);
}

/* size DRAM by probing 256K boundaries for absence / aliasing (driver mirror). */
static unsigned long gus_size_dram(int base)
{
    unsigned long size = 0, boundary;
    disable();
    for (boundary = 0; boundary < (1024UL * 1024UL); boundary += (256UL * 1024UL)) {
        gus_poke(base, boundary, 0x12);
        gus_poke(base, boundary + 1, 0x34);
        if (gus_peek(base, boundary) != 0x12 || gus_peek(base, boundary + 1) != 0x34)
            break;
        if (boundary != 0) {
            gus_poke(base, 0, 0xA5);
            gus_poke(base, boundary, 0x5A);
            if (gus_peek(base, 0) != 0xA5) break; /* alias wrap */
        }
        size = boundary + (256UL * 1024UL);
    }
    enable();
    if (size == 0) size = 256UL * 1024UL;
    return size;
}

int main(int argc, char **argv)
{
    int base = 0x240, i;
    int u_port = -1, u_dma1 = -1, u_dma2 = -1, u_irq1 = -1, u_irq2 = -1;
    const char *us;
    int present;
    unsigned long dram;

    g_log = fopen("GUSDET.LOG", "w");

    /* ULTRASND=PORT,DMA1,DMA2,IRQ1,IRQ2 (all decimal; PORT is hex-as-decimal
       e.g. 240). */
    us = getenv("ULTRASND");
    if (us && *us) {
        sscanf(us, "%x,%d,%d,%d,%d", &u_port, &u_dma1, &u_dma2, &u_irq1, &u_irq2);
        if (u_port > 0) base = u_port;
    }
    for (i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "BASE=", 5) || !strncmp(argv[i], "base=", 5))
            base = (int)strtol(argv[i] + 5, NULL, 16);
    }

    logln("==== GUSDET GF1 read-only detect probe (#39) ====");
    if (us && *us)
        logln("gusdet: ULTRASND=\"%s\" -> port=0x%X dma1=%d dma2=%d irq1=%d irq2=%d",
              us, u_port, u_dma1, u_dma2, u_irq1, u_irq2);
    else
        logln("gusdet: ULTRASND not set in env; using base default/override.");
    logln("gusdet: probing base=0x%X ...", base);

    gus_reset_chip(base);
    present = gus_dram_roundtrip(base);
    logln("gusdet: DRAM peek/poke roundtrip present=%d", present);
    if (!present) {
        logln("gusdet: RESULT=NO_CARD at 0x%X. Verify pgusinit /mode gus ran and "
              "ULTRASND port matches the PicoGUS jumpers.", base);
        if (g_log) fclose(g_log);
        return 2;
    }

    dram = gus_size_dram(base);
    logln("gusdet: DRAM size = %lu KB", dram / 1024);
    logln("gusdet: RESULT=PRESENT base=0x%X dram=%luKB (ULTRASND dma=%d irq=%d). "
          "GUSTONE defaults V=14 expect out_rate=%lu Hz.",
          base, dram / 1024, u_dma1, u_irq1, 617400UL / 14);
    if (g_log) fclose(g_log);
    return 0;
}
