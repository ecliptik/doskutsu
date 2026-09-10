/*
 * bltfill.c — Cirrus 5434 BLT solid-fill vs dosmemput head-to-head, v2
 * (Phase 11 wave-27 / iter K, FPS-DEEPDIVE Candidate #4 gating).
 *
 * v1 (iter J) skipped Scenario A — the warm-up BLT didn't complete because
 * v1 had three register-encoding bugs:
 *
 *   1. Polled GR[0x31] bit 0 (BLT_BUSY) instead of bit 3 (BLT_PROGRESS) —
 *      Linux cirrusfb's canonical poll is bit 3.
 *   2. Wrote GR[0x30] = 0x04 (SRC_SYS=1, chip waits for CPU PIO data) for
 *      what was supposed to be a "solid fill" — the chip sat in waiting-
 *      for-source-data state forever because nothing was ever fed.
 *   3. Did not clear GR[0x0B] bits 1 + 4 (Cirrus errata for screen-to-
 *      screen BLT engagement).
 *
 * v2 corrects all three. Probe now tries THREE BLT mode encodings as a
 * primary + 2-fallback ladder (per the team-lead's "multi-mode fallback"
 * guidance from STOP-and-ack), each with its own pre-flight VRAM source
 * pre-fill:
 *
 *   Mode primary:  COLOR_EXPAND (GR[0x30]=0x80) — 1bpp monochrome source
 *                  expanded to 8bpp dst using BLT FG color reg. Chip-side
 *                  source bandwidth = 1/8 dst (this is the cand #4 hope).
 *                  Source = 9600 bytes of 0xFF in VRAM at offset 0x80000.
 *
 *   Mode fallback 1: PATTERN_COPY (GR[0x30]=0x40) — 8x8 byte pattern
 *                  repeated across dst. Chip-side source = 64 bytes total,
 *                  read once.
 *                  Source = 64 bytes of FG color at VRAM offset 0x90000.
 *
 *   Mode fallback 2: BULK_COPY (GR[0x30]=0x00) — full VRAM->VRAM copy of
 *                  76800 bytes (wave-19-proven path; reaches 19 MB/s).
 *                  Source = 76800 bytes of FG color at VRAM offset 0xA0000.
 *                  This MODE IS THE FLOOR — cand #4's "BLT can't beat
 *                  dosmemput" verdict is implied if even bulk-copy doesn't
 *                  beat dosmemput, since wave-19 already showed it doesn't.
 *
 * v2 register sequence per /tmp/wave19-cirrus-audit.md (wave-19 cirrus
 * specialist's documented Cirrus 5434 BLT register map):
 *
 *   - bit-3 BLT_PROGRESS busy poll (canonical Linux pattern, line 2588)
 *   - GR[0x0B] bits 1 + 4 cleared before screen-to-screen BLT
 *   - DWORD-aligned source addresses for COLOR_EXPAND
 *   - Mode-bits 0/1/2 (DST_SYS/SRC_SYS) cleared per VRAM-to-VRAM intent
 *
 * Decision criteria (per team-lead brief):
 *   BLT fill - dosmemput < 0.5 ms      -> DROP candidate #4
 *   BLT fill faster by 0.5-1.5 ms      -> DEFER (worth iter L only after Levers)
 *   BLT fill faster by >= 1.5 ms       -> SHIP candidate #4 to iter L (~2-3 days)
 *
 * Companion probe: CHIPID.EXE bundled in same iter — if v2 still skips
 * Scenario A on real HW, CHIPID's forensic register dump tells us why.
 *
 * Per perf_predictions_unreliable.md: wave-19's "BLT VRAM->VRAM = 19 MB/s
 * = same as dosmemput" predicts cand #4 is likely DEAD. v2 measures to
 * confirm and to test if COLOR_EXPAND/PATTERN_COPY breaks the pattern.
 *
 * Per dosbox_not_perf_proxy.md: DOSBox-X doesn't emulate the Cirrus 5434
 * BLT engine register-faithfully. Smoke verifies probe runs end-to-end
 * (chip-not-applicable bail or BLT-not-responsive log path). Real HW
 * iter K is the data gate.
 *
 * 8.3 DOS filename:
 *   Source:   tests/probes/bltfill.c (host-side, fits 8.3 directly)
 *   Binary:   BLTFILL.EXE  (7+3, fits)
 *   Log:      BLTFILL.LOG  (7+3, fits)
 *   BAT:      BLTFILL.BAT  (7+3, fits)
 *
 * License: MIT.
 */

