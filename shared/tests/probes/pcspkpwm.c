/*
 * pcspkpwm.c -- PC-speaker PWM-DAC CPU-steal + ear-check probe.
 *
 * Campaign 2 Phase 2 (P2.0), task #21. Gates the multi-day shared-PIT-ch0
 * PWM-SFX implementation in docs/internal/ADLIB-PCSPK-SFX-DESIGN.md (sec.4.3).
 * The probe MEASURES the real per-fire interrupt cost on g2k BEFORE we commit
 * the build; the high-rate-ISR cost on real HW is exactly the class of
 * prediction that is 5-30x wrong (perf_measurement_discipline). The probe
 * MEASURES -- it asserts no fps number it did not observe.
 *
 * WHAT THE REAL DESIGN DOES (sec.2.2), faithfully reproduced here:
 *   ch0 runs at the PCM sample rate. Per IRQ-0 fire the ISR:
 *     (A) outputs ONE ~6-bit PWM sample to PIT ch2 (port 0x42); speaker is
 *         routed via 0x61 |= 0x03 so the ch2 OUT pin drives the cone.
 *     (B) advances a monotonic us->ms accumulator (no in-ISR 64-bit divide).
 *     (C) derives the music tick every Mth fire (M ~= pcm_rate/music_hz) via a
 *         second fractional accumulator (~120 Hz), doing representative bounded
 *         work in that branch (stand-in for MidiScheduler::tick_isr -- a handful
 *         of ISA-latency port touches; NO engine, NO OPL chip dependency).
 *     (D) chains the BIOS INT 8 at the divided rate (s_bios_accum += div; chain
 *         when it rolls 65536) so DOS time-of-day stays correct REGARDLESS of
 *         the ch0 rate; EOIs the master PIC itself on non-chained fires.
 *   This is the SDL/0110 ISR shape (pushfl+lcall BIOS chain idiom) extended
 *   with the PWM-output (A) + music-derive (C) steps the P2.1 design adds.
 *
 * WHAT IT MEASURES (the decisive 50fps-risk number):
 *   stolen_pct = (1 - count_on/count_off) * 100, per rate, where count_* is the
 *   number of passes of a fixed L1-resident compute kernel completed in a fixed
 *   wall window. The wall reference is the BIOS tick at 0040:006C, which the
 *   BIOS-chain (D) keeps at 18.2065 Hz under BOTH the ISR-off baseline and the
 *   ISR-on (reprogrammed-ch0) measurement -- so it is a valid common clock.
 *
 * Cells (cmdline rates, else the default sweep): 6000, 8000, 11025 Hz.
 *   PCSPKPWM.EXE              -> runs all three default cells
 *   PCSPKPWM.EXE 6000        -> single cell
 *   PCSPKPWM.EXE 6000 8000   -> chosen cells
 *
 * EAR-CHECK: each cell plays a ~440 Hz sine through the PWM DAC (consistent
 * pitch across rates via a Q16 phase accumulator) so the operator judges the
 * 6-bit PWM quality + how audible the PWM carrier buzz is at each rate.
 *
 * SAFETY / RESTORE (mandatory -- same quit-path risk as SDL/0110, now also ch2
 * + 0x61): atexit + normal teardown both restore PIT ch0 (divisor 0 ->
 * 18.2065 Hz), restore ch2 + port 0x61 (speaker off), and unhook IRQ-0 WITHOUT
 * masking it (a left-reprogrammed ch0 = fast DOS clock; a left-on speaker = a
 * stuck tone). A post-restore self-check latches ch0 and confirms the divisor
 * is back to ~65536 (count > 1000), and the report instructs the operator to
 * verify the DOS clock with TIME.
 *
 * DERIVED-METRIC SANITY (SDLPROBE post-mortem): the probe self-checks the
 * observed ISR fire-rate against the requested rate (+/-25%), bounds stolen_pct
 * plausibility, and refuses to emit a verdict if the ISR never fired.
 *
 * Output: PCSPKPWM.LOG (C:\ then CWD fallback), per-line fsync.
 * Pure DJGPP libc + DPMI. No SDL, no engine, no C++.
 * 8.3 DOS filename: PCSPKPWM.EXE / PCSPKPWM.LOG (8+3).
 *
 * DOSBox-X smoke is correctness-only -- emulated PIT/speaker timing is NOT a
 * real-HW perf proxy (dosbox_not_proxy); stolen_pct under DOSBox is fictitious,
 * the gate is structure + sentinels + clean restore.
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
#include <math.h>
#include <unistd.h>
#include <sys/farptr.h>

#ifndef PCSPK_SHA12
#define PCSPK_SHA12 "nostamp"
#endif
#define PCSPK_VERSION "v1"

#define PIT_INPUT_HZ      1193182UL   /* 8253 PIT input clock */
#define MUSIC_HZ          120         /* derived music-tick cadence (SDL/0110) */
#define TONE_HZ           440         /* ear-check sine pitch (rate-independent) */
#define TABLE_SIZE        64          /* sine table length */
#define WINDOW_TICKS      55          /* ~3.02 s measurement window (BIOS 18.2 Hz) */
#define EARCHECK_TICKS    27          /* ~1.48 s extra tone-only listen */
#define KERNEL_INTS       512         /* 2 KB src + 2 KB dst -> L1-resident */
#define RATE_MIN          4000
#define RATE_MAX          11025

