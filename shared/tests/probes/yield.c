/*
 * yield.c — SDL_Delay() / SDL_PumpEvents cooperative-yield cost
 *           characterization for SDL3-DOS on PODP83.
 *
 * Phase 9 wave 20 task #12 (P3 — load-bearing). Targets W20A v2's
 * 13.5 ms unbracketed flip() body baseline (audio-off ceiling): if
 * SDL_Delay(0) is a meaningful chunk of that, the engine's per-flip
 * yield call is a candidate for removal or gating.
 *
 * Four tests:
 *
 *   Test A — SDL_Delay(0) idle queue
 *     K=200 batched calls, no events queued. Measures pure
 *     scheduler/yield cost when there's nothing to dispatch.
 *
 *   Test B — SDL_Delay(0) with N events queued
 *     For N in {1, 4, 16, 64, 256}: SDL_PushEvent N synthetic events,
 *     time SDL_Delay(0), drain via SDL_PollEvent loop, repeat. The
 *     curve identifies whether yield cost scales with event-queue
 *     depth (= scheduler walks the queue) or stays flat (= pure yield).
 *
 *   Test C — SDL_PumpEvents() in isolation
 *     Same harness as Test A but calls SDL_PumpEvents() instead of
 *     SDL_Delay(0). Separates yield cost from event-pump cost.
 *
 *   Test D — SDL_Delay(N) sleep-accuracy curve
 *     For N in {0, 1, 2, 5, 10, 20} ms: time a single SDL_Delay(N)
 *     and measure actual wall-clock. PODP83's PIT granularity is
 *     ~55 ms (18.2 Hz default) so SDL_Delay(1) may actually sleep ~55 ms;
 *     this curve identifies the floor. Engine's SDL_Delay(20) at
 *     main.cpp:193,212 should sleep ~20 ms; if it sleeps ~55, that's
 *     a 35 ms bug per call.
 *
 * Decision-grade thresholds (post real-HW):
 *   SDL_Delay(0) idle >= 1 ms       -> meaningful slice of 13.5 ms baseline
 *   SDL_Delay(0) idle <  0.1 ms     -> not the cost; eliminate hypothesis
 *   Test B scaling > linear with N  -> event-queue walk sized poorly
 *   Test D SDL_Delay(20) actual ~55 -> PIT-granularity bug; engine sleeps too long
 *
 * Output: C:\YIELD.LOG (fsync per line). Falls back to ./YIELD.LOG.
 *
 * SDL3-linked (links libSDL3.a). 8.3 DOS filename: YIELD.EXE (5.3).
 *
 * Build: `make yield`. Smoke under DOSBox-X for correctness only —
 * DOSBox-X scheduler != PODP83 cooperative scheduler, so numbers are
 * real-HW-only per dosbox_not_perf_proxy.md.
 *
 * License: MIT.
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ============================================================ */
/* Logging                                                      */
/* ============================================================ */

static FILE *g_log = NULL;

static void ylog(const char *fmt, ...)
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

static void open_log(void)
{
    g_log = fopen("C:\\YIELD.LOG", "w");
    if (!g_log) g_log = fopen("YIELD.LOG", "w");
}

/* ============================================================ */
/* Timing — use SDL_GetPerformanceCounter for higher granularity */
/* ============================================================ */

static double perf_secs(void)
{
    Uint64 ticks = SDL_GetPerformanceCounter();
    return (double)ticks / (double)SDL_GetPerformanceFrequency();
}

