/*
 * s3vram.c -- S3 ViRGE/DX VRAM-resident-render de-risk probe (S3-2 task #10b).
 *
 * Standalone DJGPP probe; NO SDL, NO engine, NO C++. Decides whether a
 * VRAM-RESIDENT back-surface (render + composite directly into off-screen VRAM,
 * then page-flip via CRTC) is net-positive on the S3 ViRGE -- it was -4.17 fps
 * on the Cirrus (docs/internal/MODEX-DECISION-AND-FPS-LEVER-PLAN.md sec.: the
 * wave-50 VRAM-resident page-flip raised `present` because CPU read/composite
 * INTO VRAM was too slow on the Cirrus's 1 MB banked path).
 *
 * THE MISSING DATA (this probe fills it): membw.c measured LFB *write* (~60 MB/s
 * S3) + sysmem read/write, but NEVER (a) LFB *READ* (CPU read FROM VRAM -- the
 * slow direction that gates any composite that reads the back-surface) nor (b) a
 * representative per-frame COMPOSITE timed A/B: sysmem-back-surface-then-present
 * vs VRAM-resident-in-place. Cirrus had no 2D engine; the S3 does (s3blt v6 =
 * GREEN-ish), so the CPU-composite path is the de-risk that gates whether the
 * S3-2 VRAM-resident pivot is worth wiring.
 *
 * MEASURES (PIT/uclock-timed, membw idiom -- batch-of-K amortizes 838 ns
 * granularity; pure CPU + LFB, no 2D MMIO):
 *   SYS_READ / SYS_WRITE   sysmem read/write baselines (76800 B = 320x240 INDEX8)
 *   LFB_READ               CPU read FROM the VESA LFB  <-- THE missing metric
 *   LFB_WRITE              raw VRAM write (corroborates membw ~60)
 *   SYS_TO_LFB             sysmem->LFB memcpy = the present copy of the sysmem path
 *   FRAME_SYS              composite a representative frame into a SYSMEM back-
 *                          surface, THEN present-copy it to the LFB (current path)
 *   FRAME_VRAM             composite the SAME frame DIRECTLY into the LFB back-
 *                          page (VRAM-resident path; page-flip is ~free, omitted)
 *
 * The representative frame = a full 20x15 tilemap of 16x16 colorkey tiles
 * (300 tiles, exactly 320x240) over a backdrop fill -- the Cave Story composite
 * shape. Colorkey = INDEX8 0x00 (master black / transparent). Src tiles stay in
 * SYSMEM (engine sprite cache); only the DST (back-surface) location differs
 * between the two scenarios -- which is exactly the VRAM-resident lever.
 *
 * VERDICT (the de-risk gate):
 *   VRAM-resident is net-positive IFF FRAME_VRAM < FRAME_SYS (composing in VRAM
 *   beats composing in sysmem + the present copy). GREEN -> the S3-2 VRAM-
 *   resident pivot has a path; RED -> same trap as the Cirrus -4.17, don't wire.
 *
 * VERIFY-BEFORE-TRUST: a sentinel readback proves LFB writes landed, and a
 * composite-correctness readback (a colorkey pixel KEEPS backdrop, a non-key
 * pixel GETS src) proves the composite actually ran -- a wrong mapping that
 * "times fast" writing nowhere yields *_UNVERIF, never a false MB/s or verdict
 * (the SDLPROBE false-close lesson).
 *
 * Per [[dosbox_not_proxy]]: DOSBox-X's LFB is host RAM -- the MB/s + the verdict
 * are a g2k-with-ViRGE measurement ONLY; the DOSBox-X smoke is correctness-only
 * (runs, exits, writes a parseable log, sentinels pass on the emulated FB).
 * Per [[perf_measurement_discipline]]: predictions here have been 5-30x wrong;
 * the probe MEASURES -- the Cirrus -4.17 anchor is a reference, never asserted.
 *
 * Output: S3VRAM.LOG (CWD, fopen-direct; C:\ fallback), flush + fsync per line,
 * stdout mirror. No env vars; no args. HAZARD: sets a VESA mode + maps the LFB
 * (no 2D MMIO) -- atexit restores text mode 0x03.
 *
 * DOS constraints: -march=i486 -mtune=pentium -O2, no MMX/SSE. Pure DJGPP libc
 * + DPMI. 8.3 DOS filename: S3VRAM.EXE (6.3) -- fits. Log S3VRAM.LOG.
 *
 * License: MIT.
 */