/* ============================================================ */
/* Logging (per-line fsync, mirrors irqrate/audrq)              */
/* ============================================================ */

static FILE *g_log = NULL;

static void rlog(const char *fmt, ...)
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
    g_log = fopen("C:\\PCSPKPWM.LOG", "w");
    if (!g_log) g_log = fopen("PCSPKPWM.LOG", "w");
}

/* ============================================================ */
/* ISR-touched state (all DPMI-locked at install)              */
/* ============================================================ */

/* Far pointer (offset:selector) to the saved original protected-mode INT 8
   handler, for the manual every-Nth chain. Layout matches gas `lcall *mem`
   (32-bit offset then 16-bit selector) -- identical to SDL/0110. */
static volatile struct { uint32_t off; uint16_t sel; } s_old_int8_vec;

static volatile uint32_t s_pit_div      = 0;   /* our PIT ch0 divisor */
static volatile uint32_t s_us_per_tick  = 0;   /* microseconds per fast tick */
static volatile uint32_t s_us_frac      = 0;   /* sub-ms us accumulator */
static volatile uint32_t s_now_ms       = 0;   /* monotonic ms (clock B) */
static volatile uint32_t s_bios_accum   = 0;   /* toward the 18.2 Hz BIOS tick */
static volatile uint32_t s_music_accum  = 0;   /* toward the ~120 Hz music tick */
static volatile uint32_t s_pcm_rate     = 0;   /* this cell's sample rate */
static volatile uint32_t s_phase_acc    = 0;   /* Q16 sine phase */
static volatile uint32_t s_phase_step   = 0;   /* Q16 phase increment per fire */
static volatile uint32_t s_isr_fires    = 0;   /* total ISR entries (plausibility) */
static volatile uint32_t s_music_fires  = 0;   /* music-tick branch hits */
static volatile uint8_t  s_sine[TABLE_SIZE];   /* 6-bit PWM sine, centered 32 */

static _go32_dpmi_seginfo s_old_vec, s_new_vec;
static int     s_installed = 0;
static uint8_t s_old_port61 = 0;
static int     s_port61_saved = 0;

/* ============================================================ */
/* BIOS INT 8 chain (pushfl + lcall the saved PM vector). The   */
/* old handler issues its own EOI. Canonical DJGPP idiom; the   */
/* exact SDL/0110 OplPitChainOld. DPMI-locked.                  */
/* ============================================================ */
static void pwm_chain_old(void)
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "lcall *%0\n\t"
        :
        : "m"(s_old_int8_vec)
        : "memory", "cc");
}
static void pwm_chain_old_end(void) {}

