/*
 * bltpat-v2.c -- Cirrus 5434 BLT PATTERN_COPY mode hail-mary re-attempt
 *                (Phase 11 wave-38 ride-along; resurrect wave-36 V6).
 *
 * CONTEXT: Wave-36 BLTVAR V6 (PATTERN_COPY @ pattern@0x025800) emitted
 * dst all 0x00 / FAIL_OTHER in 1124 us blt time. Chip DID engage (1.12 ms
 * is consistent with chip-driven write, not a "no-op fast skip"). Wave-36
 * V7 (COLOR_EXPAND) emitted the same all-0x00 failure shape -- but
 * post-audit V7 was confirmed to be a probe-side classifier bug:
 *
 *   V7 source pre-fill was 8bpp-linear-gradient (byte=N&0xFF, first
 *   byte 0x00) but COLOR_EXPAND mode reads source as a 1bpp bitmap;
 *   the first source byte 0x00 has all 8 bits zero -> all 8 dst pixels
 *   = BG color = 0x00. The probe's pre-fill semantics didn't match the
 *   mode's source-interpretation semantics. NO chip-behavior signal.
 *
 *   Full audit at docs/PHASE11-WAVE-36-PROBE-PACK-RESULTS.md § 5.2.
 *
 * HYPOTHESIS: V6 may have the same shape of probe bug. PATTERN_COPY may
 * interpret source as 1bpp 8x8 bitmap (not 8bpp 8x8 pixel grid). V6's
 * pre-fill `pat[i]=i for i=0..63` has first byte 0x00 = all-zero bits;
 * with FG=BG=0 (color regs never set), dst = all 0x00. Same trap as V7.
 *
 * MISSION: refute or unblock chip-driven 8x8 tile rendering at
 * ~40 MB/s COLOR_EXPAND-class speeds. If chip-driven tile rendering
 * WORKS (any V_PAT_* passes), wave-39 lever ceiling -5 to -15 ms/flip
 * on _blit_indexed (NXEngine's hottest function, Mimiga heavy-tile
 * scenes). If REFUTED (all V_PAT_* fail across both mode interpretations
 * + alt sequences): conclusively close chip-driven tile rendering;
 * unblock CPU-side optimization priority for wave-39+ (Pentium U/V
 * pairing, dirty-rect tracking).
 *
 * ============================================================
 *   PROBE-AUTHOR TRAP (V7 lesson, MUST READ before edits)
 * ============================================================
 *
 * PATTERN_COPY semantic interpretation depends on Cirrus 5434 mode-bit
 * encoding -- the chip might read source as EITHER:
 *
 *   (a) 8x8 8bpp pixel grid: 64 src bytes = 64 pixels (1 byte each);
 *       dst tiled with src bytes verbatim. Pattern of all-0xAA src
 *       -> dst all 0xAA. NOT like BULK_COPY (which is 8bpp linear,
 *       not tiled).
 *   (b) 8x8 1bpp bitmap: 8 src bytes = 64 pixels (1 bit each); each
 *       bit selects FG (GR[0x10..0x13]) or BG (GR[0x14..0x17]) color.
 *       Source byte 0x00 -> 8 BG pixels. NOT like COLOR_EXPAND (which
 *       is 1bpp LINEAR, not tiled).
 *   (c) Raw bytes (=BULK_COPY): WRONG -- COLOR_EXPAND and PATTERN_COPY
 *       are DISTINCT modes from BULK_COPY. BULK_COPY copies a linear
 *       byte stream; PATTERN_COPY tiles an 8x8 source across dst.
 *
 * Variants below set FG+BG explicitly AND seed source with values that
 * distinguish (a) vs (b). Per-variant classifier emits explicit
 * expected-vs-got hex for BOTH interpretations; no implicit assumption.
 * DO NOT repeat V7's "dst[ALL] == FG" classifier bug (which silently
 * assumed all-1-bit source).
 *
 * VARIANTS (4 total):
 *   V_PAT_A baseline_uniform  PATTERN_COPY @ 0x025800, pat=all-0xAA,
 *                             FG=BG=0xAA. Mode-agnostic PASS: dst[ALL]=
 *                             0xAA under EITHER 1bpp or 8bpp interp.
 *                             FAIL means PATTERN_COPY doesn't emit
 *                             pattern data at all (refute mechanism).
 *   V_PAT_B byte_checker      PATTERN_COPY @ 0x025800, pat=0xAA/0x55
 *                             alternating, FG=0xFF, BG=0x00.
 *                             Discriminates:
 *                               PASS_8BPP: dst = AA 55 AA 55 ...
 *                               PASS_1BPP: dst = FF 00 FF 00 ... (with
 *                                          phase flip per src byte)
 *   V_PAT_C reset_pre         PATTERN_COPY @ 0x025800, pat=all-0xAA,
 *                             FG=BG=0xAA, GR[0x31]=0x04 BLT_RESET pre.
 *                             Tests whether stale BLT state from prior
 *                             use blocks pattern-mode engagement.
 *   V_PAT_D src_far           PATTERN_COPY @ 0x0A0000 (V2-style far
 *                             src), pat=all-0xAA, FG=BG=0xAA. Tests
 *                             src-adjacency hypothesis (per wave-36
 *                             BLTVAR V2 BULK_COPY finding).
 *
 * Sanity anchor: V_PAT_A is the mode-agnostic anchor. If V_PAT_A FAILS
 * with dst != all-0xAA, PATTERN_COPY mechanism is broken on this chip
 * AS WE PROGRAM IT (could still be a register-sequence issue, but the
 * search space narrows to V_PAT_C alt-sequence + V_PAT_D src-far).
 *
 * 8.3 DOS filenames:
 *   Source: tests/probes/bltpat-v2.c          (host-side; 9-char base)
 *   Binary: BLTPAT.EXE   (6+3)
 *   Log:    BLTPAT.LOG   (6+3)
 *   BAT:    BLTPAT.BAT   (6+3)
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
    g_log = fopen("BLTPAT.LOG", "w");
    if (!g_log) g_log = fopen("C:\\BLTPAT.LOG", "w");
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
/* Timing (RDTSC via uclock calibration)                         */
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

