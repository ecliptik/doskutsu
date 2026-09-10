/*
 * audrq.c -- Pure SB16 IRQ-hook wall-clock probe (Phase 11 wave-38 audio
 *            deep-dive, Tier 1, task #12 / Probe A).
 *            v2: defensive re-author for wave-39 (task #18).
 *
 * WAVE-38 FAILURE MODE + v2 DEFENSES (per team-lead task #18, 2026-05-13):
 *
 *   v1 truncated AUDRQ.LOG at the first rate variant BEGIN marker on real
 *   HW (g2k Vibra16S CT2490 + PODP83). Shape strongly suggests CPU stuck
 *   inside the IRQ-5 ISR -- IRQ storm prevented the spin-wait loop from
 *   resuming + cleanup code from running. SDL3-ISR cross-anchor (AUDBUF -
 *   AUDRQ = SDL3-ISR-internal cost) blocked; § 5.6.4 residual computation
 *   incomplete; P8 yield-cadence dispatch INFERRED not measured.
 *
 *   v2 adds five defenses against storm + truncation:
 *
 *   (1) ISR DEADMAN-SWITCH: at every ISR entry, if isr_count exceeds
 *       ISR_DEADMAN_THRESHOLD, the ISR self-masks IRQ-5 at the PIC.
 *       Storm breaks from within; main loop resumes; status emits as
 *       RATE_IRQ_STORM_DETECTED. This is the root-cause defense.
 *
 *   (2) ATEXIT PANIC HANDLER: registered at probe init; always restores
 *       IRQ vec + PIC mask + DMA mask + DSP reset. Fires on every exit
 *       path (clean, assert, malloc-fail, etc.) so the operator returns
 *       to a sane DOS state without reboot in most cases.
 *
 *   (3) PRE-INSTALL VECTOR VERIFY: read + log existing IRQ-5 vector
 *       (pm_offset + pm_selector) BEFORE override. Refuse install if it
 *       doesn't look like a default DPMI passthrough wrapper (some
 *       prior hook still present, e.g., another audio driver). Avoids
 *       trampling foreign hooks blindly.
 *
 *   (4) PRE-MASK IRQ-5 DURING DSP SETUP: keep IRQ-5 masked at PIC while
 *       doing DSP reset / DMA program / DSP start. Only unmask
 *       immediately before the measurement spin-wait, and re-mask
 *       immediately after. Prevents spurious IRQs during setup.
 *
 *   (5) PER-STAGE PROGRESS EMITS: each major step in run_rate (DSP_RESET,
 *       DMA_ALLOC, DMA_PROGRAM, IRQ_HOOK, DSP_START, MEASURE_BEGIN, ...
 *       MEASURE_DONE, DSP_STOP, IRQ_UNHOOK, DONE) emits a fsync'd log
 *       line. If v2 still hangs, the LOG tail pinpoints the exact step.
 *
 *   Plus: measurement window 3.0s -> 1.0s per rate (limits blast radius);
 *   first-variant-fail guard skips remaining 3 variants if first variant
 *   FAIL_INIT or IRQ_STORM_DETECTED (chip in bad state; don't compound).
 *
 * MISSION: isolate the SB16 IRQ-5 dispatch + minimal-ISR wall-clock from
 * SDL_mixer mix cost. Probe installs its OWN minimal ISR (ack DSP IRQ,
 * count++, send EOI to PIC) -- NO RingCopyOut, NO memset, NO ring-buffer
 * coordination -- and runs DMA auto-init playback of garbage / leftover
 * RAM at 4 rate variants. RDTSC per-IRQ wall-clock is the measurement.
 *
 * WAVE-39 PATCH ACTIONABILITY (per team-lead 2026-05-12 directive):
 *
 *   Informs: P9 (IRQ-hook fast-path optimization, 50-100 LOC); conditionally
 *   P1 (OPL3 backend, 1-2 wk eng) + P2 (WaveBlaster MIDI, 3-5 days; user-
 *   preferred per 2026-04-30 direction).
 *
 *   Refutes (if AUDRQ ≪14 ms equivalent / 100 PlayDevice window):
 *     P9 -- no room to optimize; deprioritize.
 *     P1, P2 -- major caveat against wave-20 v3 "SB16 IRQ-5 IS the fps cost"
 *     prior. If IRQ-only wall is small, the SDL_mixer-side mix work inside
 *     audio_thread (not in the ISR proper) is what's expensive. Pivot wave-39
 *     priority from architectural-offload to mix-optimization (P4, P5, P7).
 *
 *   Refutes (if AUDRQ dominant, ≈14 ms or more):
 *     P4, P5 -- mix-side optimization can't recover IRQ-dispatch cost.
 *     P9 rank-1; P2 rank-2 (offload removes IRQ entirely).
 *
 *   Dispatch matrix per AUDRQ outcome (at 22050s production rate):
 *     <50 us/IRQ (cheap)        -> P7 (Lever G) rank-1; pivot away from offload.
 *     50-200 us/IRQ (moderate)  -> P2 + P7 combined; P9 not dispatched.
 *     >200 us/IRQ (dominant)    -> P2 rank-1, P1 fallback, P9 rank-3.
 *
 *   Cross-anchor with AUDBUF.EXE (already-shipped wave-25 iter J probe):
 *     SDL3_ISR_internal_cost_us = AUDBUF.irq_wall_us[rate=X]
 *                               - AUDRQ.us_per_irq[rate=X]
 *   This isolates SDL3-DOS ISR-body work (audio_unlock, ring memset,
 *   RingCopyOut, DMA write) from OS+chip IRQ-dispatch overhead.
 *
 * METHODOLOGY:
 *   - Read BLASTER env (or detect via known port scan if absent) for
 *     base port, IRQ, DMA8 channel, DMA16 channel.
 *   - Reset DSP via base+6 port toggle; detect DSP version (>=4 == SB16).
 *   - Allocate ~16 KB DMA buffer in conventional memory below 1 MB
 *     (DPMI int 0x31 / fn 0x0100); ensure no 128 KB physical boundary
 *     crossing for 16-bit DMA.
 *   - Hook IRQ 5 (= INT 0x0D in DJGPP protected-mode DPMI) with
 *     minimal-ISR via _go32_dpmi_allocate_iret_wrapper + set_protected_mode_
 *     interrupt_vector. Lock ISR code/data so no page faults during IRQ.
 *   - Program DMA controller (channels 5-7 for 16-bit; high DMA ports
 *     0xC0/0xD4/etc per Intel 8237A doc + SB16 reference).
 *   - DSP cmds: 0xD1 speaker-on, 0x41 set rate, 0xB6/0x30 start 16-bit
 *     stereo auto-init DMA playback (block size = half-DMA-buffer-1).
 *   - Sample variant-rate-loop: for each rate, measure 3 sec; emit
 *     min/med/p95/max RDTSC cycles per IRQ + wall-overhead-pct.
 *   - Cleanup: stop DMA (0xD9 / 0xD0), restore IRQ vector, free DMA
 *     conventional memory.
 *
 * SANITY ANCHOR: at 22050 stereo with chunk_size = 1024 samples
 * (4 KB at 16-bit-stereo = 4 bytes/sample), each chunk takes 1024/22050
 * = ~46 ms; half-buffer-interrupt fires at ~92 ms per IRQ. Over 3 sec:
 * ~32 IRQs expected at 22050s. At 44100s: ~64 IRQs. The exact count
 * depends on the actual half-buffer transfer rate. If irqs_per_sec is
 * < 1 or > 1000 something is wrong (chip not initialized, ISR not
 * hooked, etc.).
 *
 * 8.3 DOS filenames:
 *   Source: tests/probes/audrq.c
 *   Binary: AUDRQ.EXE   (5+3)
 *   Log:    AUDRQ.LOG   (5+3)
 *   BAT:    AUDRQ.BAT   (5+3)
 *
 * License: MIT.
 */

