/*
 * opaque.c — opaque-tile bitmask audit
 * (Phase 11 wave-25 / iter J ORIGINAL; wave-36 task #10 EXTENDED to Mimiga).
 *
 * Question: in a Cave Story tileset, what fraction of 16x16 tiles contain
 * ZERO colorkey (master-black, palette index 0) pixels?
 *
 * "FPS-DEEPDIVE Candidate #2" / wave-37 rank-1 lever proposes a sister
 * cache to the FG tilemap cache that holds OPAQUE tiles only — those can
 * be blitted as straight memcpy (no colorkey-skip per-pixel test) and
 * contribute to a fast-path memcpy stripe in the back-surface flush.
 * Worth the ~2-3 day implementation iff opaque tiles are a meaningful
 * fraction across the production-route tilesheets; if every tile has at
 * least one colorkey pixel, the optimization is worthless.
 *
 * Decision criteria (per team-lead brief, wave-36 task #10):
 *   per-tilesheet pct_opaque < 15%   -> DROP candidate (no value)
 *   per-tilesheet pct_opaque 15-30%  -> DEFER (marginal)
 *   per-tilesheet pct_opaque >= 30%  -> SHIP (worth the dev cost)
 *
 * Combined verdict logic (multi-tilesheet): the engine patch is a generic
 * Surface attribute so it benefits ANY tilesheet that clears the bar. We
 * emit per-file verdicts + a "max pct_opaque across all audited tilesheets"
 * combined headline. team-lead picks the lever based on the wave-37 active
 * gameplay route (First Cave for wave-37 KPI scene; Mimiga Village as the
 * other operator-visited scene).
 *
 * Per perf_predictions_unreliable.md: a Cave Story–era tileset visual
 * inspection ("most tiles look opaque") is a hypothesis. The probe MEASURES
 * by counting actual nibble values per tile in the tileset's pixel data.
 *
 * Per dosbox_not_perf_proxy.md: this probe is FILE-PARSE only — no IO, no
 * timing, no perf-sensitive code. DOSBox-X output should match real HW
 * exactly (deterministic file parse). Real-HW iter is the honesty gate
 * (file system content matches what's loaded by the engine).
 *
 * Probe scope (per team-lead brief task #10):
 *   1. Open data/Stage/PrtCave.pbm + data/Stage/PrtMimi.pbm.
 *   2. Parse each as 4bpp BMP header + palette + pixel data.
 *   3. For each 16x16 tile, count pixels where palette index == 0 (colorkey).
 *   4. Aggregate per-file: opaque_count / transparent_count / pct_opaque.
 *   5. Emit per-file [BEGIN] + [DONE] markers + per-tile bitmask.
 *   6. Emit combined [COMBINED] anchor line + [SENTINEL_END] sentinel
 *      (per probe_smoke_recipes_check_derived_metrics.md hygiene).
 *
 * 8.3 DOS filename:
 *   Source:   tests/probes/opaque.c (6 chars host-side, fits 8.3 directly)
 *   Binary:   OPAQUE.EXE (6+3, fits)
 *   Log:      OPAQUE.LOG (6+3, fits)
 *   BAT:      OPAQUE.BAT (6+3, fits)
 *
 * License: MIT.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ============================================================ */
/* Logging — fopen-direct + stdout mirror. Per-line fsync since */
/* this probe never times anything; per-line fsync cost is fine. */
/* ============================================================ */

static FILE *g_log = NULL;

static void open_log(void)
{
    g_log = fopen("OPAQUE.LOG", "w");
    if (!g_log) g_log = fopen("C:\\OPAQUE.LOG", "w");
}

static void plog(const char *fmt, ...)
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
/* BMP loader — minimal Windows 3.x 4bpp BITMAPINFOHEADER parser. */
/* Cave Story tilesets are 4bpp packed (2 px/byte high nibble    */
/* first); rows stored bottom-up; rows 4-byte aligned.           */
/* ============================================================ */