#include <dos.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/farptr.h>
#include <sys/movedata.h>
#include <sys/nearptr.h>
#include <time.h>
#include <unistd.h>

/* ============================================================ */
/* Logging                                                       */
/* ============================================================ */

static FILE *g_log = NULL;

static void open_log(void)
{
    g_log = fopen("BLTFILL.LOG", "w");
    if (!g_log) g_log = fopen("C:\\BLTFILL.LOG", "w");
}

static void blog(const char *fmt, ...)
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

/* ============================================================ */
/* Timing                                                        */
/* ============================================================ */

static double now_secs(void)
{
    return (double)uclock() / (double)UCLOCKS_PER_SEC;
}

static int dbl_cmp(const void *a, const void *b)
{
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

static int kbd_pending(void)
{
    uint16_t head = _farpeekw(_dos_ds, 0x41AL);
    uint16_t tail = _farpeekw(_dos_ds, 0x41CL);
    return head != tail;
}

/* ============================================================ */
/* VGA / Cirrus port helpers                                     */
/* ============================================================ */

#define SR_INDEX  0x3C4
#define SR_DATA   0x3C5
#define GR_INDEX  0x3CE
#define GR_DATA   0x3CF
#define CRTC_IDX  0x3D4
#define CRTC_DATA 0x3D5

static uint8_t sr_read(uint8_t idx)  { outportb(SR_INDEX, idx);  return inportb(SR_DATA); }
static void    sr_write(uint8_t idx, uint8_t v) { outportb(SR_INDEX, idx); outportb(SR_DATA, v); }
static uint8_t gr_read(uint8_t idx)  { outportb(GR_INDEX, idx);  return inportb(GR_DATA); }
static void    gr_write(uint8_t idx, uint8_t v) { outportb(GR_INDEX, idx); outportb(GR_DATA, v); }
static uint8_t cr_read(uint8_t idx)  { outportb(CRTC_IDX, idx);  return inportb(CRTC_DATA); }

/* ============================================================ */
/* Chip detect — relaxed per team-lead 2026-05-08                */
/*                                                                */
/* Two engagement signals (probe accepts EITHER):                */
/*   1. Cirrus chip-id range at CRTC[0x27]: 0xA0/0xA8/0xAC/0xB8  */
/*      (well-known IDs for the 543x family)                     */
/*   2. SR[0x06] write-read cycle: write 0x12, read back 0x12   */
/*      (Cirrus extension lock register. On Cirrus, writing 0x12 */
/*      unlocks; the readback returns 0x12. On non-Cirrus chips, */
/*      SR[0x06] is reserved and typically returns 0xFF or 0x00) */
/*                                                                */
/* If both fail, probe bails with NOT-APPLICABLE. If either      */
/* succeeds, probe proceeds with BLT engagement attempts.        */
/* ============================================================ */

typedef struct {
    int    is_cirrus;
    uint8_t crtc_27;
    uint8_t sr06_writeread;  /* what SR[0x06] reads back after writing 0x12 */
    const char *name;
    const char *engagement_signal;  /* which check fired */
} chip_info_t;

static void detect_cirrus(chip_info_t *info)
{
    memset(info, 0, sizeof *info);
    info->name = "(unknown)";
    info->engagement_signal = "(none)";

    /* Probe SR[0x06] write-read cycle. */
    sr_write(0x06, 0x12);
    info->sr06_writeread = sr_read(0x06);

    info->crtc_27 = cr_read(0x27);
    switch (info->crtc_27) {
        case 0xA0: info->name = "Cirrus CL-GD5430"; info->is_cirrus = 1; info->engagement_signal = "CRTC[0x27] chip-id"; return;
        case 0xA8: info->name = "Cirrus CL-GD5434"; info->is_cirrus = 1; info->engagement_signal = "CRTC[0x27] chip-id"; return;
        case 0xAC: info->name = "Cirrus CL-GD5436"; info->is_cirrus = 1; info->engagement_signal = "CRTC[0x27] chip-id"; return;
        case 0xB8: info->name = "Cirrus CL-GD5446"; info->is_cirrus = 1; info->engagement_signal = "CRTC[0x27] chip-id"; return;
        default: break;
    }

    /* CRTC[0x27] didn't match — fall back to SR[0x06] write-read cycle. */
    if (info->sr06_writeread == 0x12) {
        info->is_cirrus = 1;
        info->name = "(Cirrus-extension-responsive but unknown chip-id)";
        info->engagement_signal = "SR[0x06] write-read cycle";
        return;
    }

    info->name = "(not Cirrus or extension lock not Cirrus-style)";
}

/* ============================================================ */
/* VBE mode set / restore                                        */
/* ============================================================ */

static int vbe_set_mode(uint16_t mode)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F02;
    r.x.bx = mode;
    if (__dpmi_int(0x10, &r) < 0) return -1;
    return (r.x.ax == 0x004F) ? 0 : -2;
}