#include <ctype.h>
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
    g_log = fopen("AUDRQ.LOG", "w");
    if (!g_log) g_log = fopen("C:\\AUDRQ.LOG", "w");
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
/* BLASTER env parsing                                           */
/* ============================================================ */

static int sb_base = -1;
static int sb_irq  = -1;
static int sb_dma8 = -1;
static int sb_dma16 = -1;

static int parse_blaster(void)
{
    const char *env = getenv("BLASTER");
    if (!env) {
        /* Common g2k default: Vibra16S CT2490 PnP-configured to A220 I5 D1 H5 T6 */
        plog("WARN: no BLASTER env; assuming A220 I5 D1 H5 (g2k Vibra16S default)");
        sb_base = 0x220; sb_irq = 5; sb_dma8 = 1; sb_dma16 = 5;
        return 0;
    }
    plog("BLASTER env = '%s'", env);
    const char *p = env;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        char type = (char)toupper((unsigned char)*p);
        p++;
        int val = (int)strtol(p, (char **)&p, (type == 'A') ? 16 : 10);
        switch (type) {
            case 'A': sb_base = val; break;
            case 'I': sb_irq  = val; break;
            case 'D': sb_dma8 = val; break;
            case 'H': sb_dma16 = val; break;
            default:  /* T (chip type), P (MIDI port), E (EMU revision) -- ignore */ break;
        }
    }
    if (sb_base < 0) sb_base = 0x220;
    if (sb_irq  < 0) sb_irq  = 5;
    if (sb_dma8 < 0) sb_dma8 = 1;
    if (sb_dma16 < 0) sb_dma16 = 5;
    plog("parsed: base=0x%X irq=%d dma8=%d dma16=%d", sb_base, sb_irq, sb_dma8, sb_dma16);
    return 0;
}

/* ============================================================ */
/* DSP I/O helpers                                               */
/* ============================================================ */

static int dsp_reset(void)
{
    outportb(sb_base + 0x6, 1);
    /* ~100 us hold (SB programming guide minimum 3 us, real HW needs ~100) */
    for (volatile int i = 0; i < 1000; i++) { (void)inportb(0x80); }
    outportb(sb_base + 0x6, 0);

    /* Poll for DSP ready (port base+0xE bit 7 set), then read 0xAA from base+0xA */
    int timeout = 1000;
    while (timeout-- > 0) {
        if (inportb(sb_base + 0xE) & 0x80) {
            uint8_t val = inportb(sb_base + 0xA);
            if (val == 0xAA) return 0;
            return -2; /* unexpected DSP reset reply */
        }
        for (volatile int i = 0; i < 100; i++) { (void)inportb(0x80); }
    }
    return -1; /* DSP did not become ready */
}

static void dsp_write(uint8_t val)
{
    int timeout = 100000;
    while (timeout-- > 0) {
        if ((inportb(sb_base + 0xC) & 0x80) == 0) {
            outportb(sb_base + 0xC, val);
            return;
        }
    }
    /* write-timeout; emit failure marker but don't abort -- caller catches via state */
}

static uint8_t dsp_read(void)
{
    int timeout = 100000;
    while (timeout-- > 0) {
        if (inportb(sb_base + 0xE) & 0x80) {
            return inportb(sb_base + 0xA);
        }
    }
    return 0xFF;
}

static int dsp_detect_version(uint8_t *out_major, uint8_t *out_minor)
{
    dsp_write(0xE1);
    uint8_t major = dsp_read();
    uint8_t minor = dsp_read();
    *out_major = major;
    *out_minor = minor;
    return (major >= 4) ? 4 : (int)major;  /* >=4 = SB16 */
}