#pragma pack(push, 1)
typedef struct {
    uint16_t bfType;        /* 'BM' = 0x4D42 */
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;     /* offset to pixel data */
} bmp_file_hdr_t;

typedef struct {
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} bmp_info_hdr_t;
#pragma pack(pop)

/* Read raw BMP file into mem. Returns malloc'd buffer + size; NULL on error. */
static uint8_t *read_file(const char *path, long *out_size)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) < 0) { fclose(fp); return NULL; }
    long sz = ftell(fp);
    if (sz < 0 || sz > 4 * 1024 * 1024) { fclose(fp); return NULL; }
    if (fseek(fp, 0, SEEK_SET) < 0) { fclose(fp); return NULL; }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(fp); return NULL; }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf); fclose(fp); return NULL;
    }
    fclose(fp);
    *out_size = sz;
    return buf;
}

/* ============================================================ */
/* Tile geometry                                                 */
/* ============================================================ */

#define TILE_W       16
#define TILE_H       16
#define MAX_TILES    256
#define COLORKEY_IDX 0       /* Cave Story master black (engine convention) */

/* ============================================================ */
/* Process one tileset BMP — count colorkey px per tile,        */
/* aggregate opaque/transparent counts.                          */
/* ============================================================ */

typedef struct {
    int total_tiles;
    int opaque_count;
    int transparent_count;
    int colorkey_px_per_tile[MAX_TILES];
    uint32_t opaque_bitmask[MAX_TILES / 32]; /* bit i = tile i is opaque */
} audit_t;

static int audit_tileset(const char *path, audit_t *out)
{
    plog("Loading %s ...", path);
    long fsize = 0;
    uint8_t *raw = read_file(path, &fsize);
    if (!raw) {
        plog("ERROR: cannot open %s (file missing or read failed)", path);
        return -1;
    }
    plog("File size: %ld bytes", fsize);

    if (fsize < (long)(sizeof(bmp_file_hdr_t) + sizeof(bmp_info_hdr_t))) {
        plog("ERROR: file too small for BMP headers");
        free(raw);
        return -1;
    }

    bmp_file_hdr_t fh;
    bmp_info_hdr_t ih;
    memcpy(&fh, raw, sizeof fh);
    memcpy(&ih, raw + sizeof fh, sizeof ih);

    if (fh.bfType != 0x4D42) {
        plog("ERROR: bfType=0x%04X not 'BM' — not a BMP file", fh.bfType);
        free(raw); return -1;
    }
    plog("BMP file header: bfSize=%lu bfOffBits=%lu",
         (unsigned long)fh.bfSize, (unsigned long)fh.bfOffBits);
    plog("BMP info header: biSize=%lu w=%ld h=%ld bits=%u compression=%lu",
         (unsigned long)ih.biSize, (long)ih.biWidth, (long)ih.biHeight,
         (unsigned)ih.biBitCount, (unsigned long)ih.biCompression);

    if (ih.biBitCount != 4) {
        plog("ERROR: only 4bpp BMPs supported (this is %u bpp)", ih.biBitCount);
        free(raw); return -1;
    }
    if (ih.biCompression != 0) {
        plog("ERROR: only uncompressed BMPs supported (compression=%lu)",
             (unsigned long)ih.biCompression);
        free(raw); return -1;
    }

    int img_w = (int)ih.biWidth;
    int img_h_raw = (int)ih.biHeight;
    int top_down = 0;
    int img_h = img_h_raw;
    if (img_h_raw < 0) { top_down = 1; img_h = -img_h_raw; }
    plog("Image: %dx%d (%s)", img_w, img_h, top_down ? "top-down" : "bottom-up");

    if (img_w <= 0 || img_h <= 0 || img_w > 2048 || img_h > 2048) {
        plog("ERROR: pathological dimensions"); free(raw); return -1;
    }
    if (img_w % TILE_W != 0 || img_h % TILE_H != 0) {
        plog("ERROR: image dims not multiple of %dx%d tile", TILE_W, TILE_H);
        free(raw); return -1;
    }

    int tiles_x = img_w / TILE_W;
    int tiles_y = img_h / TILE_H;
    int total_tiles = tiles_x * tiles_y;
    plog("Tile grid: %d cols x %d rows = %d tiles", tiles_x, tiles_y, total_tiles);
    if (total_tiles > MAX_TILES) {
        plog("WARN: %d > MAX_TILES=%d; capping audit at %d",
             total_tiles, MAX_TILES, MAX_TILES);
        total_tiles = MAX_TILES;
    }

    /* Row stride for 4bpp: ceil(w / 2) bytes, aligned to 4. */
    int row_bytes_unaligned = (img_w + 1) / 2;
    int row_stride = (row_bytes_unaligned + 3) & ~3;
    plog("Pixel row: %d bytes packed, %d bytes aligned", row_bytes_unaligned, row_stride);

    long pix_off = (long)fh.bfOffBits;
    long pix_end = pix_off + (long)row_stride * (long)img_h;
    if (pix_off < 0 || pix_end > fsize) {
        plog("ERROR: pixel data range [%ld..%ld] outside file size %ld",
             pix_off, pix_end, fsize);
        free(raw); return -1;
    }
    const uint8_t *pix = raw + pix_off;

    memset(out, 0, sizeof *out);
    out->total_tiles = total_tiles;

    /* For each tile, walk its 16x16 = 256 pixels and count == 0. */
    for (int ty = 0; ty < tiles_y; ty++) {
        for (int tx = 0; tx < tiles_x; tx++) {
            int idx = ty * tiles_x + tx;
            if (idx >= MAX_TILES) break;
            int ck_px = 0;
            for (int py = 0; py < TILE_H; py++) {
                int img_row;
                if (top_down) img_row = ty * TILE_H + py;
                else          img_row = img_h - 1 - (ty * TILE_H + py);
                const uint8_t *row = pix + (long)img_row * row_stride;
                for (int px = 0; px < TILE_W; px++) {
                    int img_col = tx * TILE_W + px;
                    int byte_idx = img_col >> 1;
                    int hi = ((img_col & 1) == 0);
                    uint8_t b = row[byte_idx];
                    int nib = hi ? (b >> 4) : (b & 0x0F);
                    if (nib == COLORKEY_IDX) ck_px++;
                }
            }
            out->colorkey_px_per_tile[idx] = ck_px;
            if (ck_px == 0) {
                out->opaque_count++;
                out->opaque_bitmask[idx >> 5] |= (1U << (idx & 31));
            } else {
                out->transparent_count++;
            }
        }
    }

    free(raw);
    return 0;
}