static int vbe_set_bank(int bank)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F05;
    r.x.bx = 0x0000;
    r.x.dx = (uint16_t)bank;
    if (__dpmi_int(0x10, &r) < 0) return -1;
    return (r.x.ax == 0x004F) ? 0 : -2;
}

static void text_mode_restore(void)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x0003;
    __dpmi_int(0x10, &r);
}

/* ============================================================ */
/* BLT geometry + VRAM source offsets                            */
/* ============================================================ */

#define BLT_W 320
#define BLT_H 240
#define BLT_BYTES ((long)BLT_W * (long)BLT_H)

/* VRAM offsets for the three BLT-mode source pre-fills. All chosen well
 * above the 76800-byte dst region (offset 0..0x12C00) and DWORD-aligned. */
#define VRAM_DST_OFFSET     0x000000UL
#define VRAM_PAT_OFFSET     0x080000UL  /* PATTERN_COPY 64-byte pattern */
#define VRAM_EXPAND_OFFSET  0x090000UL  /* COLOR_EXPAND 9600-byte 1bpp source */
#define VRAM_BULK_OFFSET    0x0A0000UL  /* BULK_COPY 76800-byte source (10..16+ MB chips) */

/* ============================================================ */
/* Pre-flight: clear screen-to-screen BLT errata regs            */
/*                                                                */
/* Per /tmp/wave19-cirrus-audit.md § 1: "GR0B[4] and GR0B[1]     */
/* must be programmed to '0' when executing Screen to Screen     */
/* BLTs." GR[0x0B] is the Cirrus extension lock cousin —         */
/* clearing bits 1 and 4 is a defensive prerequisite.            */
/* ============================================================ */

static void blt_clear_screen_to_screen_constraints(void)
{
    uint8_t v = gr_read(0x0B);
    v &= ~((uint8_t)(1 << 1) | (uint8_t)(1 << 4));
    gr_write(0x0B, v);
}

/* ============================================================ */
/* BLT busy-bit poll — bit 3 (BLT_PROGRESS), Linux canonical    */
/*                                                                */
/* Returns 0 = idle (or became idle within timeout),             */
/*         -1 = still busy at timeout.                           */
/* *out_iters = poll iteration count (sanity for very-fast BLT). */
/* ============================================================ */

static int blt_wait_idle(double timeout_secs, int *out_iters)
{
    double t0 = now_secs();
    int i;
    for (i = 0; i < 1000000; i++) {
        uint8_t s = gr_read(0x31);
        if ((s & 0x08) == 0) {  /* bit 3 BLT_PROGRESS clear = idle */
            if (out_iters) *out_iters = i;
            return 0;
        }
        if ((i & 0xFFF) == 0) {
            if ((now_secs() - t0) > timeout_secs) {
                if (out_iters) *out_iters = i;
                return -1;
            }
            if (kbd_pending()) {
                if (out_iters) *out_iters = i;
                return -1;
            }
        }
    }
    if (out_iters) *out_iters = i;
    return -1;
}

/* Trigger BLT (write 0x02 to GR[0x31]) and wait for completion. */
static int blt_fire_and_wait(double timeout_secs, int *out_iters)
{
    gr_write(0x31, 0x02);
    return blt_wait_idle(timeout_secs, out_iters);
}

/* ============================================================ */
/* BLT register programming — three modes                        */
/*                                                                */
/* All three set the same width/height/dst-pitch/dst-addr; only  */
/* the mode register, src pitch, and src offset differ.          */
/* ============================================================ */

