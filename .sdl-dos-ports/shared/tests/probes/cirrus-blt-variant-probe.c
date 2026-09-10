/*
 * cirrus-blt-variant-probe.c -- Cirrus 5434 BLT BULK_COPY systematic
 * variant matrix (Phase 11 wave-36 Task A; root-cause investigation
 * of BLTASYNC v2 REFUTE_VERIFY_FAIL).
 *
 * CONTEXT: BLTASYNC v2 on real-HW PODP83 + Cirrus 5434 returned
 * 99.1% cpu_parallel_pct (chip IS doing 3.1 ms of work in parallel)
 * BUT verify_status = FAIL_FIRST in BOTH Scenario D (GR[0x33]=0x00)
 * AND Scenario E (GR[0x33]=0x80). The chip is running BUT not
 * producing correct destination data in BULK_COPY mode.
 *
 * Contrast: BLTFILL same-iter used COLOR_EXPAND mode (GR[0x30]=0x80)
 * and WORKED at 43 MB/s (2.2x dosmemput). So Cirrus 5434 BLT engine
 * is NOT generally broken; BULK_COPY mode specifically is failing
 * with our register sequence.
 *
 * GOAL: identify a BULK_COPY variant that produces correct dst data
 * (or definitively refute BULK_COPY on this chip + recommend alt
 * mechanism). Each variant: program registers, kick BLT, wait idle,
 * dosmemget dst first-16 + last-16 hex bytes, emit raw.
 *
 * No timing/parallelism testing -- that was v2's job. v3 is purely
 * correctness-discovery. team-lead at decomp time picks which
 * variant (if any) produces correct output by inspecting the raw
 * hex bytes against the source-pattern + per-variant expectation.
 *
 * VARIANTS (8 total):
 *   V1 baseline_mode00       BULK_COPY+0x00, src@0x012C00, no extras (v2 control)
 *   V2 src_at_0xA0000        BULK_COPY+0x00, src@0x0A0000 (wave-19 original)
 *   V3 reset_before          BULK_COPY+0x00, src@0x012C00, BLT_RESET pre-programming
 *   V4 color_regs_cleared    BULK_COPY+0x00, src@0x012C00, GR[0x34..0x39]=0
 *   V5 mode_ext_async        BULK_COPY+0x80, src@0x012C00 (reproduces v2 Scenario E)
 *   V6 pattern_copy          PATTERN_COPY+0x00, pattern@0x025800 (alt mode)
 *   V7 color_expand_ref      COLOR_EXPAND+0x00, FG=0xAA (known-working anchor)
 *   V8 dst_at_offscreen      BULK_COPY+0x00, dst@0x040000 (offscreen target)
 *
 * Expected dst-byte patterns:
 *   V1, V2, V3, V4, V5, V8 (BULK_COPY): if correct, dst[N] = source[N] for N in [0,76800)
 *     = linear gradient i&0xFF -> dst[0..15] = 0x00..0x0F; dst[12BF0..12BFF] = 0xF0..0xFF
 *   V6 (PATTERN_COPY): 64-byte pattern tiled across dst (semantics chip-specific)
 *   V7 (COLOR_EXPAND): all dst bytes = FG color (0xAA)
 *
 * The probe emits raw dst-bytes; decomp interprets per-variant.
 *
 * 8.3 DOS filenames:
 *   Source: tests/probes/cirrus-blt-variant-probe.c
 *   Binary: BLTVAR.EXE (6+3)
 *   Log:    BLTVAR.LOG (6+3)
 *   BAT:    BLTVAR.BAT (6+3)
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
#include <time.h>
#include <unistd.h>

/* ============================================================ */
/* Logging                                                       */
/* ============================================================ */

static FILE *g_log = NULL;

static void open_log(void)
{
    g_log = fopen("BLTVAR.LOG", "w");
    if (!g_log) g_log = fopen("C:\\BLTVAR.LOG", "w");
}