/* ============================================================ */
/* IRQ-0 / INT 8 ISR -- the representative per-fire shape.      */
/* Entered via the DJGPP iret wrapper as a plain C function,    */
/* interrupts disabled. DPMI-locked.                            */
/* ============================================================ */
static void pwm_isr(void)
{
    s_isr_fires++;

    /* (A) OUTPUT ONE PWM SAMPLE FIRST (deadline-critical, bounded).
       Advance the Q16 sine phase and write the 6-bit sample to ch2 reload. */
    s_phase_acc += s_phase_step;
    {
        uint8_t idx = (uint8_t)((s_phase_acc >> 16) & (TABLE_SIZE - 1));
        outportb(0x42, s_sine[idx]);   /* ch2 mode-0 reload = PWM duty */
    }

    /* (B) advance the monotonic clock by one PCM-sample period. */
    s_us_frac += s_us_per_tick;
    while (s_us_frac >= 1000u) {
        s_us_frac -= 1000u;
        s_now_ms++;
    }

    /* (C) DERIVE THE MUSIC TICK every Mth fire (~120 Hz independent of rate).
       Representative bounded work stands in for MidiScheduler::tick_isr: a
       handful of ISA-latency port touches (port 0x80 = harmless POST port),
       sized like a dozen OPL register pokes. Fires ~120x/sec, rate-agnostic. */
    s_music_accum += MUSIC_HZ;
    if (s_music_accum >= s_pcm_rate) {
        s_music_accum -= s_pcm_rate;
        s_music_fires++;
        {
            int k;
            for (k = 0; k < 12; k++) {
                (void)inportb(0x80);   /* ~1 us ISA read, OPL-poke stand-in */
                (void)inportb(0x80);
            }
        }
    }

    /* (D) preserve the BIOS 18.2065 Hz tick. Accumulate our divisor; each roll
       past 65536 runs the original INT 8 exactly once -> BIOS fires at
       PIT_INPUT/65536 == 18.2065 Hz EXACTLY, regardless of our rate. The old
       handler EOIs, so we do NOT EOI on a chained fire; else EOI ourselves. */
    s_bios_accum += s_pit_div;
    if (s_bios_accum >= 65536u) {
        s_bios_accum -= 65536u;
        pwm_chain_old();         /* BIOS INT 8 runs + EOIs */
    } else {
        outportb(0x20, 0x20);    /* master-PIC EOI (IRQ-0) */
    }
}
static void pwm_isr_end(void) {}

/* ============================================================ */
/* PIT ch0 raw count read (latch + lo/hi), for restore check    */
/* ============================================================ */
static uint16_t pit_ch0_count(void)
{
    outportb(0x43, 0x00);                /* latch counter 0 */
    uint8_t lo = inportb(0x40);
    uint8_t hi = inportb(0x40);
    return ((uint16_t)hi << 8) | lo;
}

/* ============================================================ */
/* Restore -- idempotent; atexit + normal teardown.             */
/* Order: restore ch0 (clock) first so any in-flight fire self- */
/* EOIs at the BIOS rate; then speaker off; then unhook vector. */
/* ============================================================ */
static void pwm_restore(void)
{
    if (s_installed) {
        /* Restore PIT ch0 to BIOS default divisor (0 -> 65536 -> 18.2065 Hz). */
        __asm__ __volatile__("cli");
        outportb(0x43, 0x36);
        outportb(0x40, 0x00);
        outportb(0x40, 0x00);
        __asm__ __volatile__("sti");

        /* Restore the original INT 8 vector + free the iret wrapper. IRQ-0 is
           NEVER masked (masking the system timer would freeze the DOS clock). */
        _go32_dpmi_set_protected_mode_interrupt_vector(0x08, &s_old_vec);
        _go32_dpmi_free_iret_wrapper(&s_new_vec);
        s_installed = 0;
    }

    /* Speaker off: restore port 0x61 (clears bits 0+1 we set) + reset ch2 to a
       benign mode-3 state so no stuck level remains on the cone. */
    if (s_port61_saved) {
        outportb(0x43, 0xB6);            /* ch2, lo+hi, mode 3 (benign) */
        outportb(0x42, 0x00);
        outportb(0x42, 0x00);
        outportb(0x61, s_old_port61);    /* speaker + gate back to entry state */
        s_port61_saved = 0;
    }
}