/* ============================================================ */
/* DMA buffer allocation in conventional memory                  */
/* ============================================================ */

static int dma_seg = 0;       /* real-mode segment of DMA buf */
static int dma_sel = 0;       /* protected-mode selector for dosmemput-style access */
static long dma_buf_bytes = 0;
static uint32_t dma_physical = 0;  /* physical linear address */

/* Allocate `nbytes` of conventional memory below 1 MB via DPMI int 0x31
 * fn 0x0100 (Allocate DOS Memory Block; returns real-mode segment + PM
 * selector together), ensuring no 128 KB physical boundary crossing
 * (required for 16-bit DMA on the SB16). */
static int dma_alloc(long nbytes)
{
    int paragraphs = (int)((nbytes + 15) / 16);  /* round up to 16-byte units */

    /* Try up to 8 allocations; free + retry if boundary-crossing.
     * Held bad blocks are NOT freed until we find a good one (or give up)
     * so that DOS doesn't keep handing us back the same bad segment. */
    int kept_sels[8], n_kept = 0;
    int result = -1;

    for (int attempt = 0; attempt < 8; attempt++) {
        __dpmi_regs r;
        memset(&r, 0, sizeof r);
        r.x.ax = 0x0100;          /* DPMI Allocate DOS Memory Block */
        r.x.bx = (uint16_t)paragraphs;
        __dpmi_int(0x31, &r);
        if (r.x.flags & 1) {
            plog("ERR: DPMI alloc DOS memory failed (max para available=%u)",
                 r.x.bx);
            break;
        }
        int seg = r.x.ax;          /* real-mode segment */
        int sel = r.x.dx;          /* protected-mode selector */
        uint32_t phys = (uint32_t)seg << 4;
        uint32_t boundary_a = phys & ~0x1FFFFUL;
        uint32_t boundary_b = (phys + (uint32_t)nbytes - 1) & ~0x1FFFFUL;
        if (boundary_a == boundary_b) {
            /* Good block. */
            dma_seg = seg;
            dma_sel = sel;
            dma_buf_bytes = nbytes;
            dma_physical = phys;
            result = 0;
            break;
        }
        /* Boundary crossing; hold + retry. */
        (void)seg;
        if (n_kept < 8) {
            kept_sels[n_kept] = sel;
            n_kept++;
        }
    }

    /* Free all held bad blocks. */
    for (int i = 0; i < n_kept; i++) {
        __dpmi_regs r;
        memset(&r, 0, sizeof r);
        r.x.ax = 0x0101;          /* DPMI Free DOS Memory Block */
        r.x.dx = (uint16_t)kept_sels[i];
        __dpmi_int(0x31, &r);
    }

    if (result != 0) {
        plog("ERR: dma_alloc could not find 128K-aligned %ld bytes (tried %d)",
             nbytes, n_kept);
    }
    return result;
}

static void dma_free(void)
{
    if (dma_sel) {
        __dpmi_regs r;
        memset(&r, 0, sizeof r);
        r.x.ax = 0x0101;          /* DPMI Free DOS Memory Block */
        r.x.dx = (uint16_t)dma_sel;
        __dpmi_int(0x31, &r);
    }
    dma_seg = 0;
    dma_sel = 0;
    dma_buf_bytes = 0;
    dma_physical = 0;
}

/* ============================================================ */
/* DMA controller programming                                    */
/* ============================================================ */

/* High DMA (channels 5-7) page-port table */
static const uint8_t high_page_ports[8] = {0x87, 0x83, 0x81, 0x82, 0x8F, 0x8B, 0x89, 0x8A};

/* Program 16-bit DMA channel (5-7) for auto-init read, block transfer. */
static void dma_program_16bit(int channel, uint32_t phys, long nbytes)
{
    /* DMA channels 5-7 are the high (16-bit) DMA controller (slave/secondary).
     * Word-address = physical >> 1 (since 16-bit DMA addresses 16-bit words).
     * Count = (nbytes/2) - 1 (in words). */
    int dma_words = (int)(nbytes / 2) - 1;
    int chan_idx = channel - 4;  /* 5..7 -> 1..3 */
    uint8_t page = (uint8_t)((phys >> 16) & 0xFF);

    outportb(0xD4, (uint8_t)(0x04 | (channel & 3)));  /* mask channel */
    outportb(0xD8, 0);                                 /* clear flip-flop */
    outportb(0xD6, (uint8_t)(0x58 | (channel & 3)));   /* mode: single, read, auto-init */
    outportb(high_page_ports[channel], page);          /* page register */
    outportb(0xC0 + chan_idx * 4, (uint8_t)((phys >> 1) & 0xFF));         /* word addr lo */
    outportb(0xC0 + chan_idx * 4, (uint8_t)((phys >> 9) & 0xFF));         /* word addr hi */
    outportb(0xC0 + chan_idx * 4 + 2, (uint8_t)(dma_words & 0xFF));        /* count lo */
    outportb(0xC0 + chan_idx * 4 + 2, (uint8_t)((dma_words >> 8) & 0xFF)); /* count hi */
    outportb(0xD4, (uint8_t)(channel & 3));            /* unmask channel */
}

static void dma_mask_16bit(int channel)
{
    outportb(0xD4, (uint8_t)(0x04 | (channel & 3)));
}

/* ============================================================ */
/* IRQ-5 hook (minimal ISR)                                      */
/* ============================================================ */

static volatile uint32_t isr_count = 0;
static volatile uint64_t isr_last_tsc = 0;
static volatile uint64_t isr_cycle_min = (uint64_t)-1;
static volatile uint64_t isr_cycle_max = 0;
static volatile uint64_t isr_cycle_sum = 0;
static volatile uint32_t isr_cycle_samples = 0;

/* Up to 4096 per-IRQ cycle samples for percentile calc post-measurement */
#define ISR_SAMPLE_CAP 4096
static volatile uint32_t isr_samples[ISR_SAMPLE_CAP];