/* ============================================================ */
/* Pretty-print per-tile colorkey-pixel histogram + bitmask     */
/* ============================================================ */

static void emit_results(const audit_t *a)
{
    plog("");
    plog("---- Per-tile colorkey pixel counts (max 256/tile) ----");
    plog("Tile# : ck_px (out of 256)  opaque?");
    for (int i = 0; i < a->total_tiles; i++) {
        int ck = a->colorkey_px_per_tile[i];
        plog("  %3d : %3d  %s",
             i, ck, (ck == 0 ? "OPAQUE" : ""));
    }

    plog("");
    plog("---- Opaque-tile bitmask (1=opaque, 0=has-colorkey) ----");
    plog("Tile bitmask laid out per row of 32 tiles (8 hex digits per row):");
    int rows = (a->total_tiles + 31) / 32;
    for (int r = 0; r < rows; r++) {
        int base = r * 32;
        int top = base + 32; if (top > MAX_TILES) top = MAX_TILES;
        plog("  tiles %3d-%3d: 0x%08lX  (range %3d..%3d)",
             base, top - 1,
             (unsigned long)a->opaque_bitmask[r], base, top - 1);
    }
}

/* ============================================================ */
/* Decision verdict per team-lead brief — emits per-file ANCHOR  */
/* line + verdict string; returns pct_opaque for combined emit.  */
/* ============================================================ */

