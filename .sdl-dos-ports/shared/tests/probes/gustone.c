/*
 * gustone.c -- standalone GF1 single-voice test-tone probe (GUS Campaign 3, #39).
 *
 * Task #4 PRIMARY tool. Pure DJGPP; NO SDL, NO engine, NO C++. Mirrors the
 * PROVEN-in-DOSBox-X command stream of vendor/SDL/src/audio/dos/SDL_dosaudio_gus.c
 * but standalone, so the operator can binary-search the init delta on g2k WITHOUT
 * the SDL/engine machinery AND without the diag-wedge risk (no GF1 port-READS
 * while a voice is active; no heavy logging during song-load).
 *
 * ============================ THE ROOT CAUSE =============================
 * The whole team (picogus-research firmware read + refdriver-research canonical
 * driver + driver-audit) CONVERGED on a cause the emulator structurally cannot
 * reproduce: the GF1 OUTPUT SAMPLE RATE is selected by the active-voice count in
 * reg 0x0E -- rate = 617400/voices (PicoGUS sample_rates[]: 28 voices -> 22050Hz,
 * 14 voices -> 44100Hz). The PicoGUS PCM510xA DAC is SILENT at exactly 22.05kHz
 * on ~10% of cards (the documented reason `pgusinit /gus44k` exists). Our driver
 * default of 28 voices lands the DAC dead on 22050Hz. DOSBox-X has no real DAC,
 * so it played our stream regardless and never reproduced the silence -- which is
 * why 4 g2k register-fix iters (reap/double-write/vol-ramp/0x4C-IRQ-bit) all
 * failed: they were all run at 22050Hz, in the dead zone.
 *
 *   => THE LEVER IS OUTPUT RATE (voice count 0x0E). Everything else (reset-gap,
 *      settle time, mixctrl order, 0x8F ready-poll, ramp-vs-direct volume) the
 *      PicoGUS firmware treats as no-ops for audibility. This tool centers on the
 *      RATE SWEEP so the operator hears exactly which rate(s) are dead on THIS
 *      card. Zero-build cross-check: `pgusinit /gus44k 1` forces 44.1k in firmware.
 *
 * ============================== USAGE ===================================
 *   GUSTONE              single tone at the default V=14 (44100Hz, known-good)
 *   GUSTONE SWEEP        play the SAME 440Hz tone at voice-counts 14/16/20/24/28/32
 *                        (rates 44100/38588/30870/25725/22050/19294) back-to-back,
 *                        each announced + logged -- LISTEN for which go SILENT.
 *                        The 28-voice cell (22050Hz) is the dead-zone reproducer.
 *   GUSTONE V=28         single tone at 28 voices = 22050Hz (try to reproduce #39)
 *
 * Knobs (CLI tokens KEY=VAL, case-insensitive):
 *   V=<n>      active voices -> GF1 DAC rate = 617400/n. default 14 (=44100Hz).
 *   SWEEP      rate-sweep mode (ignores V; uses the built-in count list).
 *   VOL=ramp|direct   volume path. DEFAULT ramp = drive the volume THROUGH the
 *              GF1 ramp engine (our driver's gus-14 default, G2K-CONFIRMED AUDIBLE).
 *              This is byte-identical to SDL_dosaudio_gus.c SetVoiceVol's ramp:
 *              seed current=floor, set ramp target = top 8 bits of the 4.8 log
 *              volume, run UP at rate 0x3F (fastest -> reaches max in <1ms, an
 *              inaudible attack; NOT cut off by the cell). direct = legacy 0x09
 *              current-vol write with the ramp STOPPED -- the A/B baseline that
 *              g2k proved SILENT/faint (TONE8/TONE16 ran at max-vol yet silent
 *              while MIDIDEMO, which ramps, played on the SAME card): a real GF1's
 *              volume DAC tracks the ramp accumulator, so a ramp-stopped 0x09
 *              never reaches the DAC. Use VOL=direct ONLY to reproduce the faint
 *              baseline; leave default (ramp) for the loud, ear-judgeable sweep.
 *   B=8|16     voice sample width. default 8.
 *   HZ=<n>     tone pitch in Hz. default 440.
 *   W=sq|sin   waveform. default sq.
 *   L=0|1      loop (sustained) vs one-shot. default 1.
 *   DUR=<ms>   play duration per cell before explicit stop. default 3000 (single)
 *              / 2500 (sweep, per cell).
 *   R=full|single   reset 0x4C seq (de-emphasized; no-op on PicoGUS). default full.
 *   RV=<hex>   terminal reset value. default 07 (master+DAC+IRQ; 03 also works).
 *   MIX=<hex>  final P2X0 mix-control (written LAST, after reset). default 08
 *              (bit1=0 -> line-out enabled). NOT gated by PicoGUS firmware.
 *   BASE=<hex> GF1 base port. default 240 (or ULTRASND port field).
 *   IRQ=<n> DMA=<n>   latch-program values. default 7 / 3.
 *   LATCH=0|1  (re)program IRQ/DMA latches. default 1.
 *   RDV=0|1    pre-play DRAM read-back (safe: BEFORE voice start). default 0.
 *
 * Operator setup (g2k, PicoGUS v2.0 firmware picogus-gus, IRQ7+DMA3 jumpers):
 *   SET ULTRASND=240,3,3,7,7
 *   pgusinit /mode gus
 *   pgusinit /gusdma 12
 *   GUSTONE SWEEP        (then ear-judge AUDIBLE/SILENT per cell)
 * Positive control: Gravis MIDIDEMO is already known-audible on this card.
 *
 * SAFETY (diag-wedge rule, memory/gus_campaign_picogus_diag_wedge.md):
 *   - NO GF1 port-READS while a voice is active (no 0x8F poll, no status RBACK).
 *     The only reads are the OPTIONAL pre-play DRAM verify (RDV=1), BEFORE the
 *     voice starts (no active voice -> no contention), bounded, single-pass.
 *   - PIO upload re-selects the full DRAM address before EVERY byte (PicoGUS does
 *     NOT auto-increment) and bursts 64 bytes per cli/sti window.
 *   - Per-stage progress flushed to GUSTONE.LOG + stdout BEFORE each port group.
 *
 * Build: make gustone   (or  make probes-gus). 8.3 binary GUSTONE.EXE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <dos.h>     /* delay(), disable()/enable() */