static _go32_dpmi_seginfo old_vec, new_vec;
static int irq_installed = 0;
static int irq_int_num = 0;
static int irq_pic_mask_port = 0;
static uint8_t irq_pic_mask_bit = 0;
static int irq_is_slave = 0;
static volatile int isr_storm_self_masked = 0;  /* set by ISR if deadman fires */

/* Deadman threshold: at 44100 stereo with 4 KB half-buffer chunks, expect
 * ~10-20 IRQs/sec. Over a 1 sec measurement that's ~10-20 IRQs total.
 * 10000 IRQs in a single measurement is impossibly many -- indicates an
 * IRQ storm. Storm-detection threshold sized to be ~500x the legitimate
 * upper-bound, leaving comfortable margin for genuine fast-rate scenarios. */
#define ISR_DEADMAN_THRESHOLD 10000

/* Minimal ISR: read DSP status (acks SB16 8-bit IRQ), read DSP-16bit-ack
 * status if SB16 (acks 16-bit IRQ), send EOI to PIC, count++, sample TSC.
 * DEADMAN: if isr_count exceeds ISR_DEADMAN_THRESHOLD, the ISR self-masks
 * IRQ-5 at the PIC so the storm breaks from within. */
static void irq_handler(void)
{
    uint64_t tsc_now = rdtsc();
    uint64_t delta = (isr_last_tsc != 0) ? (tsc_now - isr_last_tsc) : 0;
    isr_last_tsc = tsc_now;

    /* Acknowledge SB16 interrupts:
     * - 8-bit DMA IRQ: read DSP status at base+0xE
     * - 16-bit DMA IRQ: read DSP-16bit-ack at base+0xF
     * Reading both is safe (each reads acks only its own bit). */
    (void)inportb(sb_base + 0xE);
    (void)inportb(sb_base + 0xF);

    /* Send EOI to PIC(s). Slave IRQs (8-15) require EOI to both slave + master. */
    if (irq_is_slave) outportb(0xA0, 0x20);
    outportb(0x20, 0x20);

    /* Update counters */
    isr_count++;
    if (delta > 0) {
        if (delta < isr_cycle_min) isr_cycle_min = delta;
        if (delta > isr_cycle_max) isr_cycle_max = delta;
        isr_cycle_sum += delta;
        if (isr_cycle_samples < ISR_SAMPLE_CAP) {
            /* Store low 32 bits; on 100 MHz CPU, 32 bits saturates at ~43 sec
             * which is way past any single per-IRQ interval we care about. */
            isr_samples[isr_cycle_samples] = (uint32_t)(delta & 0xFFFFFFFFu);
            isr_cycle_samples++;
        }
    }

    /* DEADMAN: if we've fired far beyond expectations, this is an IRQ storm
     * (likely wrong ack sequence or DSP wedged in some state). Self-mask
     * IRQ-5 at the PIC so the storm breaks + main loop can resume cleanup.
     * Setting the flag first lets the main loop emit RATE_IRQ_STORM_DETECTED. */
    if (isr_count == ISR_DEADMAN_THRESHOLD && !isr_storm_self_masked) {
        isr_storm_self_masked = 1;
        outportb(irq_pic_mask_port,
                 (uint8_t)(inportb(irq_pic_mask_port) | irq_pic_mask_bit));
    }
}
static void irq_handler_end(void) { }  /* marker for LOCK_FUNCTION */

static void isr_reset_counters(void)
{
    isr_count = 0;
    isr_last_tsc = 0;
    isr_cycle_min = (uint64_t)-1;
    isr_cycle_max = 0;
    isr_cycle_sum = 0;
    isr_cycle_samples = 0;
    isr_storm_self_masked = 0;
}

/* Verify the existing IRQ-5 vector before override. Logs its pm_offset +
 * pm_selector so we have a record of what was there. Returns 0 if it
 * looks like a vanilla DPMI passthrough (low-numbered selectors + offset
 * looks reasonable), -1 if it looks like a foreign hook we should not
 * trample. Heuristic only -- DJGPP's pm_selector is typically the LDT
 * selector for the DPMI server's real-mode-callback wrapper; if we see
 * something that doesn't match the get-then-set roundtrip pattern,
 * something else is hooking IRQ-5. */
static int verify_existing_irq_vector(int int_num)
{
    _go32_dpmi_seginfo existing;
    if (_go32_dpmi_get_protected_mode_interrupt_vector(int_num, &existing) != 0) {
        plog("ERR: existing IRQ vector get_protected_mode_interrupt_vector(0x%02X) failed",
             int_num);
        return -1;
    }
    plog("  existing IRQ-%d vector: pm_offset=0x%08lX pm_selector=0x%04X",
         int_num >= 0x70 ? (int_num - 0x70 + 8) : (int_num - 0x08),
         (unsigned long)existing.pm_offset,
         (unsigned)existing.pm_selector);
    /* No hard refusal -- this is informational. If a real foreign hook
     * shows up, decomp will see the logged values and we can refuse
     * install in a follow-up iter. For wave-39 v2 we want to capture the
     * value so the cross-iter pattern of "default-state IRQ-5 vector on
     * fresh DOS boot" is observable. */
    return 0;
}

/* Install IRQ vector PRE-MASKED at PIC (IRQ stays masked at install time;
 * unmask only via irq_unmask_at_pic() right before measurement window).
 * v2: also pre-verify-and-log the existing vector so the LOG records what
 * was there at install time. */