static void atexit_restore(void) { pwm_restore(); }

/* ============================================================ */
/* Install the ISR at `rate` Hz + arm the PWM speaker path.     */
/* Returns 0 on success.                                        */
/* ============================================================ */
static int pwm_install(uint32_t rate)
{
    uint32_t div = (uint32_t)(PIT_INPUT_HZ / rate);
    if (div < 1u)     div = 1u;
    if (div > 65535u) div = 65535u;

    /* Derive ISR state from the ACTUAL divisor (tempo tracks real fire rate). */
    s_pit_div     = div;
    s_us_per_tick = (uint32_t)(((uint64_t)div * 1000000ULL) / PIT_INPUT_HZ);
    s_us_frac     = 0;
    s_now_ms      = 0;
    s_bios_accum  = 0;
    s_music_accum = 0;
    s_pcm_rate    = rate;
    s_phase_acc   = 0;
    /* Q16 phase step for a constant TONE_HZ across rates:
       step = TONE_HZ * TABLE_SIZE / rate, in Q16. */
    s_phase_step  = (uint32_t)(((uint64_t)TONE_HZ * TABLE_SIZE << 16) / rate);
    s_isr_fires   = 0;
    s_music_fires = 0;

    /* Save + arm the speaker: 0x61 |= 0x03 (speaker on + ch2 gate on), then
       program ch2 mode 0 lobyte (0x90) so each 0x42 write is a PWM reload. */
    s_old_port61 = inportb(0x61);
    s_port61_saved = 1;
    outportb(0x61, (uint8_t)(s_old_port61 | 0x03));
    outportb(0x43, 0x90);                /* ch2, lobyte, mode 0, binary */
    outportb(0x42, 32);                  /* prime mid-rail (silence) */

    /* Save old INT 8 vector + install ours via the iret-wrapper REPLACE path
       (audrq pattern). We own the EOI + every-Nth chain. */
    if (_go32_dpmi_get_protected_mode_interrupt_vector(0x08, &s_old_vec) != 0) {
        rlog("ERR: get_protected_mode_interrupt_vector(0x08) failed");
        return -1;
    }
    s_old_int8_vec.off = (uint32_t)s_old_vec.pm_offset;
    s_old_int8_vec.sel = (uint16_t)s_old_vec.pm_selector;

    /* DPMI-lock the ISR + chain helper + ALL ISR-touched data BEFORE arming the
       fast rate so no page-fault can occur in interrupt context. */
    _go32_dpmi_lock_code((void *)pwm_isr,
                         (unsigned long)((char *)pwm_isr_end - (char *)pwm_isr));
    _go32_dpmi_lock_code((void *)pwm_chain_old,
                         (unsigned long)((char *)pwm_chain_old_end - (char *)pwm_chain_old));
    _go32_dpmi_lock_data((void *)&s_old_int8_vec, sizeof s_old_int8_vec);
    _go32_dpmi_lock_data((void *)&s_pit_div,     sizeof s_pit_div);
    _go32_dpmi_lock_data((void *)&s_us_per_tick, sizeof s_us_per_tick);
    _go32_dpmi_lock_data((void *)&s_us_frac,     sizeof s_us_frac);
    _go32_dpmi_lock_data((void *)&s_now_ms,      sizeof s_now_ms);
    _go32_dpmi_lock_data((void *)&s_bios_accum,  sizeof s_bios_accum);
    _go32_dpmi_lock_data((void *)&s_music_accum, sizeof s_music_accum);
    _go32_dpmi_lock_data((void *)&s_pcm_rate,    sizeof s_pcm_rate);
    _go32_dpmi_lock_data((void *)&s_phase_acc,   sizeof s_phase_acc);
    _go32_dpmi_lock_data((void *)&s_phase_step,  sizeof s_phase_step);
    _go32_dpmi_lock_data((void *)&s_isr_fires,   sizeof s_isr_fires);
    _go32_dpmi_lock_data((void *)&s_music_fires, sizeof s_music_fires);
    _go32_dpmi_lock_data((void *)s_sine,         sizeof s_sine);

    s_new_vec.pm_offset = (long)pwm_isr;
    s_new_vec.pm_selector = _go32_my_cs();
    if (_go32_dpmi_allocate_iret_wrapper(&s_new_vec) != 0) {
        rlog("ERR: allocate_iret_wrapper failed");
        return -1;
    }

    /* Program ch0: control 0x36 (ch0, lo+hi, mode 3, binary) + divisor, then
       point INT 8 at our wrapper -- all under cli (single owner of ch0). */
    __asm__ __volatile__("cli");
    if (_go32_dpmi_set_protected_mode_interrupt_vector(0x08, &s_new_vec) != 0) {
        __asm__ __volatile__("sti");
        rlog("ERR: set_protected_mode_interrupt_vector(0x08) failed");
        _go32_dpmi_free_iret_wrapper(&s_new_vec);
        return -1;
    }
    outportb(0x43, 0x36);
    outportb(0x40, (uint8_t)(div & 0xFF));
    outportb(0x40, (uint8_t)((div >> 8) & 0xFF));
    s_installed = 1;
    __asm__ __volatile__("sti");

    rlog("  installed: rate=%lu div=%lu us_per_tick=%lu chain_every=%lu fires "
         "(-> 18.2065 Hz BIOS) phase_step=%lu(Q16)",
         (unsigned long)rate, (unsigned long)div, (unsigned long)s_us_per_tick,
         (unsigned long)(65536u / div), (unsigned long)s_phase_step);
    return 0;
}

