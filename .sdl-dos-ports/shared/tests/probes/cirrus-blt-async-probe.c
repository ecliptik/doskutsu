/*
 * cirrus-blt-async-probe.c -- Cirrus 5434 BLT async parallelism characterization
 * (Phase 11 wave-36 task #10 v2 Option 1; ship-blocking gate for slot 0133).
 *
 * v2 deltas vs v1 (per team-lead's correction 2026-05-12 + Option 1 pick):
 *   - Source offset moved 0xA0000 -> 0x012C00 (canonical "right after visible FB"
 *     layout per nx-engine tree audit; both are valid offscreen VRAM)
 *   - Pre-fill pattern switched (i^seed)&0xFF -> linear gradient i&0xFF
 *     (deterministic; trivially verifiable post-BLT)
 *   - NEW: verify_status=PASS|FAIL post-BLT correctness gate. After Scenario D
 *     + Scenario E first rep, spot-checks first 16 + last 16 bytes of dst
 *     region vs expected linear gradient. If BLT silently no-op'd or wrote
 *     garbage, parallel_pct numbers are meaningless — emit FAIL.
 *   - NEW: Scenario E tests GR[0x33]=0x80 ("async" bit per team-lead brief)
 *     as a sub-case alongside the v1 GR[0x33]=0x00 (Scenario D). If E hangs,
 *     skips gracefully + reports D only. Verdict uses max(D, E) parallel_pct.
 *
 * QUESTION: when the Cirrus 5434 BLT engine is fired in screen-to-screen
 * BULK_COPY mode, does the CPU actually get to do useful parallel work
 * while the chip executes? Prior art: wave_19_path_b_dead.md established
 * BLT VRAM->VRAM at 19 MB/s (3.85 ms for 76800 bytes) measured SYNCHRONOUSLY
 * (kick-then-tight-poll). The hail-mary thesis for slot 0133 is: kick BLT
 * before mds_tilemap_fg, let it churn while the CPU does engine work, save
 * dosmemput time at flip end. If the chip CAN'T run in parallel (PCI bus
 * lock, DMA stall, etc.) then slot 0133 is dead-on-arrival.
 *
 * DESIGN per probe_first_optimization.md: small, isolated, deterministic.
 * No SDL, no engine. ~30-60 sec operator time. Decision gates emitted in log.
 *
 * Decision gates (team-lead brief task #10):
 *   cpu_parallel_fraction > 50%  -> SHIP slot 0133; nx-engine starts
 *   cpu_parallel_fraction in 20-50%  -> DEFER
 *   cpu_parallel_fraction < 20%  -> CANCEL slot 0133; team-lead picks alt hail-mary
 *   blt_completion_us > 10000    -> REFUTE entire mechanism
 *   verify_status FAIL on best scenario -> REFUTE (chip not actually copying)
 *
 * SCENARIOS:
 *   A. chip + mode setup (no timing, just confirm BLT engages)
 *   B. BASELINE dummy-work calibration (no BLT)            -> baseline_us_per_unit
 *   C. BLT-ONLY tight-poll (no parallel work)              -> blt_alone_us
 *   D. BLT-WITH-DUMMY interleaved poll+work, GR[0x33]=0x00 -> parallel_pct_D + verify_D
 *   E. BLT-WITH-DUMMY interleaved poll+work, GR[0x33]=0x80 -> parallel_pct_E + verify_E
 *
 * Per-rep phase-D derived metrics:
 *   blt_total_us       = (RDTSC at idle) - (RDTSC at kick)
 *   cpu_useful_us      = dummy_units_completed * baseline_us_per_unit
 *   cpu_parallel_pct   = 100 * cpu_useful_us / blt_total_us
 *
 * Aggregate emit (one ANCHOR line per scenario for trivial grepping):
 *   [cirrus-blt-async-probe BEGIN n=N reps=100 ...]
 *   [cirrus-blt-async-probe DONE
 *     baseline_us_per_unit=X.XX
 *     blt_alone_us_med=YYYY.Y blt_alone_us_min=YYYY.Y blt_alone_us_max=YYYY.Y
 *     blt_total_us_med=YYYY.Y
 *     cpu_useful_us_med=YYYY.Y
 *     cpu_parallel_pct_med=XX.X%
 *     VERDICT=SHIP|DEFER|CANCEL|REFUTE_BANDWIDTH|REFUTE_CHIP_NOT_CIRRUS|REFUTE_BLT_HUNG]
 *   [SENTINEL_END]
 *
 * PLAUSIBILITY ASSERTS (per probe_smoke_recipes_check_derived_metrics.md):
 *   - cpu_mhz_calibrated in [50, 5000] -> else UNCALIBRATED sentinel + skip us-conv
 *   - baseline_us_per_unit in [0.05, 50.0] -> else WARN line
 *   - blt_alone_us_med in [500, 20000] -> else WARN line
 *   - cpu_parallel_pct_med in [-10, 200] -> else WARN line
 *   - sentinel-emit: [SENTINEL_END] must be last line (logback verifies completion)
 *
 * 8.3 DOS filenames:
 *   Source:   tests/probes/cirrus-blt-async-probe.c (host-side, descriptive)
 *   Binary:   BLTASYNC.EXE  (8+3, fits)
 *   Log:      BLTASYNC.LOG  (8+3, fits)
 *   BAT:      BLTASYNC.BAT  (8+3, fits)
 *
 * Per iter_must_include_cwsdpmi.md: bundle CWSDPMI.EXE alongside in tarball.
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
    g_log = fopen("BLTASYNC.LOG", "w");
    if (!g_log) g_log = fopen("C:\\BLTASYNC.LOG", "w");
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
/* RDTSC + cpu_mhz calibration                                   */
/*                                                                */
/* RDTSC is available on Pentium (P5/P54C/Pentium OD); doskutsu's */
/* hard floor is 486DX-with-FPU, but: this probe targets Cirrus  */
/* 5434 which on g2k is paired with PODP5V83 (Pentium OD 83 MHz). */
/* If invoked on a 486 the RDTSC would be #UD; we trap that case. */
/*                                                                */
/* Calibration: at probe init, sample RDTSC, busy-wait ~100 ms    */
/* via uclock(), sample again. cpu_hz = delta / 0.1 sec.          */
/* us_per_cycle = 1e6 / cpu_hz.                                   */
/* ============================================================ */