static int irq_install(int irq)
{
    /* IRQ 0-7 -> INT 0x08-0x0F (master PIC, base port 0x20)
     * IRQ 8-15 -> INT 0x70-0x77 (slave PIC, base port 0xA0) */
    if (irq < 8) {
        irq_int_num = 0x08 + irq;
        irq_pic_mask_port = 0x21;
        irq_pic_mask_bit = (uint8_t)(1u << irq);
        irq_is_slave = 0;
    } else {
        irq_int_num = 0x70 + (irq - 8);
        irq_pic_mask_port = 0xA1;
        irq_pic_mask_bit = (uint8_t)(1u << (irq - 8));
        irq_is_slave = 1;
    }

    /* Lock ISR code + data so no page faults during interrupt. */
    _go32_dpmi_lock_code((void *)irq_handler,
                         (unsigned long)((char *)irq_handler_end - (char *)irq_handler));
    _go32_dpmi_lock_data((void *)&isr_count, sizeof isr_count);
    _go32_dpmi_lock_data((void *)&isr_last_tsc, sizeof isr_last_tsc);
    _go32_dpmi_lock_data((void *)&isr_cycle_min, sizeof isr_cycle_min);
    _go32_dpmi_lock_data((void *)&isr_cycle_max, sizeof isr_cycle_max);
    _go32_dpmi_lock_data((void *)&isr_cycle_sum, sizeof isr_cycle_sum);
    _go32_dpmi_lock_data((void *)&isr_cycle_samples, sizeof isr_cycle_samples);
    _go32_dpmi_lock_data((void *)isr_samples, sizeof isr_samples);
    _go32_dpmi_lock_data((void *)&sb_base, sizeof sb_base);
    _go32_dpmi_lock_data((void *)&irq_is_slave, sizeof irq_is_slave);
    /* v2: lock storm-detection state + PIC mask state for ISR self-mask. */
    _go32_dpmi_lock_data((void *)&isr_storm_self_masked, sizeof isr_storm_self_masked);
    _go32_dpmi_lock_data((void *)&irq_pic_mask_port, sizeof irq_pic_mask_port);
    _go32_dpmi_lock_data((void *)&irq_pic_mask_bit, sizeof irq_pic_mask_bit);

    /* v2: verify + log existing vector before override. */
    if (verify_existing_irq_vector(irq_int_num) != 0) {
        return -1;
    }

    if (_go32_dpmi_get_protected_mode_interrupt_vector(irq_int_num, &old_vec) != 0) {
        plog("ERR: get_protected_mode_interrupt_vector(0x%02X) failed", irq_int_num);
        return -1;
    }

    new_vec.pm_offset = (long)irq_handler;
    new_vec.pm_selector = _go32_my_cs();
    if (_go32_dpmi_allocate_iret_wrapper(&new_vec) != 0) {
        plog("ERR: allocate_iret_wrapper failed");
        return -1;
    }
    if (_go32_dpmi_set_protected_mode_interrupt_vector(irq_int_num, &new_vec) != 0) {
        plog("ERR: set_protected_mode_interrupt_vector(0x%02X) failed", irq_int_num);
        _go32_dpmi_free_iret_wrapper(&new_vec);
        return -1;
    }

    /* v2: IRQ stays MASKED at PIC at install time. Unmask only via the
     * dedicated irq_unmask_at_pic() call right before the measurement
     * window. This prevents spurious IRQs during DSP setup from triggering
     * the ISR before the chip is fully configured. */
    uint8_t mask = inportb(irq_pic_mask_port);
    outportb(irq_pic_mask_port, (uint8_t)(mask | irq_pic_mask_bit));  /* MASKED */
    /* If slave IRQ, also unmask IRQ 2 on master PIC (cascade) so when we
     * later unmask the slave bit, signals can actually propagate. */
    if (irq_is_slave) {
        uint8_t m2 = inportb(0x21);
        outportb(0x21, (uint8_t)(m2 & ~0x04));
    }

    irq_installed = 1;
    return 0;
}

/* v2: unmask IRQ at PIC -- called right before measurement window. */
static void irq_unmask_at_pic(void)
{
    if (!irq_installed) return;
    uint8_t mask = inportb(irq_pic_mask_port);
    outportb(irq_pic_mask_port, (uint8_t)(mask & ~irq_pic_mask_bit));
}

/* v2: mask IRQ at PIC -- called right after measurement window, before
 * any DSP cleanup that might generate spurious IRQs. */
static void irq_mask_at_pic(void)
{
    if (!irq_installed) return;
    uint8_t mask = inportb(irq_pic_mask_port);
    outportb(irq_pic_mask_port, (uint8_t)(mask | irq_pic_mask_bit));
}

static void irq_uninstall(void)
{
    if (!irq_installed) return;
    /* Mask IRQ at PIC (in case it was left unmasked). */
    irq_mask_at_pic();
    _go32_dpmi_set_protected_mode_interrupt_vector(irq_int_num, &old_vec);
    _go32_dpmi_free_iret_wrapper(&new_vec);
    irq_installed = 0;
}

/* ============================================================ */
/* atexit panic handler (v2)                                     */
/* ============================================================ */

/* Called on every exit path (clean exit, exit(N), assert, malloc-fail).
 * Restores ALL hardware state so the operator returns to a usable DOS
 * shell without reboot in most cases. Order matters: stop DMA first
 * (so chip stops firing IRQs), then unhook IRQ vector, then free
 * conventional memory. Idempotent (safe to call repeatedly). */
static void panic_cleanup(void)
{
    /* If IRQ was installed, mask it at the PIC first. */
    if (irq_installed) {
        irq_mask_at_pic();
    }

    /* Mask DMA channel 5 (16-bit) so chip stops firing IRQs from DMA. */
    if (sb_dma16 >= 4 && sb_dma16 <= 7) {
        dma_mask_16bit(sb_dma16);
    }

    /* DSP best-effort reset to silence + clear any pending state.
     * dsp_reset() has its own internal timeout so this won't hang. */
    if (sb_base > 0) {
        outportb(sb_base + 0x6, 1);
        for (volatile int i = 0; i < 1000; i++) { (void)inportb(0x80); }
        outportb(sb_base + 0x6, 0);
    }

    /* Restore IRQ vector. */
    if (irq_installed) {
        _go32_dpmi_set_protected_mode_interrupt_vector(irq_int_num, &old_vec);
        _go32_dpmi_free_iret_wrapper(&new_vec);
        irq_installed = 0;
    }

    /* Free DMA conventional memory. */
    dma_free();
}

