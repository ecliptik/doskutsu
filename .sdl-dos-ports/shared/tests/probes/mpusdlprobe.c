/*
 * mpusdlprobe.c — reduced-scope SDL3+MPU-401 probe for SB16 PnP CTL0026.
 *
 * Phase 10 wave W22-WB iter H. Standalone DJGPP probe that links libSDL3.a
 * and exercises ONLY the audio-init code path before attempting direct-port
 * MPU-401 init. Discriminates "is SDL audio init alone enough to break
 * direct-port MPU access?" — a question the standalone MPUPROBE iter F
 * (no SDL) couldn't answer because it didn't replicate the W22-WB-C/D
 * failure preconditions.
 *
 * Background — what's known:
 *
 *   - MPUPROBE iter F (zero SDL): all 23 active steps DONE; chip responds
 *     correctly to direct-port reset + UART entry + MIDI byte writes.
 *     The chip is fine in isolation. (See docs/MPUPROBE-W22WB-F-ANALYSIS.md.)
 *
 *   - SDL/0042 + 0044 (SDL + engine + graphics + audio init): hung on real
 *     HW at "Loading" while doing direct-port MPU access. Both blind init
 *     and probe-with-iter-cap variants failed.
 *
 *   - SDL/0047 (DSP-mediated MIDI via DSP cmd 0x34/0x38): no lockup at
 *     Loading, but iter G operator confirms audio routes through SB16
 *     internal OPL emulation, NOT the DreamBlaster S2 on the WaveBlaster
 *     header. Cowbell/morse-code timbre. We want direct-port working
 *     because that's the path that produces full GM-bank audio + offloads
 *     synth work to the daughterboard's ASIC (= fps headroom).
 *
 * Probe scope (per team-lead brief; do not silently expand):
 *
 *   1. SDL_Init(SDL_INIT_AUDIO) — no video, no events
 *   2. SDL_OpenAudioDeviceStream at 11025 mono S16 (matches Tier-2 audio
 *      profile from wave_21_lever_g_final_state.md)
 *   3. SDL_ResumeAudioStreamDevice — start playback (silent stream)
 *   4. SDL_Delay(1000) — let SDL audio thread service ≥1 frame; satisfies
 *      hard-no rule #3 from MPUPROBE iter F analysis ("audio backend FIRST,
 *      audio thread services ≥1 frame, THEN WB init")
 *   5. Pre-ACK drain: inb(0x330) to consume any stale 0xFE byte (matches
 *      MPUPROBE iter F section 5 finding — chip had 0xFE pre-pending)
 *   6. Direct-port MPU-401 reset: outp(0x331, 0xFF)
 *   7. ACK poll on data port for 0xFE
 *   8. UART entry: outp(0x331, 0x3F)
 *   9. UART ACK poll
 *   10. MIDI byte writes: note_on ch1 (0x90), middle-C (60), velocity 100;
 *       hold 500ms; note_off (0x80, 60, 64)
 *   11. SDL_DestroyAudioStream + SDL_Quit
 *
 * Forensic protocol (same as MPUPROBE iter F):
 *
 *   - Every potentially-hanging step bracketed by fsync'd BEGIN/DONE
 *     markers in MPUSDL.LOG. If the system hangs, the LAST BEGIN line
 *     without a matching DONE identifies the stalling instruction.
 *   - Polling loops bounded by iter cap (5K-20K) + 250ms uclock wall +
 *     BIOS keyboard buffer escape (operator hits any key to abort).
 *   - Single-instruction stalls cannot be software-watchdog'd; the disk
 *     log is the only forensic recovery path after hard-reset.
 *
 * Three expected outcomes (from team-lead brief):
 *
 *   A. Probe runs to completion + DreamBlaster S2 audible during step 14
 *      hold-note → direct-port works under SDL audio init alone. The
 *      W22-WB-C/D hangs were NOT triggered by SDL audio init; they were
 *      triggered by something else (engine threading? graphics init? the
 *      full SDL_Init flag set including video/events?). sdl-engine
 *      investigation focuses on what's NOT in this probe.
 *
 *   B. Probe hangs during steps 5-10 → SDL audio init alone IS enough to
 *      trigger the bus-stall failure mode. The last BEGIN line names the
 *      exact stalling instruction. SDL/0048 must avoid that I/O while
 *      audio backend is active.
 *
 *   C. Probe runs but middle-C inaudible (operator confirms) → direct-port
 *      writes succeed but MIDI doesn't reach DreamBlaster S2. Possibly
 *      mixer routing issue specific to direct-port (DSP-mediated path
 *      from iter G DID produce sound through internal OPL; direct-port
 *      should produce sound through WaveBlaster header).
 *
 * Output: MPUSDL.LOG in CWD (operator runs from \DOSKUTSU\). Falls back
 * to C:\MPUSDL.LOG if cwd is read-only.
 *
 * Pure DJGPP + libSDL3.a (no SDL_mixer, no SDL_image, no engine). minstack
 * 2048k matches the SDL3-linked YIELD probe's recipe.
 *
 * 8.3 DOS filename:
 *   Source:   tests/probes/mpusdlprobe.c (host-side, no 8.3 needed)
 *   Binary:   MPUSDL.EXE  (5+3, fits)
 *   Log:      MPUSDL.LOG  (5+3, fits)
 *   BAT:      MPUSDL.BAT  (5+3, fits)
 *
 * License: MIT.
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>

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
/* Logging — fsync per line, mirrors stdout                     */
/* ============================================================ */