#include <pc.h>      /* inportb/outportb */

/* ---- GF1 I/O port map (verbatim from SDL_dosaudio_gus.c) ---------------- */
#define P2X0_MIXCTRL(b)   ((b) + 0x000) /* mix control / reset latch select   */
#define P2X6_IRQSTAT(b)   ((b) + 0x006) /* board IRQ status (read)            */
#define P2XB_IRQDMA(b)    ((b) + 0x00B) /* IRQ/DMA control latch              */
#define P3X2_VOICESEL(b)  ((b) + 0x102) /* voice select                       */
#define P3X3_REGSEL(b)    ((b) + 0x103) /* GF1 register index select          */
#define P3X4_DATALO(b)    ((b) + 0x104) /* GF1 data low (16-bit regs)         */
#define P3X5_DATAHI(b)    ((b) + 0x105) /* GF1 data high / 8-bit reg data     */
#define P3X7_DRAMIO(b)    ((b) + 0x107) /* DRAM programmed-I/O data           */

#define REG_VOICES   0x0E
#define REG_DRAM_LO  0x43
#define REG_DRAM_HI  0x44
#define REG_RESET    0x4C

#define VREG_CTRL      0x00
#define VREG_FREQ      0x01
#define VREG_STARTHI   0x02
#define VREG_STARTLO   0x03
#define VREG_ENDHI     0x04
#define VREG_ENDLO     0x05
#define VREG_RAMPRATE  0x06
#define VREG_RAMPSTART 0x07
#define VREG_RAMPEND   0x08
#define VREG_VOLUME    0x09
#define VREG_CURHI     0x0A
#define VREG_CURLO     0x0B
#define VREG_PAN       0x0C
#define VREG_VOLCTRL   0x0D

#define VC_STOP   0x03
#define VC_16BIT  0x04
#define VC_LOOP   0x08

static const unsigned char IRQ_CODES[16] = { 0,0,1,3,0,2,0,4, 0,0,0,5,6,0,0,7 };
static const unsigned char DMA_CODES[8]  = { 0,1,0,2,0,3,4,5 };

#define RATE_NUM 617400u  /* output_rate = RATE_NUM / active_voices */