static double g_us_per_cycle = 0.0;
static uint32_t g_cpu_mhz = 0;

static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* Convert RDTSC delta (uint64) to microseconds. Returns -1.0 if
 * calibration failed (cpu_mhz==0). */
static double cycles_to_us(uint64_t cycles)
{
    if (g_us_per_cycle <= 0.0) return -1.0;
    return (double)cycles * g_us_per_cycle;
}

static double now_secs(void)
{
    return (double)uclock() / (double)UCLOCKS_PER_SEC;
}

static int calibrate_rdtsc(void)
{
    double t0 = now_secs();
    uint64_t c0 = rdtsc();
    /* Busy-wait ~100 ms. */
    while (now_secs() - t0 < 0.100) { /* spin */ }
    double t1 = now_secs();
    uint64_t c1 = rdtsc();

    double secs = t1 - t0;
    if (secs <= 0.0) return -1;
    double cpu_hz = (double)(c1 - c0) / secs;
    if (cpu_hz < 50e6 || cpu_hz > 5e9) {
        /* Out of plausibility band (50 MHz - 5 GHz). DOSBox-X cycles=max
         * often reports >300 MHz here; real PODP83 ~83 MHz. */
        g_us_per_cycle = 1e6 / cpu_hz;
        g_cpu_mhz = (uint32_t)(cpu_hz / 1e6);
        return -2;  /* return warning code, but still report values */
    }
    g_us_per_cycle = 1e6 / cpu_hz;
    g_cpu_mhz = (uint32_t)(cpu_hz / 1e6);
    return 0;
}

/* ============================================================ */
/* VGA / Cirrus port helpers (same as bltfill.c, chipid.c)       */
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

/* Relaxed Cirrus detect (per bltfill.c pattern): EITHER CRTC[0x27] chip-id
 * matches Cirrus 543x family OR SR[0x06] write-read = 0x12. Returns 1 if
 * detected, 0 if not Cirrus. Sets *out_crtc27 + *out_sr06_writeread + *out_name. */
