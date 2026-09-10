/*
 * lfbnear-probe.c -- LFB nearptr VRAM-write throughput vs banked dosmemput
 * (Phase 11 wave-36 ceiling-bust probe A).
 *
 * GOAL: surface whether g2k's 19 MB/s dosmemput ceiling is a CHIP bandwidth
 * limit OR a banked-mode-CPU-PIO artifact. Per wave-36 BLTFILL evidence,
 * COLOR_EXPAND BLT writes VRAM at 43 MB/s while dosmemput hits 19.6 MB/s.
 * The 2.2x gap is suspect: either bank-switch overhead, or per-byte CPU PIO
 * through the 64KB banked window stalls.
 *
 * Direct LFB nearptr access bypasses both: single 32-bit physical mapping
 * via DPMI int 0x31 / fn 0x0800, then __djgpp_nearptr_enable() + base-address
 * subtraction yields a dereferenceable C pointer. memcpy / memset run at
 * CPU-native bandwidth, no bank-switch, no DPMI per-byte thunk.
 *
 * KNOWN HAZARD: per `wave_19_path_b_dead.md` + CLAUDE.md SDL3-DOS quirks,
 * g2k's SDL backend force-defaults to banked because
 * `DOSVESA_DetectLFBApertureBug()` flagged the 5434+UNIVBE aperture-decoupling
 * bug. This probe tests whether the bug is specific to SDL's compositing
 * layer OR a hardware-level fault. Either outcome is informative:
 *   - LFB engages + verify=PASS at higher throughput -> re-litigates SDL/0014
 *   - LFB hangs or produces garbage -> aperture bug confirmed at low level
 *
 * SCENARIOS:
 *   A. control: dosmemput sysmem->banked-VRAM 76800 bytes (BLTFILL Scenario B baseline)
 *   B. test:    memcpy from sysmem to LFB nearptr 76800 bytes
 *   C. test:    memset on LFB nearptr 76800 bytes (fill ceiling, no sysmem read)
 *
 * Per-test: 100 reps, RDTSC-timed, MB/s + ms median emit. Verify byte-match
 * on dst spot-check (first 16 + last 16) for B + C.
 *
 * Decision gates (team-lead task):
 *   B (memcpy) > 1.4x A (dosmemput) MB/s  -> SHIP signal; LFB is dosmemput
 *                                            replacement; re-litigate SDL/0014
 *   B < 1.2x A                            -> DROP; LFB doesn't help on this chip
 *   B verify=FAIL or HANG                 -> aperture-bug confirmed at low level
 *   C > sysmem memset 43.7 MB/s ceiling   -> harness bug (measuring host RAM?)
 *
 * Plausibility bounds (generous; aim is to surface "this can't be right"
 * numbers, not gate on tight expected real-HW range):
 *   - cpu_mhz in [50, 5000]
 *   - dosmemput_mbs in [5, 300] (real HW ~19.6 expected; DOSBox-X 100+; both OK)
 *   - lfb_memcpy_mbs in [5, 300] (real HW UNKNOWN -- whole point of probe)
 *   - lfb_memset_mbs in [5, 300]
 *   - speedup_memcpy_vs_dosmemput in [0.5, 10.0]
 *
 * The verdict gates on the SPEEDUP RATIO, not the absolute MB/s, because
 * DOSBox-X emulates both banked + LFB as host RAM (both ~120 MB/s); the
 * speedup near 1.0x correctly resolves to DROP under emulation. Real-HW
 * pressure-test happens at decomp time when the operator log is parsed.
 *
 * 8.3 DOS filenames:
 *   Source:   tests/probes/lfbnear-probe.c
 *   Binary:   LFBNEAR.EXE (7+3)
 *   Log:      LFBNEAR.LOG (7+3)
 *   BAT:      LFBNEAR.BAT (7+3)
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
    g_log = fopen("LFBNEAR.LOG", "w");
    if (!g_log) g_log = fopen("C:\\LFBNEAR.LOG", "w");
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
/* VBE helpers                                                   */
/* ============================================================ */