/* ============================================================ */
/* Busy kernel -- fixed L1-resident compute (not elidable).     */
/* ============================================================ */
static int32_t  g_ksrc[KERNEL_INTS];
static int32_t  g_kdst[KERNEL_INTS];
static volatile int32_t g_sink = 0;

static void kernel_once(void)
{
    int i;
    int32_t acc = g_sink;
    int32_t seed = acc + 0x9E3779B9;
    for (i = 0; i < KERNEL_INTS; i++) {
        int32_t v = g_ksrc[i] + seed;
        g_kdst[i] = v;
        acc ^= v;
        seed = (seed << 1) ^ v;
    }
    g_sink = acc;
}

/* Run the kernel for exactly WINDOW_TICKS BIOS ticks; return pass count.
   The BIOS tick at 0040:006C is the common wall clock (held at 18.2065 Hz by
   the ISR's BIOS-chain even when ch0 is reprogrammed). */
static uint32_t measure_passes(void)
{
    uint32_t t0 = _farpeekl(_dos_ds, 0x46C);
    uint32_t passes = 0;
    for (;;) {
        uint32_t now = _farpeekl(_dos_ds, 0x46C);
        if ((now - t0) >= (uint32_t)WINDOW_TICKS) break;
        kernel_once();
        passes++;
    }
    return passes;
}

/* Spin for `ticks` BIOS ticks doing nothing but letting the ISR play tone. */
static void spin_ticks(uint32_t ticks)
{
    uint32_t t0 = _farpeekl(_dos_ds, 0x46C);
    while ((_farpeekl(_dos_ds, 0x46C) - t0) < ticks) {
        /* idle; the ISR drives the speaker */
    }
}