/* the built-in sweep voice-count list -> rates 44100/38588/30870/25725/22050/19294 */
static const int SWEEP_VOICES[] = { 14, 16, 20, 24, 28, 32 };
#define SWEEP_N ((int)(sizeof(SWEEP_VOICES)/sizeof(SWEEP_VOICES[0])))

/* ---- knobs ------------------------------------------------------------- */
static int k_base = 0x240, k_irq = 7, k_dma = 3, k_voices = 14;
static int k_reset_full = 1, k_resetval = 0x07, k_mixctrl = 0x08;
static int k_bits = 8, k_hz = 440, k_sine = 0, k_loop = 1, k_dur_ms = 3000;
/* k_vol_ramp DEFAULT 1 (ramp): the GF1 volume DAC tracks the ramp accumulator,
   so a ramp-stopped direct-0x09 write never reaches the DAC -> faint/silent on a
   real GF1/PicoGUS (g2k-confirmed via TONE8/TONE16, SDL_dosaudio_gus.c gus-14).
   The probe must use the PROVEN-AUDIBLE ramp path by default so the working-rate
   sweep cells are LOUD and the dead-zone (V=28) cell's silence is unambiguous.
   VOL=direct flips this back to the silent A/B baseline. */
static int k_latch = 1, k_rdverify = 0, k_sweep = 0, k_vol_ramp = 1;

static FILE *g_log = NULL;

static void logln(const char *fmt, ...)
{
    va_list ap; char buf[256];
    va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    fputs(buf, stdout); fputc('\n', stdout); fflush(stdout);
    if (g_log) { fputs(buf, g_log); fputc('\n', g_log); fflush(g_log); }
}

/* approximate microsecond delay via ISA dummy-port reads (NO GF1 port). */
static void udelay(unsigned us) { unsigned i; for (i = 0; i < us; i++) (void)inportb(0x80); }

/* ---- GF1 register write helpers ---------------------------------------- */
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
static void gus_select_voice(int base, int v) { outportb(P3X2_VOICESEL(base), (unsigned char)v); }
static void gus_set_dram_addr(int base, unsigned long addr)
{
    gus_w16(base, REG_DRAM_LO, (unsigned short)(addr & 0xFFFF));
    gus_w8(base, REG_DRAM_HI, (unsigned char)((addr >> 16) & 0x0F));
}
static void gus_poke(int base, unsigned long addr, unsigned char v)
{
    gus_set_dram_addr(base, addr);
    outportb(P3X7_DRAMIO(base), v);
}
static unsigned char gus_peek(int base, unsigned long addr)
{
    gus_set_dram_addr(base, addr);
    return inportb(P3X7_DRAMIO(base));
}

/* ---- unit conversions (verbatim from the driver) ----------------------- */
static unsigned short hz_to_fc(unsigned long playback_hz, unsigned long out_rate)
{
    unsigned long fc;
    if (!out_rate) return 0x400;
    fc = (unsigned long)(((unsigned long long)playback_hz << 10) / out_rate);
    if (fc > 0xFFFF) fc = 0xFFFF;
    return (unsigned short)fc;
}
static unsigned short linear_to_logvol(int lin)
{
    unsigned long amp; int p; unsigned char mant;
    if (lin < 1) lin = 1;
    if (lin > 255) lin = 255;
    amp = (unsigned long)lin << 8;
    p = 31 - __builtin_clz(amp);
    mant = (unsigned char)((amp >> (p - 8)) & 0xFF);
    return (unsigned short)((((unsigned long)p & 0x0F) << 8) | mant);
}
static void encode_addr(unsigned long byteaddr, int is16, unsigned short *msw, unsigned short *lsw)
{
    unsigned long a = byteaddr;
    if (is16) a = (a & 0xC0000UL) | ((a >> 1) & 0x1FFFFUL);
    *msw = (unsigned short)((a >> 7) & 0x1FFF);
    *lsw = (unsigned short)((a & 0x7F) << 9);
}