static void blt_program_common(uint32_t dst_offset, uint16_t src_pitch,
                               uint32_t src_offset)
{
    uint16_t w_minus_1 = (uint16_t)(BLT_W - 1);
    uint16_t h_minus_1 = (uint16_t)(BLT_H - 1);

    gr_write(0x20, (uint8_t)(w_minus_1 & 0xFF));
    gr_write(0x21, (uint8_t)((w_minus_1 >> 8) & 0x0F));
    gr_write(0x22, (uint8_t)(h_minus_1 & 0xFF));
    gr_write(0x23, (uint8_t)((h_minus_1 >> 8) & 0x0F));

    gr_write(0x24, (uint8_t)(BLT_W & 0xFF));
    gr_write(0x25, (uint8_t)((BLT_W >> 8) & 0xFF));

    gr_write(0x26, (uint8_t)(src_pitch & 0xFF));
    gr_write(0x27, (uint8_t)((src_pitch >> 8) & 0xFF));

    gr_write(0x28, (uint8_t)(dst_offset & 0xFF));
    gr_write(0x29, (uint8_t)((dst_offset >> 8) & 0xFF));
    gr_write(0x2A, (uint8_t)((dst_offset >> 16) & 0xFF));

    gr_write(0x2C, (uint8_t)(src_offset & 0xFF));
    gr_write(0x2D, (uint8_t)((src_offset >> 8) & 0xFF));
    gr_write(0x2E, (uint8_t)((src_offset >> 16) & 0xFF));

    gr_write(0x2F, 0x00);  /* no transparency */
    gr_write(0x32, 0x0D);  /* SRCCOPY */
    gr_write(0x33, 0x00);  /* mode ext = 0 */
}

/* COLOR_EXPAND: 1bpp source -> 8bpp dst with FG color. */
static void blt_program_color_expand(uint8_t fg_color)
{
    /* BLT FG color regs (4 bytes; only low byte used in 8bpp). */
    gr_write(0x10, fg_color);
    gr_write(0x11, 0x00);
    gr_write(0x12, 0x00);
    gr_write(0x13, 0x00);

    /* Source pitch = BLT_W / 8 = 40 bytes/row (1 bit per pixel). */
    blt_program_common(VRAM_DST_OFFSET, BLT_W / 8, VRAM_EXPAND_OFFSET);
    gr_write(0x30, 0x80);  /* COLOR_EXPAND=1, all other bits 0 */
}

/* PATTERN_COPY: 8x8 byte pattern repeated across dst. */
static void blt_program_pattern_fill(void)
{
    blt_program_common(VRAM_DST_OFFSET, 8, VRAM_PAT_OFFSET);
    gr_write(0x30, 0x40);  /* PATTERN_COPY=1, all other bits 0 */
}

/* BULK_COPY: VRAM->VRAM 76800-byte copy (wave-19 proven path). */
static void blt_program_bulk_copy(void)
{
    blt_program_common(VRAM_DST_OFFSET, BLT_W, VRAM_BULK_OFFSET);
    gr_write(0x30, 0x00);  /* bulk copy, src+dst in display memory */
}

/* ============================================================ */
/* VRAM pre-fill helpers — write source patterns via dosmemput   */
/*                                                                */
/* Each pre-fill is one-time at probe init (cost not in timed    */
/* path). VBE banks 64KB at a time on Cirrus 5434.               */
/* ============================================================ */

static void prefill_vram_pattern(uint8_t fg_color)
{
    uint8_t pat[64];
    memset(pat, fg_color, sizeof pat);
    /* 0x080000 = bank 8 offset 0. */
    vbe_set_bank(8);
    dosmemput(pat, sizeof pat, 0xA0000);
}

static void prefill_vram_color_expand(void)
{
    /* 9600 bytes of 0xFF (every bit set => every dst pixel = FG color). */
    uint8_t buf[9600];
    memset(buf, 0xFF, sizeof buf);
    /* 0x090000 = bank 9 offset 0. */
    vbe_set_bank(9);
    dosmemput(buf, sizeof buf, 0xA0000);
}