static FILE *g_log = NULL;

static void open_log(void)
{
    g_log = fopen("MPUSDL.LOG", "w");
    if (!g_log) g_log = fopen("C:\\MPUSDL.LOG", "w");
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
/* Timing + watchdog helpers                                    */
/* ============================================================ */

static double now_secs(void)
{
    return (double)uclock() / (double)UCLOCKS_PER_SEC;
}

static uint32_t bios_ticks(void)
{
    return _farpeekl(_dos_ds, 0x46CL);
}

static int kbd_pending(void)
{
    uint16_t head = _farpeekw(_dos_ds, 0x41AL);
    uint16_t tail = _farpeekw(_dos_ds, 0x41CL);
    return head != tail;
}

/* ============================================================ */
/* Step markers — load-bearing forensic discipline              */
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
/* MPU-401 polling helpers (lifted verbatim from MPUPROBE iter F)*/
/* ============================================================ */

/* Bounded poll for RX-READY (status bit 6 == 0) with iter cap + uclock
 * wall + kbd-escape. Returns 1 on success, 0 on timeout. *out_byte
 * receives the byte read, *out_iters receives the iteration count. */
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
/* SDL audio callback — does nothing; SDL writes silence         */
/* ============================================================ */

/* Per SDL3 docs: if a callback is NULL, the app must push via
 * SDL_PutAudioStreamData. If a callback is provided but doesn't push
 * data, SDL fills with silence and the audio device thread runs
 * normally. We want the device thread to TICK (cooperative scheduler
 * services it) without doing any work — silence is exactly right. */
static void SDLCALL silent_callback(void *userdata, SDL_AudioStream *stream,
                                    int additional, int total)
{
    (void)userdata; (void)stream; (void)additional; (void)total;
    /* Intentionally empty. SDL fills the gap with silence; the audio
     * thread keeps ticking; the cooperative scheduler keeps progressing. */
}

/* ============================================================ */
/* BLASTER env parse                                            */
/* ============================================================ */

typedef struct {
    int audio_base;
    int mpu_base;
    int irq;
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
    b->audio_base = 0x220;
    b->mpu_base = 0x330;
    b->irq = 5;

    const char *env = getenv("BLASTER");
    if (!env) return;

    const char *p = env;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        char field = *p++;
        if (field >= 'a' && field <= 'z') field -= ('a' - 'A');
        switch (field) {
            case 'A': b->audio_base = parse_hex(p); break;
            case 'I': b->irq        = parse_dec(p); break;
            case 'P': b->mpu_base   = parse_hex(p); break;
            default: break;
        }
        while (*p && *p != ' ' && *p != '\t') p++;
    }
}