/* ---- GF1 bring-up ------------------------------------------------------ */
static void gus_reset_chip(int base, int full)
{
    if (full) {
        gus_w8(base, REG_RESET, 0x00); udelay(100);
        gus_w8(base, REG_RESET, 0x01); udelay(100);
    } else {
        gus_w8(base, REG_RESET, 0x01); udelay(100);
    }
}
static void gus_program_irq_dma(int base, int irq, int dma)
{
    unsigned char irqc = (irq >= 0 && irq < 16) ? IRQ_CODES[irq] : 0;
    unsigned char dmac = (dma >= 0 && dma < 8) ? DMA_CODES[dma] : 0;
    disable();
    outportb(P2X0_MIXCTRL(base), 0x00);  /* select IRQ latch (bit6=0) */
    outportb(P2XB_IRQDMA(base), irqc);
    outportb(P2X0_MIXCTRL(base), 0x40);  /* select DMA latch (bit6=1) */
    outportb(P2XB_IRQDMA(base), dmac);
    enable();
}

#define PIO_BURST 64u
static void gus_upload_pio(int base, unsigned long dram_addr, const unsigned char *src, unsigned long len)
{
    unsigned long i = 0;
    while (i < len) {
        unsigned long n = len - i, j;
        if (n > PIO_BURST) n = PIO_BURST;
        disable();
        for (j = 0; j < n; j++) {
            gus_set_dram_addr(base, dram_addr + i + j); /* PicoGUS: re-select EVERY byte */
            outportb(P3X7_DRAMIO(base), src[i + j]);
        }
        enable();
        i += n;
    }
}

