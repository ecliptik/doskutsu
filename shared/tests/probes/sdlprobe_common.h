/*
 * sdlprobe_common.h -- shared forensic-protocol scaffolding for SDL3-DOS
 * cost-decomposition probes (Phase 11 iter L, task #24 deliverable).
 *
 * Both `sdlprob1.c` (sdl-engine task #23: per-primitive section 1 + auxiliary section 3)
 * and `sdlprob2.c` (probe-engineer task #24: composites section 2) #include this
 * header. All functions are `static` (or `static inline`) so each translation
 * unit gets its own copy -- no shared .o file, no link-time conflicts, no
 * cross-binary state sharing.
 *
 * Per docs/PHASE11-SDLPROBE-CONTRACT.md section 4 (forensic protocol):
 *   - BEGIN_<name> / DONE_<name> markers, fsync per LOG line
 *   - 60 sec per-scenario watchdog cap; cap-trip emits ABORT_PARTIAL with
 *     median-of-captured + skips next scenario (does NOT bail probe)
 *   - 8 min whole-probe hard cap
 *   - BIOS-kbd-pending escape (every 64 samples + between scenarios)
 *   - Per-scenario raw uint32_t cycles[N=1000] stash
 *   - DONE marker emits min/med/p95/max/mean + cpu_mhz_calibrated +
 *     bimodality flag + min-cluster + max-cluster (both of-5 from sorted samples)
 *   - RDTSC via SDL/0039 timer mode dispatch (RDTSC vs PIT vs BIOS-tick),
 *     mirrored from vendor/SDL/src/core/dos/SDL_dos_audio_synth.h:287-308
 *   - Mem-delta tracking via DJGPP _go32_dpmi_get_free_memory_information
 *
 * Usage in each .c file:
 *   #define SDLPROBE_LOG_PATH "SDLPROB2.LOG"   // or "SDLPROB1.LOG"
 *   #define SDLPROBE_BANNER   "=== SDLPROB2 ..."
 *   #include "sdlprobe_common.h"
 *
 *   int main() {
 *       sdlprobe_open_log();
 *       sdlprobe_plog(SDLPROBE_BANNER);
 *       sdlprobe_init_timer_and_state();
 *       // ... call sdlprobe_run_body_scenario(...) per scenario
 *       sdlprobe_emit_summary();
 *       sdlprobe_teardown_state();
 *       return 0;
 *   }
 *
 * Per dosbox_not_perf_proxy.md: DOSBox-X smoke = correctness only (probe
 * runs end-to-end, BEGIN/DONE markers fire, watchdog clean exit, LOG
 * written, structural sanity). Real-HW iter L is the data gate.
 *
 * License: MIT.
 */

#ifndef SDLPROBE_COMMON_H
#define SDLPROBE_COMMON_H

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_blendmode.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>

/* SDL/0039 timer externs -- all globally exported from libSDL3.a per
 * `nm` verify. We can't include vendor/SDL/src/core/dos/SDL_dos_audio_synth.h
 * directly (private path), so externs declared here. */
extern void SDL_DOSAudioInitTimer(void);
extern volatile uint32_t g_dos_audio_timer_mode;
extern volatile uint32_t g_dos_audio_timer_tsc_mhz;
extern volatile uint32_t dos_port_audio_irq_count;
extern volatile uint32_t dos_port_audio_irq_wall_us;
extern volatile uint32_t dos_port_audio_sfx_active_count;

#include <dpmi.h>     /* _go32_dpmi_meminfo + _go32_dpmi_get_free_memory_information */
#include <go32.h>
#include <pc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/farptr.h>
#include <time.h>
#include <unistd.h>

/* ============================================================ */
/* Tunable constants (per section 4 forensic protocol)                 */
/* ============================================================ */

#define SDLPROBE_MAX_SAMPLES     1024
#define SDLPROBE_DEFAULT_N       1000
#define SDLPROBE_WARMUP_DEFAULT  50
#define SDLPROBE_WARMUP_CHEAP    5
#define SDLPROBE_WATCHDOG_SECS   60.0
#define SDLPROBE_WHOLE_CAP_SECS  480.0  /* 8 min hard cap */

/* SDL/0039 timer mode constants (mirror vendor/SDL/.../SDL_dos_audio_synth.h). */
#define SDLPROBE_TIMER_RDTSC 1u
#define SDLPROBE_TIMER_PIT   2u

