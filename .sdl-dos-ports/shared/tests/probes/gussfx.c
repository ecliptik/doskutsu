/*
 * gussfx.c -- standalone GF1 SFX-upload + per-slot trigger probe (GUS #38).
 *
 * Campaign 3 GUS-SFX (#38), iter-1 GROUND-TRUTH cell. Pure DJGPP; NO SDL, NO
 * engine, NO C++. Reproduces the nx-0239 `SfxSynth::initGusSfx` DRAM-placement
 * sequence (8-bit UNSIGNED mono SFX bank) WITHOUT the engine, so the operator
 * can pin the exact crashing slot on the real PicoGUS (g2k) and confirm/refute
 * the leading hypothesis.
 *
 * The GF1 bring-up + upload + voice-start helpers are lifted VERBATIM from the
 * sibling gustone.c probe (already g2k-proven non-wedging), so this tool adds
 * only the SFX layout + per-slot trigger machinery.
 *
 * ===================== THE CRASH (what is known) ========================
 * From memory/gus_campaign_picogus_diag_wedge.md + GUS-SFX-38-HANDOFF.md: every
 * GUS launch must set SDL_HINT_DOS_SFX_OFF=1 or the game QUITS TO DOS at
 * SFX init/upload before music starts. The crash is "~715 KB DRAM / a LATE slot,
 * slot-data-SPECIFIC; NOT transport, NOT address-encoding, NOT plain DRAM
 * exhaustion." Root cause UNKNOWN. This probe gets ground truth.
 *
 * ================= TWO COMPETING HYPOTHESES (this probe arbitrates) ======
 * H1 (LEADING, handed by team-lead): gus_dram_alloc() (SDL_dosaudio_gus.c:606)
 *     ONLY bank-aligns 16-bit samples (the `if (is16bit)` branch bumps a sample
 *     to the next 256 KB bank so it never straddles 0x40000/0x80000/0xC0000 --
 *     REQUIRED because the 16-bit GF1 address ENCODE keeps the bank bits and
 *     shifts the in-bank offset, so a straddling 16-bit sample wraps inside its
 *     bank). 8-bit SFX get NO straddle guard. So a late 8-bit SFX whose
 *     [start,end) crosses a 256 KB boundary is the suspect. The per-slot
 *     STRADDLE flag this probe emits is exactly the signal that confirms it:
 *     if the slot that quits-to-DOS has straddle=1, H1 is confirmed.
 *
 * H2 (sibling defect, from the driver's OWN #38 comment at UploadSample ~L1129):
 *     "a host page-fault during the read means `len` EXCEEDS the caller's `data`
 *     buffer -- a caller-side length bug." i.e. the engine passes len >
 *     final_buffer's real allocation for one slot -> PIO over-reads `tmp[]` into
 *     an unmapped DPMI page -> quit to DOS. That is ENGINE-side (SfxSynth render
 *     size vs uploaded len) and this probe CANNOT reproduce it -- this probe
 *     GENERATES its sample bytes on the fly (no host buffer to over-read).
 *
 *   => DECISIVE EITHER WAY: this probe's uploads + triggers are 100% GF1-side
 *      with exact, in-bounds byte generation. If a STRADDLING slot crashes here,
 *      H1 is confirmed (it's a real GF1/PicoGUS quirk, fix = add the 8-bit
 *      straddle guard, task #2). If NOTHING here crashes -- not even the
 *      deliberate STRADDLE A/B cells -- then the GF1 trigger path is innocent and
 *      the bug is ENGINE-side H2 (len>buffer), redirecting to the nx task #3
 *      per-slot DRAM log. NOTE (analysis, NOT a measurement): for 8-bit the GF1
 *      address encode is IDENTITY (no >>1 wrap), so crossing a bank SHOULD be
 *      benign -- which would REFUTE H1. The probe MEASURES; it does not assert.
 *
 * ============================== USAGE ===================================
 *   GUSSFX               MAP only (DEFAULT, SAFE): emulate the initGusSfx alloc
 *                        sequence + upload synthetic 8-bit SFX to ~715 KB, log
 *                        per-slot index/len/[start,end)/STRADDLE flag. NO voice
 *                        triggers -> cannot crash. Produces the full DRAM layout
 *                        map to compare against the nx task-#3 real per-slot log.
 *   GUSSFX TRIG          MAP, then TRIGGER each uploaded slot one-at-a-time
 *                        (~800ms audible beep each). Logs+flushes the slot's
 *                        [start,end)+straddle BEFORE each trigger, so a quit-to-
 *                        DOS leaves that slot as the LAST line = the culprit.
 *   GUSSFX TRIG FROM=80 TO=117   trigger only slots 80..117 (bisection across
 *                        iters once MAP shows where the straddlers land).
 *   GUSSFX STRADDLE      controlled H1 A/B: ignore the size table; for each
 *                        256 KB boundary that fits in DRAM, upload a control
 *                        sample fully BELOW the boundary AND a sample STRADDLING
 *                        it, then trigger each. If straddlers crash and controls
 *                        play, H1 is confirmed independent of the real .pxt sizes.
 *
 * Knobs (KEY=VAL tokens, case-insensitive):
 *   DRAM=<kb>   GF1 DRAM size for the budget math. default 1024 (PicoGUS 1MB).
 *   RESERVE=<kb> music-priority reserve (nx-0239 RESERVE_FOR_MUSIC). default 320.
 *               SFX budget = DRAM-RESERVE (or DRAM/2 on a tiny card), matching
 *               initGusSfx. (1024-320 = 704 KB -> the ~715 KB crash band.)
 *   N=<n>       number of synthetic SFX slots. default 117 (Cave Story NUM_SOUNDS).
 *   SEED=<n>    PRNG seed for synthetic slot sizes. default 1. (sizes are
 *               REPRESENTATIVE, not the real .pxt -- see GUSSFX.SIZ below.)
 *   V=<n>       active voices -> GF1 DAC rate 617400/n. default 20 (music default).
 *   DUR=<ms>    per-slot trigger duration. default 800.
 *   BASE=<hex>  GF1 base port. default 240. IRQ=<n> DMA=<n> default 7 / 3.
 *   LATCH=0|1   (re)program IRQ/DMA latches. default 1.
 *
 * GUSSFX.SIZ (optional): if a file GUSSFX.SIZ exists in CWD, the probe reads it
 *   as one decimal byte-size per line and uses THOSE as the slot sizes instead
 *   of the synthetic table -- so a later iter can feed the REAL per-slot
 *   final_size values logged by nx task #3 and reproduce the EXACT field layout
 *   (cross-anchor reuse). Lines beyond N or non-positive sizes are ignored.
 *
 * Operator setup (g2k, PicoGUS v2.0 firmware picogus-gus, IRQ7+DMA3 jumpers):
 *   SET ULTRASND=240,3,3,7,7
 *   pgusinit /mode gus
 *   pgusinit /gusdma 12
 *   GUSSFX            (read GUSSFX.LOG: the layout map + straddle flags)
 *   GUSSFX TRIG       (LISTEN/WATCH: the slot whose line is LAST = the crasher)
 *
 * SAFETY (diag-wedge rule, memory/gus_campaign_picogus_diag_wedge.md):
 *   - NO GF1 port-READS anywhere in this probe. No 0x8F ready-poll, no voice
 *     status RBACK, no DRAM peek. Memory-only, bounded single-pass writes.
 *     (The card-present check is the SAME safe pre-voice DRAM poke roundtrip the
 *     driver detect already runs non-wedging on g2k -- see note at gus_present.)
 *   - PIO upload re-selects the full DRAM address before EVERY byte (PicoGUS does
 *     NOT auto-increment) and bursts PIO_BURST bytes per cli/sti window.
 *   - Per-slot line flushed to GUSSFX.LOG BEFORE the trigger's port group, so a
 *     crash pins the culprit slot (gus-5 #41 locator pattern). No heavy logging
 *     inside the trigger window.
 *
 * Build: make gussfx   (or  make probes-gus). 8.3 binary GUSSFX.EXE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <dos.h>     /* delay(), disable()/enable() */