static void gus_silence_all_voices(int base, int nvoices)
{
    int v;
    disable();
    for (v = 0; v < nvoices; v++) {
        gus_select_voice(base, v);
        gus_w8(base, VREG_VOLCTRL, VC_STOP);
        gus_w8(base, VREG_CTRL, VC_STOP);
        gus_w16(base, VREG_STARTHI, 0);
        gus_w16(base, VREG_STARTLO, 0);
        gus_w16(base, VREG_ENDHI, 0);
        gus_w16(base, VREG_ENDLO, 0);
        gus_w16(base, VREG_VOLUME, 0);
        gus_w8(base, VREG_PAN, 0x07);
    }
    enable();
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

/* ---- start one voice (note_on order Freq -> Vol -> Pan -> Start) -------- */
static void start_voice(int base, int voice, unsigned long start, unsigned long end,
                        unsigned long loopstart, int is16, int do_loop, int linvol,
                        unsigned long playback_hz, unsigned long out_rate)
{
    unsigned short smsw, slsw, emsw, elsw, cmsw, clsw, fc, reg;
    unsigned char ctrl = 0x00;
    unsigned long curaddr = do_loop ? loopstart : start;

    fc = hz_to_fc(playback_hz, out_rate);
    reg = (unsigned short)(linear_to_logvol(linvol) << 4);

    /* SetVoiceFreq */
    disable();
    gus_select_voice(base, voice);
    gus_w16(base, VREG_FREQ, fc);
    enable();

    /* SetVoiceVol */
    disable();
    gus_select_voice(base, voice);
    if (k_vol_ramp) {
        /* gus-14 (DEFAULT, G2K-AUDIBLE): drive volume THROUGH the GF1 ramp engine.
           Byte-identical to SDL_dosaudio_gus.c SetVoiceVol -- seed current at the
           floor, set the ramp target = top 8 bits of the 4.8 log volume, and run
           UP at rate 0x3F (fastest: climbs to max in <1ms, an inaudible attack
           that HOLDS at the target -- no loop/rollover). On a real GF1 the volume
           DAC tracks this accumulator, which is why this path is audible and the
           ramp-stopped direct-0x09 path below is not. */
        gus_w8(base, VREG_VOLCTRL, VC_STOP);              /* halt ramp to reprogram */
        gus_w8(base, VREG_RAMPSTART, 0x00);               /* floor */
        gus_w8(base, VREG_RAMPEND, (unsigned char)(reg >> 8)); /* target = top 8 bits */
        gus_w8(base, VREG_RAMPRATE, 0x3F);                /* fastest range + max incr */
        gus_w16(base, VREG_VOLUME, 0x0000);               /* current = floor */
        gus_w8(base, VREG_VOLCTRL, 0x00);                 /* RUN up */
        gus_w8(base, VREG_VOLCTRL, 0x00);                 /* errata double-write */
    } else {
        /* direct 0x09 with the ramp STOPPED -- the SILENT A/B baseline (VOL=direct).
           g2k PROVED this faint/silent: a real GF1's volume DAC only tracks the ramp
           accumulator, so a ramp-stopped 0x09 write never reaches the DAC even at
           0xFFE0. Kept ONLY to reproduce the faint baseline; NOT the default. Full
           ramp range so the held 0x09 isn't clamped to [start,end]. */
        gus_w8(base, VREG_VOLCTRL, VC_STOP);
        gus_w8(base, VREG_RAMPSTART, 0x00);
        gus_w8(base, VREG_RAMPEND, 0xFF);
        gus_w16(base, VREG_VOLUME, reg);
    }
    enable();

    /* SetVoicePan center */
    disable();
    gus_select_voice(base, voice);
    gus_w8(base, VREG_PAN, (unsigned char)(128 >> 4));
    enable();

    /* StartVoice */
    encode_addr(start, is16, &smsw, &slsw);
    encode_addr(end, is16, &emsw, &elsw);
    encode_addr(curaddr, is16, &cmsw, &clsw);
    if (is16) ctrl |= VC_16BIT;
    if (do_loop) ctrl |= VC_LOOP;

    disable();
    gus_select_voice(base, voice);
    gus_w8(base, VREG_CTRL, VC_STOP);
    gus_w16(base, VREG_STARTHI, smsw);
    gus_w16(base, VREG_STARTLO, slsw);
    gus_w16(base, VREG_ENDHI, emsw);
    gus_w16(base, VREG_ENDLO, elsw);
    gus_w16(base, VREG_CURHI, cmsw);
    gus_w16(base, VREG_CURLO, clsw);
    gus_w8(base, VREG_CTRL, ctrl);  /* GO */
    gus_w8(base, VREG_CTRL, ctrl);  /* errata double-write */
    enable();

    logln("gustone:   voice 0 GO ctrl=0x%02X fc=0x%04X (%.2f) vol=0x%04X(lin %d) "
          "%s%s", ctrl, fc, fc / 1024.0, reg, linvol,
          k_vol_ramp ? "ramp " : "direct ", do_loop ? "LOOP" : "one-shot");
}

static void stop_voice(int base, int voice)
{
    disable();
    gus_select_voice(base, voice);
    gus_w8(base, VREG_VOLCTRL, VC_STOP);
    gus_w8(base, VREG_CTRL, VC_STOP);
    enable();
}

/* ---- tone buffer ------------------------------------------------------- */
#define MAX_SAMPLES 4096
static unsigned char buf8[MAX_SAMPLES];
static short         buf16[MAX_SAMPLES];

static int build_tone(int is16, unsigned long out_rate, int hz, int sine, int *period_out)
{
    int period = (int)(out_rate / (unsigned long)(hz > 0 ? hz : 440));
    int nper, nsamp, i;
    if (period < 2) period = 2;
    if (period > MAX_SAMPLES / 2) period = MAX_SAMPLES / 2;
    nper = MAX_SAMPLES / period; if (nper < 1) nper = 1;
    nsamp = nper * period; if (nsamp > MAX_SAMPLES) nsamp = MAX_SAMPLES;
    for (i = 0; i < nsamp; i++) {
        int phase = i % period;
        if (sine) {
            double s = __builtin_sin(2.0 * 3.14159265358979 * phase / period);
            if (is16) buf16[i] = (short)(s * 28000.0);
            else      buf8[i]  = (unsigned char)(128 + (int)(s * 120.0));
        } else {
            int hi = (phase < period / 2);
            if (is16) buf16[i] = hi ? (short)28000 : (short)-28000;
            else      buf8[i]  = hi ? (unsigned char)0xFF : (unsigned char)0x00;
        }
    }
    *period_out = period;
    return nsamp;
}

/* ---- play one cell at a given voice count (sets the DAC output rate) ---- */
static void play_cell(int base, int voices, int cell_idx, int cell_total)
{
    int is16 = (k_bits == 16), nsamp, period;
    unsigned long out_rate, dram_addr = 0, byte_len, end_byte;
    const unsigned char *src;

    if (voices < 1) voices = 1;
    if (voices > 32) voices = 32;
    out_rate = RATE_NUM / (unsigned long)voices;

    logln("gustone: ---- cell %d/%d: V=%d -> GF1 DAC rate = 617400/%d = %lu Hz ----",
          cell_idx, cell_total, voices, voices, out_rate);
    if (out_rate > 21750 && out_rate < 22350)
        logln("gustone:   *** DEAD-ZONE cell: %lu Hz ~= 22.05kHz -- expect SILENCE on "
              "the PicoGUS PCM510xA DAC even though the register stream is correct.",
              out_rate);
    else
        logln("gustone:   %lu Hz clears the 22.05kHz DAC dead zone.", out_rate);

    /* program the active-voice count (THE rate lever), then re-assert terminal
       reset (canonical order: 0x0E before 0x4C=RV). */
    stop_voice(base, 0);
    gus_w8(base, REG_VOICES, (unsigned char)(0xC0 | (voices - 1)));
    gus_w8(base, REG_RESET, (unsigned char)k_resetval);
    gus_w8(base, REG_RESET, (unsigned char)k_resetval); /* errata double-write */
    udelay(100);

    /* build + upload the tone at this rate (constant 440Hz pitch per cell). */
    nsamp = build_tone(is16, out_rate, k_hz, k_sine, &period);
    if (period < 2 || period > nsamp || (nsamp % period) != 0)
        logln("gustone:   WARN derived period=%d nsamp=%d not whole-period (loop may click)",
              period, nsamp);
    src = is16 ? (const unsigned char *)buf16 : buf8;
    byte_len = is16 ? (unsigned long)nsamp * 2u : (unsigned long)nsamp;
    logln("gustone:   tone %s %d-bit nsamp=%d period=%d (%.1f Hz actual) bytes=%lu "
          "-> DRAM 0x%lX", k_sine ? "sine" : "square", k_bits, nsamp, period,
          (double)out_rate / period, byte_len, dram_addr);
    gus_upload_pio(base, dram_addr, src, byte_len);

    if (k_rdverify) {
        unsigned char b0, b1;
        disable(); b0 = gus_peek(base, dram_addr); b1 = gus_peek(base, dram_addr + 1); enable();
        logln("gustone:   RDV pre-play DRAM[0..1]=0x%02X 0x%02X (expect 0x%02X 0x%02X)",
              b0, b1, src[0], src[1]);
    }

    end_byte = is16 ? dram_addr + (unsigned long)(nsamp - 1) * 2u
                    : dram_addr + (unsigned long)(nsamp - 1);

    logln("gustone:   START voice 0 -- LISTEN ~%dms (V=%d, %lu Hz)...",
          k_dur_ms, voices, out_rate);
    start_voice(base, 0, dram_addr, end_byte, dram_addr, is16, k_loop, 255, out_rate, out_rate);
    delay(k_dur_ms);
    stop_voice(base, 0);
    logln("gustone:   cell %d/%d done (V=%d, %lu Hz). EAR-VERDICT: ______",
          cell_idx, cell_total, voices, out_rate);
}

/* ---- arg parsing ------------------------------------------------------- */
static int hexarg(const char *s) { return (int)strtol(s, NULL, 16); }
static int decarg(const char *s) { return (int)strtol(s, NULL, 10); }

static void parse_args(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        char key[16]; const char *eq = strchr(argv[i], '='); int kl, j;
        /* bare flags (no '=') */
        if (!eq) {
            if (!strcasecmp(argv[i], "SWEEP")) k_sweep = 1;
            continue;
        }
        kl = (int)(eq - argv[i]); if (kl > 15) kl = 15;
        memcpy(key, argv[i], kl); key[kl] = 0;
        for (j = 0; key[j]; j++) key[j] = (char)toupper((unsigned char)key[j]);
        eq++;
        if      (!strcmp(key, "BASE"))  k_base = hexarg(eq);
        else if (!strcmp(key, "IRQ"))   k_irq = decarg(eq);
        else if (!strcmp(key, "DMA"))   k_dma = decarg(eq);
        else if (!strcmp(key, "V"))     k_voices = decarg(eq);
        else if (!strcmp(key, "SWEEP")) k_sweep = decarg(eq);
        else if (!strcmp(key, "VOL"))   k_vol_ramp = (toupper((unsigned char)eq[0]) == 'R');
        else if (!strcmp(key, "R"))     k_reset_full = (toupper((unsigned char)eq[0]) == 'F');
        else if (!strcmp(key, "RV"))    k_resetval = hexarg(eq);
        else if (!strcmp(key, "MIX"))   k_mixctrl = hexarg(eq);
        else if (!strcmp(key, "B"))     k_bits = decarg(eq);
        else if (!strcmp(key, "HZ"))    k_hz = decarg(eq);
        else if (!strcmp(key, "W"))     k_sine = (toupper((unsigned char)eq[0]) == 'S' &&
                                                  toupper((unsigned char)eq[1]) == 'I');
        else if (!strcmp(key, "L"))     k_loop = decarg(eq);
        else if (!strcmp(key, "DUR"))   k_dur_ms = decarg(eq);
        else if (!strcmp(key, "LATCH")) k_latch = decarg(eq);
        else if (!strcmp(key, "RDV"))   k_rdverify = decarg(eq);
    }
}