/* Each .c MUST define these before including this header. */
#ifndef SDLPROBE_LOG_PATH
#  error "Define SDLPROBE_LOG_PATH (e.g. \"SDLPROB2.LOG\") before including sdlprobe_common.h"
#endif
#ifndef SDLPROBE_BANNER
#  error "Define SDLPROBE_BANNER before including sdlprobe_common.h"
#endif

/* ============================================================ */
/* Logging -- fsync per line, mirrors AUDBUF/IDLEPROB/MPUSDL pattern */
/* ============================================================ */

static FILE *sdlprobe_g_log = NULL;

static void sdlprobe_open_log(void)
{
    sdlprobe_g_log = fopen(SDLPROBE_LOG_PATH, "w");
    if (!sdlprobe_g_log) {
        char alt[80];
        snprintf(alt, sizeof alt, "C:\\%s", SDLPROBE_LOG_PATH);
        sdlprobe_g_log = fopen(alt, "w");
    }
}

static void sdlprobe_plog(const char *fmt, ...)
{
    char buf[640];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    fputs(buf, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    if (sdlprobe_g_log) {
        fputs(buf, sdlprobe_g_log);
        fputc('\n', sdlprobe_g_log);
        fflush(sdlprobe_g_log);
        fsync(fileno(sdlprobe_g_log));
    }
}

static void sdlprobe_close_log(void)
{
    if (sdlprobe_g_log) { fclose(sdlprobe_g_log); sdlprobe_g_log = NULL; }
}

/* ============================================================ */
/* Timing wall-clock + watchdog escape                           */
/* ============================================================ */

static inline double sdlprobe_now_secs(void)
{
    return (double)uclock() / (double)UCLOCKS_PER_SEC;
}

static inline int sdlprobe_kbd_pending(void)
{
    uint16_t head = _farpeekw(_dos_ds, 0x41AL);
    uint16_t tail = _farpeekw(_dos_ds, 0x41CL);
    return head != tail;
}

/* RDTSC / PIT / BIOS-tick mode-dispatched read. Mirrors SDL/0039's
 * SDL_DOSAudioReadTimer (SDL_FORCE_INLINE; we re-implement here since the
 * private SDL header isn't reachable). */
static inline uint64_t sdlprobe_read_timer(void)
{
    uint32_t mode = g_dos_audio_timer_mode;
    if (mode == SDLPROBE_TIMER_RDTSC) {
        uint32_t lo, hi;
        __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
        return ((uint64_t)hi << 32) | lo;
    } else if (mode == SDLPROBE_TIMER_PIT) {
        outportb(0x43, 0x00);
        uint8_t lo = inportb(0x40);
        uint8_t hi = inportb(0x40);
        return (uint64_t)(uint16_t)((hi << 8) | lo);
    }
    return (uint64_t)_farpeekl(_dos_ds, 0x46Cul);
}

/* Convert raw timer delta to a uint32_t sample. For RDTSC = cycles;
 * for PIT = counts (DOWN-counter, with wrap handling); for BIOS-tick =
 * 18.2 Hz ticks. Caller uses sdlprobe_cycles_to_us to convert to us. */
static inline uint32_t sdlprobe_timer_delta(uint64_t entry, uint64_t exit_)
{
    uint32_t mode = g_dos_audio_timer_mode;
    if (mode == SDLPROBE_TIMER_RDTSC) {
        uint64_t d = exit_ - entry;
        return (uint32_t)(d & 0xFFFFFFFFu);
    } else if (mode == SDLPROBE_TIMER_PIT) {
        uint16_t e = (uint16_t)entry;
        uint16_t x = (uint16_t)exit_;
        return (e >= x) ? (uint32_t)(e - x) : (uint32_t)(0x10000u - x + e);
    }
    return (uint32_t)((uint32_t)exit_ - (uint32_t)entry);
}

/* Convert sample count to us at the calibrated CPU MHz / PIT rate.
 * Returns -1.0 sentinel when mode==RDTSC but mhz==0 (DOSBox-X failure case
 * where SDL/0039's SDL_DOSAudioInitTimer couldn't calibrate TSC against
 * SDL_GetTicksNS). Caller (sdlprobe_emit_done_stats) checks for negative
 * and emits "UNCALIBRATED" instead of misapplying the BIOS-tick fallback
 * which would multiply cycles by 54925 and produce nonsense (e.g. 399
 * cycles -> 21,915,075 us). Real HW calibrates TSC cleanly so production
 * data is unaffected; this only protects DOSBox-X smoke output from
 * garbage-us misreads.
 *
 * (Bug surfaced by sdl-engine during sdlprob1 smoke 2026-05-08; team-lead
 * approved Option B fix per `force_options_on_deviations.md` carve-out
 * "smoke surfaces a defect -> re-engage".) */
static inline double sdlprobe_cycles_to_us(uint32_t cycles)
{
    uint32_t mode = g_dos_audio_timer_mode;
    uint32_t mhz  = g_dos_audio_timer_tsc_mhz;
    if (mode == SDLPROBE_TIMER_RDTSC) {
        return (mhz > 0) ? ((double)cycles / (double)mhz) : -1.0;
    } else if (mode == SDLPROBE_TIMER_PIT) {
        return (double)cycles * 1000.0 / 1193.0;  /* 838 ns / count */
    }
    return (double)cycles * 54925.0;  /* BIOS-tick: 54.925 ms / tick */
}

/* ============================================================ */
/* Stat helper                                                   */
/* ============================================================ */

typedef struct {
    uint32_t min, med, p95, max;
    double   mean;
    int      bimodal;  /* max/min > 4? */
} sdlprobe_stats_t;

static int sdlprobe_u32_cmp(const void *a, const void *b)
{
    uint32_t va = *(const uint32_t *)a, vb = *(const uint32_t *)b;
    return (va > vb) - (va < vb);
}

static void sdlprobe_compute_stats(uint32_t *samples, int n,
                                   sdlprobe_stats_t *out)
{
    qsort(samples, n, sizeof samples[0], sdlprobe_u32_cmp);
    out->min = samples[0];
    out->max = samples[n - 1];
    out->med = samples[n / 2];
    out->p95 = samples[(int)(0.95 * n)];
    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += (double)samples[i];
    out->mean = sum / n;
    out->bimodal = (out->min > 0) ? ((out->max / out->min) > 4) : 0;
}

/* ============================================================ */
/* Mem-delta probe (DJGPP _go32_dpmi_get_free_memory_information) */
/* ============================================================ */

static int sdlprobe_free_dpmi_mem(void)
{
    _go32_dpmi_meminfo info;
    memset(&info, 0, sizeof info);
    _go32_dpmi_get_free_memory_information(&info);
    return (int)(info.available_physical_pages * 4096u);
}

/* ============================================================ */
/* Scenario ledger + watchdog                                    */
/* ============================================================ */

static int sdlprobe_g_scenario_idx = 0;
static int sdlprobe_g_scenario_total = 0;
static double sdlprobe_g_whole_t0 = 0.0;
static int sdlprobe_g_completed = 0;
static int sdlprobe_g_aborted = 0;
static int sdlprobe_g_skipped = 0;
static int sdlprobe_g_mem_pre = 0;

static void sdlprobe_set_total_scenarios(int n) { sdlprobe_g_scenario_total = n; }

static void sdlprobe_scenario_begin(const char *name)
{
    sdlprobe_g_scenario_idx++;
    sdlprobe_g_mem_pre = sdlprobe_free_dpmi_mem();
    sdlprobe_plog("");
    sdlprobe_plog("=== SCENARIO %d/%d: %s ===",
         sdlprobe_g_scenario_idx, sdlprobe_g_scenario_total, name);
    sdlprobe_plog("[BEGIN_%s] uclock=%.6f free_mem_pre=%d",
         name, sdlprobe_now_secs(), sdlprobe_g_mem_pre);
}

static void sdlprobe_emit_done_stats(const char *name, uint32_t *samples, int n,
                                     int aborted, const char *abort_reason)
{
    if (n == 0) {
        sdlprobe_plog("[DONE_%s] N=0 SKIP reason=%s",
                      name, abort_reason ? abort_reason : "no_data");
        sdlprobe_g_skipped++;
        return;
    }
    sdlprobe_stats_t st;
    sdlprobe_compute_stats(samples, n, &st);

    int mem_post  = sdlprobe_free_dpmi_mem();
    int mem_delta = mem_post - sdlprobe_g_mem_pre;

    char status[48];
    if (aborted) snprintf(status, sizeof status, "ABORT_PARTIAL:%s",
                          abort_reason ? abort_reason : "unknown");
    else         snprintf(status, sizeof status, "OK");

    double med_us = sdlprobe_cycles_to_us(st.med);
    double min_us = sdlprobe_cycles_to_us(st.min);
    double max_us = sdlprobe_cycles_to_us(st.max);

    sdlprobe_plog("[DONE_%s] N=%d %s cpu_mhz=%lu mode=%lu",
         name, n, status,
         (unsigned long)g_dos_audio_timer_tsc_mhz,
         (unsigned long)g_dos_audio_timer_mode);
    sdlprobe_plog("           cycles min=%lu med=%lu p95=%lu max=%lu mean=%.1f",
         (unsigned long)st.min, (unsigned long)st.med,
         (unsigned long)st.p95, (unsigned long)st.max, st.mean);
    /* Sentinel: cycles_to_us returns -1.0 when mode==RDTSC && mhz==0 (DOSBox-X
     * un-calibrated TSC). Emit "UNCALIBRATED" instead of misleading garbage.
     * Real HW calibrates cleanly; this sentinel path is DOSBox-X-only. */
    if (med_us < 0.0) {
        sdlprobe_plog("           us(med)=UNCALIBRATED us(min)=UNCALIBRATED us(max)=UNCALIBRATED bimodal=%s",
             st.bimodal ? "Y" : "N");
        sdlprobe_plog("           (cycles_to_us sentinel: TSC not calibrated; cycle counts above are accurate)");
    } else {
        sdlprobe_plog("           us(med)=%.3f us(min)=%.3f us(max)=%.3f bimodal=%s",
             med_us, min_us, max_us, st.bimodal ? "Y" : "N");
    }
    sdlprobe_plog("           mem_delta=%d_bytes (negative = leak)", mem_delta);
    /* Note: samples buffer was qsort'd by compute_stats; min_cluster + max_cluster
     * report 5 smallest + 5 largest, not chronological first5/last5. */
    sdlprobe_plog("           min_cluster: %lu %lu %lu %lu %lu",
         (unsigned long)samples[0], (unsigned long)samples[1],
         (unsigned long)samples[2], (unsigned long)samples[3],
         (unsigned long)samples[4]);
    int top = (n >= 5) ? n - 5 : 0;
    sdlprobe_plog("           max_cluster: %lu %lu %lu %lu %lu",
         (unsigned long)samples[top + 0], (unsigned long)samples[top + 1],
         (unsigned long)samples[top + 2], (unsigned long)samples[top + 3],
         (unsigned long)samples[top + 4]);
    if (aborted) sdlprobe_g_aborted++;
    else         sdlprobe_g_completed++;
}

/* Returns 0 = continue, -1 = abort (sets *abort_reason). */
static int sdlprobe_watchdog_check(double scenario_t0, const char **abort_reason)
{
    if ((sdlprobe_now_secs() - sdlprobe_g_whole_t0) > SDLPROBE_WHOLE_CAP_SECS) {
        *abort_reason = "whole_probe_8min_cap";
        return -1;
    }
    if ((sdlprobe_now_secs() - scenario_t0) > SDLPROBE_WATCHDOG_SECS) {
        *abort_reason = "scenario_60s_cap";
        return -1;
    }
    if (sdlprobe_kbd_pending()) {
        *abort_reason = "operator_kbd_escape";
        return -1;
    }
    return 0;
}

/* ============================================================ */
/* Generic body-runner: warmup + N timed reps with raw stash    */
/* ============================================================ */

typedef void (*sdlprobe_body_fn_t)(void *ud);

static void sdlprobe_run_body_scenario(const char *name,
                                       int n_warmup, int n_reps,
                                       sdlprobe_body_fn_t body, void *ud)
{
    if (n_reps > SDLPROBE_MAX_SAMPLES) n_reps = SDLPROBE_MAX_SAMPLES;
    static uint32_t samples[SDLPROBE_MAX_SAMPLES];
    sdlprobe_scenario_begin(name);

    for (int i = 0; i < n_warmup; i++) body(ud);

    double t0 = sdlprobe_now_secs();
    int captured = 0;
    const char *abort_reason = NULL;
    int aborted = 0;

    for (int i = 0; i < n_reps; i++) {
        uint64_t te = sdlprobe_read_timer();
        body(ud);
        uint64_t tx = sdlprobe_read_timer();
        samples[i] = sdlprobe_timer_delta(te, tx);
        captured++;

        if ((i & 0x3F) == 0) {
            if (sdlprobe_watchdog_check(t0, &abort_reason) != 0) {
                aborted = 1;
                break;
            }
        }
    }

    sdlprobe_emit_done_stats(name, samples, captured, aborted, abort_reason);
}

/* ============================================================ */
/* SDL state setup -- Window + Renderer + INDEX8 surfaces        */
/*                                                                */
/* Per Q1 (sdl-engine consult): probe creates own 320x240 Window */
/* + SW Renderer (NULL driver name = backend default = SW on DOS). */
/* Per Q2: SDL_HINT_DOS_ALLOW_DIRECT_FRAMEBUFFER must be flipped */
/* via SDL_SetHintWithPriority OVERRIDE BEFORE SDL_Init.         */
/* ============================================================ */

typedef struct {
    SDL_Window  *window;
    SDL_Renderer *renderer;
    SDL_Surface *src_8x8;
    SDL_Surface *src_32x32;
    SDL_Surface *src_320x240;
    SDL_Surface *dst;
    SDL_Texture *tex_32x32_cached;  /* pre-cached for composites C/D engine-shape */
    int          init_ok;
} sdlprobe_state_t;

static int sdlprobe_setup_state(sdlprobe_state_t *s)
{
    memset(s, 0, sizeof *s);

    SDL_SetHintWithPriority(SDL_HINT_DOS_ALLOW_DIRECT_FRAMEBUFFER, "1",
                            SDL_HINT_OVERRIDE);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        sdlprobe_plog("FATAL: SDL_Init failed: %s", SDL_GetError());
        return -1;
    }
    sdlprobe_plog("SDL_Init OK (VIDEO + AUDIO)");

    s->window = SDL_CreateWindow("sdlprobe", 320, 240, 0);
    if (!s->window) {
        sdlprobe_plog("FATAL: SDL_CreateWindow failed: %s", SDL_GetError());
        return -2;
    }
    sdlprobe_plog("SDL_CreateWindow OK (320x240)");

    s->renderer = SDL_CreateRenderer(s->window, NULL);
    if (!s->renderer) {
        sdlprobe_plog("FATAL: SDL_CreateRenderer failed: %s", SDL_GetError());
        return -3;
    }
    sdlprobe_plog("SDL_CreateRenderer OK (NULL driver = SW on DOS backend)");

    s->src_8x8     = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_INDEX8);
    s->src_32x32   = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_INDEX8);
    s->src_320x240 = SDL_CreateSurface(320, 240, SDL_PIXELFORMAT_INDEX8);
    s->dst         = SDL_CreateSurface(320, 240, SDL_PIXELFORMAT_INDEX8);
    if (!s->src_8x8 || !s->src_32x32 || !s->src_320x240 || !s->dst) {
        sdlprobe_plog("FATAL: SDL_CreateSurface failed: %s", SDL_GetError());
        return -4;
    }

    /* Attach a 256-entry palette to each INDEX8 surface so blits work. */
    SDL_Palette *pal = SDL_CreatePalette(256);
    if (pal) {
        for (int i = 0; i < 256; i++) {
            pal->colors[i].r = (uint8_t)i;
            pal->colors[i].g = (uint8_t)i;
            pal->colors[i].b = (uint8_t)i;
            pal->colors[i].a = 0xFF;
        }
        SDL_SetSurfacePalette(s->src_8x8,     pal);
        SDL_SetSurfacePalette(s->src_32x32,   pal);
        SDL_SetSurfacePalette(s->src_320x240, pal);
        SDL_SetSurfacePalette(s->dst,         pal);
        SDL_DestroyPalette(pal);
    }

    /* Fill source surfaces with non-trivial pixel patterns; avoid 0
     * (the colorkey value) at the small surfaces so every-pixel blits
     * actually copy data. */
    if (s->src_8x8->pixels) {
        uint8_t *p = (uint8_t *)s->src_8x8->pixels;
        for (int i = 0; i < 8 * 8; i++) p[i] = (uint8_t)((i & 0xF) + 1);
    }
    if (s->src_32x32->pixels) {
        uint8_t *p = (uint8_t *)s->src_32x32->pixels;
        for (int i = 0; i < 32 * 32; i++) p[i] = (uint8_t)((i & 0xFF) ^ 0x55);
    }
    if (s->src_320x240->pixels) {
        uint8_t *p = (uint8_t *)s->src_320x240->pixels;
        for (long i = 0; i < 320L * 240L; i++)
            p[i] = (uint8_t)((i & 0xFF) ^ 0xAA);
    }

    /* Pre-cache texture from 32x32 src (engine-shape per Q3 from sdl-engine:
     * engine creates texture once at Surface load; per-call only does
     * SetTextureAlphaMod + RenderTexture). */
    s->tex_32x32_cached = SDL_CreateTextureFromSurface(s->renderer, s->src_32x32);
    if (!s->tex_32x32_cached) {
        sdlprobe_plog("WARN: SDL_CreateTextureFromSurface failed: %s",
             SDL_GetError());
        sdlprobe_plog("      Composites C/D will SKIP.");
    }

    sdlprobe_plog("SDL state ready: window + renderer + 4 surfaces + 1 cached texture");
    s->init_ok = 1;
    return 0;
}