#define VRAM_DST_DEFAULT    0x000000UL  /* visible FB (sampling target) */
#define VRAM_PAT_NEAR       0x025800UL  /* canonical pattern slot (V_PAT_A/B/C) */
#define VRAM_PAT_FAR        0x0A0000UL  /* V2-style far src (V_PAT_D) */

/* ============================================================ */
/* Pre-fill helpers                                              */
/* ============================================================ */

/* Pre-fill `len` bytes of `byte` at given VRAM offset; bank-aware. */
static void prefill_uniform_at(uint32_t vram_off, long len, uint8_t byte)
{
    uint8_t *buf = (uint8_t *)malloc(len);
    if (!buf) return;
    memset(buf, byte, len);

    long remaining = len;
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

/* Pre-fill `len` bytes alternating byte_a, byte_b at given VRAM offset. */
static void prefill_alternating_at(uint32_t vram_off, long len,
                                   uint8_t byte_a, uint8_t byte_b)
{
    uint8_t *buf = (uint8_t *)malloc(len);
    if (!buf) return;
    for (long i = 0; i < len; i++) {
        buf[i] = (i & 1) ? byte_b : byte_a;
    }

    long remaining = len;
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

/* Clear dst region to 0xCC sentinel BEFORE each variant; leftover successful
 * state from a prior variant can't false-PASS a broken variant. 0xCC is not
 * in our expected dst patterns (0xAA, 0x55, 0xFF, 0x00). */
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

/* Read N bytes from VRAM at given offset via dosmemget + bank-switch.
 * Caller-supplied buffer; reads do not cross a bank boundary because all
 * sampling offsets in this probe are within a single 64K bank. */
static void read_vram_n(uint32_t vram_off, int n, uint8_t *out)
{
    int bank = (int)(vram_off >> 16);
    uint32_t off_in_bank = vram_off & 0xFFFF;
    vbe_set_bank(bank);
    dosmemget(0xA0000UL + off_in_bank, n, out);
}

static void format_hex_n(const uint8_t *b, int n, char *out, size_t out_sz)
{
    size_t w = 0;
    for (int i = 0; i < n; i++) {
        w += snprintf(out + w, out_sz - w, "%02X%s",
                      b[i], (i < n - 1) ? " " : "");
    }
}

/* ============================================================ */
/* BLT register programming                                      */
/* ============================================================ */

/* Per Cirrus 5434 extension errata: GR[0x0B] bits 1+4 must be clear for
 * screen-to-screen / pattern BLT to function. SR[0x06]=0x12 unlocks first. */
static void blt_clear_screen_to_screen_constraints(void)
{
    uint8_t v = gr_read(0x0B);
    v &= ~((uint8_t)(1 << 1) | (uint8_t)(1 << 4));
    gr_write(0x0B, v);
}

/* Program BLT registers for PATTERN_COPY. Source pitch = 8 (8x8 tile,
 * 8 bytes per row). Dst pitch = BLT_W = 320 (8bpp linear FB).
 * GR[0x30] = 0x40 = PATTERN_COPY mode.
 * GR[0x32] = 0x0D = SRCCOPY ROP (matches what BLTVAR V2 used for BULK_COPY
 *                   and what BLTFILL used for COLOR_EXPAND; consistent
 *                   across modes per Cirrus 5434 ROP encoding).
 * GR[0x33] = gr_33_ext (per-variant; 0x00 default). */
static void blt_program_pattern(uint32_t dst_off, uint32_t src_off,
                                uint8_t gr_33_ext)
{
    uint16_t w_m1 = (uint16_t)(BLT_W - 1);
    uint16_t h_m1 = (uint16_t)(BLT_H - 1);
    uint16_t dst_pitch = BLT_W;
    uint16_t src_pitch = 8;       /* 8x8 tile: 8 bytes per row */

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
    gr_write(0x30, 0x40);  /* PATTERN_COPY mode */
    gr_write(0x32, 0x0D);  /* SRCCOPY ROP */
    gr_write(0x33, gr_33_ext);
}

/* Set FG color in GR[0x10..0x13] (used by 1bpp-interp for bits=1 pixels). */
static void blt_set_fg_color(uint8_t fg)
{
    gr_write(0x10, fg);
    gr_write(0x11, 0x00);
    gr_write(0x12, 0x00);
    gr_write(0x13, 0x00);
}

/* Set BG color in GR[0x14..0x17] (used by 1bpp-interp for bits=0 pixels). */
static void blt_set_bg_color(uint8_t bg)
{
    gr_write(0x14, bg);
    gr_write(0x15, 0x00);
    gr_write(0x16, 0x00);
    gr_write(0x17, 0x00);
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
    PAT_UNIFORM,       /* pre-fill 64 bytes of byte_a */
    PAT_ALTERNATING,   /* pre-fill alternating byte_a/byte_b */
} pat_kind_e;

typedef struct {
    const char *label;
    pat_kind_e  pat_kind;
    uint8_t     pat_byte_a;
    uint8_t     pat_byte_b;        /* unused when pat_kind == PAT_UNIFORM */
    uint8_t     fg_color;
    uint8_t     bg_color;
    uint32_t    src_off;
    uint32_t    dst_off;
    uint8_t     gr_33_ext;
    int         reset_before;
    const char *hypothesis;        /* one-line human-readable */
} variant_t;

static const variant_t variants[] = {
    {
        "V_PAT_A_baseline_uniform",
        PAT_UNIFORM, 0xAA, 0x00,
        0xAA, 0xAA,                /* FG=BG=0xAA: dst all 0xAA under EITHER interp */
        VRAM_PAT_NEAR, VRAM_DST_DEFAULT,
        0x00, 0,
        "PATTERN_COPY engages at all (mode-agnostic): expect dst[ALL]=0xAA",
    },
    {
        "V_PAT_B_byte_checker",
        PAT_ALTERNATING, 0xAA, 0x55,
        0xFF, 0x00,                /* FG=0xFF / BG=0x00: discriminates 1bpp vs 8bpp */
        VRAM_PAT_NEAR, VRAM_DST_DEFAULT,
        0x00, 0,
        "8bpp interp: dst alternates AA/55; 1bpp interp: dst alternates FF/00 (phase per src byte)",
    },
    {
        "V_PAT_C_reset_pre",
        PAT_UNIFORM, 0xAA, 0x00,
        0xAA, 0xAA,
        VRAM_PAT_NEAR, VRAM_DST_DEFAULT,
        0x00, 1,                   /* BLT_RESET pre */
        "Same as V_PAT_A + GR[0x31]=0x04 BLT_RESET pre-programming; tests stale-state blocker",
    },
    {
        "V_PAT_D_src_far",
        PAT_UNIFORM, 0xAA, 0x00,
        0xAA, 0xAA,
        VRAM_PAT_FAR, VRAM_DST_DEFAULT,
        0x00, 0,
        "Same as V_PAT_A + src@0xA0000 (V2-style); tests src-adjacency-to-dst constraint",
    },
};

#define N_VARIANTS ((int)(sizeof(variants) / sizeof(variants[0])))

/* Per-variant classifier. Returns status string.
 * Emits "expected vs got" raw hex for BOTH 1bpp + 8bpp interpretations, so
 * decomp can override the classifier verdict by inspecting raw bytes. */
static const char *classify_uniform(const uint8_t f[16], const uint8_t m[16],
                                    const uint8_t l[16], uint8_t expected)
{
    int all_sent = 1, all_ok = 1;
    for (int j = 0; j < 16; j++) {
        if (f[j] != 0xCC || m[j] != 0xCC || l[j] != 0xCC) all_sent = 0;
        if (f[j] != expected || m[j] != expected || l[j] != expected) all_ok = 0;
    }
    if (all_sent) return "SENTINEL_UNTOUCHED";
    if (all_ok)   return "PASS_UNIFORM";
    return "FAIL_OTHER";
}

static const char *classify_checker(const uint8_t f[16], const uint8_t m[16],
                                    const uint8_t l[16],
                                    uint8_t byte_a, uint8_t byte_b,
                                    uint8_t fg, uint8_t bg)
{
    int all_sent = 1;
    int ok_8bpp_first = 1, ok_8bpp_mid = 1, ok_8bpp_last = 1;
    int ok_1bpp_first = 1, ok_1bpp_mid = 1, ok_1bpp_last = 1;

    /* 8bpp interp: bytes alternate byte_a, byte_b */
    /* 1bpp interp: bits of src bytes select FG/BG. Source byte_a=0xAA bits
     * 10101010 -> pixels FG,BG,FG,BG,FG,BG,FG,BG (MSB-first per Cirrus convention).
     * Source byte_b=0x55 bits 01010101 -> pixels BG,FG,BG,FG,BG,FG,BG,FG.
     * For the FIRST tile row, dst[0..7] from byte_a, dst[8..15] from byte_b.
     * So dst[0..15] = FG BG FG BG FG BG FG BG  BG FG BG FG BG FG BG FG */
    for (int j = 0; j < 16; j++) {
        if (f[j] != 0xCC || m[j] != 0xCC || l[j] != 0xCC) all_sent = 0;

        uint8_t exp_8 = (j & 1) ? byte_b : byte_a;
        if (f[j] != exp_8) ok_8bpp_first = 0;
        if (m[j] != exp_8) ok_8bpp_mid   = 0;
        if (l[j] != exp_8) ok_8bpp_last  = 0;

        int src_byte_idx = (j >> 3) & 1;
        int bit_idx      = 7 - (j & 7);  /* MSB-first */
        uint8_t src_byte = src_byte_idx ? byte_b : byte_a;
        uint8_t exp_1    = ((src_byte >> bit_idx) & 1) ? fg : bg;
        if (f[j] != exp_1) ok_1bpp_first = 0;
        if (m[j] != exp_1) ok_1bpp_mid   = 0;
        if (l[j] != exp_1) ok_1bpp_last  = 0;
    }

    if (all_sent) return "SENTINEL_UNTOUCHED";
    if (ok_8bpp_first && ok_8bpp_mid && ok_8bpp_last) return "PASS_CHECKER_8BPP";
    if (ok_1bpp_first && ok_1bpp_mid && ok_1bpp_last) return "PASS_CHECKER_1BPP";
    return "FAIL_OTHER";
}

/* Run one variant. Returns 0 if BLT completed within timeout, -1 if hung. */
static int run_variant(const variant_t *V)
{
    plog("");
    plog("[bltpat FILE=%s BEGIN]", V->label);
    plog("  hypothesis: %s", V->hypothesis);
    plog("  spec: src_off=0x%06lX dst_off=0x%06lX FG=0x%02X BG=0x%02X gr_33=0x%02X reset_pre=%d",
         (unsigned long)V->src_off, (unsigned long)V->dst_off,
         (unsigned)V->fg_color, (unsigned)V->bg_color,
         (unsigned)V->gr_33_ext, V->reset_before);

    /* Re-fill pattern source per variant (variants may share src offset
     * but different content; safer to re-seed than rely on prior fill). */
    if (V->pat_kind == PAT_UNIFORM) {
        prefill_uniform_at(V->src_off, 64, V->pat_byte_a);
        plog("  pat_src: 64 bytes of 0x%02X @ 0x%06lX",
             (unsigned)V->pat_byte_a, (unsigned long)V->src_off);
    } else {
        prefill_alternating_at(V->src_off, 64, V->pat_byte_a, V->pat_byte_b);
        plog("  pat_src: 64 bytes alternating 0x%02X/0x%02X @ 0x%06lX",
             (unsigned)V->pat_byte_a, (unsigned)V->pat_byte_b,
             (unsigned long)V->src_off);
    }

    /* Pre-flight: clear dst to 0xCC sentinel. */
    clear_dst_to_sentinel(V->dst_off);

    /* Re-unlock + clear errata regs. */
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
            plog("[bltpat FILE=%s DONE status=HUNG_PREFLIGHT]", V->label);
            return -1;
        }
    }

    /* Set FG + BG color regs. */
    blt_set_fg_color(V->fg_color);
    blt_set_bg_color(V->bg_color);

    /* Program PATTERN_COPY registers. */
    blt_program_pattern(V->dst_off, V->src_off, V->gr_33_ext);

    /* Kick + time. */
    uint64_t c_kick = rdtsc();
    blt_kick();
    int rc = blt_wait_idle_uclock(0.50);
    uint64_t c_done = rdtsc();
    double blt_us = cycles_to_us(c_done - c_kick);

    if (rc != 0) {
        plog("  BLT did NOT complete within 500 ms (likely hang)");
        gr_write(0x31, 0x04);  /* reset for next variant */
        plog("[bltpat FILE=%s DONE status=HUNG_KICK blt_us=%.1f]",
             V->label, blt_us);
        return -1;
    }

    /* Sample dst at 3 positions: first 16 bytes, middle 16 bytes, last 16 bytes.
     * Middle = byte 38400 (half of 76800); last = byte 76784 (76800-16). */
    uint8_t dst_first[16], dst_mid[16], dst_last[16];
    read_vram_n(V->dst_off,             16, dst_first);
    read_vram_n(V->dst_off + 0x09600UL, 16, dst_mid);   /* offset 38400 */
    read_vram_n(V->dst_off + 0x12BF0UL, 16, dst_last);  /* offset 76784 */

    char hex_first[80], hex_mid[80], hex_last[80];
    format_hex_n(dst_first, 16, hex_first, sizeof hex_first);
    format_hex_n(dst_mid,   16, hex_mid,   sizeof hex_mid);
    format_hex_n(dst_last,  16, hex_last,  sizeof hex_last);

    plog("  blt_us=%.1f cycles=%llu",
         blt_us, (unsigned long long)(c_done - c_kick));
    plog("  dst_first_16 (offset 0x%06lX): %s",
         (unsigned long)V->dst_off, hex_first);
    plog("  dst_mid_16   (offset 0x%06lX): %s",
         (unsigned long)(V->dst_off + 0x09600UL), hex_mid);
    plog("  dst_last_16  (offset 0x%06lX): %s",
         (unsigned long)(V->dst_off + 0x12BF0UL), hex_last);

    /* Emit explicit expected-vs-got per interpretation. NO implicit
     * assumption -- decomp reads raw hex if classifier is uncertain. */
    if (V->pat_kind == PAT_UNIFORM) {
        plog("  expected: dst[ALL]=0x%02X (both 1bpp and 8bpp interp produce "
             "uniform output when pat=uniform AND FG==BG==pat_byte)",
             (unsigned)V->pat_byte_a);
        const char *status = classify_uniform(dst_first, dst_mid, dst_last,
                                              V->pat_byte_a);
        plog("[bltpat FILE=%s DONE blt_us=%.1f status=%s]",
             V->label, blt_us, status);
    } else {
        /* PAT_ALTERNATING -- compute expected hex for both interps */
        char hex_8[80], hex_1[80];
        uint8_t e8[16], e1[16];
        for (int j = 0; j < 16; j++) {
            e8[j] = (j & 1) ? V->pat_byte_b : V->pat_byte_a;
            int src_byte_idx = (j >> 3) & 1;
            int bit_idx      = 7 - (j & 7);
            uint8_t src_byte = src_byte_idx ? V->pat_byte_b : V->pat_byte_a;
            e1[j] = ((src_byte >> bit_idx) & 1) ? V->fg_color : V->bg_color;
        }
        format_hex_n(e8, 16, hex_8, sizeof hex_8);
        format_hex_n(e1, 16, hex_1, sizeof hex_1);
        plog("  expected (8bpp interp): dst_first_16 = %s", hex_8);
        plog("  expected (1bpp interp): dst_first_16 = %s", hex_1);
        const char *status = classify_checker(dst_first, dst_mid, dst_last,
                                              V->pat_byte_a, V->pat_byte_b,
                                              V->fg_color, V->bg_color);
        plog("[bltpat FILE=%s DONE blt_us=%.1f status=%s]",
             V->label, blt_us, status);
    }
    return 0;
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    plog("=== bltpat-v2 (wave-38 PATTERN_COPY hail-mary) starting ===");
    plog("DJGPP pure-C; target Cirrus 5434 PATTERN_COPY (BLT_MODE 0x40)");
    plog("Context: wave-36 BLTVAR V6 emitted dst=all-0x00 / FAIL_OTHER;");
    plog("V7 same-shape failure was probe-side classifier bug (1bpp vs 8bpp");
    plog("source-interp mismatch). V6 may have same trap. Goal: refute or");
    plog("unblock chip-driven 8x8 tile rendering.");
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
        plog("[bltpat SUITE_BEGIN n=0 reason=not_cirrus]");
        plog("[bltpat SUITE_DONE verdict=REFUTE_CHIP_NOT_CIRRUS]");
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
        plog("[bltpat SUITE_BEGIN n=0 reason=vbe_failed]");
        plog("[bltpat SUITE_DONE verdict=REFUTE_VBE_FAILED]");
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 2;
    }
    plog("VBE mode set OK");
    sr_write(0x06, 0x12);
    blt_clear_screen_to_screen_constraints();
    plog("Re-applied SR[0x06]=0x12 + GR[0x0B] bits 1/4 cleared");
    plog("");

    /* Step 4: announce variant matrix. */
    plog("---- Step 4: run %d variants ----", N_VARIANTS);
    plog("[bltpat SUITE_BEGIN n=%d chip=%s]", N_VARIANTS, chip_name);
    int hung_count = 0;
    for (int i = 0; i < N_VARIANTS; i++) {
        if (run_variant(&variants[i]) != 0) hung_count++;
    }
    plog("");
    plog("---- Step 5: SUITE_DONE ----");
    plog("[bltpat SUITE_DONE hung_count=%d / n=%d]", hung_count, N_VARIANTS);
    plog("");

    plog("Decomp guide:");
    plog("  PASS_UNIFORM on V_PAT_A => chip-driven tile rendering UNBLOCKED.");
    plog("    Mechanism live. Next: V_PAT_B status discriminates 1bpp vs 8bpp:");
    plog("      PASS_CHECKER_8BPP -> 8x8 pixel-grid (preferred for blit_indexed)");
    plog("      PASS_CHECKER_1BPP -> 8x8 bitmap (FG/BG color only; limited use)");
    plog("  FAIL_OTHER on V_PAT_A + PASS on V_PAT_C => stale-state blocker;");
    plog("    issue BLT_RESET pre-programming each tile-blit in production.");
    plog("  FAIL_OTHER on V_PAT_A + PASS on V_PAT_D => src-adjacency constraint;");
    plog("    stage 8x8 pattern at VRAM>=0xA0000 (parallels BLTVAR V2 finding).");
    plog("  FAIL_OTHER across ALL variants => PATTERN_COPY conclusively refuted");
    plog("    on Cirrus 5434 + this register-sequence; close chip-driven tile");
    plog("    rendering; wave-39+ prioritizes CPU-side optimization.");

    text_mode_restore();
    plog("");
    plog("=== bltpat-v2 done ===");
    plog("[SENTINEL_END]");
    if (g_log) fclose(g_log);
    return 0;
}