static const char *verdict_for_pct(double pct)
{
    if (pct < 15.0)       return "DROP";
    else if (pct < 30.0)  return "DEFER";
    else                  return "SHIP";
}

static double emit_verdict(const char *label, const audit_t *a)
{
    double pct = a->total_tiles > 0
                   ? (100.0 * (double)a->opaque_count / (double)a->total_tiles)
                   : 0.0;
    const char *v = verdict_for_pct(pct);

    plog("");
    plog("---- Aggregate audit (%s) ----", label);
    plog("Total tiles audited:    %d", a->total_tiles);
    plog("Opaque tiles:           %d", a->opaque_count);
    plog("Has-colorkey tiles:     %d", a->transparent_count);
    plog("Pct opaque:             %.1f%%", pct);
    plog("");
    plog("Decision criteria (per team-lead brief, wave-36 task #10):");
    plog("  pct_opaque <  15%%    -> DROP (no lever value)");
    plog("  pct_opaque 15-30%%    -> DEFER (marginal)");
    plog("  pct_opaque >= 30%%    -> SHIP wave-37 rank-1 lever");
    plog("");
    plog("=== HEADLINE [%s]: %.1f%% opaque -> RECOMMEND %s ===", label, pct, v);
    /* ANCHOR line for grep: per-file verdict in machine-readable form. */
    plog("[opaque-audit FILE=%s DONE pct_opaque=%.2f opaque_count=%d total_tiles=%d verdict=%s]",
         label, pct, a->opaque_count, a->total_tiles, v);
    return pct;
}

/* ============================================================ */
/* Per-tilesheet path-list + audit driver                        */
/* ============================================================ */

typedef struct {
    const char        *label;     /* short tag for emit lines (e.g. "PrtCave") */
    const char *const *paths;     /* NULL-terminated list of candidate paths  */
} tilesheet_spec_t;

/* PrtCave (First Cave foreground + Mimiga Village interior maps use this). */
static const char *const paths_cave[] = {
    "data\\Stage\\PrtCave.pbm",
    "Stage\\PrtCave.pbm",
    "data/Stage/PrtCave.pbm",
    "Stage/PrtCave.pbm",
    "C:\\DOSKUTSU\\data\\Stage\\PrtCave.pbm",
    "PrtCave.pbm",            /* cwd fallback */
    "C:\\PrtCave.pbm",        /* CF-root fallback */
    NULL
};

/* PrtMimi (Mimiga Village foreground; outdoor village + dialogue scenes). */
static const char *const paths_mimi[] = {
    "data\\Stage\\PrtMimi.pbm",
    "Stage\\PrtMimi.pbm",
    "data/Stage/PrtMimi.pbm",
    "Stage/PrtMimi.pbm",
    "C:\\DOSKUTSU\\data\\Stage\\PrtMimi.pbm",
    "PrtMimi.pbm",
    "C:\\PrtMimi.pbm",
    NULL
};

/* Audit one tilesheet from a path-list; return 0 on success + leaves
 * result in *out, or -1 on failure (no result emitted). */