static void plog(const char *fmt, ...)
{
    char buf[640];
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
/* Timing (RDTSC via uclock calibration; lightweight; v3 needs   */
/* per-variant blt_us to know if the engine engaged at all)      */
/* ============================================================ */

static double g_us_per_cycle = 0.0;
static uint32_t g_cpu_mhz = 0;

static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static double cycles_to_us(uint64_t cycles)
{
    if (g_us_per_cycle <= 0.0) return -1.0;
    return (double)cycles * g_us_per_cycle;
}

static double now_secs(void)
{
    return (double)uclock() / (double)UCLOCKS_PER_SEC;
}

static void calibrate_rdtsc(void)
{
    double t0 = now_secs();
    uint64_t c0 = rdtsc();
    while (now_secs() - t0 < 0.100) { /* spin */ }
    double t1 = now_secs();
    uint64_t c1 = rdtsc();
    double secs = t1 - t0;
    if (secs <= 0.0) return;
    double cpu_hz = (double)(c1 - c0) / secs;
    g_us_per_cycle = 1e6 / cpu_hz;
    g_cpu_mhz = (uint32_t)(cpu_hz / 1e6);
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

static uint8_t sr_read(uint8_t idx)  { outportb(SR_INDEX, idx); return inportb(SR_DATA); }
static void    sr_write(uint8_t idx, uint8_t v) { outportb(SR_INDEX, idx); outportb(SR_DATA, v); }
static uint8_t gr_read(uint8_t idx)  { outportb(GR_INDEX, idx); return inportb(GR_DATA); }
static void    gr_write(uint8_t idx, uint8_t v) { outportb(GR_INDEX, idx); outportb(GR_DATA, v); }
static uint8_t cr_read(uint8_t idx)  { outportb(CRTC_IDX, idx); return inportb(CRTC_DATA); }

static int detect_cirrus(uint8_t *out_crtc27, const char **out_name)
{
    sr_write(0x06, 0x12);
    uint8_t sr06 = sr_read(0x06);
    uint8_t crtc27 = cr_read(0x27);
    *out_crtc27 = crtc27;
    switch (crtc27) {
        case 0xA0: *out_name = "Cirrus CL-GD5430"; return 1;
        case 0xA8: *out_name = "Cirrus CL-GD5434 (g2k expected)"; return 1;
        case 0xAC: *out_name = "Cirrus CL-GD5436"; return 1;
        case 0xB8: *out_name = "Cirrus CL-GD5446"; return 1;
        default:   break;
    }
    if (sr06 == 0x12) {
        *out_name = "(Cirrus-extension-responsive, unknown chip-id)";
        return 1;
    }
    *out_name = "(NOT Cirrus or extension lock not Cirrus-style)";
    return 0;
}

/* ============================================================ */
/* VBE mode helpers                                              */
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
/* BLT geometry + source-region offsets                          */
/* ============================================================ */

#define BLT_W       320
#define BLT_H       240
#define BLT_BYTES   ((long)BLT_W * (long)BLT_H)  /* 76800 */

#define VRAM_DST_DEFAULT    0x000000UL  /* visible FB */
#define VRAM_DST_OFFSCREEN  0x040000UL  /* offscreen dst (V8) */
#define VRAM_SRC_CANONICAL  0x012C00UL  /* right after visible-FB region */
#define VRAM_SRC_HIGH       0x0A0000UL  /* wave-19 original src position (V2) */
#define VRAM_PATTERN_OFFSET 0x025800UL  /* 64-byte pattern for V6 */

/* ============================================================ */
/* Pre-fill helpers                                              */
/* ============================================================ */

/* Linear gradient pre-fill at given VRAM offset; 76800 bytes total.
 * Handles bank crossing per VBE banked-mode 64KB windows. */
static void prefill_linear_gradient_at(uint32_t vram_off)
{
    uint8_t *buf = (uint8_t *)malloc(BLT_BYTES);
    if (!buf) return;
    for (long i = 0; i < BLT_BYTES; i++) {
        buf[i] = (uint8_t)(i & 0xFF);
    }

    /* Write 76800 bytes to vram_off..vram_off+76800 via VBE banks. */
    long remaining = BLT_BYTES;
    long buf_pos = 0;
    uint32_t cur_off = vram_off;
    while (remaining > 0) {
        int bank = (int)(cur_off >> 16);
        uint32_t off_in_bank = cur_off & 0xFFFF;
        long bytes_in_bank = 0x10000 - (long)off_in_bank;
        if (bytes_in_bank > remaining) bytes_in_bank = remaining;
        vbe_set_bank(bank);
        dosmemput(buf + buf_pos, bytes_in_bank, 0xA0000UL + off_in_bank);
        buf_pos += bytes_in_bank;
        remaining -= bytes_in_bank;
        cur_off += (uint32_t)bytes_in_bank;
    }
    free(buf);
}

/* Pre-fill a 64-byte pattern at VRAM_PATTERN_OFFSET for V6 (PATTERN_COPY). */
static void prefill_pattern_64_at(uint32_t vram_off)
{
    uint8_t pat[64];
    for (int i = 0; i < 64; i++) pat[i] = (uint8_t)i;  /* gradient 0..63 */
    int bank = (int)(vram_off >> 16);
    uint32_t off_in_bank = vram_off & 0xFFFF;
    vbe_set_bank(bank);
    dosmemput(pat, sizeof pat, 0xA0000UL + off_in_bank);
}

/* Clear dst region (76800 bytes of 0xCC sentinel) BEFORE each variant,
 * so leftover successful state from a prior variant can't false-PASS
 * a broken variant. 0xCC is not in our gradient (max gradient byte is
 * 0xFF for the last source byte; 0x00 for the first) so we can tell
 * "nothing written" from "wrong bytes written". */
static void clear_dst_to_sentinel(uint32_t dst_off)
{
    uint8_t *buf = (uint8_t *)malloc(BLT_BYTES);
    if (!buf) return;
    memset(buf, 0xCC, BLT_BYTES);
    long remaining = BLT_BYTES;
    long buf_pos = 0;
    uint32_t cur_off = dst_off;
    while (remaining > 0) {
        int bank = (int)(cur_off >> 16);
        uint32_t off_in_bank = cur_off & 0xFFFF;
        long bytes_in_bank = 0x10000 - (long)off_in_bank;
        if (bytes_in_bank > remaining) bytes_in_bank = remaining;
        vbe_set_bank(bank);
        dosmemput(buf + buf_pos, bytes_in_bank, 0xA0000UL + off_in_bank);
        buf_pos += bytes_in_bank;
        remaining -= bytes_in_bank;
        cur_off += (uint32_t)bytes_in_bank;
    }
    free(buf);
}

/* Read 16 bytes from VRAM at given offset via dosmemget + bank-switch. */
static void read_vram_16(uint32_t vram_off, uint8_t out[16])
{
    int bank = (int)(vram_off >> 16);
    uint32_t off_in_bank = vram_off & 0xFFFF;
    vbe_set_bank(bank);
    dosmemget(0xA0000UL + off_in_bank, 16, out);
}

/* Format 16 bytes as space-separated hex string. */
static void format_hex16(const uint8_t b[16], char out[64])
{
    int n = 0;
    for (int i = 0; i < 16; i++) {
        n += snprintf(out + n, 64 - n, "%02X%s",
                      b[i], (i < 15) ? " " : "");
    }
}

/* ============================================================ */
/* BLT register programming (helper; per-variant calls)          */
/* ============================================================ */

static void blt_clear_screen_to_screen_constraints(void)
{
    uint8_t v = gr_read(0x0B);
    v &= ~((uint8_t)(1 << 1) | (uint8_t)(1 << 4));
    gr_write(0x0B, v);
}

static void blt_program_registers(uint32_t dst_off, uint32_t src_off,
                                  uint16_t src_pitch_override,
                                  uint8_t  gr_30_mode,
                                  uint8_t  gr_33_ext)
{
    uint16_t w_m1 = (uint16_t)(BLT_W - 1);
    uint16_t h_m1 = (uint16_t)(BLT_H - 1);
    uint16_t dst_pitch = BLT_W;
    uint16_t src_pitch = src_pitch_override ? src_pitch_override : BLT_W;

    gr_write(0x20, (uint8_t)(w_m1 & 0xFF));
    gr_write(0x21, (uint8_t)((w_m1 >> 8) & 0x0F));
    gr_write(0x22, (uint8_t)(h_m1 & 0xFF));
    gr_write(0x23, (uint8_t)((h_m1 >> 8) & 0x0F));
    gr_write(0x24, (uint8_t)(dst_pitch & 0xFF));
    gr_write(0x25, (uint8_t)((dst_pitch >> 8) & 0xFF));
    gr_write(0x26, (uint8_t)(src_pitch & 0xFF));
    gr_write(0x27, (uint8_t)((src_pitch >> 8) & 0xFF));
    gr_write(0x28, (uint8_t)(dst_off & 0xFF));
    gr_write(0x29, (uint8_t)((dst_off >> 8) & 0xFF));
    gr_write(0x2A, (uint8_t)((dst_off >> 16) & 0xFF));
    gr_write(0x2C, (uint8_t)(src_off & 0xFF));
    gr_write(0x2D, (uint8_t)((src_off >> 8) & 0xFF));
    gr_write(0x2E, (uint8_t)((src_off >> 16) & 0xFF));
    gr_write(0x2F, 0x00);  /* no transparency */
    gr_write(0x30, gr_30_mode);
    gr_write(0x32, 0x0D);  /* SRCCOPY ROP */
    gr_write(0x33, gr_33_ext);
}

/* For V7 COLOR_EXPAND: set FG color in GR[0x10..0x13]. */
static void blt_set_fg_color(uint8_t fg)
{
    gr_write(0x10, fg);
    gr_write(0x11, 0x00);
    gr_write(0x12, 0x00);
    gr_write(0x13, 0x00);
}

/* For V4: clear BLT color regs that might carry stale state. */
static void blt_clear_color_regs(void)
{
    for (uint8_t r = 0x34; r <= 0x39; r++) gr_write(r, 0x00);
}

static inline int blt_is_busy(void)
{
    outportb(GR_INDEX, 0x31);
    return (inportb(GR_DATA) & 0x08) != 0;
}

static inline void blt_kick(void)
{
    outportb(GR_INDEX, 0x31);
    outportb(GR_DATA,  0x02);
}

static int blt_wait_idle_uclock(double timeout_secs)
{
    double t0 = now_secs();
    for (int i = 0; ; i++) {
        if (!blt_is_busy()) return 0;
        if ((i & 0xFFF) == 0 && (now_secs() - t0) > timeout_secs) return -1;
    }
}

/* ============================================================ */
/* Variant spec + driver                                         */
/* ============================================================ */

typedef enum {
    BLT_MODE_BULK    = 0x00,
    BLT_MODE_PATTERN = 0x40,
    BLT_MODE_EXPAND  = 0x80,
} blt_mode_e;

typedef struct {
    const char *label;
    uint8_t     gr_30;
    uint8_t     gr_33;
    uint32_t    src_off;
    uint32_t    dst_off;
    uint16_t    src_pitch_override;  /* 0 = use BLT_W default */
    int         clear_color_regs;
    int         reset_before;
    int         is_color_expand;     /* set FG color reg before kick */
    int         is_pattern;          /* use 64-byte pattern source */
    const char *expected_summary;    /* one-line human-readable expected dst */
} variant_t;

static const variant_t variants[] = {
    {
        "V1_baseline_mode00",
        BLT_MODE_BULK, 0x00, VRAM_SRC_CANONICAL, VRAM_DST_DEFAULT,
        0, 0, 0, 0, 0,
        "BULK_COPY @ 0x012C00->0; expect dst[0..15]=00..0F dst[last16]=F0..FF (linear gradient)",
    },
    {
        "V2_src_at_0xA0000",
        BLT_MODE_BULK, 0x00, VRAM_SRC_HIGH, VRAM_DST_DEFAULT,
        0, 0, 0, 0, 0,
        "BULK_COPY @ 0xA0000->0 (wave-19 original src pos); expect linear gradient at dst",
    },
    {
        "V3_reset_before",
        BLT_MODE_BULK, 0x00, VRAM_SRC_CANONICAL, VRAM_DST_DEFAULT,
        0, 0, 1, 0, 0,
        "BULK_COPY with BLT_RESET pre-programming; expect linear gradient at dst",
    },
    {
        "V4_color_regs_cleared",
        BLT_MODE_BULK, 0x00, VRAM_SRC_CANONICAL, VRAM_DST_DEFAULT,
        0, 1, 0, 0, 0,
        "BULK_COPY with GR[0x34..0x39]=0; expect linear gradient at dst",
    },
    {
        "V5_mode_ext_async",
        BLT_MODE_BULK, 0x80, VRAM_SRC_CANONICAL, VRAM_DST_DEFAULT,
        0, 0, 0, 0, 0,
        "BULK_COPY+0x80 (v2 Scenario E reproduction); expect linear gradient at dst",
    },
    {
        "V6_pattern_copy",
        BLT_MODE_PATTERN, 0x00, VRAM_PATTERN_OFFSET, VRAM_DST_DEFAULT,
        8, 0, 0, 0, 1,
        "PATTERN_COPY @ pattern[64]->dst; expect 8x8 byte pattern tiled (semantics chip-specific)",
    },
    {
        "V7_color_expand_ref",
        BLT_MODE_EXPAND, 0x00, VRAM_PATTERN_OFFSET, VRAM_DST_DEFAULT,
        BLT_W / 8, 0, 0, 1, 0,
        "COLOR_EXPAND FG=0xAA; expect dst[ALL]=0xAA (probe-harness sanity anchor)",
    },
    {
        "V8_dst_at_offscreen",
        BLT_MODE_BULK, 0x00, VRAM_SRC_CANONICAL, VRAM_DST_OFFSCREEN,
        0, 0, 0, 0, 0,
        "BULK_COPY @ 0x012C00->0x040000 (offscreen dst); expect linear gradient at dst",
    },
};

#define N_VARIANTS ((int)(sizeof(variants) / sizeof(variants[0])))

/* Run one variant + emit. Returns 0 if BLT completed within timeout, -1 if hung. */
static int run_variant(const variant_t *V)
{
    plog("");
    plog("[blt-variant FILE=%s BEGIN]", V->label);
    plog("  spec: GR[0x30]=0x%02X GR[0x33]=0x%02X src_off=0x%06lX dst_off=0x%06lX",
         (unsigned)V->gr_30, (unsigned)V->gr_33,
         (unsigned long)V->src_off, (unsigned long)V->dst_off);
    plog("  expect: %s", V->expected_summary);

    /* Pre-flight: clear dst to 0xCC sentinel. */
    clear_dst_to_sentinel(V->dst_off);

    /* Re-unlock + clear errata regs (in case prior variant disturbed). */
    sr_write(0x06, 0x12);
    blt_clear_screen_to_screen_constraints();

    /* Optional pre-reset. */
    if (V->reset_before) {
        gr_write(0x31, 0x04);
        plog("  pre-reset issued (GR[0x31]=0x04)");
    }

    /* Wait idle from any prior BLT. */
    if (blt_wait_idle_uclock(0.20) != 0) {
        plog("  PRE-FLIGHT WARN: chip not idle within 200 ms; resetting + retry");
        gr_write(0x31, 0x04);
        if (blt_wait_idle_uclock(0.20) != 0) {
            plog("[blt-variant FILE=%s DONE status=HUNG_PREFLIGHT]", V->label);
            return -1;
        }
    }

    /* Optional color-reg clear. */
    if (V->clear_color_regs) {
        blt_clear_color_regs();
        plog("  color regs cleared (GR[0x34..0x39]=0)");
    }

    /* Optional COLOR_EXPAND FG color. */
    if (V->is_color_expand) {
        blt_set_fg_color(0xAA);
        plog("  COLOR_EXPAND FG color set to 0xAA");
    }

    /* Program registers. */
    blt_program_registers(V->dst_off, V->src_off,
                          V->src_pitch_override,
                          V->gr_30, V->gr_33);

    /* Kick + time. */
    uint64_t c_kick = rdtsc();
    blt_kick();
    int rc = blt_wait_idle_uclock(0.50);
    uint64_t c_done = rdtsc();
    double blt_us = cycles_to_us(c_done - c_kick);

    if (rc != 0) {
        plog("  BLT did NOT complete within 500 ms (likely hang)");
        gr_write(0x31, 0x04);  /* reset for next variant */
        plog("[blt-variant FILE=%s DONE status=HUNG_KICK blt_us=%.1f]",
             V->label, blt_us);
        return -1;
    }

    /* Read dst first-16 + last-16 bytes. */
    uint8_t dst_first[16], dst_last[16];
    read_vram_16(V->dst_off, dst_first);
    /* For BULK_COPY/PATTERN_COPY/COLOR_EXPAND, the dst region is 76800
     * bytes; last 16 bytes are at offset (dst_off + 0x12C00 - 0x10). */
    read_vram_16(V->dst_off + 0x12BF0UL, dst_last);

    char hex_first[64], hex_last[64];
    format_hex16(dst_first, hex_first);
    format_hex16(dst_last,  hex_last);

    plog("  blt_us=%.1f cycles=%llu",
         blt_us, (unsigned long long)(c_done - c_kick));
    plog("  dst_first_16 (offset 0x%06lX): %s",
         (unsigned long)V->dst_off, hex_first);
    plog("  dst_last_16  (offset 0x%06lX): %s",
         (unsigned long)(V->dst_off + 0x12BF0UL), hex_last);

    /* Auto-classify based on simple heuristic for grep-friendly status:
     *   - All 0xCC -> SENTINEL_UNTOUCHED (BLT did not write dst)
     *   - dst_first[0..15] = 0x00..0x0F AND dst_last[0..15] = 0xF0..0xFF -> PASS_LINEAR_GRADIENT
     *   - dst_first[ALL]   = 0xAA -> PASS_COLOR_EXPAND_AA
     *   - Otherwise -> FAIL_PARTIAL or FAIL_OTHER (decomp interprets via raw hex)
     */
    int all_sentinel_first = 1, all_aa = 1;
    int linear_first_ok = 1, linear_last_ok = 1;
    for (int j = 0; j < 16; j++) {
        if (dst_first[j] != 0xCC) all_sentinel_first = 0;
        if (dst_first[j] != 0xAA) all_aa = 0;
        if (dst_first[j] != (uint8_t)(j & 0xFF)) linear_first_ok = 0;
        if (dst_last[j]  != (uint8_t)((0x12BF0 + j) & 0xFF)) linear_last_ok = 0;
    }
    const char *status;
    if (all_sentinel_first)              status = "SENTINEL_UNTOUCHED";
    else if (all_aa)                     status = "PASS_COLOR_EXPAND_AA";
    else if (linear_first_ok && linear_last_ok) status = "PASS_LINEAR_GRADIENT";
    else if (linear_first_ok)            status = "PARTIAL_FIRST_ONLY";
    else if (linear_last_ok)             status = "PARTIAL_LAST_ONLY";
    else                                  status = "FAIL_OTHER";
    plog("[blt-variant FILE=%s DONE blt_us=%.1f status=%s]",
         V->label, blt_us, status);
    return 0;
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    plog("=== cirrus-blt-variant-probe v1 (wave-36 Task A) starting ===");
    plog("DJGPP pure-C; target Cirrus 5434 BULK_COPY systematic variant matrix");
    plog("Context: BLTASYNC v2 returned REFUTE_VERIFY_FAIL with 99.1%% parallel");
    plog("but FAIL_FIRST verify; BLTFILL COLOR_EXPAND worked at 43 MB/s.");
    plog("Goal: identify a BULK_COPY variant producing correct dst data.");
    plog("UCLOCKS_PER_SEC = %lu", (unsigned long)UCLOCKS_PER_SEC);
    plog("");

    /* Step 1: RDTSC calibration. */
    plog("---- Step 1: RDTSC calibration ----");
    calibrate_rdtsc();
    plog("cpu_mhz_calibrated = %u  us_per_cycle = %.6f",
         (unsigned)g_cpu_mhz, g_us_per_cycle);
    plog("");

    /* Step 2: chip detect. */
    plog("---- Step 2: chip detect ----");
    uint8_t crtc27 = 0;
    const char *chip_name = NULL;
    int is_cirrus = detect_cirrus(&crtc27, &chip_name);
    plog("CRTC[0x27] = 0x%02X  identified: %s", (unsigned)crtc27, chip_name);
    if (!is_cirrus) {
        plog("");
        plog("[blt-variant SUITE_BEGIN n=0 reason=not_cirrus]");
        plog("[blt-variant SUITE_DONE verdict=REFUTE_CHIP_NOT_CIRRUS]");
        plog("All variants skipped (chip-specific). On g2k expect CRTC[0x27]=0xA8.");
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 0;
    }
    plog("");

    /* Step 3: VBE mode 0x101. */
    plog("---- Step 3: VBE mode 0x101 (640x480x8 banked) ----");
    int vbe_rc = vbe_set_mode(0x0101);
    if (vbe_rc != 0) {
        plog("WARN: VBE 0x101 failed (rc=%d); trying 0x100", vbe_rc);
        vbe_rc = vbe_set_mode(0x0100);
    }
    if (vbe_rc != 0) {
        plog("FATAL: VESA 8bpp graphics mode set failed");
        plog("[blt-variant SUITE_BEGIN n=0 reason=vbe_failed]");
        plog("[blt-variant SUITE_DONE verdict=REFUTE_VBE_FAILED]");
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 2;
    }
    plog("VBE mode set OK");
    sr_write(0x06, 0x12);
    blt_clear_screen_to_screen_constraints();
    plog("Re-applied SR[0x06]=0x12 + GR[0x0B] bits 1/4 cleared");
    plog("");

    /* Step 4: pre-fill source regions. */
    plog("---- Step 4: pre-fill source regions ----");
    plog("  Linear gradient @ 0x%06lX (canonical; V1,V3,V4,V5,V8)",
         (unsigned long)VRAM_SRC_CANONICAL);
    prefill_linear_gradient_at(VRAM_SRC_CANONICAL);
    plog("  Linear gradient @ 0x%06lX (wave-19 high; V2)",
         (unsigned long)VRAM_SRC_HIGH);
    prefill_linear_gradient_at(VRAM_SRC_HIGH);
    plog("  64-byte pattern @ 0x%06lX (V6,V7 pattern source)",
         (unsigned long)VRAM_PATTERN_OFFSET);
    prefill_pattern_64_at(VRAM_PATTERN_OFFSET);
    plog("Pre-fills done.");
    plog("");

    /* Step 5: run variant matrix. */
    plog("---- Step 5: run %d variants ----", N_VARIANTS);
    plog("[blt-variant SUITE_BEGIN n=%d chip=%s]", N_VARIANTS, chip_name);
    int hung_count = 0;
    for (int i = 0; i < N_VARIANTS; i++) {
        if (run_variant(&variants[i]) != 0) hung_count++;
    }
    plog("");
    plog("---- Step 6: SUITE_DONE ----");
    plog("[blt-variant SUITE_DONE hung_count=%d / n=%d]", hung_count, N_VARIANTS);
    plog("");
    plog("Sanity anchor: V7 (COLOR_EXPAND FG=0xAA) status MUST be PASS_COLOR_EXPAND_AA.");
    plog("If V7 fails, probe harness is suspect, not BULK_COPY.");
    plog("");
    plog("Decomp guide: any variant emitting status=PASS_LINEAR_GRADIENT identifies");
    plog("the BULK_COPY mechanism that works on this chip + slot 0133 v2 has a path.");
    plog("All BULK_COPY variants emitting FAIL_OTHER + dst_first_16 != 0xCC == chip is");
    plog("writing dst but wrong bytes; analyze the raw hex against source-pattern");
    plog("offset hypothesis (stride? src-bank? bus-master semantics?).");

    text_mode_restore();
    plog("");
    plog("=== cirrus-blt-variant-probe done ===");
    plog("[SENTINEL_END]");
    if (g_log) fclose(g_log);
    return 0;
}
