/*
 * mpuwbprobe.c — MPU-401 / WaveBlaster init-sequence probe for SB16 PnP CTL0026.
 *
 * Phase 11 / Phase 10 wave Wave-22-WB-E. Standalone DJGPP diagnostic probe
 * targeting the gate-blocker on the operator's g2k machine: SDL/0042 (probe
 * with bus-cap) and SDL/0044 (blind init, no read) BOTH locked the system
 * during MPU-401 init at port 0x330/0x331 on real Vibra16S+S2 -- but the
 * actual hardware per the 2026-05-07 UNIVBE photo is **SB16 PnP CTL0026 +
 * DSP v4.13** with a DreamBlaster S2 on the WaveBlaster header (g2k_audio
 * _config.md memory updated). Port-direct MPU access stalls the ISA bus
 * indefinitely; the next inb / outp instruction hangs the CPU and even a
 * defensive iteration cap can't recover because the cap-loop variable is
 * inside the hung instruction's bus cycle.
 *
 * The probe answers "which exact instruction hangs?" by writing per-step
 * fsync'd markers BEFORE and AFTER every potentially-hanging operation.
 * If the operator hard-resets after a hang, MPUPROBE.LOG on disk shows
 * the last BEGIN line WITHOUT a matching DONE line -- the BEGIN that
 * survived narrows the lockup to a single I/O instruction.
 *
 * Probe phases (ordered safe-first so we collect maximum data even if
 * a later phase hangs):
 *
 *   Section 1: Setup + BLASTER env parse
 *   Section 2: SB16 mixer dump (port 0x224/0x225 -- known-good on this card)
 *   Section 3: BDA snapshot (BIOS tick + kbd buffer baseline)
 *   Section 4: DSP probe (reset @ 0x226 + version read @ 0x22A/0x22C/0x22E)
 *   Section 5: MPU-401 direct-port status read (port 0x331 inb -- FIRST risk)
 *   Section 6: MPU-401 direct-port reset write (port 0x331 outp 0xFF)
 *   Section 7: MPU-401 direct-port ACK poll (port 0x330 inb for 0xFE)
 *   Section 8: MPU-401 direct-port UART entry (port 0x331 outp 0x3F)
 *   Section 9: MPU-401 direct-port MIDI write (port 0x330 outp 0x90/60/100)
 *   Section 10: DSP-mediated MIDI alternative (DSP cmd 0x34 + 0x38 -- bypasses
 *               direct-port access entirely; this is the candidate fallback
 *               path for SDL/0046 if direct-port is fundamentally toxic)
 *   Section 11: Post-activity mixer state re-read (IRQ-status reg 0x82)
 *   Section 12: Summary + reading guide
 *
 * Watchdog discipline (since every hang we've seen is a polling loop, not a
 * single-instruction stall, except the bus-cycle-stall hypothesis below):
 *
 *   - Every polling loop is bounded by both an iteration count cap AND a
 *     uclock wall-clock cap (~250 ms). Whichever fires first wins.
 *   - Single-instruction operations (single outp / single inb) cannot be
 *     watchdog'd from software; if the bus stalls, we'd need NMI which we
 *     can't generate. The fsync-BEFORE / fsync-AFTER pattern makes the
 *     hang location forensically recoverable from MPUPROBE.LOG instead.
 *   - Every iteration of any polling loop also peeks at the BIOS keyboard
 *     buffer (BDA 0x40:0x1C != 0x40:0x1A) and if any key is queued, aborts
 *     the loop. Operator can hit any key during a stuck polling loop to
 *     bail without hard-reset.
 *
 * Per-step log format (fsync'd, line-buffered):
 *
 *   [step N/M] BEGIN  <description>            tick=<bios>  uclock=<sec>
 *   [step N/M] DONE   <description>            elapsed_us=<n>  result=<...>
 *
 * Output path: MPUPROBE.LOG in CWD (operator runs from C:\DOSKUTSU\); falls
 * back to C:\MPUPROBE.LOG if cwd is read-only. Stdout mirrored.
 *
 * Pure DJGPP. No SDL, no engine. Uses only `<pc.h>` outportb/inportb,
 * `<dos.h>` for kb_hit-style keyboard checks (we go BDA-direct instead),
 * `<sys/farptr.h>` + `<go32.h>` for BDA reads, `<time.h>` uclock for timing.
 *
 * 8.3 DOS filename:
 *   Source:   tests/probes/mpuwbprobe.c (host-side -- no 8.3 needed)
 *   Binary:   MPUPROBE.EXE  (8+3, fits) -- explicit Makefile rule renames
 *                            the host build artifact to mpuprobe.exe
 *   Log:      MPUPROBE.LOG  (8+3, fits)
 *   BAT:      MPUPROBE.BAT  (8+3, fits)
 *
 * License: MIT.
 */