static int audit_one_tilesheet(const tilesheet_spec_t *spec, audit_t *out)
{
    plog("");
    plog("[opaque-audit FILE=%s BEGIN candidate_paths=%s,...]",
         spec->label, spec->paths[0]);
    int rc = -1;
    for (int i = 0; spec->paths[i]; i++) {
        rc = audit_tileset(spec->paths[i], out);
        if (rc == 0) {
            plog("Loaded from path: %s", spec->paths[i]);
            return 0;
        }
    }
    plog("FATAL[%s]: could not locate tilesheet from any of %d candidate paths.",
         spec->label, 7);
    plog("[opaque-audit FILE=%s DONE pct_opaque=0.00 verdict=SKIP_FILE_MISSING]",
         spec->label);
    return -1;
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    plog("=== OPAQUE wave-25 / wave-36 task #10 starting ===");
    plog("DJGPP build, target = opaque-tile bitmask audit (wave-37 rank-1 gate)");
    plog("");
    plog("Question: what fraction of Cave Story tileset tiles are 100%% opaque");
    plog("(zero colorkey pixels)? Answer drives wave-37 rank-1 lever ship/no-ship.");
    plog("This iter audits TWO production-route tilesheets: First Cave + Mimiga.");
    plog("");

    /* Drive both tilesheets. Per team-lead brief task #10: First Cave +
     * Mimiga foreground. Both must succeed for a complete combined verdict;
     * partial results still emitted if one file is missing. */
    static const tilesheet_spec_t sheets[] = {
        { "PrtCave", paths_cave },
        { "PrtMimi", paths_mimi },
    };
    const int n_sheets = (int)(sizeof(sheets) / sizeof(sheets[0]));

    audit_t  audits[2];
    double   pct_each[2] = { -1.0, -1.0 };
    int      audited_count = 0;

    for (int i = 0; i < n_sheets; i++) {
        if (audit_one_tilesheet(&sheets[i], &audits[i]) == 0) {
            emit_results(&audits[i]);
            pct_each[i] = emit_verdict(sheets[i].label, &audits[i]);
            audited_count++;
        }
    }

    /* ============================================================ */
    /* Combined verdict                                              */
    /* ============================================================ */
    plog("");
    plog("---- Combined verdict across audited tilesheets ----");
    if (audited_count == 0) {
        plog("ERROR: no tilesheets audited; verdict=FATAL_NO_INPUT");
        plog("[opaque-audit COMBINED audited_count=0 verdict=FATAL_NO_INPUT]");
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 2;
    }

    double pct_max = -1.0;
    double pct_sum = 0.0;
    int    pct_n   = 0;
    const char *max_label = "(none)";
    for (int i = 0; i < n_sheets; i++) {
        if (pct_each[i] >= 0.0) {
            if (pct_each[i] > pct_max) {
                pct_max = pct_each[i];
                max_label = sheets[i].label;
            }
            pct_sum += pct_each[i];
            pct_n++;
        }
    }
    double pct_avg = (pct_n > 0) ? (pct_sum / pct_n) : 0.0;

    const char *combined_verdict = verdict_for_pct(pct_max);

    plog("Audited count: %d / %d", audited_count, n_sheets);
    for (int i = 0; i < n_sheets; i++) {
        if (pct_each[i] >= 0.0) {
            plog("  %-8s pct_opaque=%.2f%% verdict=%s",
                 sheets[i].label, pct_each[i], verdict_for_pct(pct_each[i]));
        } else {
            plog("  %-8s pct_opaque=N/A verdict=SKIP_FILE_MISSING",
                 sheets[i].label);
        }
    }
    plog("Max pct_opaque: %.2f%% (%s)", pct_max, max_label);
    plog("Avg pct_opaque: %.2f%% (across %d audited)", pct_avg, pct_n);
    plog("");
    plog("Combined verdict logic: lever is a generic Surface attribute that");
    plog("benefits ANY tilesheet clearing the bar. We headline the MAX pct_opaque;");
    plog("team-lead picks based on wave-37 active gameplay route.");
    plog("");
    plog("=== HEADLINE [COMBINED]: max=%.2f%% (%s) -> RECOMMEND %s ===",
         pct_max, max_label, combined_verdict);

    /* ============================================================ */
    /* ANCHOR + SENTINEL emit (grep-targets)                         */
    /* ============================================================ */
    plog("");
    plog("[opaque-audit COMBINED audited_count=%d", audited_count);
    plog("    PrtCave_pct_opaque=%.2f PrtMimi_pct_opaque=%.2f",
         pct_each[0], pct_each[1]);
    plog("    pct_max=%.2f pct_max_label=%s pct_avg=%.2f",
         pct_max, max_label, pct_avg);
    plog("    verdict=%s reason=\"max_pct_opaque vs team-lead rule (>=30 SHIP / 15-30 DEFER / <15 DROP)\"]",
         combined_verdict);
    plog("");
    plog("=== OPAQUE done ===");
    plog("[SENTINEL_END]");
    if (g_log) fclose(g_log);
    return 0;
}