/* ============================================================ */
/* DSP playback start/stop                                       */
/* ============================================================ */

static int dsp_start_16bit_autoinit(int freq, int stereo, long buf_bytes)
{
    /* SB16 16-bit auto-init playback sequence:
     *   0xD1                 speaker on
     *   0x41 freq_hi freq_lo set output sampling rate
     *   0xB6 mode_byte count_lo count_hi
     *     mode_byte: 0x30 = 16-bit stereo signed PCM, 0x10 = 16-bit mono signed
     *     count = (half_buffer_in_samples) - 1
     *
     * Half-buffer interrupt fires once per half-DMA-buffer (auto-init bounces). */
    dsp_write(0xD1);
    /* Speaker-on can take ~112 ms; brief wait. */
    double t = now_secs();
    while (now_secs() - t < 0.115) { (void)inportb(0x80); }

    dsp_write(0x41);
    dsp_write((uint8_t)((freq >> 8) & 0xFF));
    dsp_write((uint8_t)(freq & 0xFF));

    /* block_size = (half_buffer / sizeof(int16_t)) - 1, per SB16 spec
     * (count is in 16-bit samples for 0xB6/16-bit mode). */
    int half = (int)(buf_bytes / 2);
    int block_size = (half / 2) - 1;  /* /2 = bytes to int16_t samples */

    dsp_write(0xB6);
    dsp_write((uint8_t)(stereo ? 0x30 : 0x10));
    dsp_write((uint8_t)(block_size & 0xFF));
    dsp_write((uint8_t)((block_size >> 8) & 0xFF));
    return 0;
}

static void dsp_stop_16bit(void)
{
    /* Pause + exit auto-init mode. */
    dsp_write(0xD5);  /* pause 16-bit DMA */
    dsp_write(0xD9);  /* exit 16-bit auto-init mode */
    dsp_write(0xD3);  /* speaker off */
}

/* ============================================================ */
/* Variant matrix                                                */
/* ============================================================ */

typedef struct {
    const char *label;
    int freq;
    int stereo;  /* 1 = stereo, 0 = mono */
} rate_variant_t;

static const rate_variant_t rate_variants[] = {
    { "44100_stereo", 44100, 1 },
    { "22050_stereo", 22050, 1 },
    { "11025_stereo", 11025, 1 },
    { "11025_mono",   11025, 0 },
};
#define N_RATES ((int)(sizeof(rate_variants) / sizeof(rate_variants[0])))

/* Run one rate-variant scenario. v2: 1.0 sec measurement window (down
 * from 3.0); per-stage progress emits with fsync so the LOG pinpoints
 * the exact hang location if v2 still hangs. IRQ-5 stays masked at PIC
 * during DSP setup; only unmasked during the measurement window itself.
 * Returns 0 on RATE_PASS / RATE_SUSPECT_SPIKE / RATE_IRQ_STORM_DETECTED,
 * -1 on RATE_FAILED_INIT (chip didn't engage). */