static void sdlprobe_teardown_state(sdlprobe_state_t *s)
{
    if (s->tex_32x32_cached) SDL_DestroyTexture(s->tex_32x32_cached);
    if (s->src_8x8)          SDL_DestroySurface(s->src_8x8);
    if (s->src_32x32)        SDL_DestroySurface(s->src_32x32);
    if (s->src_320x240)      SDL_DestroySurface(s->src_320x240);
    if (s->dst)              SDL_DestroySurface(s->dst);
    if (s->renderer)         SDL_DestroyRenderer(s->renderer);
    if (s->window)           SDL_DestroyWindow(s->window);
    SDL_Quit();
}

/* ============================================================ */
/* Probe lifecycle helpers                                       */
/* ============================================================ */

static void sdlprobe_init_timer_log_banner(void)
{
    SDL_DOSAudioInitTimer();
    sdlprobe_plog("UCLOCKS_PER_SEC=%lu  WHOLE_CAP_SECS=%.0f  PER_SCENARIO_CAP=%.0f",
         (unsigned long)UCLOCKS_PER_SEC, SDLPROBE_WHOLE_CAP_SECS,
         SDLPROBE_WATCHDOG_SECS);
    sdlprobe_plog("SDL_DOSAudioInitTimer done; mode=%lu cpu_mhz=%lu",
         (unsigned long)g_dos_audio_timer_mode,
         (unsigned long)g_dos_audio_timer_tsc_mhz);
    if (g_dos_audio_timer_mode == SDLPROBE_TIMER_RDTSC &&
        g_dos_audio_timer_tsc_mhz > 0) {
        sdlprobe_plog("Timer = RDTSC. cpu_mhz=%lu (PODP83 expected ~83; DOSBox-X varies).",
             (unsigned long)g_dos_audio_timer_tsc_mhz);
    } else if (g_dos_audio_timer_mode == SDLPROBE_TIMER_RDTSC) {
        /* mode=RDTSC but mhz=0: TSC not calibrated (DOSBox-X failure case
         * where SDL/0039 SDL_DOSAudioInitTimer couldn't pin TSC freq via
         * SDL_GetTicksNS). Cycle counts are still accurate; us-conversion
         * is sentinel-suppressed (see sdlprobe_cycles_to_us). Sibling fix
         * to the cycles_to_us defect surfaced by sdl-engine 2026-05-08. */
        sdlprobe_plog("Timer = RDTSC UNCALIBRATED (cpu_mhz=0; DOSBox-X case).");
        sdlprobe_plog("  Cycle counts above are accurate; us-conversion will emit UNCALIBRATED sentinel.");
    } else if (g_dos_audio_timer_mode == SDLPROBE_TIMER_PIT) {
        sdlprobe_plog("Timer = PIT. 838 ns / count resolution.");
    } else {
        sdlprobe_plog("Timer = BIOS-tick fallback (54925 us; per-call deltas useless).");
    }
}

static void sdlprobe_emit_summary(void)
{
    double elapsed = sdlprobe_now_secs() - sdlprobe_g_whole_t0;
    sdlprobe_plog("");
    sdlprobe_plog("=== probe summary ===");
    sdlprobe_plog("Wall-clock elapsed: %.1f sec (whole-probe cap=%.0f)",
         elapsed, SDLPROBE_WHOLE_CAP_SECS);
    sdlprobe_plog("Scenarios completed: %d", sdlprobe_g_completed);
    sdlprobe_plog("Scenarios aborted (cap-trip): %d", sdlprobe_g_aborted);
    sdlprobe_plog("Scenarios skipped (no data):  %d", sdlprobe_g_skipped);
    sdlprobe_plog("Cross-anchor: cpu_mhz=%lu (PODP83 ~83; DOSBox-X varies).",
         (unsigned long)g_dos_audio_timer_tsc_mhz);
}

#endif  /* SDLPROBE_COMMON_H */
