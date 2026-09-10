/*
 * dpmithn.c — DPMI / DJGPP thunk-overhead characterization.
 *
 * Phase 9 wave 20 task #8 (P1). Decomposes the per-call cost of three DOS
 * primitives the SDL3-DOS framebuffer-flush path uses:
 *
 *   DOSMEMPUT  dosmemput() into a DOS-conventional-memory buffer at a
 *              range of payload sizes. The actual VRAM-flush call costs
 *              memcpy bandwidth + DPMI selector setup; this probe
 *              isolates the DPMI side by writing into ordinary RAM (no
 *              chip-side bus contention), so the dosmemput-RAM cost is
 *              the lower bound for what the SDL flush pays per call.
 *
 *   DPMIINT    __dpmi_int(0x21, AH=0x30) (get DOS version) — a fast,
 *              safe, read-only INT 21h call. Measures the protected
 *              -> real -> protected mode-switch cost itself, which
 *              shows up if any code path issues an INT 21h (or other
 *              DPMI-reflected interrupt) per flush. Wave-20 hypothesis
 *              candidate (per cold-pickup §1).
 *
 *   OUTPORTB   Plain `outportb` to port 0x80 (POST diagnostic port,
 *              ignored on real HW; safe). Baseline non-DPMI I/O cost
 *              for comparison against DACPROG's outportb-to-DAC numbers.
 *
 * Per-call cost decomposition (informs the wave-20 misc slice):
 *
 *   The 11.6 ms unidentified slice in the cold-pickup §1 candidate list
 *   includes "dosmemput pre/post DPMI thunk overhead." If DPMITHN shows
 *   DOSMEMPUT-1B (1-byte payload, all overhead) << 1 ms, the per-call
 *   thunk is NOT the misc-slice culprit — it costs ~thousands per flush
 *   and would have to be ~1000 calls to add up. If it's ≥ 100 µs each,
 *   suspicion shifts up the stack.
 *
 * Output: C:\DPMITHN.LOG (fsync per line). Falls back to ./DPMITHN.LOG.
 *
 * Pure DJGPP. Uses `__dpmi_allocate_dos_memory` for the DOS conventional
 * destination buffer. No SDL.
 *
 * 8.3 DOS filename: DPMITHN.EXE (7.3) — fits per dos_filename_8_3.md.
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
#include <sys/movedata.h>
#include <time.h>
#include <unistd.h>

/* ============================================================ */
/* Logging                                                      */
/* ============================================================ */

static FILE *g_log = NULL;

static void tlog(const char *fmt, ...)
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
    g_log = fopen("C:\\DPMITHN.LOG", "w");
    if (!g_log) g_log = fopen("DPMITHN.LOG", "w");
}

/* ============================================================ */
/* Timing                                                       */
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

    tlog("THN-STAT %-22s n=%d K=%d  per-call:  min=%9.3f  med=%9.3f  mean=%9.3f  max=%9.3f  us",
         label, n, K, mn * 1e6, med * 1e6, mean * 1e6, mx * 1e6);
}

/* ============================================================ */
/* DOS conventional memory destination buffer                   */
/* ============================================================ */

/* Allocates 64 KB of DOS conventional memory; returns the linear DOS
 * address (paragraph << 4) and the DPMI selector. Caller must free via
 * __dpmi_free_dos_memory(sel). */
static int alloc_dos_buf(uint32_t bytes,
                         uint32_t *out_dos_lin,
                         int      *out_sel)
{
    int paragraphs = (bytes + 15) / 16;
    int sel = -1;
    int seg = __dpmi_allocate_dos_memory(paragraphs, &sel);
    if (seg < 0) return -1;
    *out_dos_lin = (uint32_t)seg << 4;
    *out_sel     = sel;
    return 0;
}

/* ============================================================ */
/* DOSMEMPUT timing — for a list of payload sizes                */
/* ============================================================ */

static const int g_dosmem_sizes[] = {
    1, 16, 64, 256, 1024, 4096, 16384, 65536, 0
};

/* Time K back-to-back dosmemput calls. */
static double timed_dosmemput(const uint8_t *src,
                              uint32_t dos_lin,
                              uint32_t bytes,
                              int K)
{
    double t0 = now_secs();
    for (int k = 0; k < K; k++) {
        dosmemput(src, bytes, dos_lin);
    }
    return now_secs() - t0;
}