/* VBE 2.0 ModeInfoBlock (256-byte structure; we care about a few fields). */
typedef struct __attribute__((packed)) {
    uint16_t ModeAttributes;       /* offset 0x00 */
    uint8_t  WinAAttributes;       /* offset 0x02 */
    uint8_t  WinBAttributes;       /* offset 0x03 */
    uint16_t WinGranularity;       /* offset 0x04 */
    uint16_t WinSize;              /* offset 0x06 */
    uint16_t WinASegment;          /* offset 0x08 */
    uint16_t WinBSegment;          /* offset 0x0A */
    uint32_t WinFuncPtr;           /* offset 0x0C */
    uint16_t BytesPerScanLine;     /* offset 0x10 */
    uint16_t XResolution;          /* offset 0x12 */
    uint16_t YResolution;          /* offset 0x14 */
    uint8_t  XCharSize;            /* offset 0x16 */
    uint8_t  YCharSize;            /* offset 0x17 */
    uint8_t  NumberOfPlanes;       /* offset 0x18 */
    uint8_t  BitsPerPixel;         /* offset 0x19 */
    uint8_t  NumberOfBanks;        /* offset 0x1A */
    uint8_t  MemoryModel;          /* offset 0x1B */
    uint8_t  BankSize;             /* offset 0x1C */
    uint8_t  NumberOfImagePages;   /* offset 0x1D */
    uint8_t  Reserved1;            /* offset 0x1E */
    uint8_t  RedMaskSize;          /* offset 0x1F */
    uint8_t  RedFieldPosition;     /* offset 0x20 */
    uint8_t  GreenMaskSize;        /* offset 0x21 */
    uint8_t  GreenFieldPosition;   /* offset 0x22 */
    uint8_t  BlueMaskSize;         /* offset 0x23 */
    uint8_t  BlueFieldPosition;    /* offset 0x24 */
    uint8_t  RsvdMaskSize;         /* offset 0x25 */
    uint8_t  RsvdFieldPosition;    /* offset 0x26 */
    uint8_t  DirectColorModeInfo;  /* offset 0x27 */
    uint32_t PhysBasePtr;          /* offset 0x28 -- LFB physical address (VBE 2.0+) */
    uint32_t Reserved2;
    uint16_t Reserved3;
    /* ... rest of 256-byte block omitted */
} vbe_mode_info_t;

/* Read mode info via INT 0x10 AX=0x4F01 CX=mode into __tb, then dosmemget. */
static int vbe_get_mode_info(uint16_t mode, vbe_mode_info_t *out)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F01;
    r.x.cx = mode;
    r.x.es = (__tb >> 4) & 0xFFFF;
    r.x.di = __tb & 0x0F;
    if (__dpmi_int(0x10, &r) < 0) return -1;
    if (r.x.ax != 0x004F) return -2;
    memset(out, 0, sizeof *out);
    dosmemget(__tb, sizeof *out, out);
    return 0;
}

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
/* Geometry                                                      */
/* ============================================================ */

#define BLT_BYTES   76800L  /* 320 x 240 */

/* ============================================================ */
/* Stat helper                                                   */
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

/* ============================================================ */
/* Verify spot-check helper                                      */
/*                                                                */
/* Tests whether dst region got the expected linear-gradient     */
/* pattern (byte at offset N = N & 0xFF). Reads via the same     */
/* nearptr the writes used.                                      */
/* ============================================================ */

static const char *verify_lfb_linear_gradient(volatile uint8_t *lfb_base)
{
    for (int j = 0; j < 16; j++) {
        uint8_t exp = (uint8_t)(j & 0xFF);
        if (lfb_base[j] != exp) {
            plog("  verify FAIL: lfb[%d] = 0x%02X (expected 0x%02X)",
                 j, (unsigned)lfb_base[j], (unsigned)exp);
            return "FAIL_FIRST";
        }
    }
    long off = BLT_BYTES - 16;
    for (int j = 0; j < 16; j++) {
        uint8_t exp = (uint8_t)((off + j) & 0xFF);
        if (lfb_base[off + j] != exp) {
            plog("  verify FAIL: lfb[%ld] = 0x%02X (expected 0x%02X)",
                 off + j, (unsigned)lfb_base[off + j], (unsigned)exp);
            return "FAIL_LAST";
        }
    }
    return "PASS";
}

static const char *verify_lfb_memset_pattern(volatile uint8_t *lfb_base, uint8_t fill)
{
    for (int j = 0; j < 16; j++) {
        if (lfb_base[j] != fill) {
            plog("  verify FAIL: lfb[%d] = 0x%02X (expected 0x%02X)",
                 j, (unsigned)lfb_base[j], (unsigned)fill);
            return "FAIL_FIRST";
        }
    }
    long off = BLT_BYTES - 16;
    for (int j = 0; j < 16; j++) {
        if (lfb_base[off + j] != fill) {
            plog("  verify FAIL: lfb[%ld] = 0x%02X (expected 0x%02X)",
                 off + j, (unsigned)lfb_base[off + j], (unsigned)fill);
            return "FAIL_LAST";
        }
    }
    return "PASS";
}