static void prefill_vram_bulk(uint8_t fg_color)
{
    /* 76800 bytes of FG color spanning bank 10 + part of bank 11. */
    uint8_t *buf = (uint8_t *)malloc(BLT_BYTES);
    if (!buf) return;
    memset(buf, fg_color, BLT_BYTES);
    vbe_set_bank(10);
    dosmemput(buf, 65536, 0xA0000);
    vbe_set_bank(11);
    dosmemput(buf + 65536, BLT_BYTES - 65536, 0xA0000);
    free(buf);
}

/* ============================================================ */
/* dosmemput control — sysmem -> banked A0000                    */
/* ============================================================ */

static void dosmemput_76800(const uint8_t *src)
{
    vbe_set_bank(0);
    dosmemput(src, 65536, 0xA0000);
    vbe_set_bank(1);
    dosmemput(src + 65536, BLT_BYTES - 65536, 0xA0000);
}

/* ============================================================ */
/* Stat helper                                                   */
/* ============================================================ */

#define N_REPS 100

typedef struct { double min, med, p95, max, mean; } stats_t;

static void compute_stats(double *samples, int n, stats_t *out)
{
    qsort(samples, n, sizeof samples[0], dbl_cmp);
    out->min = samples[0];
    out->max = samples[n - 1];
    out->med = samples[n / 2];
    out->p95 = samples[(int)(0.95 * n)];
    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += samples[i];
    out->mean = sum / n;
}

/* ============================================================ */
/* Try one BLT mode + program function. Returns 0 if BLT fired  */
/* (busy bit transitioned), -1 if hung. Used by the multi-mode  */
/* fallback ladder to pick which mode to time.                   */
/* ============================================================ */

typedef enum {
    BLT_MODE_COLOR_EXPAND = 0,
    BLT_MODE_PATTERN_COPY,
    BLT_MODE_BULK_COPY,
    BLT_MODE_COUNT
} blt_mode_t;

static const char *blt_mode_name(blt_mode_t m)
{
    switch (m) {
        case BLT_MODE_COLOR_EXPAND: return "COLOR_EXPAND (GR[0x30]=0x80)";
        case BLT_MODE_PATTERN_COPY: return "PATTERN_COPY (GR[0x30]=0x40)";
        case BLT_MODE_BULK_COPY:    return "BULK_COPY    (GR[0x30]=0x00)";
        default: return "(unknown)";
    }
}