static int detect_cirrus(uint8_t *out_crtc27, uint8_t *out_sr06_wr, const char **out_name)
{
    sr_write(0x06, 0x12);
    uint8_t sr06 = sr_read(0x06);
    uint8_t crtc27 = cr_read(0x27);
    *out_crtc27 = crtc27;
    *out_sr06_wr = sr06;
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
/* BLT geometry + offsets                                        */
/*                                                                */
/* BULK_COPY mode (VRAM->VRAM 76800 bytes): the only Cirrus 5434  */
/* BLT mode that actually moves 76800 bytes from one "buffer" to  */
/* another, matching team-lead spec "320x240 8bpp source->dest". */
/* Both source + dest are in VRAM (the chip can't read sysmem    */
/* without CPU PIO, which would serialize and defeat async). The */
/* "two 76800-byte buffers" of the spec map to two non-overlap   */
/* VRAM regions, set up at probe init.                           */
/* ============================================================ */

#define BLT_W       320
#define BLT_H       240
#define BLT_BYTES   ((long)BLT_W * (long)BLT_H)  /* 76800 */

#define VRAM_DST_OFFSET     0x000000UL  /* bank 0 + part of bank 1 (visible FB) */
#define VRAM_SRC_OFFSET     0x012C00UL  /* bank 1 offset 0x2C00 + part of bank 2;
                                         * canonical "right after 76800-byte
                                         * visible region" offscreen layout */

/* ============================================================ */
/* BLT register programming -- BULK_COPY VRAM->VRAM              */
/*                                                                */
/* Per /tmp/wave19-cirrus-audit.md + bltfill.c v2 history:        */
/*   - GR[0x0B] bits 1+4 cleared for screen-to-screen BLT errata */
/*   - GR[0x20/0x21] width-1, GR[0x22/0x23] height-1              */
/*   - GR[0x24/0x25] dst pitch (=BLT_W=320)                       */
/*   - GR[0x26/0x27] src pitch (=BLT_W=320)                       */
/*   - GR[0x28/0x29/0x2A] dst start offset                        */
/*   - GR[0x2C/0x2D/0x2E] src start offset                        */
/*   - GR[0x30] = 0x00 -> BULK_COPY VRAM->VRAM                    */
/*   - GR[0x31] = 0x02 -> KICK BLT                                */
/*   - GR[0x31] bit 3 (BLT_PROGRESS) -- canonical Linux busy poll */
/*   - GR[0x32] = 0x0D -> SRCCOPY ROP                             */
/* ============================================================ */

static void blt_clear_screen_to_screen_constraints(void)
{
    uint8_t v = gr_read(0x0B);
    v &= ~((uint8_t)(1 << 1) | (uint8_t)(1 << 4));
    gr_write(0x0B, v);
}

/* Program BULK_COPY VRAM->VRAM with caller-specified GR[0x33] mode ext.
 * Common values:
 *   mode_ext = 0x00 -- canonical (bltfill.c v2 + wave-19 19 MB/s baseline)
 *   mode_ext = 0x80 -- bit 7 = "async" per team-lead spec correction;
 *                      may unlock no-bus-lock async mode OR may cause
 *                      BLT to refuse engagement (chip-dependent). */
static void blt_program_bulk_copy_ext(uint8_t mode_ext)
{
    uint16_t w_m1 = (uint16_t)(BLT_W - 1);
    uint16_t h_m1 = (uint16_t)(BLT_H - 1);

    gr_write(0x20, (uint8_t)(w_m1 & 0xFF));
    gr_write(0x21, (uint8_t)((w_m1 >> 8) & 0x0F));
    gr_write(0x22, (uint8_t)(h_m1 & 0xFF));
    gr_write(0x23, (uint8_t)((h_m1 >> 8) & 0x0F));

    gr_write(0x24, (uint8_t)(BLT_W & 0xFF));         /* dst pitch */
    gr_write(0x25, (uint8_t)((BLT_W >> 8) & 0xFF));

    gr_write(0x26, (uint8_t)(BLT_W & 0xFF));         /* src pitch */
    gr_write(0x27, (uint8_t)((BLT_W >> 8) & 0xFF));

    gr_write(0x28, (uint8_t)(VRAM_DST_OFFSET & 0xFF));
    gr_write(0x29, (uint8_t)((VRAM_DST_OFFSET >> 8) & 0xFF));
    gr_write(0x2A, (uint8_t)((VRAM_DST_OFFSET >> 16) & 0xFF));

    gr_write(0x2C, (uint8_t)(VRAM_SRC_OFFSET & 0xFF));
    gr_write(0x2D, (uint8_t)((VRAM_SRC_OFFSET >> 8) & 0xFF));
    gr_write(0x2E, (uint8_t)((VRAM_SRC_OFFSET >> 16) & 0xFF));

    gr_write(0x2F, 0x00);  /* no transparency */
    gr_write(0x30, 0x00);  /* BULK_COPY: src+dst in display memory */
    gr_write(0x32, 0x0D);  /* SRCCOPY ROP */
    gr_write(0x33, mode_ext);  /* mode ext (Scenario D=0x00, E=0x80) */
}

/* Back-compat alias for v1 call sites (Scenario A warmup + Scenario C
 * tight-poll); uses canonical mode_ext=0x00. */
static void blt_program_bulk_copy(void)
{
    blt_program_bulk_copy_ext(0x00);
}

/* Read BLT status reg ONCE (single port read). Returns nonzero if busy
 * (bit 3 = BLT_PROGRESS set). */
static inline int blt_is_busy(void)
{
    outportb(GR_INDEX, 0x31);
    return (inportb(GR_DATA) & 0x08) != 0;
}

/* Kick BLT (write trigger to GR[0x31]). Inlined for minimum overhead. */
static inline void blt_kick(void)
{
    outportb(GR_INDEX, 0x31);
    outportb(GR_DATA,  0x02);
}

/* Wait for BLT idle with a wall-clock timeout (uses uclock not RDTSC to
 * avoid contaminating the RDTSC timing). Returns 0 if idle within
 * timeout, -1 if timed out. */
static int blt_wait_idle_uclock(double timeout_secs)
{
    double t0 = now_secs();
    for (int i = 0; ; i++) {
        if (!blt_is_busy()) return 0;
        if ((i & 0xFFF) == 0 && (now_secs() - t0) > timeout_secs) return -1;
    }
}

/* ============================================================ */
/* VRAM pre-fill -- write a 76800-byte source pattern into        */
/* VRAM_SRC_OFFSET so the chip has real bytes to copy.            */
/* ============================================================ */

/* Pre-fill source VRAM region with a deterministic linear gradient
 * (buf[i] = i & 0xFF). Post-BLT, dst[i] should equal source[i] = i & 0xFF
 * for every i in [0, 76800) -- the verify spot-check uses this property.
 *
 * Argument retained for ABI continuity with v1; ignored in v2. */
static void prefill_vram_source(uint8_t pattern_seed_unused)
{
    (void)pattern_seed_unused;
    uint8_t *buf = (uint8_t *)malloc(BLT_BYTES);
    if (!buf) return;
    for (long i = 0; i < BLT_BYTES; i++) {
        buf[i] = (uint8_t)(i & 0xFF);  /* linear gradient */
    }
    /* VRAM_SRC_OFFSET = 0x012C00 = bank 1 offset 0x2C00 on Cirrus 5434
     * (64 KB banks). 76800 bytes spans bank-1-tail (53248 bytes from 0x2C00
     * to 0xFFFF) + bank-2-head (23552 bytes from 0x0000 to 0x5C00). */
    vbe_set_bank(1);
    dosmemput(buf, 65536 - 0x2C00, 0xA0000 + 0x2C00);
    vbe_set_bank(2);
    dosmemput(buf + (65536 - 0x2C00), BLT_BYTES - (65536 - 0x2C00), 0xA0000);
    free(buf);
}

/* ============================================================ */
/* Verify spot-check: read first 16 + last 16 bytes from dst,    */
/* compare to expected linear gradient. Returns PASS string if   */
/* all 32 bytes match, FAIL otherwise (logs the first mismatch). */
/*                                                                */
/* dst layout: bank 0 covers VRAM offset 0..0xFFFF; bank 1 covers */
/* 0x10000..0x1FFFF. The 76800-byte dst region is in bank 0      */
/* (0..0xFFFF) + bank 1 (0x10000..0x12BFF).                       */
/* ============================================================ */

static const char *verify_dst_spot_check(void)
{
    uint8_t buf_first[16];
    uint8_t buf_last[16];

    /* First 16 bytes: VRAM offset 0..15 = bank 0 offset 0..15. */
    vbe_set_bank(0);
    dosmemget(0xA0000UL, sizeof buf_first, buf_first);

    /* Last 16 bytes: VRAM offset 0x12BF0..0x12BFF = bank 1 offset
     * 0x2BF0..0x2BFF = real-mode address 0xA0000 + 0x2BF0 = 0xA2BF0. */
    vbe_set_bank(1);
    dosmemget(0xA0000UL + 0x2BF0UL, sizeof buf_last, buf_last);

    /* Expected: dst[i] = i & 0xFF for i in [0, 76800).
     * So first 16: 0x00..0x0F. Last 16 at offsets 0x12BF0..0x12BFF:
     * (0x12BF0 + j) & 0xFF for j in 0..15 = (0xF0..0xFF). */
    for (int j = 0; j < 16; j++) {
        uint8_t exp = (uint8_t)(j & 0xFF);
        if (buf_first[j] != exp) {
            plog("  verify FAIL: dst[%d] = 0x%02X (expected 0x%02X)",
                 j, buf_first[j], exp);
            return "FAIL_FIRST";
        }
    }
    for (int j = 0; j < 16; j++) {
        uint8_t exp = (uint8_t)((0x12BF0 + j) & 0xFF);
        if (buf_last[j] != exp) {
            plog("  verify FAIL: dst[%d] = 0x%02X (expected 0x%02X)",
                 0x12BF0 + j, buf_last[j], exp);
            return "FAIL_LAST";
        }
    }
    return "PASS";
}

/* ============================================================ */
/* Dummy CPU work unit                                            */
/*                                                                */
/* A tight integer ALU loop with a volatile accumulator (so gcc   */
/* can't optimize it out). 64 iterations per unit; expected ~2-3 */
/* us per unit at 83 MHz Pentium. Touches one sysmem cache line  */
/* (the accum is a static volatile, so each call loads+stores it */
/* once). Chip is busy in VRAM during this; no contention with    */
/* the CPU L1/L2 cache for this work.                             */
/* ============================================================ */

static volatile uint32_t dummy_accum = 0;

#define DUMMY_INNER_ITERS 64

static inline void dummy_work_unit(uint32_t salt)
{
    uint32_t a = dummy_accum;
    for (int i = 0; i < DUMMY_INNER_ITERS; i++) {
        a = a * 1103515245u + 12345u + (uint32_t)i + salt;
    }
    dummy_accum = a;
}

/* ============================================================ */
/* Stat helpers                                                   */
/* ============================================================ */

#define N_REPS 100

static int dbl_cmp(const void *a, const void *b)
{
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

typedef struct { double min, med, p95, max, mean; } stats_t;

static void compute_stats(double *samples, int n, stats_t *out)
{
    qsort(samples, n, sizeof samples[0], dbl_cmp);
    out->min  = samples[0];
    out->max  = samples[n - 1];
    out->med  = samples[n / 2];
    out->p95  = samples[(int)(0.95 * n)];
    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += samples[i];
    out->mean = sum / n;
}

static void plog_stats(const char *label, const stats_t *st, const char *unit)
{
    plog("  %-30s min=%9.3f med=%9.3f mean=%9.3f p95=%9.3f max=%9.3f %s",
         label, st->min, st->med, st->mean, st->p95, st->max, unit);
}

/* ============================================================ */
/* Plausibility checks (sentinel pattern per                     */
/* probe_smoke_recipes_check_derived_metrics.md)                 */
/* ============================================================ */

static int plausible(double v, double lo, double hi)
{
    return (v >= lo && v <= hi);
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    plog("=== cirrus-blt-async-probe v2 Option 1 (wave-36 task #10) starting ===");
    plog("Built-via: DJGPP pure-C; target Cirrus 5434 BLT async parallelism");
    plog("UCLOCKS_PER_SEC = %lu", (unsigned long)UCLOCKS_PER_SEC);
    plog("v2 features: source@0x012C00 + linear-gradient pre-fill + verify_status");
    plog("             + Scenario D (GR[0x33]=0x00) + Scenario E (GR[0x33]=0x80)");
    plog("");

    /* ============================================================ */
    /* Step 1: calibrate RDTSC -> us conversion                     */
    /* ============================================================ */
    plog("---- Step 1: RDTSC calibration (100 ms uclock wait) ----");
    int cal_rc = calibrate_rdtsc();
    plog("cpu_mhz_calibrated = %u (PODP83 expected ~83; DOSBox-X varies)",
         (unsigned)g_cpu_mhz);
    plog("us_per_cycle = %.6f", g_us_per_cycle);
    if (cal_rc != 0) {
        plog("WARN: RDTSC calibration out of plausibility band (rc=%d).", cal_rc);
        plog("      us values may be unreliable. Cycle counts still emitted.");
    }
    plog("");

    /* ============================================================ */
    /* Step 2: chip detect                                          */
    /* ============================================================ */
    plog("---- Step 2: chip detect (CRTC[0x27] + SR[0x06] write-read) ----");
    uint8_t crtc27 = 0, sr06_wr = 0;
    const char *chip_name = NULL;
    int is_cirrus = detect_cirrus(&crtc27, &sr06_wr, &chip_name);
    plog("CRTC[0x27]      = 0x%02X", crtc27);
    plog("SR[0x06] readback after write 0x12 = 0x%02X", sr06_wr);
    plog("Identified: %s", chip_name);
    plog("");

    if (!is_cirrus) {
        plog("[cirrus-blt-async-probe BEGIN n=0 reps=0 reason=not_cirrus]");
        plog("[cirrus-blt-async-probe DONE VERDICT=REFUTE_CHIP_NOT_CIRRUS]");
        plog("BLT scenarios skipped (chip-specific). Operator: this is normal");
        plog("if running on a non-g2k system. On g2k expect CRTC[0x27]=0xA8.");
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 0;
    }

    /* ============================================================ */
    /* Step 3: VBE mode 0x101 (640x480x8 banked)                    */
    /* ============================================================ */
    plog("---- Step 3: VBE mode 0x101 (640x480x8 banked) ----");
    int vbe_rc = vbe_set_mode(0x0101);
    if (vbe_rc != 0) {
        plog("WARN: VBE 0x101 set failed (rc=%d). Trying 0x100.", vbe_rc);
        vbe_rc = vbe_set_mode(0x0100);
    }
    if (vbe_rc != 0) {
        plog("FATAL: cannot enter VESA 8bpp graphics. Cannot run BLT scenarios.");
        plog("[cirrus-blt-async-probe BEGIN n=0 reps=0 reason=vbe_failed]");
        plog("[cirrus-blt-async-probe DONE VERDICT=REFUTE_BLT_HUNG reason=vbe_mode_set_failed]");
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 2;
    }
    plog("VBE mode set OK.");
    /* Re-unlock Cirrus extensions + clear errata regs after mode set. */
    sr_write(0x06, 0x12);
    blt_clear_screen_to_screen_constraints();
    plog("Re-applied SR[0x06]=0x12 unlock + cleared GR[0x0B] bits 1/4.");
    plog("");

    /* ============================================================ */
    /* Step 4: pre-fill VRAM source                                 */
    /* ============================================================ */
    plog("---- Step 4: pre-fill VRAM source region (76800 bytes @ 0x%06lX) ----",
         VRAM_SRC_OFFSET);
    prefill_vram_source(0x55);
    plog("Source pre-fill done (pattern: byte^0x55).");
    plog("");

    /* ============================================================ */
    /* Step 5: Scenario A — BLT engagement warmup                   */
    /* ============================================================ */
    plog("---- Step 5: Scenario A — BLT engagement warmup (5 reps) ----");
    int blt_engaged = 1;
    for (int i = 0; i < 5; i++) {
        if (blt_wait_idle_uclock(0.05) != 0) {
            plog("WARN rep=%d: chip not idle pre-flight (50 ms cap).", i);
            blt_engaged = 0; break;
        }
        blt_program_bulk_copy();
        blt_kick();
        if (blt_wait_idle_uclock(0.20) != 0) {
            plog("FAIL rep=%d: BLT did not complete within 200 ms.", i);
            blt_engaged = 0; break;
        }
    }

    if (!blt_engaged) {
        text_mode_restore();
        plog("[cirrus-blt-async-probe BEGIN n=0 reps=0 reason=blt_hung]");
        plog("[cirrus-blt-async-probe DONE VERDICT=REFUTE_BLT_HUNG reason=warmup_failed]");
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 3;
    }
    plog("BLT warmup OK: 5/5 reps engaged + completed.");
    plog("");

    /* ============================================================ */
    /* Step 6: Scenario B — BASELINE dummy-work calibration         */
    /*                                                                */
    /* Goal: pin baseline_us_per_unit. Run K dummy units in a       */
    /* tight loop, time the whole thing via RDTSC, divide.          */
    /* Repeat N_REPS times for variance.                            */
    /* ============================================================ */
    plog("---- Step 6: Scenario B — baseline dummy-work calibration ----");
    plog("DUMMY_INNER_ITERS=%d per unit; batch=%d units; reps=%d",
         DUMMY_INNER_ITERS, 1000, N_REPS);

    double *samples_baseline_us_per_unit = (double *)malloc(sizeof(double) * N_REPS);
    if (!samples_baseline_us_per_unit) {
        plog("FATAL: malloc failed for baseline samples");
        text_mode_restore();
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 4;
    }

    /* Warmup once to populate caches. */
    for (int k = 0; k < 1000; k++) dummy_work_unit((uint32_t)k);

    for (int s = 0; s < N_REPS; s++) {
        uint64_t c0 = rdtsc();
        for (int k = 0; k < 1000; k++) dummy_work_unit((uint32_t)(k + s));
        uint64_t c1 = rdtsc();
        double us_total = cycles_to_us(c1 - c0);
        samples_baseline_us_per_unit[s] = us_total / 1000.0;
    }

    stats_t st_baseline;
    compute_stats(samples_baseline_us_per_unit, N_REPS, &st_baseline);
    plog_stats("baseline us/unit", &st_baseline, "us");
    double baseline_us_per_unit = st_baseline.med;
    plog("baseline_us_per_unit (median) = %.4f us", baseline_us_per_unit);

    int baseline_ok = plausible(baseline_us_per_unit, 0.05, 50.0);
    if (!baseline_ok) {
        plog("WARN: baseline_us_per_unit out of plausibility band [0.05, 50.0].");
    }
    plog("");

    /* ============================================================ */
    /* Step 7: Scenario C — BLT-ONLY tight-poll                     */
    /*                                                                */
    /* Kick BLT, immediately spin in tight blt_is_busy() poll. Time */
    /* via RDTSC. N_REPS samples.                                   */
    /* ============================================================ */
    plog("---- Step 7: Scenario C — BLT-ONLY tight-poll (n=%d) ----", N_REPS);
    double *samples_blt_alone_us = (double *)malloc(sizeof(double) * N_REPS);
    if (!samples_blt_alone_us) {
        plog("FATAL: malloc failed for blt-alone samples");
        free(samples_baseline_us_per_unit);
        text_mode_restore();
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 5;
    }

    int blt_alone_failures = 0;
    for (int s = 0; s < N_REPS; s++) {
        if (blt_wait_idle_uclock(0.20) != 0) {
            blt_alone_failures++;
            samples_blt_alone_us[s] = 99999.0;
            continue;
        }
        blt_program_bulk_copy();
        uint64_t c_kick = rdtsc();
        blt_kick();
        /* Tight poll, no other work. */
        for (;;) {
            if (!blt_is_busy()) break;
        }
        uint64_t c_done = rdtsc();
        samples_blt_alone_us[s] = cycles_to_us(c_done - c_kick);
    }

    stats_t st_blt_alone;
    compute_stats(samples_blt_alone_us, N_REPS, &st_blt_alone);
    plog_stats("blt-alone tight-poll us", &st_blt_alone, "us");
    plog("blt_alone_failures = %d / %d", blt_alone_failures, N_REPS);

    int blt_alone_ok = plausible(st_blt_alone.med, 500.0, 20000.0);
    if (!blt_alone_ok) {
        plog("WARN: blt_alone_us_med=%.3f out of plausibility band [500, 20000].",
             st_blt_alone.med);
    }
    /* Cross-anchor: wave_19_path_b_dead.md measured 3.85 ms / 19 MB/s for
     * this exact transfer on this exact chip. If we land far from that,
     * something is off. */
    plog("Cross-anchor: wave_19_path_b_dead.md = 3853 us / 19 MB/s on this chip.");
    plog("");

    /* ============================================================ */
    /* Step 8/9: Scenarios D + E — BLT-WITH-DUMMY interleaved       */
    /*                                                                */
    /* Per rep: kick BLT (with the scenario's GR[0x33] mode_ext),    */
    /* then in a loop:                                               */
    /*   - do BATCH dummy work units                                 */
    /*   - check BLT status (1 port read)                            */
    /*   - exit when chip is idle                                    */
    /* Count total dummy units completed during BLT window.         */
    /*                                                                */
    /* Scenario D: GR[0x33] = 0x00 (canonical mode; wave-19 baseline)*/
    /* Scenario E: GR[0x33] = 0x80 (bit 7 = "async" per team-lead).  */
    /* If a scenario fails to engage (BLT hung first rep), it skips   */
    /* and reports parallel_pct=0 so the max-verdict logic ignores it.*/
    /*                                                                */
    /* After each scenario's first successful rep, runs the verify   */
    /* spot-check on dst (first 16 + last 16 bytes vs linear gradient). */
    /* If verify FAIL on the best scenario, the verdict downgrades   */
    /* to REFUTE since parallel_pct numbers can't be trusted without */
    /* confidence the chip is actually copying.                      */
    /* ============================================================ */
    const int BATCH_UNITS = 16;

    typedef struct {
        const char *label;
        uint8_t     mode_ext;
        stats_t     st_blt_total, st_dummy_units, st_cpu_useful, st_parallel_pct;
        int         engaged;        /* 1 if BLT fired + completed first rep */
        int         failures;       /* count of reps that timed out */
        const char *verify_status;  /* "PASS"|"FAIL_FIRST"|"FAIL_LAST"|"SKIPPED" */
    } scenario_de_t;

    scenario_de_t scenarios[2] = {
        { "D", 0x00, {0}, {0}, {0}, {0}, 0, 0, "SKIPPED" },
        { "E", 0x80, {0}, {0}, {0}, {0}, 0, 0, "SKIPPED" },
    };

    double *samples_blt_total_us = (double *)malloc(sizeof(double) * N_REPS);
    double *samples_dummy_units  = (double *)malloc(sizeof(double) * N_REPS);
    double *samples_cpu_useful_us = (double *)malloc(sizeof(double) * N_REPS);
    double *samples_parallel_pct = (double *)malloc(sizeof(double) * N_REPS);
    if (!samples_blt_total_us || !samples_dummy_units ||
        !samples_cpu_useful_us || !samples_parallel_pct) {
        plog("FATAL: malloc failed for phase-D/E samples");
        free(samples_baseline_us_per_unit);
        free(samples_blt_alone_us);
        text_mode_restore();
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 6;
    }

    for (int sc = 0; sc < 2; sc++) {
        scenario_de_t *S = &scenarios[sc];
        plog("---- Step 8.%d: Scenario %s — BLT-WITH-DUMMY interleaved",
             sc + 1, S->label);
        plog("                 (GR[0x33]=0x%02X, n=%d) ----",
             (unsigned)S->mode_ext, N_REPS);
        plog("Pattern: kick BLT, then loop {BATCH=%d units, poll}, exit on idle.",
             BATCH_UNITS);

        /* Defensive: issue BLT_RESET (GR[0x31]=0x04) before each scenario.
         * If prior scenario hung the engine in BLT_PROGRESS state, reset
         * recovers it before the new mode_ext takes effect. Mirrors the
         * bltfill.c v2 inter-mode recovery pattern. */
        gr_write(0x31, 0x04);

        /* First rep: programmed + kicked + run; on success, sets engaged=1 +
         * runs the verify spot-check. On failure (BLT hung within 200 ms),
         * marks scenario skipped and reports parallel_pct=0 for all reps. */
        S->failures = 0;
        for (int s = 0; s < N_REPS; s++) {
            if (blt_wait_idle_uclock(0.20) != 0) {
                S->failures++;
                samples_blt_total_us[s]  = 99999.0;
                samples_dummy_units[s]   = 0.0;
                samples_cpu_useful_us[s] = 0.0;
                samples_parallel_pct[s]  = 0.0;
                if (s == 0) {
                    /* First-rep hang => scenario unsupported. Fill rest with
                     * sentinels and break out. */
                    plog("Scenario %s first-rep BLT hung; chip rejects mode_ext=0x%02X.",
                         S->label, (unsigned)S->mode_ext);
                    for (int t = 1; t < N_REPS; t++) {
                        samples_blt_total_us[t]  = 99999.0;
                        samples_dummy_units[t]   = 0.0;
                        samples_cpu_useful_us[t] = 0.0;
                        samples_parallel_pct[t]  = 0.0;
                    }
                    break;
                }
                continue;
            }

            blt_program_bulk_copy_ext(S->mode_ext);
            uint64_t c_kick = rdtsc();
            blt_kick();

            long dummy_units_completed = 0;
            int  blt_completed = 0;
            for (int g = 0; g < 200000; g++) {
                for (int k = 0; k < BATCH_UNITS; k++) {
                    dummy_work_unit((uint32_t)(g * BATCH_UNITS + k));
                }
                dummy_units_completed += BATCH_UNITS;
                if (!blt_is_busy()) { blt_completed = 1; break; }
            }
            uint64_t c_done = rdtsc();

            if (!blt_completed) {
                /* Runaway: 200000 batches and still busy. Mark as failure. */
                S->failures++;
                samples_blt_total_us[s]  = 99999.0;
                samples_dummy_units[s]   = 0.0;
                samples_cpu_useful_us[s] = 0.0;
                samples_parallel_pct[s]  = 0.0;
                continue;
            }

            double blt_total = cycles_to_us(c_done - c_kick);
            double cpu_useful = (double)dummy_units_completed * baseline_us_per_unit;
            double parallel_pct = (blt_total > 0.0)
                ? (100.0 * cpu_useful / blt_total)
                : 0.0;

            samples_blt_total_us[s]   = blt_total;
            samples_dummy_units[s]    = (double)dummy_units_completed;
            samples_cpu_useful_us[s]  = cpu_useful;
            samples_parallel_pct[s]   = parallel_pct;

            /* After first successful rep, run verify spot-check. */
            if (s == 0 && S->engaged == 0) {
                S->engaged = 1;
                /* Wait for BLT idle one more time to be safe, then verify. */
                blt_wait_idle_uclock(0.10);
                S->verify_status = verify_dst_spot_check();
                plog("Scenario %s verify_status = %s", S->label, S->verify_status);
            }
        }

        if (S->engaged) {
            compute_stats(samples_blt_total_us,  N_REPS, &S->st_blt_total);
            compute_stats(samples_dummy_units,   N_REPS, &S->st_dummy_units);
            compute_stats(samples_cpu_useful_us, N_REPS, &S->st_cpu_useful);
            compute_stats(samples_parallel_pct,  N_REPS, &S->st_parallel_pct);

            plog_stats("blt-total (kick->idle) us", &S->st_blt_total,    "us");
            plog_stats("dummy_units / rep",         &S->st_dummy_units,  "units");
            plog_stats("cpu_useful us / rep",       &S->st_cpu_useful,   "us");
            plog_stats("cpu_parallel pct",          &S->st_parallel_pct, "%");
            plog("Scenario %s failures = %d / %d (engaged=%d verify=%s)",
                 S->label, S->failures, N_REPS, S->engaged, S->verify_status);
        } else {
            plog("Scenario %s SKIPPED (BLT rejected mode_ext=0x%02X).",
                 S->label, (unsigned)S->mode_ext);
        }
        plog("");
    }

    /* For backward-compat references in the verdict + ANCHOR emit, alias
     * the "best" scenario to st_* names. "Best" = max parallel_pct_med
     * among engaged scenarios. */
    int best_idx = -1;
    double best_parallel = -1e9;
    for (int sc = 0; sc < 2; sc++) {
        if (scenarios[sc].engaged && scenarios[sc].st_parallel_pct.med > best_parallel) {
            best_parallel = scenarios[sc].st_parallel_pct.med;
            best_idx = sc;
        }
    }

    stats_t st_blt_total = {0}, st_parallel_pct = {0};
    const char *verify_status_best = "SKIPPED";
    const char *best_label = "(none)";
    uint8_t     best_mode_ext = 0xFF;
    if (best_idx >= 0) {
        st_blt_total    = scenarios[best_idx].st_blt_total;
        st_parallel_pct = scenarios[best_idx].st_parallel_pct;
        verify_status_best = scenarios[best_idx].verify_status;
        best_label = scenarios[best_idx].label;
        best_mode_ext = scenarios[best_idx].mode_ext;
    }
    plog("Best scenario: %s (GR[0x33]=0x%02X) parallel_pct_med=%.1f verify=%s",
         best_label, (unsigned)best_mode_ext, best_parallel, verify_status_best);
    plog("");

    /* ============================================================ */
    /* Step 9: derived verdict                                       */
    /*                                                                */
    /* Verdict is gated by:                                          */
    /*   1. verify_status PASS on the best scenario (chip ACTUALLY    */
    /*      copied the source pattern to dst). FAIL on best -> REFUTE */
    /*      because parallel_pct numbers can't be trusted.            */
    /*   2. blt_total_us_med within plausibility band.               */
    /*   3. parallel_pct_med thresholds per team-lead brief.         */
    /* ============================================================ */
    plog("---- Step 9: Verdict ----");

    int parallel_pct_ok = plausible(st_parallel_pct.med, -10.0, 200.0);
    if (!parallel_pct_ok) {
        plog("WARN: cpu_parallel_pct_med=%.1f out of plausibility band [-10, 200].",
             st_parallel_pct.med);
    }
    int verify_ok = (verify_status_best && strcmp(verify_status_best, "PASS") == 0);

    const char *verdict = "UNKNOWN";
    const char *reason  = "";

    if (best_idx < 0) {
        verdict = "REFUTE_BLT_HUNG";
        reason  = "all D/E scenarios hung (chip refused both 0x00 and 0x80 mode_ext)";
    } else if (!verify_ok) {
        verdict = "REFUTE_VERIFY_FAIL";
        reason  = "best scenario verify_status != PASS (chip not copying correctly)";
    } else if (st_blt_total.med > 10000.0) {
        verdict = "REFUTE_BANDWIDTH";
        reason  = "blt_total_us_med > 10 ms (mechanism refuted)";
    } else if (st_parallel_pct.med > 50.0) {
        verdict = "SHIP";
        reason  = "cpu_parallel_pct_med > 50% (slot 0133 viable)";
    } else if (st_parallel_pct.med >= 20.0) {
        verdict = "DEFER";
        reason  = "cpu_parallel_pct_med in 20-50% (marginal; defer)";
    } else if (st_parallel_pct.med >= -10.0) {
        verdict = "CANCEL";
        reason  = "cpu_parallel_pct_med < 20% (slot 0133 dead)";
    } else {
        verdict = "REFUTE_BANDWIDTH";
        reason  = "cpu_parallel_pct_med out of plausibility band";
    }

    plog("VERDICT = %s  (%s)", verdict, reason);
    plog("");

    /* ============================================================ */
    /* Step 10: text mode restore + ANCHOR emit                      */
    /* ============================================================ */
    text_mode_restore();
    plog("---- Step 10: VBE text mode restored ----");
    plog("");

    plog("[cirrus-blt-async-probe BEGIN n=%d reps=%d batch_units=%d chip=%s scenarios=D,E]",
         N_REPS, N_REPS, BATCH_UNITS, chip_name);
    plog("[cirrus-blt-async-probe DONE");
    plog("    cpu_mhz=%u us_per_cycle=%.6f",
         (unsigned)g_cpu_mhz, g_us_per_cycle);
    plog("    baseline_us_per_unit=%.4f baseline_us_per_unit_min=%.4f baseline_us_per_unit_max=%.4f",
         baseline_us_per_unit, st_baseline.min, st_baseline.max);
    plog("    blt_alone_us_med=%.1f blt_alone_us_min=%.1f blt_alone_us_max=%.1f failures=%d",
         st_blt_alone.med, st_blt_alone.min, st_blt_alone.max, blt_alone_failures);
    /* Per-scenario emit lines for both D + E (whether engaged or skipped). */
    for (int sc = 0; sc < 2; sc++) {
        scenario_de_t *S = &scenarios[sc];
        if (S->engaged) {
            plog("    scenario_%s mode_ext=0x%02X engaged=1 verify=%s",
                 S->label, (unsigned)S->mode_ext, S->verify_status);
            plog("        blt_total_us_med=%.1f min=%.1f max=%.1f failures=%d",
                 S->st_blt_total.med, S->st_blt_total.min,
                 S->st_blt_total.max, S->failures);
            plog("        dummy_units_med=%.0f cpu_useful_us_med=%.1f",
                 S->st_dummy_units.med, S->st_cpu_useful.med);
            plog("        cpu_parallel_pct_med=%.1f min=%.1f max=%.1f",
                 S->st_parallel_pct.med, S->st_parallel_pct.min,
                 S->st_parallel_pct.max);
        } else {
            plog("    scenario_%s mode_ext=0x%02X engaged=0 verify=SKIPPED parallel_pct_med=0.0",
                 S->label, (unsigned)S->mode_ext);
        }
    }
    plog("    best_scenario=%s best_mode_ext=0x%02X best_parallel_pct_med=%.1f best_verify=%s",
         best_label, (unsigned)best_mode_ext, best_parallel, verify_status_best);
    plog("    plausibility: cpu_mhz_in_band=%d baseline_in_band=%d blt_alone_in_band=%d parallel_pct_in_band=%d verify_ok=%d",
         plausible((double)g_cpu_mhz, 50.0, 5000.0) ? 1 : 0,
         baseline_ok ? 1 : 0, blt_alone_ok ? 1 : 0,
         parallel_pct_ok ? 1 : 0, verify_ok ? 1 : 0);
    plog("    VERDICT=%s reason=\"%s\"]", verdict, reason);
    plog("");
    plog("Decision gates (team-lead task #10):");
    plog("  cpu_parallel_pct > 50  -> SHIP slot 0133; nx-engine starts");
    plog("  cpu_parallel_pct 20-50 -> DEFER");
    plog("  cpu_parallel_pct < 20  -> CANCEL slot 0133");
    plog("  blt_total_us > 10000   -> REFUTE entire mechanism");
    plog("");
    plog("Cross-anchor: wave_19_path_b_dead.md = 3853 us BLT-only on this chip.");
    plog("If our blt_alone_us_med is far from ~3850, sanity-check the probe.");
    plog("");
    plog("=== cirrus-blt-async-probe done ===");
    plog("[SENTINEL_END]");

    free(samples_baseline_us_per_unit);
    free(samples_blt_alone_us);
    free(samples_blt_total_us);
    free(samples_dummy_units);
    free(samples_cpu_useful_us);
    free(samples_parallel_pct);
    if (g_log) fclose(g_log);
    return 0;
}