/* ============================================================ */
/* main                                                         */
/* ============================================================ */
int main(int argc, char **argv)
{
    uint32_t rates[8];
    int n_rates = 0;
    int i;

    open_log();
    atexit(atexit_restore);

    rlog("=== PCSPKPWM %s (sha12=%s) -- PC-speaker PWM-DAC CPU-steal probe ===",
         PCSPK_VERSION, PCSPK_SHA12);
    rlog("Campaign 2 Phase-2 (P2.0) gate; design ADLIB-PCSPK-SFX-DESIGN.md sec.4.3.");
    rlog("Reproduces the SDL/0110-extended shared-ch0 ISR: PWM-out + music-derive");
    rlog("+ BIOS-chain. Measures stolen_pct = (1 - count_on/count_off)*100 / rate.");
    rlog("window=%d ticks (~%.2f s) earcheck=%d ticks; tone=%d Hz sine, 6-bit PWM.",
         WINDOW_TICKS, (double)WINDOW_TICKS / 18.2065, EARCHECK_TICKS, TONE_HZ);

    /* Parse optional rate cells; else the default 6k/8k/11k sweep. */
    for (i = 1; i < argc && n_rates < 8; i++) {
        long v = atol(argv[i]);
        if (v >= RATE_MIN && v <= RATE_MAX) rates[n_rates++] = (uint32_t)v;
        else rlog("  (ignoring out-of-band arg '%s'; band %d-%d Hz)",
                  argv[i], RATE_MIN, RATE_MAX);
    }
    if (n_rates == 0) {
        rates[n_rates++] = 6000;
        rates[n_rates++] = 8000;
        rates[n_rates++] = 11025;
        rlog("No cmdline rates -> default sweep: 6000 8000 11025 Hz.");
    }

    /* Build the 6-bit sine table (0..63, centered 32). Done at startup, never
       in-ISR. The low duty (sample/period) encodes amplitude via PWM. */
    for (i = 0; i < TABLE_SIZE; i++) {
        double s = sin(2.0 * M_PI * (double)i / (double)TABLE_SIZE);
        int v = (int)(32.0 + 31.0 * s + 0.5);
        if (v < 1)  v = 1;          /* keep low-time short rel. to period */
        if (v > 63) v = 63;
        s_sine[i] = (uint8_t)v;
    }

    /* Seed the kernel buffers deterministically. */
    for (i = 0; i < KERNEL_INTS; i++) g_ksrc[i] = (int32_t)(i * 2654435761u);

    /* ---- BASELINE: ISR OFF, PIT at default 18.2 Hz. ---- */
    rlog("");
    rlog("--- baseline (ISR OFF) ---");
    uint32_t bios_start = _farpeekl(_dos_ds, 0x46C);
    uint32_t count_off = measure_passes();
    rlog("BASELINE count_off=%lu passes over %d ticks", (unsigned long)count_off,
         WINDOW_TICKS);
    if (count_off == 0) {
        rlog("FATAL: baseline pass count is 0 -- BIOS tick not advancing? aborting.");
        if (g_log) fclose(g_log);
        return 2;
    }

    /* ---- PER-RATE: ISR ON. ---- */
    for (i = 0; i < n_rates; i++) {
        uint32_t rate = rates[i];
        rlog("");
        rlog("--- cell rate=%lu Hz (ISR ON) ---", (unsigned long)rate);

        if (pwm_install(rate) != 0) {
            rlog("PCSPKPWM rate=%lu STATUS=INSTALL_FAILED", (unsigned long)rate);
            pwm_restore();
            continue;
        }

        uint32_t count_on = measure_passes();
        uint32_t fires    = s_isr_fires;
        uint32_t mfires   = s_music_fires;

        /* Ear-check: keep the ISR running (tone audible) without measuring. */
        rlog("  ear-check: playing %d Hz PWM tone for ~%.2f s ...",
             TONE_HZ, (double)EARCHECK_TICKS / 18.2065);
        spin_ticks(EARCHECK_TICKS);

        pwm_restore();

        /* ---- derived metrics + plausibility (SDLPROBE post-mortem) ---- */
        double wall_s   = (double)WINDOW_TICKS / 18.2065;
        double obs_rate = (double)fires / wall_s;
        double obs_mrate= (double)mfires / wall_s;
        double stolen   = (1.0 - (double)count_on / (double)count_off) * 100.0;

        rlog("  count_on=%lu  fires=%lu  music_fires=%lu  wall=%.3f s",
             (unsigned long)count_on, (unsigned long)fires,
             (unsigned long)mfires, wall_s);
        rlog("  observed ISR rate=%.0f Hz (requested %lu)  music_rate=%.1f Hz (~%d expected)",
             obs_rate, (unsigned long)rate, obs_mrate, MUSIC_HZ);

        /* Sentinel: ISR must have fired. */
        if (fires == 0) {
            rlog("PCSPKPWM rate=%lu STATUS=ISR_NEVER_FIRED stolen_pct=NA "
                 "(SUSPECT: vector/lock/PIT-program failure)", (unsigned long)rate);
            continue;
        }
        /* Sentinel: observed fire-rate within +/-25% of requested. */
        const char *rate_flag = "ok";
        if (obs_rate < rate * 0.75 || obs_rate > rate * 1.25) rate_flag = "RATE_MISMATCH";
        /* Sentinel: music-tick cadence near MUSIC_HZ. */
        const char *music_flag = "ok";
        if (obs_mrate < MUSIC_HZ * 0.6 || obs_mrate > MUSIC_HZ * 1.4) music_flag = "MUSIC_OFF";
        /* Plausibility bound on stolen_pct. */
        const char *plaus = "ok";
        if (stolen < -2.0)       plaus = "SUSPECT_NEGATIVE";
        else if (stolen > 90.0)  plaus = "SUSPECT_HIGH";

        rlog("PCSPKPWM rate=%lu stolen_pct=%.2f count_off=%lu count_on=%lu "
             "obs_rate=%.0f rate_flag=%s music_flag=%s plaus=%s",
             (unsigned long)rate, stolen, (unsigned long)count_off,
             (unsigned long)count_on, obs_rate, rate_flag, music_flag, plaus);

        /* One-line per-cell verdict (hypothesis framing -- the operator/g2k
           decide acceptability against the 50fps KPI; the probe states no fps). */
        if (strcmp(plaus, "ok") == 0 && strcmp(rate_flag, "ok") == 0) {
            rlog("  VERDICT rate=%lu: ~%.1f%% continuous CPU steal while a PWM SFX "
                 "sounds (hypothesis; gate vs 50fps KPI on g2k+DX2-66).",
                 (unsigned long)rate, stolen);
        } else {
            rlog("  VERDICT rate=%lu: measurement SUSPECT (%s/%s/%s) -- do NOT use "
                 "this stolen_pct; re-run.", (unsigned long)rate, rate_flag,
                 music_flag, plaus);
        }
    }

    /* ---- post-restore clock self-check ---- */
    rlog("");
    rlog("--- restore + clock self-check ---");
    pwm_restore();   /* idempotent belt-and-braces */
    {
        uint16_t c1 = pit_ch0_count();
        for (volatile int s = 0; s < 20000; s++) { (void)inportb(0x80); }
        uint16_t c2 = pit_ch0_count();
        uint32_t bios_end = _farpeekl(_dos_ds, 0x46C);
        rlog("PIT ch0 post-restore count: c1=%u c2=%u (default divisor 65536 -> "
             "counts span up to ~65535)", c1, c2);
        if (c1 > 1000 || c2 > 1000) {
            rlog("CLOCK-RESTORE=OK: ch0 divisor restored to ~65536 (18.2065 Hz).");
        } else {
            rlog("CLOCK-RESTORE=SUSPECT: ch0 counts still small -- PCM divisor may "
                 "persist; CHECK + RESET THE DOS CLOCK MANUALLY (TIME).");
        }
        rlog("BIOS ticks elapsed this run: %lu (start=%lu end=%lu).",
             (unsigned long)(bios_end - bios_start),
             (unsigned long)bios_start, (unsigned long)bios_end);
    }

    rlog("");
    rlog("=== PCSPKPWM done ===");
    rlog("OPERATOR EAR-CHECK: for each rate, was the 440 Hz tone recognizable and");
    rlog("  how audible was the PWM carrier buzz? (6 kHz carrier ~6 kHz; higher");
    rlog("  rate pushes the buzz out of band but costs more CPU -- sec.1.2/4.)");
    rlog("OPERATOR CLOCK-CHECK: run TIME at the DOS prompt; if the clock runs fast,");
    rlog("  the PIT restore failed -- report it (this is the quit-path risk).");
    rlog("DECISION: pick the lowest rate whose stolen_pct is acceptable on BOTH");
    rlog("  POD-83 and DX2-66 AND whose tone is recognizable (design sec.4.3).");

    if (g_log) fclose(g_log);
    return 0;
}