#include <dos.h>
#include <dpmi.h>
#include <go32.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/farptr.h>
#include <sys/nearptr.h>
#include <time.h>
#include <unistd.h>

#define S3VRAM_VERSION "v1 (S3-2 task #10b VRAM-resident de-risk: LFB_READ + composite A/B)"

/* 320x240 INDEX8 = the doskutsu back-surface. */
#define SURF_W     320
#define SURF_H     240
#define SURF_BYTES (SURF_W * SURF_H)      /* 76800 */
#define TILE       16
#define TILES_X    (SURF_W / TILE)        /* 20 */
#define TILES_Y    (SURF_H / TILE)        /* 15 */
#define COLORKEY   0x00                   /* INDEX8 master-black = transparent  */
#define LFB_BACK_OFF 0x00040000u          /* off-screen back-page (256 KB in)   */

/* ============================================================ */
/* Logging -- fopen-direct S3VRAM.LOG + stdout mirror, fsync/line */
/* ============================================================ */

static FILE *g_log = NULL;

static void open_log(void)
{
    g_log = fopen("S3VRAM.LOG", "w");
    if (!g_log) g_log = fopen("C:\\S3VRAM.LOG", "w");
}

static void vlog(const char *fmt, ...)
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
/* Timing -- uclock (PIT, ~838 ns); batch-of-K amortizes it.     */
/* ============================================================ */

static double now_secs(void) { return (double)uclock() / (double)UCLOCKS_PER_SEC; }