static int dbl_cmp(const void *a, const void *b)
{
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

static void summarize(const char *label, double *samples, int n, int K)
{
    /* Convert each batch sample -> per-call seconds. */
    for (int i = 0; i < n; i++) samples[i] /= (double)K;
    qsort(samples, n, sizeof samples[0], dbl_cmp);

    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += samples[i];
    double mean = sum / n;
    double mn = samples[0];
    double mx = samples[n - 1];
    double med = samples[n / 2];

    ylog("YLD-STAT %-22s n=%d K=%d  per-call:  min=%9.3f  med=%9.3f  mean=%9.3f  max=%9.3f  us",
         label, n, K, mn * 1e6, med * 1e6, mean * 1e6, mx * 1e6);
}

/* ============================================================ */
/* Test A — SDL_Delay(0) idle queue                              */
/* ============================================================ */

static void run_test_a(void)
{
    ylog("---- Test A: SDL_Delay(0) on idle event queue ----");
    const int K = 200, N = 100;

    /* Drain any pending events first to ensure idle. */
    SDL_PumpEvents();
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) { /* drain */ }

    /* Warm-up. */
    for (int k = 0; k < K; k++) SDL_Delay(0);

    double *samples = (double *)malloc(sizeof(double) * N);
    if (!samples) { ylog("FATAL: malloc failed"); return; }
    for (int s = 0; s < N; s++) {
        double t0 = perf_secs();
        for (int k = 0; k < K; k++) SDL_Delay(0);
        samples[s] = perf_secs() - t0;
    }
    summarize("DELAY(0) idle", samples, N, K);
    free(samples);
}

/* ============================================================ */
/* Test B — SDL_Delay(0) with N events queued                    */
/* ============================================================ */

/* Push N synthetic SDL_USEREVENTs. */
static void push_n_events(int n)
{
    for (int i = 0; i < n; i++) {
        SDL_Event ev;
        memset(&ev, 0, sizeof ev);
        ev.type = SDL_EVENT_USER;
        ev.user.code = i;
        SDL_PushEvent(&ev);
    }
}

static void drain_events(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) { /* discard */ }
}

static void run_test_b(void)
{
    ylog("---- Test B: SDL_Delay(0) with N synthetic events queued ----");
    const int K = 50, N = 100;
    static const int n_values[] = { 1, 4, 16, 64, 256, 0 };

    double *samples = (double *)malloc(sizeof(double) * N);
    if (!samples) { ylog("FATAL: malloc failed"); return; }

    for (int i = 0; n_values[i]; i++) {
        int nev = n_values[i];
        char label[40];
        snprintf(label, sizeof label, "DELAY(0) N=%d evts", nev);

        /* Warm-up. */
        push_n_events(nev);
        SDL_Delay(0);
        drain_events();

        for (int s = 0; s < N; s++) {
            push_n_events(nev);
            double t0 = perf_secs();
            for (int k = 0; k < K; k++) {
                SDL_Delay(0);
            }
            samples[s] = perf_secs() - t0;
            drain_events();
        }
        summarize(label, samples, N, K);
    }
    free(samples);
}

/* ============================================================ */
/* Test C — SDL_PumpEvents() in isolation                        */
/* ============================================================ */

static void run_test_c(void)
{
    ylog("---- Test C: SDL_PumpEvents() in isolation ----");
    const int K = 200, N = 100;

    drain_events();
    for (int k = 0; k < K; k++) SDL_PumpEvents();

    double *samples = (double *)malloc(sizeof(double) * N);
    if (!samples) { ylog("FATAL: malloc failed"); return; }
    for (int s = 0; s < N; s++) {
        double t0 = perf_secs();
        for (int k = 0; k < K; k++) SDL_PumpEvents();
        samples[s] = perf_secs() - t0;
    }
    summarize("PUMP idle", samples, N, K);
    free(samples);
}

/* ============================================================ */
/* Test D — SDL_Delay(N) sleep-accuracy curve                    */
/* ============================================================ */