#include <pc.h>      /* inportb/outportb */

/* ---- GF1 I/O port map (verbatim from SDL_dosaudio_gus.c / gustone.c) ----- */
#define P2X0_MIXCTRL(b)   ((b) + 0x000)
#define P2XB_IRQDMA(b)    ((b) + 0x00B)
#define P3X2_VOICESEL(b)  ((b) + 0x102)
#define P3X3_REGSEL(b)    ((b) + 0x103)
#define P3X4_DATALO(b)    ((b) + 0x104)
#define P3X5_DATAHI(b)    ((b) + 0x105)
#define P3X7_DRAMIO(b)    ((b) + 0x107)

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

#define RATE_NUM  617400u           /* output_rate = RATE_NUM / active_voices  */
#define BANK_SIZE 0x40000UL         /* 256 KB GF1 DRAM bank                    */
#define PIO_BURST 64u
#define MAX_SLOTS 256
#define SQ_PERIOD 70                /* ~440Hz square at rate 30870 (V=20)      */

/* ---- knobs ------------------------------------------------------------- */
static int   k_base = 0x240, k_irq = 7, k_dma = 3, k_voices = 20;
static int   k_latch = 1, k_dur_ms = 800;
static int   k_nslots = 117, k_seed = 1;
static unsigned long k_dram_kb = 1024, k_reserve_kb = 320;
static int   k_mode_trig = 0, k_mode_straddle = 0;
static int   k_from = 0, k_to = 1000000;   /* trigger slot range (TRIG mode) */