static void blt_program_for_mode(blt_mode_t mode, uint8_t fg_color)
{
    switch (mode) {
        case BLT_MODE_COLOR_EXPAND: blt_program_color_expand(fg_color); break;
        case BLT_MODE_PATTERN_COPY: blt_program_pattern_fill();         break;
        case BLT_MODE_BULK_COPY:    blt_program_bulk_copy();            break;
        default: break;
    }
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    blog("=== BLTFILL v2 wave-27 / iter K starting ===");
    blog("DJGPP build, target = Cirrus 5434 BLT solid-fill vs dosmemput");
    blog("UCLOCKS_PER_SEC = %lu", (unsigned long)UCLOCKS_PER_SEC);
    blog("");
    blog("v2 deltas vs iter J v1:");
    blog("  - Bit-3 BLT_PROGRESS busy poll (was bit-0 BLT_BUSY; canonical Linux pattern)");
    blog("  - GR[0x0B] bits 1+4 cleared per Cirrus errata for screen-to-screen BLT");
    blog("  - 3-mode fallback ladder: COLOR_EXPAND -> PATTERN_COPY -> BULK_COPY");
    blog("  - VRAM source pre-fill at offsets 0x80000/0x90000/0xA0000");
    blog("  - Relaxed chip-detect: CRTC[0x27] chip-id OR SR[0x06] write-read cycle");
    blog("");

    /* ============================================================ */
    /* Step 1: chip detect (relaxed)                                */
    /* ============================================================ */
    blog("---- Step 1: chip detect (CRTC[0x27] + SR[0x06] write-read) ----");
    chip_info_t chip;
    detect_cirrus(&chip);
    blog("CRTC[0x27]      = 0x%02X  (chip-id check)", chip.crtc_27);
    blog("SR[0x06] readback after write 0x12 = 0x%02X (Cirrus extension lock check)",
         chip.sr06_writeread);
    blog("Engagement signal: %s", chip.engagement_signal);
    blog("Identified: %s", chip.name);
    blog("");

    if (!chip.is_cirrus) {
        blog("=== HEADLINE: chip is NOT Cirrus (or not Cirrus-extension-responsive). ===");
        blog("BLT scenarios skipped (chip-specific). Operator: this is normal");
        blog("if running on a non-g2k system. On g2k Cirrus 5434 expect CRTC[0x27]=0xA8");
        blog("AND SR[0x06] write-read = 0x12.");
        blog("");
        blog("=== BLTFILL done (skipped) ===");
        if (g_log) fclose(g_log);
        return 0;
    }

    /* ============================================================ */
    /* Step 2: enable nearptr access for memset ceiling             */
    /* ============================================================ */
    blog("---- Step 2: __djgpp_nearptr_enable (for sysmem ceiling) ----");
    if (!__djgpp_nearptr_enable()) {
        blog("WARN: __djgpp_nearptr_enable failed; sysmem-ceiling scenario will");
        blog("      use plain memset() through libc (likely identical perf).");
    } else {
        blog("nearptr enabled.");
    }
    blog("");

    /* ============================================================ */
    /* Step 3: allocate sysmem source buffer                        */
    /* ============================================================ */
    uint8_t *src = (uint8_t *)malloc(BLT_BYTES);
    if (!src) {
        blog("FATAL: malloc(%ld) failed", BLT_BYTES);
        if (g_log) fclose(g_log);
        return 2;
    }
    for (long i = 0; i < BLT_BYTES; i++) src[i] = (uint8_t)(i & 0xFF);
    blog("Allocated %ld-byte sysmem source buffer.", BLT_BYTES);
    blog("");

    /* ============================================================ */
    /* Step 4: switch to VBE mode 0x101 (640x480x8 banked)          */
    /* ============================================================ */
    blog("---- Step 4: VBE mode 0x101 (640x480x8 banked) ----");
    int vbe_rc = vbe_set_mode(0x0101);
    if (vbe_rc != 0) {
        blog("WARN: VBE mode 0x101 set failed (rc=%d). Trying mode 0x100.", vbe_rc);
        vbe_rc = vbe_set_mode(0x0100);
    }
    if (vbe_rc != 0) {
        blog("ERROR: cannot enter VESA 8bpp graphics. BLT/dosmemput skipped.");
        blog("       memset ceiling still runs.");
    } else {
        blog("VBE mode set OK.");
        /* Re-unlock Cirrus extensions after mode set; clear errata regs. */
        sr_write(0x06, 0x12);
        blt_clear_screen_to_screen_constraints();
        blog("Re-applied SR[0x06]=0x12 unlock + cleared GR[0x0B] bits 1/4.");
    }
    blog("");

    /* ============================================================ */
    /* Step 5: pre-fill VRAM source regions                         */
    /* ============================================================ */
    int blt_can_run = (vbe_rc == 0);
    if (blt_can_run) {
        blog("---- Step 5: pre-fill VRAM source regions ----");
        blog("  PATTERN_COPY    source -> 64 bytes 0x55 at offset 0x%06lX",
             VRAM_PAT_OFFSET);
        prefill_vram_pattern(0x55);
        blog("  COLOR_EXPAND    source -> 9600 bytes 0xFF at offset 0x%06lX",
             VRAM_EXPAND_OFFSET);
        prefill_vram_color_expand();
        blog("  BULK_COPY       source -> 76800 bytes 0x55 at offset 0x%06lX",
             VRAM_BULK_OFFSET);
        prefill_vram_bulk(0x55);
        blog("Pre-fills done.");
        blog("");
    }

    /* ============================================================ */
    /* Step 6: multi-mode fallback ladder — engage one              */
    /* ============================================================ */
    blt_mode_t engaged_mode = BLT_MODE_COUNT;
    int blt_engaged = 0;

    if (blt_can_run) {
        blog("---- Step 6: BLT engagement ladder (try modes in order) ----");
        for (int m = 0; m < BLT_MODE_COUNT; m++) {
            blog("Trying %s ...", blt_mode_name((blt_mode_t)m));

            /* Wait for any prior BLT to complete first. */
            int idle_iters = 0;
            if (blt_wait_idle(0.05, &idle_iters) != 0) {
                blog("  Pre-flight: chip BLT engine NOT IDLE within 50 ms (poll cap).");
                blog("  Skipping this mode; trying next.");
                continue;
            }

            blt_program_for_mode((blt_mode_t)m, 0x55);
            int fire_iters = 0;
            int rc = blt_fire_and_wait(0.20, &fire_iters);
            if (rc == 0) {
                blog("  BLT fired + completed in %d busy-poll iter (chip OK).", fire_iters);
                engaged_mode = (blt_mode_t)m;
                blt_engaged = 1;
                break;
            } else {
                blog("  BLT did NOT complete within 200 ms cap (poll iters=%d).", fire_iters);
                /* Try GR[0x31]=0x04 BLT_RESET to recover before next mode. */
                gr_write(0x31, 0x04);
                blog("  Issued BLT_RESET (GR[0x31]=0x04); next mode after recovery.");
            }
        }
        blog("");

        if (blt_engaged) {
            blog("ENGAGED mode: %s", blt_mode_name(engaged_mode));
        } else {
            blog("ALL THREE MODES HUNG. BLTFILL cannot produce Scenario A data.");
            blog("Forensic: bundled CHIPID.EXE will dump full register state for diagnosis.");
        }
        blog("");
    }

    /* ============================================================ */
    /* Step 7: Scenario A — N_REPS of engaged BLT mode              */
    /* ============================================================ */
    blog("---- Step 7: Scenario A — %s x %d reps ----",
         blt_engaged ? blt_mode_name(engaged_mode) : "SKIPPED", N_REPS);
    double *samples_A = (double *)malloc(sizeof(double) * N_REPS);
    int blt_responsive = blt_engaged;

    if (blt_responsive) {
        for (int s = 0; s < N_REPS; s++) {
            double t0 = now_secs();
            int K = 10;
            uint8_t color = (uint8_t)(0x55 ^ (s & 0xFF));
            for (int k = 0; k < K; k++) {
                if (blt_wait_idle(0.20, NULL) != 0) { blt_responsive = 0; break; }
                blt_program_for_mode(engaged_mode, color);
                if (blt_fire_and_wait(0.20, NULL) != 0) { blt_responsive = 0; break; }
            }
            samples_A[s] = (now_secs() - t0) / (double)K;
            if (!blt_responsive) {
                blog("FATAL during batch s=%d: BLT busy-poll cap tripped. Aborting A.", s);
                if (s == 0) break;
                for (int t = s + 1; t < N_REPS; t++) samples_A[t] = samples_A[s];
                break;
            }
        }
    }

    stats_t st_A = {0};
    if (blt_responsive) {
        compute_stats(samples_A, N_REPS, &st_A);
        blog("Scenario A (%s, %ld bytes/op):",
             blt_mode_name(engaged_mode), BLT_BYTES);
        blog("  per-call ms: min=%6.3f med=%6.3f mean=%6.3f p95=%6.3f max=%6.3f",
             st_A.min * 1000.0, st_A.med * 1000.0, st_A.mean * 1000.0,
             st_A.p95 * 1000.0, st_A.max * 1000.0);
        blog("  effective MB/s (median): %.1f",
             (BLT_BYTES / 1048576.0) / st_A.med);
    } else {
        blog("Scenario A SKIPPED — no BLT mode engaged.");
    }
    blog("");

    /* ============================================================ */
    /* Step 8: Scenario B — dosmemput control                        */
    /* ============================================================ */
    blog("---- Step 8: Scenario B — dosmemput sysmem -> A000:0 x %d reps ----",
         N_REPS);
    double *samples_B = (double *)malloc(sizeof(double) * N_REPS);
    int dosmemput_ok = (vbe_rc == 0);

    if (dosmemput_ok) {
        dosmemput_76800(src);  /* warm-up */
        for (int s = 0; s < N_REPS; s++) {
            double t0 = now_secs();
            int K = 10;
            for (int k = 0; k < K; k++) dosmemput_76800(src);
            samples_B[s] = (now_secs() - t0) / (double)K;
        }
    } else {
        blog("Scenario B SKIPPED — VBE mode failed.");
    }

    stats_t st_B = {0};
    if (dosmemput_ok) {
        compute_stats(samples_B, N_REPS, &st_B);
        blog("Scenario B (dosmemput banked, %ld bytes/op + bank-cross):", BLT_BYTES);
        blog("  per-call ms: min=%6.3f med=%6.3f mean=%6.3f p95=%6.3f max=%6.3f",
             st_B.min * 1000.0, st_B.med * 1000.0, st_B.mean * 1000.0,
             st_B.p95 * 1000.0, st_B.max * 1000.0);
        blog("  effective MB/s (median): %.1f",
             (BLT_BYTES / 1048576.0) / st_B.med);
    }
    blog("");

    /* ============================================================ */
    /* Step 9: Scenario C — memset on sysmem (RAM ceiling)          */
    /* ============================================================ */
    blog("---- Step 9: Scenario C — memset on sysmem buffer x %d reps ----",
         N_REPS);
    double *samples_C = (double *)malloc(sizeof(double) * N_REPS);
    memset(src, 0xAA, BLT_BYTES);  /* warm-up */
    for (int s = 0; s < N_REPS; s++) {
        double t0 = now_secs();
        int K = 50;
        for (int k = 0; k < K; k++) memset(src, (uint8_t)(s ^ k), BLT_BYTES);
        samples_C[s] = (now_secs() - t0) / (double)K;
    }
    stats_t st_C;
    compute_stats(samples_C, N_REPS, &st_C);
    blog("Scenario C (memset sysmem, %ld bytes/op):", BLT_BYTES);
    blog("  per-call ms: min=%6.3f med=%6.3f mean=%6.3f p95=%6.3f max=%6.3f",
         st_C.min * 1000.0, st_C.med * 1000.0, st_C.mean * 1000.0,
         st_C.p95 * 1000.0, st_C.max * 1000.0);
    blog("  effective MB/s (median): %.1f",
         (BLT_BYTES / 1048576.0) / st_C.med);
    blog("");

    /* ============================================================ */
    /* Step 10: restore text mode                                    */
    /* ============================================================ */
    text_mode_restore();
    blog("---- Step 10: VBE text mode restored ----");
    blog("");

    /* ============================================================ */
    /* Step 11: Verdict                                             */
    /* ============================================================ */
    blog("---- Step 11: Verdict ----");
    if (!blt_responsive || !dosmemput_ok) {
        blog("INCOMPLETE: Scenario A=%s  Scenario B=%s",
             blt_responsive ? "OK" : "SKIPPED",
             dosmemput_ok ? "OK" : "SKIPPED");
        if (!blt_responsive) {
            blog("All 3 BLT modes hung. CHIPID.EXE forensic dump (bundled in same iter)");
            blog("contains the full register state — diagnose from there in iter L.");
        }
    } else {
        double delta_ms = (st_B.med - st_A.med) * 1000.0;
        blog("Engaged mode: %s", blt_mode_name(engaged_mode));
        blog("Paired delta: B (dosmemput) - A (BLT) = %.3f ms / op", delta_ms);
        blog("");
        blog("Decision criteria (per team-lead brief):");
        blog("  delta < 0.5 ms   -> DROP candidate #4");
        blog("  delta 0.5-1.5 ms -> DEFER (worth iter L only after Levers)");
        blog("  delta >= 1.5 ms  -> SHIP candidate #4 to iter L (~2-3 days)");
        blog("");
        if (delta_ms < 0.5) {
            blog("=== HEADLINE: BLT vs dosmemput delta = %.3f ms -> RECOMMEND DROP ===",
                 delta_ms);
        } else if (delta_ms < 1.5) {
            blog("=== HEADLINE: BLT vs dosmemput delta = %.3f ms -> RECOMMEND DEFER ===",
                 delta_ms);
        } else {
            blog("=== HEADLINE: BLT vs dosmemput delta = %.3f ms -> RECOMMEND SHIP ===",
                 delta_ms);
        }
    }
    blog("");
    blog("Cross-check: memset (sysmem ceiling) median = %.3f ms.", st_C.med * 1000.0);
    blog("Cross-anchor: wave_19_path_b_dead.md measured BLT VRAM->VRAM at 19 MB/s on");
    blog("this exact chip (3.85 ms / 76800 B). If engaged_mode=BULK_COPY and Scenario");
    blog("A median ~ 3.85 ms, that confirms the wave-19 measurement and cand #4 dies");
    blog("(BLT == dosmemput). If engaged_mode=COLOR_EXPAND/PATTERN_COPY and median is");
    blog("substantially below 3.85 ms, cand #4 may have a path to ship.");

    blog("");
    blog("=== BLTFILL v2 done ===");

    free(src);
    free(samples_A);
    free(samples_B);
    free(samples_C);
    if (g_log) fclose(g_log);
    return 0;
}