static void run_test_d(void)
{
    ylog("---- Test D: SDL_Delay(N) actual-wall-time per requested-N ----");

    /* Smaller K + smaller Nrep for the larger N (each call sleeps for
     * that long, so wall-clock is K * Nrep * req_ms — keep total under
     * ~5s per step so the full Test D fits in DOSBox-X's smoke window
     * + real-HW operator wait time stays tolerable. The smaller Nrep
     * still gives min/median/max readings; we don't need 50 samples
     * to identify a PIT-granularity bug. */
    static const struct {
        Uint32 ms_req;
        int    K;
        int    N;
    } steps[] = {
        {  0, 1000, 50 },
        {  1,  100, 30 },
        {  2,  100, 20 },
        {  5,   50, 15 },
        { 10,   25, 10 },
        { 20,   12,  8 },
        { 50,    5,  6 },
    };
    int nsteps = sizeof steps / sizeof steps[0];

    drain_events();

    for (int i = 0; i < nsteps; i++) {
        Uint32 req = steps[i].ms_req;
        int K = steps[i].K;
        int Nrep = steps[i].N;

        /* Warm-up. */
        SDL_Delay(req);

        double *samples = (double *)malloc(sizeof(double) * Nrep);
        if (!samples) { ylog("FATAL: malloc failed"); return; }
        for (int s = 0; s < Nrep; s++) {
            double t0 = perf_secs();
            for (int k = 0; k < K; k++) SDL_Delay(req);
            samples[s] = perf_secs() - t0;
        }

        /* Convert to per-call. */
        for (int s = 0; s < Nrep; s++) samples[s] /= (double)K;
        qsort(samples, Nrep, sizeof samples[0], dbl_cmp);
        double mn = samples[0];
        double mx = samples[Nrep - 1];
        double med = samples[Nrep / 2];
        double sum = 0.0;
        for (int s = 0; s < Nrep; s++) sum += samples[s];
        double mean = sum / Nrep;

        ylog("YLD-DELAY  req=%2u ms   actual per-call ms:  min=%7.3f  med=%7.3f  mean=%7.3f  max=%7.3f  (overshoot=%+.2f ms)",
             (unsigned)req, mn * 1000.0, med * 1000.0, mean * 1000.0, mx * 1000.0,
             (med * 1000.0) - (double)req);
        free(samples);
    }
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    ylog("=== YIELD wave-20 task #12 (P3 — load-bearing for SDL_Delay(0) hypothesis) starting ===");

    /* Match production hint posture so the modeset behaves identically
     * to the engine's startup. SDL_HINT_DOS_ALLOW_DIRECT_FRAMEBUFFER per
     * memory/sdl3_dos_quirks.md. */
    SDL_SetHint(SDL_HINT_DOS_ALLOW_DIRECT_FRAMEBUFFER, "1");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        ylog("FATAL: SDL_Init failed: %s", SDL_GetError());
        if (g_log) fclose(g_log);
        return 2;
    }

    Uint64 freq = SDL_GetPerformanceFrequency();
    ylog("SDL_GetPerformanceFrequency = %llu (resolution ~%.0f ns)",
         (unsigned long long)freq, 1e9 / (double)freq);

    /* Window optional — Delay/PumpEvents don't need it, but having one
     * matches engine state. Cheap. */
    SDL_Window *win = SDL_CreateWindow("yield-probe", 320, 240, 0);
    if (win) {
        ylog("WINDOW: created 320x240");
    } else {
        ylog("WINDOW: not created (%s); proceeding without", SDL_GetError());
    }

    run_test_a();
    run_test_b();
    run_test_c();
    run_test_d();

    if (win) SDL_DestroyWindow(win);
    SDL_Quit();

    ylog("=== YIELD done ===");
    ylog("");
    ylog("Reading the results:");
    ylog("  Test A 'DELAY(0) idle' med >= 1000 us  -> SDL_Delay(0) is a meaningful chunk");
    ylog("                                            of the 13.5 ms flip() baseline.");
    ylog("                                            Engine should remove or gate the");
    ylog("                                            per-flip SDL_Delay(0) call.");
    ylog("  Test A 'DELAY(0) idle' med <  100 us   -> NOT the cost; eliminate hypothesis.");
    ylog("");
    ylog("  Test B scaling > linear with N         -> event-queue walk in the yield path.");
    ylog("");
    ylog("  Test D req=20 ms actual >= 50 ms       -> PIT-granularity bug; engine's");
    ylog("                                            SDL_Delay(20) at main.cpp:193,212");
    ylog("                                            sleeps far too long.");

    if (g_log) fclose(g_log);
    return 0;
}