/* ---- slot model -------------------------------------------------------- */
typedef struct {
    unsigned long len;       /* bytes (== 8-bit mono samples)               */
    unsigned long start;     /* DRAM byte addr (or 0 if not uploaded)       */
    unsigned long end;       /* exclusive end byte addr                     */
    int           straddle;  /* [start,end) crosses a 256KB boundary?       */
    unsigned long bound;     /* the crossed boundary addr (if straddle)     */
    int           uploaded;  /* 1 if it fit budget+DRAM and was poked        */
} slot_t;

static slot_t  g_slot[MAX_SLOTS];
static FILE   *g_log = NULL;

static void logln(const char *fmt, ...)
{
    va_list ap; char buf[256];
    va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    fputs(buf, stdout); fputc('\n', stdout); fflush(stdout);
    if (g_log) { fputs(buf, g_log); fputc('\n', g_log); fflush(g_log); }
}

static void udelay(unsigned us) { unsigned i; for (i = 0; i < us; i++) (void)inportb(0x80); }

/* ---- GF1 register write helpers (write-only; NO reads) ----------------- */
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

/* ---- unit conversions (verbatim from the driver / gustone.c) ----------- */
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
/* 8-bit SFX -> is16=0 -> IDENTITY encode (the very reason H1 predicts a bank
   straddle is harmless for 8-bit; kept general so 16-bit could be tested too). */
static void encode_addr(unsigned long byteaddr, int is16, unsigned short *msw, unsigned short *lsw)
{
    unsigned long a = byteaddr;
    if (is16) a = (a & 0xC0000UL) | ((a >> 1) & 0x1FFFFUL);
    *msw = (unsigned short)((a >> 7) & 0x1FFF);
    *lsw = (unsigned short)((a & 0x7F) << 9);
}