static void run_dosmemput(uint32_t dos_lin)
{
    /* Use a 64 KB protected-mode source buffer with deterministic content. */
    uint8_t *src = (uint8_t *)malloc(65536);
    if (!src) {
        tlog("FATAL: malloc(65536) failed for dosmemput src");
        return;
    }
    for (int i = 0; i < 65536; i++) src[i] = (uint8_t)(i & 0xFF);

    tlog("---- DOSMEMPUT: per-call cost as a function of payload size ----");
    for (int i = 0; g_dosmem_sizes[i]; i++) {
        int payload = g_dosmem_sizes[i];

        /* Pick K so each batch is ~5-10 ms (large vs uclock granularity).
         * Smaller payloads -> more reps. */
        int K;
        if (payload <= 16)        K = 5000;
        else if (payload <= 256)  K = 2000;
        else if (payload <= 1024) K = 500;
        else if (payload <= 4096) K = 200;
        else if (payload <= 16384)K = 50;
        else                      K = 20;
        const int N = 100;

        /* Warm-up. */
        timed_dosmemput(src, dos_lin, (uint32_t)payload, K);

        /* Sample. */
        double *samples = (double *)malloc(sizeof(double) * N);
        if (!samples) { tlog("FATAL: malloc samples failed"); free(src); return; }
        for (int s = 0; s < N; s++) {
            samples[s] = timed_dosmemput(src, dos_lin, (uint32_t)payload, K);
        }
        char label[40];
        snprintf(label, sizeof label, "DOSMEMPUT %5dB", payload);
        summarize(label, samples, N, K);

        /* Effective bandwidth (median). */
        qsort(samples, N, sizeof samples[0], dbl_cmp);
        double med = samples[N / 2];
        double mb_per_s = (double)payload / (med * 1024.0 * 1024.0);
        tlog("THN-BW   DOSMEMPUT %5dB  med=%.3f us  bandwidth=%.1f MB/s",
             payload, med * 1e6, mb_per_s);

        free(samples);
    }
    free(src);
}

/* ============================================================ */
/* DPMIINT timing — INT 21h AH=0x30 get DOS version              */
/* ============================================================ */

static double timed_dpmiint(int K)
{
    __dpmi_regs r;
    double t0 = now_secs();
    for (int k = 0; k < K; k++) {
        memset(&r, 0, sizeof r);
        r.h.ah = 0x30;          /* Get DOS version — read-only, safe. */
        __dpmi_int(0x21, &r);
    }
    return now_secs() - t0;
}

static void run_dpmiint(void)
{
    tlog("---- DPMIINT: __dpmi_int(0x21, AH=0x30) -- 'get DOS version' ----");
    const int K = 200, N = 100;

    /* Sanity: confirm the call returns a plausible DOS version. */
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x30;
    __dpmi_int(0x21, &r);
    tlog("DPMIINT sanity: AL=%d.BL=%d (DOS version = %d.%d)  CX=%04X (OEM/ser)",
         r.h.al, r.h.bl, r.h.al, r.h.bl, r.x.cx);

    /* Warm-up. */
    timed_dpmiint(K);

    double *samples = (double *)malloc(sizeof(double) * N);
    if (!samples) { tlog("FATAL: malloc samples failed"); return; }
    for (int s = 0; s < N; s++) samples[s] = timed_dpmiint(K);
    summarize("DPMIINT (INT 21h/30)", samples, N, K);
    free(samples);
}

/* ============================================================ */
/* OUTPORTB timing — baseline non-DPMI I/O                       */
/* ============================================================ */

static double timed_outportb(int K)
{
    double t0 = now_secs();
    for (int k = 0; k < K; k++) {
        /* Port 0x80 is the BIOS POST diagnostic port; writes to it have
         * no software-visible effect on real HW (some chipsets latch the
         * value to a hex display, harmless). On DOSBox-X it's a no-op. */
        outportb(0x80, (uint8_t)k);
    }
    return now_secs() - t0;
}

static void run_outportb(void)
{
    tlog("---- OUTPORTB: outportb to port 0x80 (POST diag, no-op baseline) ----");
    const int K = 50000, N = 100;

    timed_outportb(K);  /* warm-up */

    double *samples = (double *)malloc(sizeof(double) * N);
    if (!samples) { tlog("FATAL: malloc samples failed"); return; }
    for (int s = 0; s < N; s++) samples[s] = timed_outportb(K);
    summarize("OUTPORTB(0x80)", samples, N, K);
    free(samples);
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    tlog("=== DPMITHN wave-20 task #8 (P1) starting ===");
    tlog("DJGPP build, target = DPMI thunk + outportb cost characterization");
    tlog("UCLOCKS_PER_SEC = %lu", (unsigned long)UCLOCKS_PER_SEC);

    /* Allocate DOS conventional buffer for dosmemput dest. */
    uint32_t dos_lin = 0;
    int sel = -1;
    if (alloc_dos_buf(65536, &dos_lin, &sel) < 0) {
        tlog("FATAL: __dpmi_allocate_dos_memory(65536) failed");
        if (g_log) fclose(g_log);
        return 2;
    }
    tlog("DOS-buf: 64 KB at lin=0x%05lX  selector=0x%04X",
         (unsigned long)dos_lin, sel);

    run_dosmemput(dos_lin);
    run_dpmiint();
    run_outportb();

    __dpmi_free_dos_memory(sel);

    tlog("=== DPMITHN done ===");
    tlog("");
    tlog("Wave-20 misc-slice decomposition cues:");
    tlog("  DOSMEMPUT 1B   ~ pure DPMI selector setup cost (call overhead)");
    tlog("  DOSMEMPUT 4096B+ ~ memcpy bandwidth dominates");
    tlog("  DPMIINT        ~ INT 21h reflector cost (PM<->RM mode-switch)");
    tlog("  OUTPORTB       ~ baseline I/O port write (no DPMI involvement)");
    tlog("");
    tlog("If DOSMEMPUT 1B << 5 us, dosmemput per-call thunk is NOT the misc-slice culprit.");
    tlog("If DPMIINT >> 50 us, any per-flip INT 21h call (e.g. file/log writes) is suspect.");

    if (g_log) fclose(g_log);
    return 0;
}