static int dbl_cmp(const void *a, const void *b)
{
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

static volatile uint32_t g_read_sink = 0;

/* ============================================================ */
/* Primitive kernels (membw idiom)                               */
/* ============================================================ */

static void op_read_seq(const uint32_t *src, uint32_t n_bytes)
{
    const uint32_t *end = src + (n_bytes >> 2);
    uint32_t sum = 0;
    while (src < end) sum += *src++;
    g_read_sink += sum;   /* accumulate (not =) so the sanity-sink stays non-zero
                           * even when the LAST timed read happens to scan an
                           * all-zero region (e.g. uninitialized VRAM) */
}
static void op_write_seq(uint8_t *dst, uint32_t n_bytes) { memset(dst, 0xA5, n_bytes); }
static void op_memcpy(uint8_t *dst, const uint8_t *src, uint32_t n_bytes)
{
    memcpy(dst, src, n_bytes);
}

/* Read-modify-write 50% blend: dst = (dst + src) >> 1. READS the dst back-
 * surface, blends, writes it -- the ALPHA-composite pattern. When dst is in
 * VRAM this pays the CPU-read-FROM-VRAM cost per pixel, which is the wave-50
 * VRAM-resident killer (VRAM reads have no read-combining, far slower than
 * writes). The colorkey FRAME A/B below only WRITES the back-surface; this RMW
 * is the read-the-back-surface case flush-instr flagged as load-bearing. */
static void op_rmw_blend(uint8_t *dst, const uint8_t *src, uint32_t n_bytes)
{
    for (uint32_t i = 0; i < n_bytes; i++)
        dst[i] = (uint8_t)((dst[i] + src[i]) >> 1);
}

/* One 16x16 colorkey tile blit into a `stride`-wide surface at (x,y).
 * Reads src tile (sysmem), conditionally writes dst (sysmem OR LFB) -- the
 * _blit_indexed inner loop. The dst-write side is what VRAM-residency changes. */
static void blit_tile_ck(uint8_t *dst, int stride, int x, int y, const uint8_t *tile)
{
    for (int row = 0; row < TILE; row++) {
        uint8_t *d = dst + (long)(y + row) * stride + x;
        const uint8_t *s = tile + row * TILE;
        for (int col = 0; col < TILE; col++) {
            uint8_t px = s[col];
            if (px != COLORKEY) d[col] = px;
        }
    }
}

/* Composite a representative Cave Story frame into `surface` (stride SURF_W):
 * backdrop fill + full 20x15 colorkey tilemap. Src tile stays in sysmem. */
static void composite_frame(uint8_t *surface, const uint8_t *tile)
{
    memset(surface, 0x10, SURF_BYTES);                 /* backdrop */
    for (int ty = 0; ty < TILES_Y; ty++)
        for (int tx = 0; tx < TILES_X; tx++)
            blit_tile_ck(surface, SURF_W, tx * TILE, ty * TILE, tile);
}

/* ============================================================ */
/* Timed wrappers -- median of NSAMP batches, return ms/iter      */
/* ============================================================ */

#define NSAMP 9

typedef enum { K_READ, K_WRITE, K_COPY, K_RMW } prim_t;

static void run_prim(prim_t k, uint8_t *dst, const uint8_t *src, uint32_t n)
{
    if (k == K_READ)       op_read_seq((const uint32_t *)src, n);
    else if (k == K_WRITE) op_write_seq(dst, n);
    else if (k == K_COPY)  op_memcpy(dst, src, n);
    else                   op_rmw_blend(dst, src, n);   /* K_RMW */
}

static double timed_prim_ms(prim_t k, uint8_t *dst, const uint8_t *src,
                            uint32_t n, int iters)
{
    double s[NSAMP];
    /* warm-up */
    for (int w = 0; w < (iters > 3 ? 3 : iters); w++) run_prim(k, dst, src, n);
    for (int j = 0; j < NSAMP; j++) {
        double t0 = now_secs();
        for (int i = 0; i < iters; i++) run_prim(k, dst, src, n);
        s[j] = now_secs() - t0;
    }
    qsort(s, NSAMP, sizeof s[0], dbl_cmp);
    return (s[NSAMP / 2] / (double)iters) * 1000.0;     /* ms per iter */
}

/* FRAME_SYS: composite into sysmem back-surface + present-copy to LFB.
 * FRAME_VRAM: composite directly into the LFB back-page. Returns ms/frame. */
static double timed_frame_sys_ms(uint8_t *sys_surf, uint8_t *lfb_back,
                                 const uint8_t *tile, int iters)
{
    double s[NSAMP];
    composite_frame(sys_surf, tile);                    /* warm-up */
    op_memcpy(lfb_back, sys_surf, SURF_BYTES);
    for (int j = 0; j < NSAMP; j++) {
        double t0 = now_secs();
        for (int i = 0; i < iters; i++) {
            composite_frame(sys_surf, tile);            /* compose in sysmem    */
            op_memcpy(lfb_back, sys_surf, SURF_BYTES);  /* present -> VRAM       */
        }
        s[j] = now_secs() - t0;
    }
    qsort(s, NSAMP, sizeof s[0], dbl_cmp);
    return (s[NSAMP / 2] / (double)iters) * 1000.0;
}

static double timed_frame_vram_ms(uint8_t *lfb_back, const uint8_t *tile, int iters)
{
    double s[NSAMP];
    composite_frame(lfb_back, tile);                    /* warm-up (into VRAM)  */
    for (int j = 0; j < NSAMP; j++) {
        double t0 = now_secs();
        for (int i = 0; i < iters; i++)
            composite_frame(lfb_back, tile);            /* compose IN VRAM      */
        s[j] = now_secs() - t0;
    }
    qsort(s, NSAMP, sizeof s[0], dbl_cmp);
    return (s[NSAMP / 2] / (double)iters) * 1000.0;
}

/* ============================================================ */
/* VESA 8bpp LFB mode finder + text restore (membw idiom)        */
/* ============================================================ */

static int g_text_dirty = 0;
static void restore_text_mode(void)
{
    if (!g_text_dirty) return;
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x0003;
    __dpmi_int(0x10, &r);
    g_text_dirty = 0;
}

static uint16_t find_lfb_mode(uint32_t *out_phys, uint16_t *out_xres,
                              uint16_t *out_yres, uint32_t *out_total_vram,
                              int *out_vbe_major)
{
    *out_phys = 0; *out_xres = 0; *out_yres = 0; *out_total_vram = 0; *out_vbe_major = 0;
    int sel = 0;
    int seg = __dpmi_allocate_dos_memory(32, &sel);
    if (seg < 0) return 0;
    uint32_t buflin = (uint32_t)seg << 4;
    _farpokeb(_dos_ds, buflin + 0, 'V'); _farpokeb(_dos_ds, buflin + 1, 'B');
    _farpokeb(_dos_ds, buflin + 2, 'E'); _farpokeb(_dos_ds, buflin + 3, '2');
    for (int i = 4; i < 512; i++) _farpokeb(_dos_ds, buflin + i, 0);

    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F00; r.x.es = (uint16_t)seg; r.x.di = 0;
    if (__dpmi_int(0x10, &r) < 0 || r.x.ax != 0x004F) { __dpmi_free_dos_memory(sel); return 0; }
    uint16_t vbe_ver = _farpeekw(_dos_ds, buflin + 0x04);
    *out_vbe_major   = (vbe_ver >> 8) & 0xFF;
    uint16_t totblk  = _farpeekw(_dos_ds, buflin + 0x12);
    *out_total_vram  = (uint32_t)totblk * 65536u;
    uint16_t mp_off  = _farpeekw(_dos_ds, buflin + 0x0E);
    uint16_t mp_seg  = _farpeekw(_dos_ds, buflin + 0x10);
    uint32_t mode_lin = ((uint32_t)mp_seg << 4) + mp_off;

    uint16_t chosen = 0; uint32_t chosen_phys = 0, chosen_pixels = 0xFFFFFFFFul;
    uint16_t cx = 0, cy = 0;
    for (int mi = 0; mi < 256; mi++) {
        uint16_t mode = _farpeekw(_dos_ds, mode_lin + mi * 2);
        if (mode == 0xFFFF) break;
        for (int i = 256; i < 512; i++) _farpokeb(_dos_ds, buflin + i, 0);
        memset(&r, 0, sizeof r);
        r.x.ax = 0x4F01; r.x.cx = mode; r.x.es = (uint16_t)seg; r.x.di = 256;
        if (__dpmi_int(0x10, &r) < 0 || r.x.ax != 0x004F) continue;
        uint32_t mb = buflin + 256;
        uint16_t attr = _farpeekw(_dos_ds, mb + 0x00);
        uint16_t xres = _farpeekw(_dos_ds, mb + 0x12);
        uint16_t yres = _farpeekw(_dos_ds, mb + 0x14);
        uint8_t  bpp  = _farpeekb(_dos_ds, mb + 0x19);
        uint8_t  model= _farpeekb(_dos_ds, mb + 0x1B);
        uint32_t phys = _farpeekl(_dos_ds, mb + 0x28);
        if (!(attr & 0x0001)) continue;
        if (!(attr & 0x0080)) continue;
        if (bpp != 8) continue;
        if (model != 4) continue;
        if (phys == 0) continue;
        uint32_t pixels = (uint32_t)xres * (uint32_t)yres;
        if (pixels < (uint32_t)SURF_BYTES) continue;
        if (pixels < chosen_pixels) { chosen = mode; chosen_phys = phys; chosen_pixels = pixels; cx = xres; cy = yres; }
    }
    __dpmi_free_dos_memory(sel);
    if (chosen) { *out_phys = chosen_phys; *out_xres = cx; *out_yres = cy; }
    return chosen;
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    open_log();
    atexit(restore_text_mode);

    vlog("=== S3VRAM -- S3 ViRGE VRAM-resident render de-risk (task #10b) ===");
    vlog("S3VRAM-BEGIN");
    vlog("version: %s", S3VRAM_VERSION);
    vlog("build: DJGPP -march=i486 -mtune=pentium -O2; uclocks_per_sec=%lu",
         (unsigned long)UCLOCKS_PER_SEC);
    vlog("plan: docs/internal/MODEX-DECISION-AND-FPS-LEVER-PLAN.md (Cirrus VRAM-resident=-4.17 fps)");

    /* sysmem buffers: a back-surface + a 16x16 src tile (sprite-cache stand-in). */
    uint8_t *sys_surf = (uint8_t *)malloc(SURF_BYTES);
    uint8_t *sys_src  = (uint8_t *)malloc(SURF_BYTES);
    uint8_t *tile     = (uint8_t *)malloc(TILE * TILE);
    if (!sys_surf || !sys_src || !tile) {
        vlog("FATAL: malloc failed");
        vlog("[s3vram SUITE_DONE verdict=ABORT_NOMEM]");
        vlog("S3VRAM-DONE");
        if (g_log) fclose(g_log);
        return 2;
    }
    /* representative tile: ~25% colorkey (transparent), rest a gradient. */
    for (int i = 0; i < TILE * TILE; i++)
        tile[i] = ((i % 4) == 0) ? COLORKEY : (uint8_t)(0x20 + (i & 0x3F));
    for (uint32_t i = 0; i < (uint32_t)SURF_BYTES; i++) { sys_src[i] = (uint8_t)(i & 0xFF); sys_surf[i] = 0; }

    /* ---- find + set an 8bpp LFB mode, map LFB ---- */
    uint32_t phys = 0, total_vram = 0; uint16_t xres = 0, yres = 0; int vbemaj = 0;
    uint16_t mode = find_lfb_mode(&phys, &xres, &yres, &total_vram, &vbemaj);
    if (mode == 0) {
        vlog("LFB_UNAVAILABLE: no 8bpp linear-framebuffer VBE mode (vbe_major=%d)", vbemaj);
        vlog("[s3vram SUITE_DONE verdict=ABORT_NO_LFB]");
        vlog("S3VRAM-DONE");
        free(sys_surf); free(sys_src); free(tile);
        if (g_log) fclose(g_log);
        return 2;
    }
    vlog("LFB mode=0x%04X %ux%u 8bpp phys=0x%08lX total_vram=%lu vbe_major=%d",
         mode, xres, yres, (unsigned long)phys, (unsigned long)total_vram, vbemaj);

    /* need room for the off-screen back-page. */
    uint32_t need = LFB_BACK_OFF + SURF_BYTES;
    uint32_t map_size = (total_vram >= need) ? need : SURF_BYTES;
    uint32_t back_off = (total_vram >= need) ? LFB_BACK_OFF : 0;

    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F02; r.x.bx = mode | 0x4000;            /* LFB bit 14 */
    __dpmi_int(0x10, &r);
    g_text_dirty = 1;
    int set_ok = (r.x.ax == 0x004F);

    __dpmi_meminfo info;
    memset(&info, 0, sizeof info);
    info.address = phys; info.size = map_size;
    if (__dpmi_physical_address_mapping(&info) != 0 || !__djgpp_nearptr_enable()) {
        restore_text_mode();
        vlog("LFB_UNAVAILABLE: DPMI map / nearptr failed (phys=0x%08lX size=%lu)",
             (unsigned long)phys, (unsigned long)map_size);
        vlog("[s3vram SUITE_DONE verdict=ABORT_MAP_FAILED]");
        vlog("S3VRAM-DONE");
        free(sys_surf); free(sys_src); free(tile);
        if (g_log) fclose(g_log);
        return 2;
    }
    uint8_t *lfb = (uint8_t *)(info.address + __djgpp_conventional_base);
    uint8_t *lfb_back = lfb + back_off;

    /* Pre-fill the off-screen back-page so LFB_READ scans real (non-zero) data
     * rather than uninitialized VRAM -- makes the read-bandwidth number + the
     * read_sink sanity check meaningful (bandwidth itself is content-agnostic). */
    op_memcpy(lfb_back, sys_src, SURF_BYTES);

    /* ===== timed in graphics mode; vlog still writes the LOG file (stdout may
     * corrupt the screen, harmless). Collect, restore, then the verdict prose. */

    double sys_rd = timed_prim_ms(K_READ,  NULL,     sys_src, SURF_BYTES, 200);
    double sys_wr = timed_prim_ms(K_WRITE, sys_surf, NULL,    SURF_BYTES, 200);
    double lfb_rd = timed_prim_ms(K_READ,  NULL,     lfb_back, SURF_BYTES, 60);
    double lfb_wr = timed_prim_ms(K_WRITE, lfb_back, NULL,     SURF_BYTES, 60);
    double s2l    = timed_prim_ms(K_COPY,  lfb_back, sys_src,  SURF_BYTES, 60);

    /* RMW (read-modify-write) blend A/B -- the alpha-composite case that READS
     * the back-surface (refinement 2 / flush-instr): sysmem dst vs VRAM dst. */
    double rmw_sys = timed_prim_ms(K_RMW, sys_surf, sys_src, SURF_BYTES, 100);
    double rmw_lfb = timed_prim_ms(K_RMW, lfb_back, sys_src, SURF_BYTES, 40);

    double frame_sys  = timed_frame_sys_ms(sys_surf, lfb_back, tile, 30);
    double frame_vram = timed_frame_vram_ms(lfb_back, tile, 30);

    /* ---- verify-before-trust ---- */
    /* (a) sentinel: LFB writes physically landed. */
    lfb_back[0] = 0xA5; lfb_back[1] = 0x5A;
    volatile uint8_t s0 = lfb_back[0], s1 = lfb_back[1];
    int sentinel_ok = (s0 == 0xA5 && s1 == 0x5A);
    /* (b) composite correctness in VRAM: composite once, check a known colorkey
     * pixel KEPT backdrop (0x10) + a known non-key pixel GOT src. tile[0]=COLORKEY
     * (top-left of every tile keeps backdrop); tile[1]=non-key (gets copied). */
    composite_frame(lfb_back, tile);
    volatile uint8_t ck_px = lfb_back[0];          /* tile(0,0) px(0,0): src=CK -> backdrop 0x10 */
    volatile uint8_t cp_px = lfb_back[1];          /* tile(0,0) px(1,0): src!=CK -> tile[1]      */
    int composite_ok = (ck_px == 0x10 && cp_px == tile[1]);

    __djgpp_nearptr_disable();
    restore_text_mode();
    __dpmi_free_physical_address_mapping(&info);

    /* ===== back in text mode -- emit ===== */
    if (!set_ok) vlog("WARN: AX=4F02 set-mode != 0x004F -- numbers suspect");

    double sys_rd_mb = sys_rd > 0 ? (SURF_BYTES / sys_rd) / 1048.576 : -1;
    double sys_wr_mb = sys_wr > 0 ? (SURF_BYTES / sys_wr) / 1048.576 : -1;
    double lfb_rd_mb = lfb_rd > 0 ? (SURF_BYTES / lfb_rd) / 1048.576 : -1;
    double lfb_wr_mb = lfb_wr > 0 ? (SURF_BYTES / lfb_wr) / 1048.576 : -1;
    double s2l_mb    = s2l    > 0 ? (SURF_BYTES / s2l)    / 1048.576 : -1;

    vlog("[s3vram] PRIM SYS_READ  ms=%.4f mbps=%.2f", sys_rd, sys_rd_mb);
    vlog("[s3vram] PRIM SYS_WRITE ms=%.4f mbps=%.2f", sys_wr, sys_wr_mb);
    vlog("[s3vram] PRIM LFB_READ  ms=%.4f mbps=%.2f  <-- the missing metric (CPU read FROM VRAM)", lfb_rd, lfb_rd_mb);
    vlog("[s3vram] PRIM LFB_WRITE ms=%.4f mbps=%.2f", lfb_wr, lfb_wr_mb);
    vlog("[s3vram] PRIM SYS_TO_LFB ms=%.4f mbps=%.2f  (= the present copy of the sysmem path)", s2l, s2l_mb);
    if (lfb_rd_mb > 0 && sys_rd_mb > 0)
        vlog("[s3vram] LFB_READ_PENALTY=%.2fx slower than sysmem read (VRAM-read cost gates any composite that reads the back-surface, e.g. alpha)",
             sys_rd_mb / lfb_rd_mb);

    double rmw_sys_mb = rmw_sys > 0 ? (SURF_BYTES / rmw_sys) / 1048.576 : -1;
    double rmw_lfb_mb = rmw_lfb > 0 ? (SURF_BYTES / rmw_lfb) / 1048.576 : -1;
    vlog("[s3vram] PRIM RMW_SYS  ms=%.4f mbps=%.2f  (read-modify-write blend, dst in sysmem)", rmw_sys, rmw_sys_mb);
    vlog("[s3vram] PRIM RMW_LFB  ms=%.4f mbps=%.2f  (RMW blend, dst in VRAM = ALPHA-composite VRAM-resident)", rmw_lfb, rmw_lfb_mb);
    if (rmw_sys_mb > 0 && rmw_lfb_mb > 0)
        vlog("[s3vram] RMW_LFB_PENALTY=%.2fx slower than sysmem RMW (the alpha-composite VRAM-resident penalty = the wave-50 killer; >>1 => alpha composites are NOT viable VRAM-resident even if colorkey blits are)",
             rmw_sys_mb / rmw_lfb_mb);

    vlog("[s3vram] FRAME_SYS  ms=%.4f  (composite in sysmem + present-copy to VRAM = current path)", frame_sys);
    vlog("[s3vram] FRAME_VRAM ms=%.4f  (composite DIRECTLY into VRAM back-page = VRAM-resident)", frame_vram);

    vlog("[s3vram] SELFTEST sentinel=%s composite=%s",
         sentinel_ok ? "OK" : "FAIL", composite_ok ? "OK" : "FAIL");

    /* ---- GATE ---- */
    const char *verdict;
    if (!sentinel_ok || !composite_ok) {
        verdict = "UNVERIF_LFB";        /* mapping/composite unverified -> no trustable number */
    } else if (frame_sys <= 0.0 || frame_vram <= 0.0) {
        verdict = "UNVERIF_TIMER";
    } else if (frame_vram < frame_sys) {
        verdict = "GREEN_VRAM_RESIDENT_WINS";
    } else {
        verdict = "RED_VRAM_RESIDENT_LOSES";
    }
    double delta = (frame_sys > 0 && frame_vram > 0) ? (frame_sys - frame_vram) : 0.0;
    vlog("[s3vram GATE frame_sys_ms=%.4f frame_vram_ms=%.4f delta_ms=%.4f verdict=%s]",
         frame_sys, frame_vram, delta, verdict);
    vlog("  GREEN (frame_vram<frame_sys) -> composing in VRAM beats sysmem+present;");
    vlog("    the S3-2 VRAM-resident pivot has a path (unlike Cirrus -4.17 fps).");
    vlog("  RED -> same trap as the Cirrus: VRAM read/write composite too slow;");
    vlog("    keep the sysmem back-surface + dumb present. flush-instr applies the");
    vlog("    final gate cross-referenced with the 2D-engine (s3blt v6) numbers.");
    vlog("  KEY: the FRAME A/B gates COLORKEY-blit VRAM-residency (write-only to the");
    vlog("    back-surface); RMW_LFB_PENALTY + LFB_READ gate ALPHA-composite VRAM-");
    vlog("    residency (which READS the back-surface) -- the wave-50 read-bound case.");

    vlog("[s3vram SUITE_DONE]");
    vlog("S3VRAM-DONE");
    vlog("read_sink=0x%08lX (non-zero confirms the read loops ran)", (unsigned long)g_read_sink);

    free(sys_surf); free(sys_src); free(tile);
    if (g_log) fclose(g_log);
    return 0;
}