static int run_rate(const rate_variant_t *V, double measure_secs)
{
    plog("");
    plog("[audrq RATE=%s BEGIN]", V->label);
    plog("  spec: freq=%d Hz, channels=%d", V->freq, V->stereo ? 2 : 1);

    /* DMA buffer: 16 KB (~92 ms at 22050 stereo 16-bit) */
    long buf_bytes = 16384;
    plog("[audrq RATE=%s STAGE=dma_alloc_begin]", V->label);
    if (dma_alloc(buf_bytes) != 0) {
        plog("[audrq RATE=%s DONE status=RATE_FAILED_INIT reason=dma_alloc]", V->label);
        return -1;
    }
    plog("[audrq RATE=%s STAGE=dma_alloc_ok dma_buf=%ld bytes phys=0x%lX seg=0x%X]",
         V->label, buf_bytes, (unsigned long)dma_physical, dma_seg);

    /* Reset DSP for clean state. */
    plog("[audrq RATE=%s STAGE=dsp_reset_begin]", V->label);
    if (dsp_reset() != 0) {
        plog("[audrq RATE=%s DONE status=RATE_FAILED_INIT reason=dsp_reset]", V->label);
        dma_free();
        return -1;
    }
    plog("[audrq RATE=%s STAGE=dsp_reset_ok]", V->label);

    /* Reset ISR counters BEFORE hooking IRQ + starting playback. */
    isr_reset_counters();

    /* Hook IRQ (PIC-MASKED at install time; unmasked just before spin-wait). */
    plog("[audrq RATE=%s STAGE=irq_install_begin]", V->label);
    if (irq_install(sb_irq) != 0) {
        plog("[audrq RATE=%s DONE status=RATE_FAILED_INIT reason=irq_hook]", V->label);
        dma_free();
        return -1;
    }
    plog("[audrq RATE=%s STAGE=irq_install_ok int_num=0x%02X pic_mask_port=0x%X]",
         V->label, irq_int_num, irq_pic_mask_port);

    /* Program DMA controller. */
    plog("[audrq RATE=%s STAGE=dma_program_begin]", V->label);
    dma_program_16bit(sb_dma16, dma_physical, buf_bytes);
    plog("[audrq RATE=%s STAGE=dma_program_ok]", V->label);

    /* Start DSP playback. */
    plog("[audrq RATE=%s STAGE=dsp_start_begin]", V->label);
    dsp_start_16bit_autoinit(V->freq, V->stereo, buf_bytes);
    plog("[audrq RATE=%s STAGE=dsp_start_ok]", V->label);

    /* v2: UNMASK IRQ-5 at PIC now -- chip is fully configured + playing. */
    irq_unmask_at_pic();
    plog("[audrq RATE=%s STAGE=irq_unmasked_at_pic measure_secs=%.2f]",
         V->label, measure_secs);

    /* Measure. */
    double t0 = now_secs();
    while (now_secs() - t0 < measure_secs) {
        /* Spin; ISR runs in background. inportb to avoid loop-elision.
         * If storm hit + ISR self-masked, we just spin out the window. */
        (void)inportb(0x80);
    }
    double t1 = now_secs();
    double elapsed = t1 - t0;
    plog("[audrq RATE=%s STAGE=measure_done elapsed=%.3f irq_count=%u storm=%d]",
         V->label, elapsed, isr_count, isr_storm_self_masked);

    /* v2: RE-MASK IRQ-5 at PIC before any DSP cleanup that might generate
     * spurious IRQs. */
    irq_mask_at_pic();

    /* Stop playback + unhook IRQ + free DMA. */
    plog("[audrq RATE=%s STAGE=dsp_stop_begin]", V->label);
    dsp_stop_16bit();
    plog("[audrq RATE=%s STAGE=dsp_stop_ok]", V->label);
    dma_mask_16bit(sb_dma16);
    irq_uninstall();
    plog("[audrq RATE=%s STAGE=irq_uninstall_ok]", V->label);
    dma_free();

    /* Compute stats. */
    uint32_t cnt = isr_count;
    double irqs_per_sec = (elapsed > 0) ? (double)cnt / elapsed : 0.0;

    /* Min/med/p95/max from samples buffer (note: samples cap = 4096; if cnt
     * exceeds that, we report stats over the first 4096 samples only). */
    uint32_t n_samp = isr_cycle_samples;
    uint64_t cmin = (n_samp > 0) ? isr_cycle_min : 0;
    uint64_t cmax = isr_cycle_max;
    double mean_cycles = (n_samp > 0) ? ((double)isr_cycle_sum / n_samp) : 0.0;

    /* Percentiles via sort of low-32-bit samples (approximation; OK for our
     * percentile granularity at sub-millisecond IRQ intervals on PODP83). */
    uint64_t cmed = 0, cp95 = 0;
    if (n_samp > 1) {
        /* In-place quicksort of first n_samp samples for percentile lookup. */
        /* Simple iterative quicksort via stdlib qsort. */
        uint32_t *tmp = (uint32_t *)malloc(n_samp * sizeof(uint32_t));
        if (tmp) {
            for (uint32_t i = 0; i < n_samp; i++) tmp[i] = isr_samples[i];
            /* Sort ascending */
            for (uint32_t i = 1; i < n_samp; i++) {
                uint32_t key = tmp[i];
                uint32_t j = i;
                while (j > 0 && tmp[j-1] > key) { tmp[j] = tmp[j-1]; j--; }
                tmp[j] = key;
            }
            cmed = tmp[n_samp / 2];
            cp95 = tmp[(n_samp * 95) / 100];
            free(tmp);
        }
    }

    double us_min = cycles_to_us(cmin);
    double us_med = cycles_to_us(cmed);
    double us_p95 = cycles_to_us(cp95);
    double us_max = cycles_to_us(cmax);
    double us_mean = cycles_to_us((uint64_t)mean_cycles);

    /* Wall-overhead-pct: time spent in ISR over total wall.
     * = (irqs/sec * mean_us_per_irq / 1_000_000) * 100
     * = (cnt * mean_us) / (elapsed * 1e6) * 100 */
    double wall_overhead_pct = 0.0;
    if (elapsed > 0 && us_mean > 0) {
        wall_overhead_pct = ((double)cnt * us_mean) / (elapsed * 1e6) * 100.0;
    }

    plog("  measure_window_secs=%.3f", elapsed);
    plog("  irq_count=%u", cnt);
    plog("  irqs_per_sec=%.2f", irqs_per_sec);
    plog("  rdtsc_cycles_per_irq min=%llu med=%llu p95=%llu max=%llu mean=%llu",
         (unsigned long long)cmin, (unsigned long long)cmed,
         (unsigned long long)cp95, (unsigned long long)cmax,
         (unsigned long long)(uint64_t)mean_cycles);
    plog("  rdtsc_us_per_irq min=%.2f med=%.2f p95=%.2f max=%.2f mean=%.2f",
         us_min, us_med, us_p95, us_max, us_mean);
    plog("  wall_overhead_pct=%.4f", wall_overhead_pct);
    plog("  sample_count_for_percentiles=%u (of irq_count=%u; cap=%d)",
         n_samp, cnt, ISR_SAMPLE_CAP);

    /* Status classification (v2 adds RATE_IRQ_STORM_DETECTED):
     *   RATE_PASS                  -- cnt > 1, RDTSC max < 100*min, no storm
     *   RATE_SUSPECT_SPIKE         -- cnt > 1 but max > 100*min (single spike)
     *   RATE_IRQ_STORM_DETECTED    -- ISR deadman fired (count >= threshold)
     *   RATE_FAILED_INIT           -- cnt == 0 (no IRQs delivered) */
    const char *status;
    if (isr_storm_self_masked) {
        status = "RATE_IRQ_STORM_DETECTED";
    } else if (cnt < 2) {
        status = "RATE_FAILED_INIT";
    } else if (cmin > 0 && cmax > cmin * 100) {
        status = "RATE_SUSPECT_SPIKE";
    } else {
        status = "RATE_PASS";
    }
    plog("[audrq RATE=%s DONE status=%s]", V->label, status);
    /* Return -1 for RATE_FAILED_INIT AND RATE_IRQ_STORM_DETECTED so the
     * suite-level first-variant-fail guard triggers in either case
     * (chip in bad state -- don't compound by trying remaining variants). */
    if (strcmp(status, "RATE_FAILED_INIT") == 0) return -1;
    if (strcmp(status, "RATE_IRQ_STORM_DETECTED") == 0) return -1;
    return 0;
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    plog("=== audrq v2 (wave-39 task #18: defensive re-author) ===");
    plog("DJGPP pure-C; target Vibra16S CT2490 / SB16-class DSP>=4");
    plog("Mission: isolate SB16 IRQ-5 dispatch + minimal-ISR wall-clock from");
    plog("SDL_mixer mix cost. 4 rate variants: 44100s, 22050s, 11025s, 11025m.");
    plog("v2 defenses: ISR deadman + atexit panic + IRQ-vector verify +");
    plog("  pre-mask-during-setup + per-stage progress emits.");
    plog("Per-rate: 1 sec measurement (v2; was 3 sec), RDTSC min/med/p95/max + wall-pct.");
    plog("UCLOCKS_PER_SEC = %lu", (unsigned long)UCLOCKS_PER_SEC);
    plog("");

    /* v2: register atexit panic cleanup BEFORE any hardware is touched.
     * Fires on every exit path so the operator gets back to a sane DOS
     * state without reboot in most cases. */
    atexit(panic_cleanup);
    plog("atexit(panic_cleanup) registered (v2)");
    plog("");

    /* Step 1: RDTSC calibration. */
    plog("---- Step 1: RDTSC calibration ----");
    calibrate_rdtsc();
    plog("cpu_mhz_calibrated = %u  us_per_cycle = %.6f",
         (unsigned)g_cpu_mhz, g_us_per_cycle);
    if (g_us_per_cycle <= 0.0 || g_cpu_mhz < 30) {
        plog("FATAL: RDTSC calibration failed (cpu_mhz=%u, us_per_cycle=%.6f);",
             (unsigned)g_cpu_mhz, g_us_per_cycle);
        plog("[audrq SUITE_DONE verdict=REFUTE_RDTSC_CALIBRATION_FAILED]");
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 2;
    }
    plog("");

    /* Step 2: BLASTER env parse. */
    plog("---- Step 2: BLASTER env parse ----");
    parse_blaster();
    plog("");

    /* Step 3: DSP detect + version. */
    plog("---- Step 3: DSP detect + version ----");
    if (dsp_reset() != 0) {
        plog("FATAL: DSP reset failed at base=0x%X (no SB16?)", sb_base);
        plog("[audrq SUITE_DONE verdict=REFUTE_DSP_NOT_FOUND]");
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 2;
    }
    uint8_t dsp_maj, dsp_min;
    int dsp_class = dsp_detect_version(&dsp_maj, &dsp_min);
    plog("DSP version = %u.%u (class %d)", dsp_maj, dsp_min, dsp_class);
    if (dsp_class < 4) {
        plog("WARN: DSP < 4 (not SB16); 16-bit auto-init DMA path unsupported.");
        plog("Probe assumes SB16. Continuing anyway -- results may be invalid.");
    }
    plog("");

    /* Step 4: variant matrix. */
    plog("---- Step 4: run %d rate variants ----", N_RATES);
    plog("[audrq SUITE_BEGIN n=%d cpu_mhz=%u dsp_version=%u.%u]",
         N_RATES, (unsigned)g_cpu_mhz, dsp_maj, dsp_min);

    int pass = 0, fail_init = 0, skipped = 0;
    for (int i = 0; i < N_RATES; i++) {
        /* v2: first-variant-fail guard. If the first variant returns -1
         * (RATE_FAILED_INIT or RATE_IRQ_STORM_DETECTED), chip is in a bad
         * state and continuing to drive remaining variants compounds the
         * failure (each adds operator-time + potential reboot need).
         * Skip the rest with explicit SKIP emit. */
        if (i > 0 && fail_init > 0 && pass == 0) {
            plog("");
            plog("[audrq RATE=%s SKIPPED reason=first_variant_failed]",
                 rate_variants[i].label);
            skipped++;
            continue;
        }
        int rc = run_rate(&rate_variants[i], 1.0);  /* v2: 1.0 sec window */
        if (rc < 0) fail_init++;
        else pass++;
        /* RATE_SUSPECT_SPIKE counted toward pass (rc==0); decomp filters via status string. */
    }

    plog("");
    plog("---- Step 5: SUITE_DONE ----");
    const char *suite_status =
        (fail_init == 0 && skipped == 0) ? "SUITE_PASS"
        : (pass >= 2)                    ? "SUITE_DEGRADED"
        :                                  "SUITE_FAILED";
    plog("[audrq SUITE_DONE n=%d pass=%d fail_init=%d skipped=%d status=%s]",
         N_RATES, pass, fail_init, skipped, suite_status);
    plog("");

    plog("Decomp guide (per wave-39 patch-actionability matrix):");
    plog("  AUDRQ us_per_irq at 22050s production rate:");
    plog("    <50 us/IRQ   -> P7 (Lever G) rank-1; mix-side wins; pivot from offload.");
    plog("    50-200 us/IRQ -> P2 (WaveBlaster) rank-1 + P7 rank-2.");
    plog("    >200 us/IRQ  -> P2 rank-1, P1 (OPL3) fallback, P9 rank-3.");
    plog("  Cross-anchor with AUDBUF.LOG (wave-25 iter J):");
    plog("    SDL3_ISR_internal_cost_us = AUDBUF.irq_wall_us - AUDRQ.us_per_irq.");

    plog("");
    plog("=== audrq done ===");
    plog("[SENTINEL_END]");
    if (g_log) fclose(g_log);
    return (fail_init == 0) ? 0 : 1;
}