#include <dos.h>
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
/* Logging — fsync per line is load-bearing.                    */
/* If a later step hangs the CPU, the disk has an authoritative */
/* trail right up to the hung instruction's BEGIN marker.       */
/* ============================================================ */

static FILE *g_log = NULL;

static void open_log(void)
{
    /* Try cwd first (operator runs probe from \DOSKUTSU\ alongside the game
     * binary -- logback collects MPUPROBE.LOG from /media/micheal/DOS/
     * doskutsu/MPUPROBE.LOG via the realhw wrapper script). Fall back to
     * CF root if cwd is read-only or the dir doesn't exist. */
    g_log = fopen("MPUPROBE.LOG", "w");
    if (!g_log) g_log = fopen("C:\\MPUPROBE.LOG", "w");
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
/* Timing                                                       */
/* ============================================================ */

static double now_secs(void)
{
    return (double)uclock() / (double)UCLOCKS_PER_SEC;
}

static uint32_t bios_ticks(void)
{
    /* BDA 0x40:0x6C = 32-bit timer-tick counter (incremented by INT 8h
     * once per 18.2 Hz PIT IRQ). Useful as a coarse wall-clock baseline
     * that survives uclock() weirdness. */
    return _farpeekl(_dos_ds, 0x46CL);
}

/* ============================================================ */
/* Watchdog — keyboard escape (operator-driven abort)            */
/*                                                               */
/* BDA 0x40:0x1A = kbd buffer head; 0x40:0x1C = kbd buffer tail. */
/* If they differ, the operator typed something. Any key aborts. */
/* This costs ~2 BDA reads per polling iteration -- negligible.  */
/* ============================================================ */

static int kbd_pending(void)
{
    uint16_t head = _farpeekw(_dos_ds, 0x41AL);
    uint16_t tail = _farpeekw(_dos_ds, 0x41CL);
    return head != tail;
}

/* ============================================================ */
/* Step markers — load-bearing forensic discipline.             */
/*                                                               */
/* If the system hangs after step_begin and before step_done,   */
/* the disk has the BEGIN line. Nothing in this probe should    */
/* attempt risky I/O without bracketing.                         */
/* ============================================================ */

static int g_step_n = 0;
static int g_step_total = 0;
static double g_step_t0 = 0.0;

static void step_begin(const char *desc)
{
    g_step_n++;
    g_step_t0 = now_secs();
    plog("[step %d/%d] BEGIN  %-50s tick=%lu  uclock=%.6f",
         g_step_n, g_step_total, desc,
         (unsigned long)bios_ticks(), g_step_t0);
}

static void step_done(const char *result_fmt, ...)
{
    char rbuf[256];
    va_list ap;
    va_start(ap, result_fmt);
    vsnprintf(rbuf, sizeof rbuf, result_fmt, ap);
    va_end(ap);

    double dt = now_secs() - g_step_t0;
    plog("[step %d/%d] DONE   elapsed_us=%-9.0f result: %s",
         g_step_n, g_step_total, dt * 1e6, rbuf);
}

/* ============================================================ */
/* BLASTER env parse                                            */
/* ============================================================ */

typedef struct {
    int audio_base;   /* A field; 0x220 default */
    int irq;          /* I field; 5 default */
    int dma_lo;       /* D field; 1 default */
    int dma_hi;       /* H field; 5 default */
    int mpu_base;     /* P field; 0x330 default */
    int sb_type;      /* T field; 6 default = SB16 */
    char raw[160];
} blaster_t;

static int parse_hex(const char *s)
{
    int v = 0;
    while (*s && *s != ' ' && *s != '\t') {
        char c = *s++;
        if (c >= '0' && c <= '9') v = v*16 + (c - '0');
        else if (c >= 'a' && c <= 'f') v = v*16 + (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = v*16 + (c - 'A' + 10);
        else break;
    }
    return v;
}

static int parse_dec(const char *s)
{
    int v = 0;
    while (*s >= '0' && *s <= '9') v = v*10 + (*s++ - '0');
    return v;
}

static void parse_blaster(blaster_t *b)
{
    /* Defaults match SB16 g2k profile per docs/HARDWARE.md. */
    b->audio_base = 0x220;
    b->irq = 5;
    b->dma_lo = 1;
    b->dma_hi = 5;
    b->mpu_base = 0x330;
    b->sb_type = 6;
    b->raw[0] = 0;

    const char *env = getenv("BLASTER");
    if (!env) return;

    strncpy(b->raw, env, sizeof b->raw - 1);
    b->raw[sizeof b->raw - 1] = 0;

    /* BLASTER format: A220 I5 D1 H5 P330 T6 (space-separated tokens). */
    const char *p = env;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        char field = *p++;
        if (field >= 'a' && field <= 'z') field -= ('a' - 'A');
        switch (field) {
            case 'A': b->audio_base = parse_hex(p); break;
            case 'I': b->irq        = parse_dec(p); break;
            case 'D': b->dma_lo     = parse_dec(p); break;
            case 'H': b->dma_hi     = parse_dec(p); break;
            case 'P': b->mpu_base   = parse_hex(p); break;
            case 'T': b->sb_type    = parse_dec(p); break;
            default: break;
        }
        while (*p && *p != ' ' && *p != '\t') p++;
    }
}

/* ============================================================ */
/* SB16 mixer access (port 0x224 = address, 0x225 = data)        */
/* These ports are NOT the lockup-prone ones — SB16 mixer is    */
/* settled hardware on every Creative SB16 variant.              */
/* ============================================================ */

static uint8_t mixer_read(int audio_base, uint8_t reg)
{
    outportb(audio_base + 0x4, reg);
    /* No documented inter-access delay needed for mixer; SB16 latches reg
     * select on the port write and serves the data port read immediately. */
    return inportb(audio_base + 0x5);
}

static void mixer_write(int audio_base, uint8_t reg, uint8_t val)
{
    outportb(audio_base + 0x4, reg);
    outportb(audio_base + 0x5, val);
}

/* ============================================================ */
/* DSP access (port_base + 0x6 reset, +0xA read, +0xC write,    */
/* +0xE read-status). Reads can hang if DSP is in a bad state,  */
/* so the polling loops are bounded.                             */
/* ============================================================ */

/* Wait for DSP to be ready to accept a command (write-status bit 7 == 0).
 * Bounded by iter cap + 250 ms wall + kbd-escape. Returns 1 on success,
 * 0 on timeout / abort. */
static int dsp_wait_write(int audio_base)
{
    int port = audio_base + 0xC;
    double t0 = now_secs();
    for (int i = 0; i < 100000; i++) {
        uint8_t s = inportb(port);
        if (!(s & 0x80)) return 1;
        if ((i & 0xFF) == 0) {
            if ((now_secs() - t0) > 0.25) return 0;
            if (kbd_pending()) return 0;
        }
    }
    return 0;
}

/* Wait for DSP to have data available (read-status bit 7 == 1). */
static int dsp_wait_read(int audio_base)
{
    int port = audio_base + 0xE;
    double t0 = now_secs();
    for (int i = 0; i < 100000; i++) {
        uint8_t s = inportb(port);
        if (s & 0x80) return 1;
        if ((i & 0xFF) == 0) {
            if ((now_secs() - t0) > 0.25) return 0;
            if (kbd_pending()) return 0;
        }
    }
    return 0;
}

/* DSP reset: outp(reset, 1) -- delay -- outp(reset, 0) -- expect 0xAA on read. */
static int dsp_reset(int audio_base)
{
    int reset_port = audio_base + 0x6;
    outportb(reset_port, 1);
    /* SB16 spec requires >=3 us delay; busy-wait via uclock. */
    double t_delay = now_secs() + 0.00001;  /* 10 us padding */
    while (now_secs() < t_delay) { /* spin */ }
    outportb(reset_port, 0);

    if (!dsp_wait_read(audio_base)) return -1;
    uint8_t b = inportb(audio_base + 0xA);
    return (b == 0xAA) ? 0 : -2;
}

static int dsp_write(int audio_base, uint8_t cmd)
{
    if (!dsp_wait_write(audio_base)) return -1;
    outportb(audio_base + 0xC, cmd);
    return 0;
}

static int dsp_read(int audio_base, uint8_t *out)
{
    if (!dsp_wait_read(audio_base)) return -1;
    *out = inportb(audio_base + 0xA);
    return 0;
}

/* ============================================================ */
/* MPU-401 direct-port helpers — THESE ARE THE LOCKUP-PRONE ONES */
/*                                                               */
/* Port layout:                                                  */
/*   mpu_base + 0  = data port (read/write)                      */
/*   mpu_base + 1  = status (read) / command (write)             */
/*                                                               */
/* Status bits:                                                  */
/*   bit 7 = TX READY: 0 = ready to accept command write         */
/*   bit 6 = RX READY: 0 = data byte available on data port read */
/*                                                               */
/* The instructions in this section are the ones that hung the   */
/* W22-WB-D operator iter. Each is wrapped in step_begin/done    */
/* so a hang's location is recoverable from MPUPROBE.LOG.        */
/* ============================================================ */

/* Bounded poll for TX-READY (status bit 7 == 0). */
static int mpu_wait_tx(int mpu_base, int *out_iters)
{
    int port = mpu_base + 1;
    double t0 = now_secs();
    int i;
    for (i = 0; i < 5000; i++) {
        /* THE inb here is the load-bearing risk. If it stalls indefinitely,
         * the iter cap and uclock check below never run. The fsync log of
         * the surrounding step_begin is the forensic evidence. */
        uint8_t s = inportb(port);
        if (!(s & 0x80)) {
            if (out_iters) *out_iters = i;
            return 1;
        }
        if ((i & 0x3F) == 0) {
            if ((now_secs() - t0) > 0.25) {
                if (out_iters) *out_iters = i;
                return 0;
            }
            if (kbd_pending()) {
                if (out_iters) *out_iters = i;
                return 0;
            }
        }
    }
    if (out_iters) *out_iters = i;
    return 0;
}

/* Bounded poll for RX-READY (status bit 6 == 0). Returns the data byte
 * via *out_byte on success, 1 on success, 0 on timeout. */
static int mpu_wait_rx_byte(int mpu_base, uint8_t *out_byte, int *out_iters)
{
    int status_port = mpu_base + 1;
    int data_port   = mpu_base + 0;
    double t0 = now_secs();
    int i;
    for (i = 0; i < 20000; i++) {
        uint8_t s = inportb(status_port);
        if (!(s & 0x40)) {
            *out_byte = inportb(data_port);
            if (out_iters) *out_iters = i;
            return 1;
        }
        if ((i & 0x3F) == 0) {
            if ((now_secs() - t0) > 0.25) {
                if (out_iters) *out_iters = i;
                return 0;
            }
            if (kbd_pending()) {
                if (out_iters) *out_iters = i;
                return 0;
            }
        }
    }
    if (out_iters) *out_iters = i;
    return 0;
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    plog("=== MPUPROBE wave-22-WB-E starting ===");
    plog("DJGPP build, target = MPU-401 / WaveBlaster init-sequence diagnosis");
    plog("UCLOCKS_PER_SEC = %lu", (unsigned long)UCLOCKS_PER_SEC);
    plog("");
    plog("Forensic protocol: every potentially-hanging operation is bracketed by");
    plog("[step N/M] BEGIN ... DONE markers, fsync'd. If the system hangs, the");
    plog("LAST line in MPUPROBE.LOG identifies which instruction stalled the bus.");
    plog("Operator: hard-reset is OK after a hang; the log is on disk.");
    plog("Operator: hit any key during a polling loop to abort that loop early.");
    plog("");

    /* Total step count for progress display. Update if the section list changes. */
    g_step_total = 30;

    /* ============================================================ */
    /* Section 1: BLASTER env parse                                 */
    /* ============================================================ */
    plog("---- Section 1: BLASTER env parse ----");
    blaster_t b;
    parse_blaster(&b);
    plog("BLASTER raw: \"%s\"", b.raw);
    plog("BLASTER parsed: A=0x%03X I=%d D=%d H=%d P=0x%03X T=%d",
         b.audio_base, b.irq, b.dma_lo, b.dma_hi, b.mpu_base, b.sb_type);
    plog("Mixer ports:    addr=0x%03X data=0x%03X",
         b.audio_base + 0x4, b.audio_base + 0x5);
    plog("DSP ports:      reset=0x%03X read=0x%03X write=0x%03X read_status=0x%03X",
         b.audio_base + 0x6, b.audio_base + 0xA,
         b.audio_base + 0xC, b.audio_base + 0xE);
    plog("MPU-401 ports:  data=0x%03X status_or_cmd=0x%03X",
         b.mpu_base + 0, b.mpu_base + 1);
    plog("");

    /* ============================================================ */
    /* Section 2: SB16 mixer dump (safe -- known-good hardware)     */
    /* ============================================================ */
    plog("---- Section 2: SB16 mixer dump (port 0x%03X/0x%03X) ----",
         b.audio_base + 0x4, b.audio_base + 0x5);

    step_begin("SB16 mixer dump (regs 0x00-0x47, 0x80-0x83)");
    /* Dump the canonical SB16 mixer register set. Each is 8-bit; the SB16
     * mixer responds to addresses 0x00-0xFE in 8-bit chunks. We dump the
     * standard ranges:
     *   0x00       reset (write-only; reading returns 0 typically)
     *   0x04-0x0E  legacy SB Pro mixer (back-compat)
     *   0x22-0x47  SB16 master/voice/midi/cd/line/mic + EQ + input routing
     *   0x80-0x83  IRQ select / DMA select / IRQ status / power mgmt
     */
    static const uint8_t mixer_regs[] = {
        0x00, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E,
        0x22, 0x26, 0x28, 0x2E,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
        0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E,
        0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
        0x80, 0x81, 0x82, 0x83
    };
    int n_mixer = sizeof(mixer_regs) / sizeof(mixer_regs[0]);
    for (int i = 0; i < n_mixer; i++) {
        uint8_t v = mixer_read(b.audio_base, mixer_regs[i]);
        plog("MIXER[0x%02X] = 0x%02X    %s", mixer_regs[i], v,
             mixer_regs[i] == 0x22 ? "master vol" :
             mixer_regs[i] == 0x26 ? "FM vol" :
             mixer_regs[i] == 0x30 ? "master L" :
             mixer_regs[i] == 0x31 ? "master R" :
             mixer_regs[i] == 0x32 ? "voice L (DAC)" :
             mixer_regs[i] == 0x33 ? "voice R (DAC)" :
             mixer_regs[i] == 0x34 ? "MIDI L (synth out)" :
             mixer_regs[i] == 0x35 ? "MIDI R (synth out)" :
             mixer_regs[i] == 0x3C ? "OUT mixer switch (b5=midiL b4=midiR ...)" :
             mixer_regs[i] == 0x3D ? "IN  mixer switch L" :
             mixer_regs[i] == 0x3E ? "IN  mixer switch R" :
             mixer_regs[i] == 0x80 ? "IRQ select (b1=IRQ5)" :
             mixer_regs[i] == 0x81 ? "DMA select (b1=DMA1, b5=DMA5)" :
             mixer_regs[i] == 0x82 ? "IRQ status (b0=SB b2=MPU)" :
             "");
    }
    step_done("dumped %d mixer registers", n_mixer);
    plog("");

    /* Speculative mixer-write to enable any MPU-related routing bits before
     * later sections issue MPU traffic. This is "test (c)" from the brief --
     * even if SB16 spec doesn't formally have an "MPU enable" bit, ensuring
     * MIDI is routed through the OUT mixer switch (reg 0x3C bits 5/4) and
     * MIDI volume (regs 0x34/0x35) are non-zero is the cheapest way to
     * eliminate "MPU IRQ fires but no audible output" as a confounder. We
     * read-modify-write to preserve unrelated bits. */
    step_begin("SB16 mixer: ensure MIDI L/R routing on (regs 0x3C, 0x34, 0x35)");
    uint8_t outsw_before = mixer_read(b.audio_base, 0x3C);
    uint8_t midi_l_before = mixer_read(b.audio_base, 0x34);
    uint8_t midi_r_before = mixer_read(b.audio_base, 0x35);
    /* Force MIDI L (bit 5) + MIDI R (bit 4) on in OUT mixer switch. */
    mixer_write(b.audio_base, 0x3C, outsw_before | 0x30);
    /* Force MIDI L+R volume to ~80% if currently below half. */
    if ((midi_l_before >> 3) < 0x10) mixer_write(b.audio_base, 0x34, 0xC0);
    if ((midi_r_before >> 3) < 0x10) mixer_write(b.audio_base, 0x35, 0xC0);
    uint8_t outsw_after = mixer_read(b.audio_base, 0x3C);
    uint8_t midi_l_after = mixer_read(b.audio_base, 0x34);
    uint8_t midi_r_after = mixer_read(b.audio_base, 0x35);
    step_done("0x3C=0x%02X->0x%02X  0x34=0x%02X->0x%02X  0x35=0x%02X->0x%02X",
              outsw_before, outsw_after,
              midi_l_before, midi_l_after,
              midi_r_before, midi_r_after);
    plog("");

    /* ============================================================ */
    /* Section 3: BDA snapshot                                      */
    /* ============================================================ */
    plog("---- Section 3: BDA snapshot ----");
    uint32_t bda_t0 = bios_ticks();
    uint16_t kbd_h0 = _farpeekw(_dos_ds, 0x41AL);
    uint16_t kbd_t0 = _farpeekw(_dos_ds, 0x41CL);
    plog("BDA-BEFORE bios_ticks=%lu  kbd_head=0x%04X  kbd_tail=0x%04X",
         (unsigned long)bda_t0, kbd_h0, kbd_t0);
    plog("");

    /* ============================================================ */
    /* Section 4: DSP probe                                         */
    /* ============================================================ */
    plog("---- Section 4: DSP probe (port 0x%03X / 0x%03X / 0x%03X / 0x%03X) ----",
         b.audio_base + 0x6, b.audio_base + 0xA,
         b.audio_base + 0xC, b.audio_base + 0xE);

    int dsp_alive = 0;
    int dsp_major = 0, dsp_minor = 0;

    step_begin("DSP reset (outp 0x226 1; delay; outp 0x226 0; expect 0xAA)");
    int rc = dsp_reset(b.audio_base);
    if (rc == 0) {
        dsp_alive = 1;
        step_done("DSP responded 0xAA -- DSP alive");
    } else if (rc == -1) {
        step_done("DSP reset TIMEOUT (no 0xAA after 250 ms) -- DSP unresponsive");
    } else {
        step_done("DSP reset returned non-0xAA byte -- DSP in bad state, rc=%d", rc);
    }

    if (dsp_alive) {
        step_begin("DSP version query (cmd 0xE1, expect 2 bytes)");
        int wrote = dsp_write(b.audio_base, 0xE1);
        uint8_t mb = 0, lb = 0;
        if (wrote == 0) {
            int rd1 = dsp_read(b.audio_base, &mb);
            int rd2 = dsp_read(b.audio_base, &lb);
            if (rd1 == 0 && rd2 == 0) {
                dsp_major = mb;
                dsp_minor = lb;
                step_done("DSP version = %d.%02d (expected 4.13 for SB16 PnP CTL0026)",
                          dsp_major, dsp_minor);
            } else {
                step_done("DSP version read incomplete (rd1=%d rd2=%d)", rd1, rd2);
            }
        } else {
            step_done("DSP version cmd write failed (write timeout)");
        }
    }
    plog("");

    /* ============================================================ */
    /* Section 5: MPU-401 direct-port status read (FIRST RISK)      */
    /*                                                               */
    /* This is the canonical "did the bus stall here" question. If   */
    /* the system has hung at exactly this step in prior iters, the  */
    /* log will show this BEGIN line and nothing after it.           */
    /* ============================================================ */
    plog("---- Section 5: MPU-401 direct-port status read (port 0x%03X) ----",
         b.mpu_base + 1);
    plog("HAZARD: this read is the suspected lockup point on SB16 PnP CTL0026.");
    plog("If MPUPROBE.LOG ends at the next BEGIN line, the lockup is inb(0x%03X).",
         b.mpu_base + 1);
    plog("");

    int mpu_open_bus = 0;
    uint8_t mpu_status_initial = 0;

    step_begin("MPU-401 status read: inportb(mpu_base+1)");
    mpu_status_initial = inportb(b.mpu_base + 1);
    step_done("status=0x%02X (0xFF=open-bus indicator; 0x80/0x40 bits=TX_READY/RX_READY)",
              mpu_status_initial);

    if (mpu_status_initial == 0xFF) {
        mpu_open_bus = 1;
        plog("OPEN-BUS detected (status=0xFF) -- no chip decodes MPU port 0x%03X.",
             b.mpu_base + 1);
        plog("Skipping subsequent direct-port writes (would go to nowhere).");
        plog("DSP-mediated MIDI alternative will still be attempted.");
    }
    plog("");

    /* Also read the data port to characterize behavior. */
    step_begin("MPU-401 data-port read: inportb(mpu_base+0)");
    uint8_t mpu_data_initial = inportb(b.mpu_base + 0);
    step_done("data=0x%02X", mpu_data_initial);
    plog("");

    /* ============================================================ */
    /* Section 6-9: MPU-401 direct-port reset sequence              */
    /* (skipped if open-bus detected at section 5)                  */
    /* ============================================================ */
    int mpu_direct_ok = 0;

    if (!mpu_open_bus) {
        plog("---- Section 6: MPU-401 reset write (outp 0x%03X 0xFF) ----",
             b.mpu_base + 1);
        step_begin("MPU-401 reset: outportb(mpu_base+1, 0xFF)");
        outportb(b.mpu_base + 1, 0xFF);
        step_done("reset cmd dispatched (return alone doesn't confirm chip ack)");
        plog("");

        plog("---- Section 7: MPU-401 ACK poll (expect 0xFE on data port) ----");
        step_begin("MPU-401 ACK poll: inb(mpu_base+0) until 0xFE or timeout");
        int iters = 0;
        uint8_t ack = 0;
        int got = mpu_wait_rx_byte(b.mpu_base, &ack, &iters);
        if (got && ack == 0xFE) {
            mpu_direct_ok = 1;
            step_done("ACK 0xFE received after %d iter (chip alive on direct port)", iters);
        } else if (got) {
            step_done("got byte=0x%02X (NOT 0xFE) after %d iter -- chip in odd state", ack, iters);
        } else {
            step_done("ACK poll TIMEOUT after %d iter / 250 ms -- chip silent or RX bit stuck", iters);
        }
        plog("");

        plog("---- Section 8: MPU-401 UART entry (outp 0x%03X 0x3F) ----",
             b.mpu_base + 1);
        step_begin("MPU-401 wait-TX: poll status until bit 7 == 0");
        int wait_iters = 0;
        int tx_ok = mpu_wait_tx(b.mpu_base, &wait_iters);
        step_done("TX-ready %s after %d iter", tx_ok ? "OK" : "TIMEOUT", wait_iters);

        step_begin("MPU-401 UART entry: outportb(mpu_base+1, 0x3F)");
        outportb(b.mpu_base + 1, 0x3F);
        step_done("UART entry cmd dispatched");

        step_begin("MPU-401 UART ACK poll: inb(mpu_base+0) until 0xFE or timeout");
        ack = 0; iters = 0;
        got = mpu_wait_rx_byte(b.mpu_base, &ack, &iters);
        if (got && ack == 0xFE) {
            step_done("UART ACK received after %d iter -- in UART mode", iters);
        } else if (got) {
            step_done("UART poll got 0x%02X (not 0xFE) after %d iter", ack, iters);
        } else {
            step_done("UART ACK TIMEOUT after %d iter -- chip silent in UART transition", iters);
        }
        plog("");

        plog("---- Section 9: MPU-401 MIDI byte writes (data port 0x%03X) ----",
             b.mpu_base + 0);
        plog("Sequence: note_on ch1 (0x90) middle C (60) velocity 100");

        step_begin("MIDI byte 1: outportb(mpu_base+0, 0x90) [note_on ch1]");
        outportb(b.mpu_base + 0, 0x90);
        step_done("byte 1 dispatched");

        step_begin("MIDI byte 2: outportb(mpu_base+0, 0x3C) [middle C / 60]");
        outportb(b.mpu_base + 0, 60);
        step_done("byte 2 dispatched");

        step_begin("MIDI byte 3: outportb(mpu_base+0, 0x64) [velocity 100]");
        outportb(b.mpu_base + 0, 100);
        step_done("byte 3 dispatched");

        /* Hold the note ~500 ms (audible if WaveBlaster is responding). */
        plog("Holding note ~500 ms (DreamBlaster S2 should be audible if alive)...");
        double t_hold = now_secs() + 0.5;
        while (now_secs() < t_hold) { /* spin -- non-blocking, no IO */ }

        step_begin("MIDI note_off: outp 0x80, 0x3C, 0x40 (note_off ch1 middle C vel 64)");
        outportb(b.mpu_base + 0, 0x80);
        outportb(b.mpu_base + 0, 60);
        outportb(b.mpu_base + 0, 64);
        step_done("note_off dispatched");
        plog("");
    } else {
        /* Skip 6-9; bump step counter to keep numbering stable. */
        plog("---- Sections 6-9 SKIPPED (open-bus at section 5) ----");
        for (int k = 0; k < 11; k++) g_step_n++;
        plog("");
    }

    /* ============================================================ */
    /* Section 10: DSP-mediated MIDI alternative                    */
    /*                                                               */
    /* SB16 DSP commands 0x34 (enter UART MIDI mode) and 0x38 (write */
    /* MIDI byte) are an alternative path that DOESN'T touch the     */
    /* direct MPU port. The SB16 chip itself handles the MPU-401     */
    /* protocol on its WaveBlaster header internally. If direct-port */
    /* is fundamentally toxic on this hardware, this DSP-mediated    */
    /* path is the candidate fallback for SDL/0046.                  */
    /* ============================================================ */
    plog("---- Section 10: DSP-mediated MIDI alternative (DSP cmd 0x34, 0x38) ----");

    if (!dsp_alive) {
        plog("DSP unresponsive at section 4 -- DSP-mediated MIDI cannot proceed.");
        for (int k = 0; k < 6; k++) g_step_n++;  /* bump for missing steps */
    } else {
        step_begin("DSP cmd 0x34: enter MIDI UART mode (no IRQ, no time-stamp)");
        int wrc = dsp_write(b.audio_base, 0x34);
        if (wrc == 0) step_done("0x34 dispatched");
        else step_done("0x34 write FAILED (DSP write-status bit 7 stuck)");

        plog("DSP cmd 0x38 sequence: note_on ch1 + middle C + velocity 100");
        step_begin("DSP cmd 0x38 + 0x90: write MIDI status byte note_on ch1");
        wrc = dsp_write(b.audio_base, 0x38);
        if (wrc == 0) wrc = dsp_write(b.audio_base, 0x90);
        step_done("status byte %s", wrc == 0 ? "OK" : "FAILED");

        step_begin("DSP cmd 0x38 + 60: write MIDI data1 middle C");
        wrc = dsp_write(b.audio_base, 0x38);
        if (wrc == 0) wrc = dsp_write(b.audio_base, 60);
        step_done("data1 %s", wrc == 0 ? "OK" : "FAILED");

        step_begin("DSP cmd 0x38 + 100: write MIDI data2 velocity 100");
        wrc = dsp_write(b.audio_base, 0x38);
        if (wrc == 0) wrc = dsp_write(b.audio_base, 100);
        step_done("data2 %s", wrc == 0 ? "OK" : "FAILED");

        plog("Holding note ~500 ms (DreamBlaster S2 should be audible if DSP-mediated path works)...");
        double t_hold = now_secs() + 0.5;
        while (now_secs() < t_hold) { /* spin */ }

        step_begin("DSP cmd 0x38 sequence: note_off ch1 + middle C + velocity 64");
        wrc = dsp_write(b.audio_base, 0x38);
        if (wrc == 0) wrc = dsp_write(b.audio_base, 0x80);
        if (wrc == 0) wrc = dsp_write(b.audio_base, 0x38);
        if (wrc == 0) wrc = dsp_write(b.audio_base, 60);
        if (wrc == 0) wrc = dsp_write(b.audio_base, 0x38);
        if (wrc == 0) wrc = dsp_write(b.audio_base, 64);
        step_done("note_off %s", wrc == 0 ? "OK" : "FAILED");

        /* Exit UART mode by issuing DSP reset. Cleanest way to leave the chip
         * in a state subsequent code can touch. */
        step_begin("DSP reset to exit UART MIDI mode");
        int rrc = dsp_reset(b.audio_base);
        step_done("post-MIDI DSP reset rc=%d (0=OK)", rrc);
    }
    plog("");

    /* ============================================================ */
    /* Section 11: Post-activity mixer state re-read                */
    /* ============================================================ */
    plog("---- Section 11: Post-activity mixer state ----");
    step_begin("Re-read IRQ-status mixer reg 0x82 (b0=SB IRQ b1=MIDI? b2=MPU IRQ)");
    uint8_t irq_status = mixer_read(b.audio_base, 0x82);
    step_done("MIXER[0x82] = 0x%02X (b0=%d b1=%d b2=%d b3=%d)",
              irq_status,
              !!(irq_status & 0x01), !!(irq_status & 0x02),
              !!(irq_status & 0x04), !!(irq_status & 0x08));

    step_begin("Re-read OUT mixer switch 0x3C (verify MIDI bits stayed on)");
    uint8_t outsw_post = mixer_read(b.audio_base, 0x3C);
    step_done("MIXER[0x3C] = 0x%02X (MIDI L=%d MIDI R=%d)",
              outsw_post, !!(outsw_post & 0x20), !!(outsw_post & 0x10));
    plog("");

    /* ============================================================ */
    /* BDA snapshot after — measures kbd-buffer activity from any   */
    /* keys the operator pressed during polling-loop hangs.         */
    /* ============================================================ */
    uint32_t bda_t1 = bios_ticks();
    uint16_t kbd_h1 = _farpeekw(_dos_ds, 0x41AL);
    uint16_t kbd_t1 = _farpeekw(_dos_ds, 0x41CL);
    plog("BDA-AFTER  bios_ticks=%lu (delta=%lu = ~%.1f sec)  kbd_head=0x%04X  kbd_tail=0x%04X",
         (unsigned long)bda_t1, (unsigned long)(bda_t1 - bda_t0),
         (double)(bda_t1 - bda_t0) / 18.2, kbd_h1, kbd_t1);
    plog("");

    /* ============================================================ */
    /* Section 12: Summary + reading guide                          */
    /* ============================================================ */
    plog("---- Section 12: Summary ----");
    plog("DSP alive:           %s (version %d.%02d)",
         dsp_alive ? "YES" : "NO", dsp_major, dsp_minor);
    plog("MPU-401 open bus:    %s (status read = 0x%02X at section 5)",
         mpu_open_bus ? "YES (no chip)" : "NO (chip claims port)", mpu_status_initial);
    plog("MPU-401 direct-port: %s",
         mpu_open_bus ? "untested (open bus)" :
         mpu_direct_ok ? "ACK 0xFE received" : "no ACK -- chip silent");
    plog("");
    plog("=== MPUPROBE done ===");
    plog("");
    plog("Reading the result for SDL/0046 design:");
    plog("");
    plog("  1. MPUPROBE.LOG ends mid-step with no DONE line:");
    plog("     -> the BEGIN line names the exact instruction that hung the bus.");
    plog("        SDL/0046 must AVOID that exact instruction on this hardware.");
    plog("");
    plog("  2. Section 5 status_initial == 0xFF + section 4 DSP alive:");
    plog("     -> MPU-401 chip not present at port 0x%03X (open ISA bus); the WB",
         b.mpu_base + 1);
    plog("        backend MUST use DSP-mediated MIDI (cmd 0x34/0x38) not direct port.");
    plog("        DreamBlaster S2 still hooked up via WaveBlaster header internal");
    plog("        to the SB16 chip; SB16 routes MIDI bytes to the header on its own.");
    plog("");
    plog("  3. Section 5 status_initial != 0xFF + section 7 ACK 0xFE seen:");
    plog("     -> direct port MPU-401 is responsive. SDL/0044 blind init was on");
    plog("        the right track but missed something (timing? mixer routing?).");
    plog("        Compare mixer regs 0x3C / 0x34 / 0x35 before vs after section 2.");
    plog("");
    plog("  4. Section 10 DSP-mediated steps all DONE + section 9 hung:");
    plog("     -> the DSP-mediated path is viable. SDL/0046 should switch to");
    plog("        cmd 0x34 + cmd 0x38 sequence, drop direct-port access entirely.");
    plog("");
    plog("  5. All steps DONE + DreamBlaster S2 was audible during sections 9 / 10:");
    plog("     -> hardware is fine; the original SDL/0042/0044 hangs were SDL-glue");
    plog("        timing (e.g., SDL_GetTicksNS deadlock, IRQ contention with the");
    plog("        timer source). Probe shows the chip itself is responsive.");

    if (g_log) fclose(g_log);
    return 0;
}