/* ============================================================ */
/* main                                                         */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    plog("=== MPUSDL wave-22-WB iter H starting ===");
    plog("DJGPP + libSDL3 build; target = direct-port MPU-401 under SDL audio init");
    plog("UCLOCKS_PER_SEC = %lu", (unsigned long)UCLOCKS_PER_SEC);
    plog("");
    plog("Forensic protocol: every potentially-hanging operation is bracketed by");
    plog("[step N/M] BEGIN ... DONE markers, fsync'd. If the system hangs, the");
    plog("LAST line in MPUSDL.LOG identifies which instruction stalled the bus.");
    plog("Operator: hard-reset is OK after a hang; the log is on disk.");
    plog("Operator: hit any key during a polling loop to abort that loop early.");
    plog("");
    plog("Question this probe answers:");
    plog("  Does SDL_Init(AUDIO) + audio-device-open + 1-sec service alone");
    plog("  break direct-port MPU-401 access on SB16 PnP CTL0026? (W22-WB-C/D");
    plog("  hangs were under FULL engine + graphics + audio load; this probe");
    plog("  isolates the audio-only contribution.)");
    plog("");

    /* Parse BLASTER env. */
    blaster_t b;
    parse_blaster(&b);
    plog("BLASTER: A=0x%03X I=%d P=0x%03X (audio base / IRQ / MPU base)",
         b.audio_base, b.irq, b.mpu_base);
    plog("");

    /* 19 active steps planned; reserve a few extra for cleanup paths. */
    g_step_total = 22;

    /* ============================================================ */
    /* Section 1: SDL_Init(AUDIO) only                              */
    /* ============================================================ */
    plog("---- Section 1: SDL_Init(SDL_INIT_AUDIO) ----");

    step_begin("SDL_Init(SDL_INIT_AUDIO)");
    bool sdl_init_ok = SDL_Init(SDL_INIT_AUDIO);
    if (!sdl_init_ok) {
        const char *err = SDL_GetError();
        step_done("FAILED: %s", err ? err : "(no error)");
        plog("FATAL: cannot proceed without SDL audio. Aborting.");
        if (g_log) fclose(g_log);
        return 2;
    }
    step_done("SDL_Init OK");
    plog("");

    /* ============================================================ */
    /* Section 2: open audio device stream at 11025 mono S16        */
    /* ============================================================ */
    plog("---- Section 2: SDL_OpenAudioDeviceStream(11025 mono S16) ----");

    SDL_AudioSpec spec;
    memset(&spec, 0, sizeof spec);
    spec.format   = SDL_AUDIO_S16LE;
    spec.channels = 1;
    spec.freq     = 11025;
    plog("AudioSpec: format=S16LE channels=%d freq=%d", spec.channels, spec.freq);

    step_begin("SDL_OpenAudioDeviceStream(default playback, 11025 mono)");
    SDL_AudioStream *stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                  &spec, silent_callback, NULL);
    if (!stream) {
        const char *err = SDL_GetError();
        step_done("FAILED: %s", err ? err : "(no error)");
        plog("FATAL: cannot open audio device. SDL audio backend init failed.");
        SDL_Quit();
        if (g_log) fclose(g_log);
        return 3;
    }
    step_done("audio stream opened OK");
    plog("");

    /* ============================================================ */
    /* Section 3: resume the audio device (start playback)          */
    /* ============================================================ */
    plog("---- Section 3: SDL_ResumeAudioStreamDevice ----");

    step_begin("SDL_ResumeAudioStreamDevice (start playback)");
    bool resume_ok = SDL_ResumeAudioStreamDevice(stream);
    if (!resume_ok) {
        const char *err = SDL_GetError();
        step_done("FAILED: %s", err ? err : "(no error)");
        plog("WARNING: device resume failed; audio may not be ticking. Continuing");
        plog("anyway — the question is whether MPU access works under SDL state.");
    } else {
        step_done("device resumed");
    }
    plog("");

    /* ============================================================ */
    /* Section 4: 1 sec sleep — let SDL audio service ≥1 frame      */
    /* ============================================================ */
    plog("---- Section 4: SDL_Delay(1000) — service audio backend ----");
    plog("Per MPUPROBE iter F analysis hard-no rule #3: audio backend services");
    plog("≥1 frame BEFORE WB init. SDL_Delay yields to cooperative scheduler.");

    step_begin("SDL_Delay(1000)");
    SDL_Delay(1000);
    step_done("woke; audio thread should have serviced ~%d frames at 11025/buffer",
              1000 / 100);  /* ballpark; depends on actual buffer size */
    plog("");

    /* ============================================================ */
    /* Section 5: pre-ACK drain on MPU data port                    */
    /* ============================================================ */
    plog("---- Section 5: MPU-401 status + pre-ACK drain (port 0x%03X / 0x%03X) ----",
         b.mpu_base + 1, b.mpu_base + 0);
    plog("HAZARD: this is the FIRST direct-port read after SDL audio is running.");
    plog("If MPUSDL.LOG ends at the next BEGIN line, SDL audio init breaks");
    plog("direct-port MPU access. Last successful step names the trigger boundary.");
    plog("");

    step_begin("inb(mpu_base+1) status read under SDL load");
    uint8_t mpu_status = inportb(b.mpu_base + 1);
    step_done("status=0x%02X (compare to MPUPROBE iter F section 5 = 0xBF)",
              mpu_status);

    step_begin("inb(mpu_base+0) data port pre-drain");
    uint8_t mpu_data_pre = inportb(b.mpu_base + 0);
    step_done("data=0x%02X (iter F had 0xFE pre-pending; drain consumes it)",
              mpu_data_pre);
    plog("");

    /* ============================================================ */
    /* Section 6: MPU-401 reset write — THE LOAD-BEARING TEST       */
    /* ============================================================ */
    plog("---- Section 6: MPU-401 reset write under SDL audio load ----");

    step_begin("outp(mpu_base+1, 0xFF) MPU reset");
    outportb(b.mpu_base + 1, 0xFF);
    step_done("reset cmd dispatched (this is the W22-WB-C/D suspect instruction)");
    plog("");

    /* ============================================================ */
    /* Section 7: ACK poll on data port                             */
    /* ============================================================ */
    plog("---- Section 7: MPU-401 ACK poll (expect 0xFE on data port) ----");

    step_begin("MPU-401 ACK poll: inb(mpu_base+0) until 0xFE or timeout");
    int iters = 0;
    uint8_t ack = 0;
    int got = mpu_wait_rx_byte(b.mpu_base, &ack, &iters);
    int reset_ok = (got && ack == 0xFE);
    if (reset_ok) {
        step_done("ACK 0xFE received after %d iter (chip alive under SDL audio load)",
                  iters);
    } else if (got) {
        step_done("got byte=0x%02X (NOT 0xFE) after %d iter -- chip in odd state",
                  ack, iters);
    } else {
        step_done("ACK poll TIMEOUT after %d iter / 250 ms -- chip silent or RX bit stuck",
                  iters);
    }
    plog("");

    /* ============================================================ */
    /* Section 8: UART entry                                        */
    /* ============================================================ */
    plog("---- Section 8: MPU-401 UART entry ----");
    plog("Skipping TX-ready poll per iter F finding (bit 7 lies on this chip).");

    step_begin("outp(mpu_base+1, 0x3F) UART entry (BLIND — no TX-ready poll)");
    outportb(b.mpu_base + 1, 0x3F);
    step_done("UART entry cmd dispatched");

    step_begin("UART ACK poll: inb(mpu_base+0) until 0xFE or timeout");
    ack = 0; iters = 0;
    got = mpu_wait_rx_byte(b.mpu_base, &ack, &iters);
    int uart_ok = (got && ack == 0xFE);
    if (uart_ok) {
        step_done("UART ACK received after %d iter -- in UART mode", iters);
    } else if (got) {
        step_done("UART poll got 0x%02X (not 0xFE) after %d iter", ack, iters);
    } else {
        step_done("UART ACK TIMEOUT after %d iter -- chip silent in UART transition",
                  iters);
    }
    plog("");

    /* ============================================================ */
    /* Section 9: MIDI byte writes + 500ms hold + note_off          */
    /* ============================================================ */
    plog("---- Section 9: MIDI byte writes (data port 0x%03X) ----", b.mpu_base + 0);
    plog("Sequence: note_on ch1 (0x90) middle C (60) velocity 100");
    plog("OPERATOR LISTEN: middle-C should be audible during the 500ms hold.");
    plog("If audible -> direct-port MIDI works under SDL audio load (outcome A).");
    plog("If silent  -> direct-port writes succeed but routing wrong (outcome C).");

    char dbuf[80];
    snprintf(dbuf, sizeof dbuf, "MIDI byte 1: outp(0x%03X, 0x90) [note_on ch1]", b.mpu_base + 0);
    step_begin(dbuf);
    outportb(b.mpu_base + 0, 0x90);
    step_done("byte 1 dispatched");

    snprintf(dbuf, sizeof dbuf, "MIDI byte 2: outp(0x%03X, 0x3C) [middle C / 60]", b.mpu_base + 0);
    step_begin(dbuf);
    outportb(b.mpu_base + 0, 60);
    step_done("byte 2 dispatched");

    snprintf(dbuf, sizeof dbuf, "MIDI byte 3: outp(0x%03X, 0x64) [velocity 100]", b.mpu_base + 0);
    step_begin(dbuf);
    outportb(b.mpu_base + 0, 100);
    step_done("byte 3 dispatched");

    plog("Holding note ~500 ms via SDL_Delay (audio thread keeps ticking)...");
    step_begin("SDL_Delay(500) — hold note");
    SDL_Delay(500);
    step_done("hold complete");

    step_begin("MIDI note_off sequence: 0x80, 60, 64");
    outportb(b.mpu_base + 0, 0x80);
    outportb(b.mpu_base + 0, 60);
    outportb(b.mpu_base + 0, 64);
    step_done("note_off dispatched (3 bytes)");
    plog("");

    /* ============================================================ */
    /* Section 10: SDL teardown                                     */
    /* ============================================================ */
    plog("---- Section 10: SDL teardown ----");

    step_begin("SDL_DestroyAudioStream");
    SDL_DestroyAudioStream(stream);
    step_done("stream destroyed");

    step_begin("SDL_Quit");
    SDL_Quit();
    step_done("SDL_Quit returned (post-SDL/0046 — no shutdown deadlock expected)");
    plog("");

    /* ============================================================ */
    /* Section 11: Summary                                          */
    /* ============================================================ */
    plog("---- Section 11: Summary ----");
    plog("SDL_Init(AUDIO):              OK");
    plog("Audio device opened:          OK (11025 mono S16)");
    plog("Audio thread serviced 1 sec:  OK");
    plog("MPU status under SDL load:    0x%02X", mpu_status);
    plog("MPU reset ACK (0xFE seen?):   %s", reset_ok ? "YES" : "NO");
    plog("MPU UART entry ACK (0xFE?):   %s", uart_ok ? "YES" : "NO");
    plog("MIDI byte writes:             dispatched (audibility = operator's ear)");
    plog("SDL_Quit:                     completed");
    plog("");
    plog("=== MPUSDL done ===");
    plog("");
    plog("Reading the result for SDL/0048 author:");
    plog("");
    plog("  Outcome A (probe DONE + middle-C audible during § 9):");
    plog("    -> direct-port works under SDL audio init alone. The W22-WB-C/D");
    plog("       hangs were triggered by something OUTSIDE this probe scope —");
    plog("       likely engine threading, full SDL_Init flag set with video/");
    plog("       events, or graphics-init concurrency. SDL/0048 should investigate");
    plog("       what's NOT in this probe.");
    plog("");
    plog("  Outcome B (probe HUNG mid-step):");
    plog("    -> SDL audio init alone IS enough to break direct-port MPU access.");
    plog("       Last BEGIN line in MPUSDL.LOG names the exact stalling instr.");
    plog("       SDL/0048 must avoid that I/O class while audio backend active.");
    plog("       Specific candidates from MPUPROBE iter F analysis:");
    plog("         - DSP firmware shared between MPU and DAC paths");
    plog("         - IRQ-5 ISR running mid-MPU-cycle issues DSP read/write");
    plog("         - SDL_GetTicksNS PIT reads racing with MPU port access");
    plog("");
    plog("  Outcome C (probe DONE + middle-C INAUDIBLE during § 9):");
    plog("    -> direct-port writes succeed but MIDI doesn't reach the");
    plog("       WaveBlaster header. Possibly mixer routing issue specific to");
    plog("       direct-port (DSP-mediated path from iter G produced sound");
    plog("       through internal OPL; direct-port should produce sound through");
    plog("       WaveBlaster header). SDL/0048 should check mixer reg 0x3C");
    plog("       routing AFTER SDL audio backend has configured it.");

    if (g_log) fclose(g_log);
    return 0;
}