/* Read dst first-16 + last-16 via banked VBE (Scenario A control). */
static const char *verify_banked_linear_gradient(void)
{
    uint8_t buf_first[16], buf_last[16];
    vbe_set_bank(0);
    dosmemget(0xA0000UL, sizeof buf_first, buf_first);
    /* Last 16 bytes of 76800 at VRAM offset 0x12BF0 = bank 1 offset 0x2BF0. */
    vbe_set_bank(1);
    dosmemget(0xA0000UL + 0x2BF0UL, sizeof buf_last, buf_last);
    for (int j = 0; j < 16; j++) {
        if (buf_first[j] != (uint8_t)(j & 0xFF)) {
            plog("  verify FAIL: banked dst[%d] = 0x%02X (expected 0x%02X)",
                 j, (unsigned)buf_first[j], (unsigned)(j & 0xFF));
            return "FAIL_FIRST";
        }
    }
    for (int j = 0; j < 16; j++) {
        uint8_t exp = (uint8_t)((0x12BF0 + j) & 0xFF);
        if (buf_last[j] != exp) {
            plog("  verify FAIL: banked dst[%d] = 0x%02X (expected 0x%02X)",
                 0x12BF0 + j, (unsigned)buf_last[j], (unsigned)exp);
            return "FAIL_LAST";
        }
    }
    return "PASS";
}

/* ============================================================ */
/* Plausibility                                                  */
/* ============================================================ */

static int plausible(double v, double lo, double hi)
{
    return (v >= lo && v <= hi);
}