/* ---- GF1 bring-up (write-only) ----------------------------------------- */
static void gus_reset_chip(int base)
{
    gus_w8(base, REG_RESET, 0x00); udelay(100);
    gus_w8(base, REG_RESET, 0x01); udelay(100);
}
static void gus_program_irq_dma(int base, int irq, int dma)
{
    unsigned char irqc = (irq >= 0 && irq < 16) ? IRQ_CODES[irq] : 0;
    unsigned char dmac = (dma >= 0 && dma < 8) ? DMA_CODES[dma] : 0;
    disable();
    outportb(P2X0_MIXCTRL(base), 0x00);
    outportb(P2XB_IRQDMA(base), irqc);
    outportb(P2X0_MIXCTRL(base), 0x40);
    outportb(P2XB_IRQDMA(base), dmac);
    enable();
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

/* Card-present check: a SINGLE pre-voice DRAM poke pair, NO read-back. We do
   NOT peek (the diag-wedge rule bans GF1 reads); instead we WRITE a known low
   byte and trust the subsequent upload. (gusdet.c does the read-back roundtrip;
   here we stay strictly write-only because triggers will follow.) */
static void gus_prime_dram(int base)
{
    disable();
    gus_set_dram_addr(base, 0); outportb(P3X7_DRAMIO(base), 0x80);
    enable();
}

/* PIO upload that GENERATES an 8-bit unsigned square on the fly (period samples
   between 0xFF and 0x00, centered for loudness). No host buffer -> structurally
   immune to the H2 len>buffer over-read; a crash here is purely GF1-side. */
static void gus_upload_square(int base, unsigned long dram_addr, unsigned long len, int period)
{
    unsigned long i = 0;
    if (period < 2) period = 2;
    while (i < len) {
        unsigned long n = len - i, j;
        if (n > PIO_BURST) n = PIO_BURST;
        disable();
        for (j = 0; j < n; j++) {
            unsigned long k = i + j;
            unsigned char b = ((k % (unsigned long)period) < (unsigned long)(period / 2))
                              ? 0xFF : 0x00;
            gus_set_dram_addr(base, dram_addr + k);  /* PicoGUS: re-select EVERY byte */
            outportb(P3X7_DRAMIO(base), b);
        }
        enable();
        i += n;
    }
}

/* ---- start one 8-bit voice (audible beep), ramp-driven volume ---------- */
static void start_voice8(int base, int voice, unsigned long start, unsigned long end,
                         int do_loop, unsigned long out_rate)
{
    unsigned short smsw, slsw, emsw, elsw, cmsw, clsw, fc, reg;
    unsigned char ctrl = 0x00;
    fc  = hz_to_fc(out_rate, out_rate);            /* play at native rate     */
    reg = (unsigned short)(linear_to_logvol(255) << 4);

    disable(); gus_select_voice(base, voice); gus_w16(base, VREG_FREQ, fc); enable();

    /* ramp-engine volume (the g2k-AUDIBLE path; gustone gus-14) */
    disable();
    gus_select_voice(base, voice);
    gus_w8(base, VREG_VOLCTRL, VC_STOP);
    gus_w8(base, VREG_RAMPSTART, 0x00);
    gus_w8(base, VREG_RAMPEND, (unsigned char)(reg >> 8));
    gus_w8(base, VREG_RAMPRATE, 0x3F);
    gus_w16(base, VREG_VOLUME, 0x0000);
    gus_w8(base, VREG_VOLCTRL, 0x00);
    gus_w8(base, VREG_VOLCTRL, 0x00);
    enable();

    disable();
    gus_select_voice(base, voice);
    gus_w8(base, VREG_PAN, (unsigned char)(128 >> 4));
    enable();

    encode_addr(start, 0, &smsw, &slsw);
    encode_addr(end,   0, &emsw, &elsw);
    encode_addr(start, 0, &cmsw, &clsw);
    if (do_loop) ctrl |= VC_LOOP;   /* 8-bit -> no VC_16BIT */

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
}
static void stop_voice(int base, int voice)
{
    disable();
    gus_select_voice(base, voice);
    gus_w8(base, VREG_VOLCTRL, VC_STOP);
    gus_w8(base, VREG_CTRL, VC_STOP);
    enable();
}

/* ---- straddle classification ------------------------------------------ */
static int classify_straddle(unsigned long start, unsigned long end, unsigned long *bound_out)
{
    /* end is exclusive; last touched byte is end-1. */
    unsigned long last = (end > start) ? end - 1 : start;
    unsigned long b0 = start / BANK_SIZE, b1 = last / BANK_SIZE;
    if (b1 != b0) { if (bound_out) *bound_out = (b0 + 1) * BANK_SIZE; return 1; }
    if (bound_out) *bound_out = 0;
    return 0;
}

/* ---- synthetic SFX size table (representative, NOT the real .pxt) ------- */
static unsigned long lcg_next(unsigned long *s)
{
    *s = (*s * 1103515245UL) + 12345UL;
    return (*s >> 16) & 0x7FFF;
}
/* fill g_slot[].len for k_nslots; sizes spread ~1..16 KB with an occasional
   large ambient looper, deterministic from k_seed. Or, if GUSSFX.SIZ exists,
   read real sizes from it (one decimal per line). Returns the source label. */
static const char *load_sizes(void)
{
    FILE *f = fopen("GUSSFX.SIZ", "r");
    if (f) {
        int i = 0; char line[64];
        while (i < k_nslots && fgets(line, sizeof(line), f)) {
            long v = atol(line);
            if (v > 0) { g_slot[i].len = (unsigned long)v; i++; }
        }
        fclose(f);
        if (i > 0) { k_nslots = i; return "GUSSFX.SIZ (real per-slot sizes)"; }
    }
    {
        unsigned long s = (unsigned long)k_seed; int i;
        for (i = 0; i < k_nslots; i++) {
            unsigned long r = lcg_next(&s);
            unsigned long len = 1024UL + (r % 15360UL);      /* ~1..16 KB */
            if ((r & 0x1F) == 0) len += 24576UL + (r % 49152UL); /* rare big loopers */
            g_slot[i].len = len;
        }
    }
    return "synthetic (representative; feed GUSSFX.SIZ for the real layout)";
}

/* ---- emulate nx-0239 initGusSfx DRAM placement (8-bit, NO straddle guard) */
#define GUS_BAD 0xFFFFFFFFUL
static unsigned long g_dram_used = 0, g_dram_size = 0, g_sfx_budget = 0, g_sfx_used = 0;

static unsigned long dram_alloc8(unsigned long len)
{
    /* mirrors gus_dram_alloc(len, is16bit=0): 16-byte align, NO bank guard. */
    unsigned long addr = (g_dram_used + 15UL) & ~15UL;
    if (addr + len > g_dram_size) return GUS_BAD;
    g_dram_used = addr + len;
    return addr;
}

static void do_map_and_upload(void)
{
    int i, uploaded = 0, skipped = 0, full = 0, straddlers = 0;
    const char *src = load_sizes();

    g_dram_size  = k_dram_kb * 1024UL;
    g_dram_used  = 0; g_sfx_used = 0;
    g_sfx_budget = (g_dram_size > k_reserve_kb * 1024UL)
                   ? (g_dram_size - k_reserve_kb * 1024UL)
                   : (g_dram_size / 2);

    logln("gussfx: ---- SFX MAP: emulate nx-0239 initGusSfx (8-bit unsigned mono) ----");
    logln("gussfx: DRAM=%lu KB reserve=%lu KB -> SFX budget=%lu KB; N=%d slots; sizes: %s",
          k_dram_kb, k_reserve_kb, g_sfx_budget / 1024UL, k_nslots, src);
    logln("gussfx: bank boundaries 0x40000(256KB) 0x80000(512KB) 0xC0000(768KB); "
          "H1 suspect = an 8-bit slot whose [start,end) crosses one.");

    for (i = 0; i < k_nslots; i++) {
        unsigned long len = g_slot[i].len, addr;
        g_slot[i].start = g_slot[i].end = 0;
        g_slot[i].straddle = 0; g_slot[i].bound = 0; g_slot[i].uploaded = 0;

        /* music-priority budget cap (continue, not break -- nx-0239) */
        if (g_sfx_used + len > g_sfx_budget) { skipped++; continue; }
        addr = dram_alloc8(len);
        if (addr == GUS_BAD) { full++; continue; }   /* DRAM exhausted */

        g_slot[i].start    = addr;
        g_slot[i].end      = addr + len;
        g_slot[i].straddle = classify_straddle(addr, addr + len, &g_slot[i].bound);
        g_slot[i].uploaded = 1;
        g_sfx_used += len;
        uploaded++;
        if (g_slot[i].straddle) straddlers++;

        /* upload BEFORE logging the line (the line is the durable record) */
        gus_upload_square(k_base, addr, len, SQ_PERIOD);

        if (g_slot[i].straddle)
            logln("gussfx: slot %3d len=%6lu [0x%05lX,0x%05lX) used=%luKB  *** STRADDLES "
                  "0x%05lX (256KB bank) -- H1 PRIME SUSPECT", i + 1, len, addr, addr + len,
                  g_sfx_used / 1024UL, g_slot[i].bound);
        else
            logln("gussfx: slot %3d len=%6lu [0x%05lX,0x%05lX) used=%luKB",
                  i + 1, len, addr, addr + len, g_sfx_used / 1024UL);
    }

    logln("gussfx: MAP done: uploaded=%d skipped(budget)=%d dram-full=%d straddlers=%d "
          "final SFX bank=%lu KB", uploaded, skipped, full, straddlers, g_sfx_used / 1024UL);
    if (straddlers == 0)
        logln("gussfx: NOTE no slot straddled a bank in this layout. If TRIG still "
              "crashes, H1 (straddle) is REFUTED -> look engine-side (H2 len>buffer, "
              "nx task #3). Feed GUSSFX.SIZ with real .pxt sizes to match the field layout.");
}

static void trigger_uploaded(unsigned long out_rate)
{
    int i, triggered = 0;
    logln("gussfx: ---- TRIGGER each uploaded slot one-at-a-time (V=%d, %lu Hz, %dms) ----",
          k_voices, out_rate, k_dur_ms);
    logln("gussfx: WATCH/LISTEN: the slot whose line is the LAST in GUSSFX.LOG = the "
          "one that quit to DOS. A slot that beeps then advances is innocent.");
    for (i = 0; i < k_nslots; i++) {
        if (!g_slot[i].uploaded) continue;
        if ((i + 1) < k_from || (i + 1) > k_to) continue;
        /* flush the culprit-naming line BEFORE any trigger port op */
        logln("gussfx: TRIG slot %3d [0x%05lX,0x%05lX) straddle=%d bank=0x%05lX -- listen...",
              i + 1, g_slot[i].start, g_slot[i].end, g_slot[i].straddle, g_slot[i].bound);
        start_voice8(k_base, 0, g_slot[i].start, g_slot[i].end, 1 /*loop*/, out_rate);
        delay(k_dur_ms);
        stop_voice(k_base, 0);
        triggered++;
    }
    logln("gussfx: TRIGGER sweep SURVIVED all %d slots (no quit-to-DOS). If a STRADDLE "
          "slot was among them and did NOT crash, H1 is REFUTED for 8-bit.", triggered);
}

/* ---- controlled H1 A/B: deliberate below-boundary vs straddling cells --- */
static void do_straddle_ab(unsigned long out_rate)
{
    static const unsigned long bounds[3] = { BANK_SIZE, 2*BANK_SIZE, 3*BANK_SIZE };
    const unsigned long len = 8192UL;   /* 8 KB test sample */
    int bi;
    g_dram_size = k_dram_kb * 1024UL;
    logln("gussfx: ---- STRADDLE A/B: control (below) vs straddling per bank boundary ----");
    logln("gussfx: each cell uploads an 8KB 8-bit square then triggers it. H1: the "
          "straddling cell crashes; the control plays. (DRAM=%lu KB)", k_dram_kb);
    for (bi = 0; bi < 3; bi++) {
        unsigned long B = bounds[bi];
        unsigned long ctl_start, str_start;
        if (B + len/2 > g_dram_size) {
            logln("gussfx: boundary 0x%05lX beyond DRAM (%lu KB) -- skipped.", B, k_dram_kb);
            continue;
        }
        /* control: fully below the boundary */
        ctl_start = (B > len + 0x4000UL) ? (B - len - 0x4000UL) : 0;
        /* straddler: centered on the boundary */
        str_start = B - len/2;

        gus_upload_square(k_base, ctl_start, len, SQ_PERIOD);
        logln("gussfx: AB bound 0x%05lX CONTROL  [0x%05lX,0x%05lX) straddle=0 -- listen...",
              B, ctl_start, ctl_start + len);
        start_voice8(k_base, 0, ctl_start, ctl_start + len, 1, out_rate);
        delay(k_dur_ms); stop_voice(k_base, 0);

        gus_upload_square(k_base, str_start, len, SQ_PERIOD);
        logln("gussfx: AB bound 0x%05lX STRADDLE [0x%05lX,0x%05lX) crosses 0x%05lX -- listen "
              "(H1: THIS one quits to DOS)...", B, str_start, str_start + len, B);
        start_voice8(k_base, 0, str_start, str_start + len, 1, out_rate);
        delay(k_dur_ms); stop_voice(k_base, 0);
        logln("gussfx: AB bound 0x%05lX SURVIVED both control + straddle.", B);
    }
    logln("gussfx: STRADDLE A/B complete -- if every straddle cell SURVIVED, H1 is "
          "REFUTED for 8-bit; redirect to H2 (engine-side len>buffer, nx task #3).");
}

/* ---- arg parsing ------------------------------------------------------- */
static int hexarg(const char *s) { return (int)strtol(s, NULL, 16); }
static int decarg(const char *s) { return (int)strtol(s, NULL, 10); }

static void parse_args(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        char key[16]; const char *eq = strchr(argv[i], '='); int kl, j;
        if (!eq) {
            if      (!strcasecmp(argv[i], "TRIG"))     k_mode_trig = 1;
            else if (!strcasecmp(argv[i], "STRADDLE")) k_mode_straddle = 1;
            else if (!strcasecmp(argv[i], "MAP"))      { k_mode_trig = 0; k_mode_straddle = 0; }
            continue;
        }
        kl = (int)(eq - argv[i]); if (kl > 15) kl = 15;
        memcpy(key, argv[i], kl); key[kl] = 0;
        for (j = 0; key[j]; j++) key[j] = (char)toupper((unsigned char)key[j]);
        eq++;
        if      (!strcmp(key, "BASE"))    k_base = hexarg(eq);
        else if (!strcmp(key, "IRQ"))     k_irq = decarg(eq);
        else if (!strcmp(key, "DMA"))     k_dma = decarg(eq);
        else if (!strcmp(key, "V"))       k_voices = decarg(eq);
        else if (!strcmp(key, "DUR"))     k_dur_ms = decarg(eq);
        else if (!strcmp(key, "N"))       k_nslots = decarg(eq);
        else if (!strcmp(key, "SEED"))    k_seed = decarg(eq);
        else if (!strcmp(key, "DRAM"))    k_dram_kb = (unsigned long)decarg(eq);
        else if (!strcmp(key, "RESERVE")) k_reserve_kb = (unsigned long)decarg(eq);
        else if (!strcmp(key, "LATCH"))   k_latch = decarg(eq);
        else if (!strcmp(key, "FROM"))    { k_from = decarg(eq); k_mode_trig = 1; }
        else if (!strcmp(key, "TO"))      { k_to = decarg(eq);   k_mode_trig = 1; }
    }
    if (k_nslots < 1) k_nslots = 1;
    if (k_nslots > MAX_SLOTS) k_nslots = MAX_SLOTS;
    if (k_voices < 1) k_voices = 1;
    if (k_voices > 32) k_voices = 32;
}