int main(int argc, char **argv)
{
    int present, dur_defaulted = 1, i;

    g_log = fopen("GUSTONE.LOG", "w");
    parse_args(argc, argv);

    /* sweep uses a shorter per-cell default duration unless DUR was given. */
    for (i = 1; i < argc; i++) if (!strncasecmp(argv[i], "DUR=", 4)) dur_defaulted = 0;
    if (k_sweep && dur_defaulted) k_dur_ms = 2500;

    logln("==== GUSTONE GF1 single-voice test-tone probe (#39) ====");
    logln("gustone: mode=%s base=0x%X irq=%d dma=%d vol=%s B=%d HZ=%d W=%s loop=%d "
          "dur=%dms reset=%s RV=0x%02X mix=0x%02X latch=%d rdv=%d",
          k_sweep ? "SWEEP" : "single", k_base, k_irq, k_dma,
          k_vol_ramp ? "ramp" : "direct", k_bits, k_hz, k_sine ? "sin" : "sq",
          k_loop, k_dur_ms, k_reset_full ? "full" : "single", k_resetval,
          k_mixctrl, k_latch, k_rdverify);
    logln("gustone: ROOT-CAUSE LEVER = output rate via voice count 0x0E "
          "(617400/V). 22050Hz (V=28) is the PicoGUS DAC dead zone.");

    /* --- one-time bring-up ------------------------------------------------ */
    logln("gustone: [bring-up] reset chip (R=%s)...", k_reset_full ? "full" : "single");
    gus_reset_chip(k_base, k_reset_full);
    present = gus_dram_roundtrip(k_base);
    logln("gustone: [bring-up] DRAM roundtrip present=%d", present);
    if (!present) {
        logln("gustone: RESULT=NO_CARD at base 0x%X. Check ULTRASND port + "
              "pgusinit /mode gus.", k_base);
        if (g_log) fclose(g_log);
        return 2;
    }
    if (k_latch) {
        logln("gustone: [bring-up] program IRQ/DMA latches (irq=%d dma=%d)...", k_irq, k_dma);
        gus_program_irq_dma(k_base, k_irq, k_dma);
    } else {
        logln("gustone: [bring-up] SKIP latch program (LATCH=0; pgusinit owns it)");
    }
    /* terminal reset (master+DAC) -- voice count is (re)programmed per cell. */
    gus_w8(k_base, REG_RESET, (unsigned char)k_resetval);
    gus_w8(k_base, REG_RESET, (unsigned char)k_resetval);
    udelay(100);
    gus_silence_all_voices(k_base, 32);
    /* mix control written LAST (after reset), bit1=0 -> line-out enabled. */
    logln("gustone: [bring-up] P2X0 mix-control = 0x%02X (bit1=%d -> line-out %s)",
          k_mixctrl, (k_mixctrl >> 1) & 1, ((k_mixctrl >> 1) & 1) ? "DISABLED" : "enabled");
    outportb(P2X0_MIXCTRL(k_base), (unsigned char)k_mixctrl);

    /* --- play ------------------------------------------------------------- */
    if (k_sweep) {
        logln("gustone: SWEEP -- %d cells; SAME 440Hz tone at each rate. Note which "
              "go SILENT (the dead one(s) are this card's DAC dead-zone).", SWEEP_N);
        for (i = 0; i < SWEEP_N; i++)
            play_cell(k_base, SWEEP_VOICES[i], i + 1, SWEEP_N);
    } else {
        play_cell(k_base, k_voices, 1, 1);
    }

    gus_w8(k_base, REG_RESET, 0x00); /* park chip in reset */
    logln("gustone: RESULT=DONE mode=%s -- EAR-JUDGE AUDIBLE vs SILENT per cell and "
          "record in the iter log (the .LOG only proves the register stream ran).",
          k_sweep ? "sweep" : "single");
    if (g_log) fclose(g_log);
    return 0;
}