/* Compute MB/s from ms-per-op + bytes-per-op. */
static double mbs_from_ms(double ms_per_op, long bytes_per_op)
{
    if (ms_per_op <= 0.0) return 0.0;
    /* (bytes / 1048576) / (ms / 1000) = (bytes * 1000) / (1048576 * ms) */
    return ((double)bytes_per_op * 1000.0) / (1048576.0 * ms_per_op);
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    plog("=== lfbnear-probe v1 (wave-36 ceiling-bust A) starting ===");
    plog("DJGPP pure-C; target: LFB nearptr VRAM-write throughput");
    plog("Hypothesis: 19 MB/s dosmemput ceiling is banked-mode + CPU-PIO artifact,");
    plog("not a chip bandwidth limit. LFB nearptr access bypasses both.");
    plog("UCLOCKS_PER_SEC = %lu", (unsigned long)UCLOCKS_PER_SEC);
    plog("");

    /* ----- Step 1: RDTSC calibration ----- */
    plog("---- Step 1: RDTSC calibration ----");
    calibrate_rdtsc();
    plog("cpu_mhz_calibrated = %u  us_per_cycle = %.6f",
         (unsigned)g_cpu_mhz, g_us_per_cycle);
    int cpu_mhz_ok = plausible((double)g_cpu_mhz, 50.0, 5000.0);
    plog("");

    /* ----- Step 2: VBE 2.0 LFB mode-info inquiry ----- */
    plog("---- Step 2: VBE 2.0 mode-info inquiry (mode=0x4101) ----");
    vbe_mode_info_t mi;
    int rc = vbe_get_mode_info(0x4101, &mi);
    if (rc != 0) {
        plog("FAIL: vbe_get_mode_info(0x4101) rc=%d", rc);
        plog("[lfbnear-probe DONE VERDICT=FAIL_NO_LFB reason=mode_info_inquiry_failed]");
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 2;
    }
    plog("ModeInfo: %dx%d %dbpp ModeAttributes=0x%04X",
         (int)mi.XResolution, (int)mi.YResolution, (int)mi.BitsPerPixel,
         (unsigned)mi.ModeAttributes);
    plog("PhysBasePtr = 0x%08lX  (raw VBE-reported LFB physical address)",
         (unsigned long)mi.PhysBasePtr);
    int has_lfb_attr = (mi.ModeAttributes & 0x80) != 0;  /* bit 7 = LFB Available */
    plog("LFB-Available attribute bit 7: %s", has_lfb_attr ? "SET" : "CLEAR");
    if (!has_lfb_attr || mi.PhysBasePtr == 0) {
        plog("FAIL: mode 0x4101 does NOT advertise LFB on this chip.");
        plog("[lfbnear-probe DONE VERDICT=FAIL_NO_LFB reason=lfb_attr_missing]");
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 3;
    }
    plog("");

    /* ----- Step 3: set mode 0x4101 ----- */
    plog("---- Step 3: VBE set mode 0x4101 ----");
    int set_rc = vbe_set_mode(0x4101);
    if (set_rc != 0) {
        plog("FAIL: vbe_set_mode(0x4101) rc=%d", set_rc);
        plog("[lfbnear-probe DONE VERDICT=FAIL_NO_LFB reason=mode_set_failed]");
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 4;
    }
    plog("Mode 0x4101 set OK.");
    plog("");

    /* ----- Step 4: DPMI physical-address mapping ----- */
    plog("---- Step 4: DPMI physical-address mapping ----");
    __dpmi_meminfo info;
    memset(&info, 0, sizeof info);
    info.address = mi.PhysBasePtr;
    info.size    = 1024UL * 1024UL;  /* map 1 MB of LFB (entire 5434 framebuffer) */
    if (__dpmi_physical_address_mapping(&info) != 0) {
        plog("FAIL: __dpmi_physical_address_mapping(phys=0x%08lX size=1MB) failed",
             (unsigned long)mi.PhysBasePtr);
        text_mode_restore();
        plog("[lfbnear-probe DONE VERDICT=FAIL_NO_LFB reason=dpmi_map_failed]");
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 5;
    }
    plog("DPMI mapping OK: phys=0x%08lX -> linear=0x%08lX size=%lu",
         (unsigned long)mi.PhysBasePtr,
         (unsigned long)info.address,
         (unsigned long)info.size);
    plog("");

    /* ----- Step 5: __djgpp_nearptr_enable ----- */
    plog("---- Step 5: __djgpp_nearptr_enable ----");
    if (!__djgpp_nearptr_enable()) {
        plog("FAIL: __djgpp_nearptr_enable() failed (DPMI host lacks CS-unlimit?)");
        __dpmi_free_physical_address_mapping(&info);
        text_mode_restore();
        plog("[lfbnear-probe DONE VERDICT=FAIL_NEARPTR reason=nearptr_enable_failed]");
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 6;
    }
    plog("nearptr enabled.  __djgpp_base_address = 0x%08lX",
         (unsigned long)__djgpp_base_address);
    volatile uint8_t *lfb = (volatile uint8_t *)(info.address - __djgpp_base_address);
    plog("LFB nearptr base = 0x%08lX (= linear 0x%08lX - base 0x%08lX)",
         (unsigned long)(uintptr_t)lfb,
         (unsigned long)info.address,
         (unsigned long)__djgpp_base_address);
    plog("");

    /* ----- Step 6: allocate + pre-fill sysmem source buffer ----- */
    plog("---- Step 6: sysmem source buffer (linear gradient i&0xFF) ----");
    uint8_t *src = (uint8_t *)malloc(BLT_BYTES);
    if (!src) {
        plog("FATAL: malloc(%ld) failed", BLT_BYTES);
        __djgpp_nearptr_disable();
        __dpmi_free_physical_address_mapping(&info);
        text_mode_restore();
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 7;
    }
    for (long i = 0; i < BLT_BYTES; i++) src[i] = (uint8_t)(i & 0xFF);
    plog("Sysmem source pre-fill done (%ld bytes).", BLT_BYTES);
    plog("");

    /* ----- Step 7: Scenario A control (dosmemput sysmem -> banked-VRAM) ----- */
    plog("---- Step 7: Scenario A control (dosmemput sysmem -> banked-VRAM x %d) ----",
         N_REPS);
    /* NOTE: we're in LFB mode 0x4101. Banked VBE-window operations still work
     * on the 5434 (VBE supports both banked + LFB simultaneously when LFB is
     * advertised), so dosmemput via the 0xA0000 window is comparable to the
     * 0x0101 banked baseline from BLTFILL. Cross-check the resulting MB/s
     * against BLTFILL Scenario B (~19.6 MB/s); within 10% confirms.
     */
    double *samples_A = (double *)malloc(sizeof(double) * N_REPS);

    /* Warmup. */
    vbe_set_bank(0);
    dosmemput(src, 65536, 0xA0000);
    vbe_set_bank(1);
    dosmemput(src + 65536, BLT_BYTES - 65536, 0xA0000);

    for (int s = 0; s < N_REPS; s++) {
        uint64_t c0 = rdtsc();
        vbe_set_bank(0);
        dosmemput(src, 65536, 0xA0000);
        vbe_set_bank(1);
        dosmemput(src + 65536, BLT_BYTES - 65536, 0xA0000);
        uint64_t c1 = rdtsc();
        samples_A[s] = cycles_to_us(c1 - c0) / 1000.0;  /* ms */
    }
    stats_t st_A;
    compute_stats(samples_A, N_REPS, &st_A);
    double mbs_A = mbs_from_ms(st_A.med, BLT_BYTES);
    plog("Scenario A (dosmemput banked): min=%.3f med=%.3f mean=%.3f p95=%.3f max=%.3f ms",
         st_A.min, st_A.med, st_A.mean, st_A.p95, st_A.max);
    plog("Scenario A effective MB/s (median): %.2f", mbs_A);
    const char *verify_A = verify_banked_linear_gradient();
    plog("Scenario A verify (banked readback): %s", verify_A);
    plog("");

    /* ----- Step 8: Scenario B test (memcpy sysmem -> LFB nearptr) ----- */
    plog("---- Step 8: Scenario B test (memcpy sysmem -> LFB nearptr x %d) ----",
         N_REPS);
    double *samples_B = (double *)malloc(sizeof(double) * N_REPS);
    int b_engaged = 1;

    /* Warmup. */
    memcpy((void *)lfb, src, BLT_BYTES);

    for (int s = 0; s < N_REPS; s++) {
        uint64_t c0 = rdtsc();
        memcpy((void *)lfb, src, BLT_BYTES);
        uint64_t c1 = rdtsc();
        samples_B[s] = cycles_to_us(c1 - c0) / 1000.0;
    }
    stats_t st_B;
    compute_stats(samples_B, N_REPS, &st_B);
    double mbs_B = mbs_from_ms(st_B.med, BLT_BYTES);
    plog("Scenario B (LFB memcpy): min=%.3f med=%.3f mean=%.3f p95=%.3f max=%.3f ms",
         st_B.min, st_B.med, st_B.mean, st_B.p95, st_B.max);
    plog("Scenario B effective MB/s (median): %.2f", mbs_B);
    const char *verify_B = verify_lfb_linear_gradient(lfb);
    plog("Scenario B verify (LFB readback first16 + last16): %s", verify_B);
    plog("");

    /* ----- Step 9: Scenario C test (memset LFB nearptr 0xAA) ----- */
    plog("---- Step 9: Scenario C test (memset LFB nearptr 0xAA x %d) ----",
         N_REPS);
    double *samples_C = (double *)malloc(sizeof(double) * N_REPS);

    /* Warmup. */
    memset((void *)lfb, 0xAA, BLT_BYTES);

    for (int s = 0; s < N_REPS; s++) {
        uint64_t c0 = rdtsc();
        memset((void *)lfb, 0xAA, BLT_BYTES);
        uint64_t c1 = rdtsc();
        samples_C[s] = cycles_to_us(c1 - c0) / 1000.0;
    }
    stats_t st_C;
    compute_stats(samples_C, N_REPS, &st_C);
    double mbs_C = mbs_from_ms(st_C.med, BLT_BYTES);
    plog("Scenario C (LFB memset 0xAA): min=%.3f med=%.3f mean=%.3f p95=%.3f max=%.3f ms",
         st_C.min, st_C.med, st_C.mean, st_C.p95, st_C.max);
    plog("Scenario C effective MB/s (median): %.2f", mbs_C);
    const char *verify_C = verify_lfb_memset_pattern(lfb, 0xAA);
    plog("Scenario C verify (LFB readback first16 + last16): %s", verify_C);
    plog("");

    /* ----- Step 10: cleanup ----- */
    __djgpp_nearptr_disable();
    __dpmi_free_physical_address_mapping(&info);
    text_mode_restore();
    plog("---- Step 10: cleanup (nearptr_disable + free_mapping + text mode) ----");
    plog("");

    /* ----- Step 11: verdict + ANCHOR emit ----- */
    plog("---- Step 11: Verdict ----");
    int dosmemput_ok = plausible(mbs_A, 5.0, 300.0);
    int lfb_memcpy_ok = plausible(mbs_B, 5.0, 300.0);
    int lfb_memset_ok = plausible(mbs_C, 5.0, 300.0);
    int verify_B_pass = (strcmp(verify_B, "PASS") == 0);
    int verify_A_pass = (strcmp(verify_A, "PASS") == 0);
    /* verify_C captured + emitted but not currently gating the verdict
     * (Scenario C is a chip-write-ceiling reference, not the lever decision). */
    double speedup_B_vs_A = (mbs_A > 0.0) ? (mbs_B / mbs_A) : 0.0;
    int speedup_ok = plausible(speedup_B_vs_A, 0.5, 10.0);

    const char *verdict = "UNKNOWN";
    const char *reason  = "";

    if (!verify_A_pass) {
        verdict = "HARNESS_SUSPECT";
        reason  = "control Scenario A verify FAIL (banked dosmemput broken? aborted)";
    } else if (!verify_B_pass) {
        verdict = "FAIL_LFB_APERTURE_BUG";
        reason  = "Scenario B verify FAIL (LFB writes not landing); aperture-bug confirmed at low level";
    } else if (!lfb_memcpy_ok) {
        verdict = "FAIL_PLAUSIBILITY";
        reason  = "lfb_memcpy_mbs out of [10, 60] band; harness suspect";
    } else if (speedup_B_vs_A > 1.4) {
        verdict = "SHIP";
        reason  = "lfb_memcpy > 1.4x dosmemput; LFB is dosmemput replacement; re-litigate SDL/0014";
    } else if (speedup_B_vs_A >= 1.2) {
        verdict = "DEFER";
        reason  = "lfb_memcpy 1.2-1.4x dosmemput (marginal); defer";
    } else if (speedup_B_vs_A >= 0.7) {
        verdict = "DROP";
        reason  = "lfb_memcpy near parity with dosmemput; LFB doesn't help on this chip";
    } else {
        verdict = "DROP";
        reason  = "lfb_memcpy SLOWER than dosmemput; LFB strictly worse";
    }

    plog("dosmemput_mbs = %.2f  (banked baseline; BLTFILL anchor ~19.6 MB/s)", mbs_A);
    plog("lfb_memcpy_mbs = %.2f  verify=%s", mbs_B, verify_B);
    plog("lfb_memset_mbs = %.2f  verify=%s", mbs_C, verify_C);
    plog("speedup_memcpy_vs_dosmemput = %.2fx", speedup_B_vs_A);
    plog("VERDICT = %s  (%s)", verdict, reason);
    plog("");

    plog("[lfbnear-probe BEGIN n=%d chip=cirrus5434 mode=0x4101 phys=0x%08lX]",
         N_REPS, (unsigned long)mi.PhysBasePtr);
    plog("[lfbnear-probe DONE");
    plog("    cpu_mhz=%u", (unsigned)g_cpu_mhz);
    plog("    dosmemput_ms_med=%.3f dosmemput_mbs=%.2f dosmemput_verify=%s",
         st_A.med, mbs_A, verify_A);
    plog("    lfb_memcpy_ms_med=%.3f lfb_memcpy_mbs=%.2f lfb_memcpy_verify=%s",
         st_B.med, mbs_B, verify_B);
    plog("    lfb_memset_ms_med=%.3f lfb_memset_mbs=%.2f lfb_memset_verify=%s",
         st_C.med, mbs_C, verify_C);
    plog("    speedup_memcpy_vs_dosmemput=%.3fx", speedup_B_vs_A);
    plog("    plausibility: cpu_mhz_in_band=%d dosmemput_in_band=%d lfb_memcpy_in_band=%d lfb_memset_in_band=%d speedup_in_band=%d",
         cpu_mhz_ok, dosmemput_ok, lfb_memcpy_ok, lfb_memset_ok, speedup_ok);
    plog("    VERDICT=%s reason=\"%s\"]", verdict, reason);
    plog("");
    plog("Cross-anchor: BLTFILL Scenario B (banked dosmemput) = 3.738 ms / 19.6 MB/s.");
    plog("Cross-anchor: BLTFILL Scenario C (sysmem memset ceiling) = 1.676 ms / 43.7 MB/s.");
    plog("If lfb_memcpy approaches 43.7 MB/s, LFB ceiling matches chip-driven-write");
    plog("ceiling and the 19 MB/s dosmemput floor is exposed as banked-mode-CPU-PIO artifact.");
    plog("");
    plog("=== lfbnear-probe done ===");
    plog("[SENTINEL_END]");

    free(src);
    free(samples_A);
    free(samples_B);
    free(samples_C);
    if (g_log) fclose(g_log);
    (void)b_engaged;
    return 0;
}