int main(int argc, char **argv)
{
    unsigned long out_rate;

    g_log = fopen("GUSSFX.LOG", "w");
    parse_args(argc, argv);
    out_rate = RATE_NUM / (unsigned long)k_voices;

    logln("==== GUSSFX GF1 SFX-upload + per-slot trigger probe (#38) ====");
    logln("gussfx: mode=%s base=0x%X irq=%d dma=%d V=%d (rate=%lu Hz) dur=%dms "
          "DRAM=%lu KB reserve=%lu KB N=%d seed=%d",
          k_mode_straddle ? "STRADDLE-AB" : (k_mode_trig ? "MAP+TRIG" : "MAP-only"),
          k_base, k_irq, k_dma, k_voices, out_rate, k_dur_ms,
          k_dram_kb, k_reserve_kb, k_nslots, k_seed);
    logln("gussfx: H1=8-bit bank-straddle (no guard in gus_dram_alloc); "
          "H2=engine len>buffer over-read. This probe is GF1-side only -> "
          "arbitrates H1; a clean run redirects to H2.");

    /* --- one-time bring-up (write-only; NO GF1 reads) -------------------- */
    logln("gussfx: [bring-up] reset chip + prime DRAM (write-only, no peek)...");
    gus_reset_chip(k_base);
    gus_prime_dram(k_base);
    if (k_latch) {
        logln("gussfx: [bring-up] program IRQ/DMA latches (irq=%d dma=%d)...", k_irq, k_dma);
        gus_program_irq_dma(k_base, k_irq, k_dma);
    } else {
        logln("gussfx: [bring-up] SKIP latch program (LATCH=0; pgusinit owns it)");
    }
    /* active-voice count (rate lever) + terminal reset (master+DAC+IRQ) */
    gus_w8(k_base, REG_VOICES, (unsigned char)(0xC0 | (k_voices - 1)));
    gus_w8(k_base, REG_RESET, 0x07);
    gus_w8(k_base, REG_RESET, 0x07);
    udelay(100);
    gus_silence_all_voices(k_base, 32);
    outportb(P2X0_MIXCTRL(k_base), 0x08);  /* line-out enabled (bit1=0) */

    /* --- run ------------------------------------------------------------- */
    if (k_mode_straddle) {
        do_straddle_ab(out_rate);
    } else {
        do_map_and_upload();
        if (k_mode_trig)
            trigger_uploaded(out_rate);
        else
            logln("gussfx: MAP-only -- no triggers (cannot crash). Run 'GUSSFX TRIG' to "
                  "pin the crashing slot, or 'GUSSFX STRADDLE' for the controlled H1 A/B.");
    }

    gus_w8(k_base, REG_RESET, 0x00);  /* park chip in reset */
    logln("gussfx: RESULT=DONE mode=%s -- the .LOG proves the stream ran; a quit-to-DOS "
          "leaves the culprit slot as the LAST line (no DONE line).",
          k_mode_straddle ? "straddle-ab" : (k_mode_trig ? "map+trig" : "map"));
    if (g_log) fclose(g_log);
    return 0;
}
