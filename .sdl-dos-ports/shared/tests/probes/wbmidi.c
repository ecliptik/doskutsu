/*
 * wbmidi.c -- WaveBlaster MIDI sanity probe (Phase 11 wave-40 task #29).
 *
 * MISSION: validate that g2k's DreamBlaster S2 daughterboard (on the
 * SB16 WaveBlaster header) receives + plays a MIDI Note On byte sequence
 * via the MPU-401 UART-mode ports (0x330 data, 0x331 status/command).
 * One-shot output-only probe; no IRQ hook, no DMA, no SDL link.
 *
 * WAVE-41+ PATCH ACTIONABILITY: this probe is the FIRST sanity gate for
 * the wave-41+ WaveBlaster MIDI offload pipeline (Organya synth -> MIDI
 * events -> WaveBlaster hardware FM/wavetable; bypasses SDL_mixer's
 * mix loop entirely for music). Per audio_offload_options_for_50fps.md,
 * WaveBlaster MIDI offload is the user-preferred direction (2026-04-30);
 * estimated -5 to -10 ms/flip on music-heavy scenes. If WBMIDI fails
 * (no audible note OR MPU-401 status timeout), the entire offload
 * pipeline is gated to wave-43+ pending hardware/driver investigation.
 *
 * SUCCESS CRITERIA (operator-side):
 *   - WBMIDI.LOG shows [wbmidi SUITE_DONE verdict=PROBE_COMPLETED]
 *   - Operator hears a 1-second middle-C tone from the DreamBlaster S2
 *     output during the wait_secs=1.0 stage
 *
 * FAILURE MODES (each pinpointed by per-stage emit per
 * defensive_rewrite_requires_identified_failure_stage memory):
 *   - midi_send_TIMEOUT at any byte: MPU-401 status port not clearing
 *     bit 6 (TX-READY); could be MPU-401 not in UART mode, or
 *     daughterboard not present, or port conflict with SB16 DSP
 *   - SUITE_DONE but NO AUDIBLE NOTE: bytes accepted by MPU-401 but
 *     WaveBlaster header not wired or daughterboard not powered
 *   - SUITE_DONE WITH AUDIBLE NOTE: hardware path validated; wave-41+
 *     Organya->MIDI converter authoring proceeds
 *
 * METHODOLOGY (single mechanism per single_lever_per_binary_or_else_
 * attribution_impossible.md):
 *   1. Emit startup_ok marker
 *   2. Note On (0x90, 60, 100) -- channel 1, middle C, velocity 100
 *   3. Wait 1 sec (operator audibility window)
 *   4. Note Off (0x80, 60, 0) -- channel 1, middle C, velocity 0
 *   5. Emit SUITE_DONE verdict + sentinel
 *
 * Per-stage emits between every outportb so any hang location is
 * identifiable on first iter (avoids the AUDRQ wave-38 pattern of
 * hung-but-where-unknown).
 *
 * 8.3 DOS filenames:
 *   Source: tests/probes/wbmidi.c
 *   Binary: WBMIDI.EXE   (6+3)
 *   Log:    WBMIDI.LOG   (6+3)
 *   BAT:    WBMIDI.BAT   (6+3)
 *
 * License: MIT.
 */

#include <dos.h>
#include <pc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* MPU-401 ports (typical SB16 WaveBlaster header configuration).
 * Per W22-WB-F probe analysis (docs/MPUPROBE-W22WB-F-ANALYSIS.md): the
 * status port bit 7 (TX-READY) on real SB16 PnP CTL0026 never clears
 * (load-bearing issue). The DSP-mediated MIDI path (cmd 0x34 + 0x38)
 * is the production fix. This sanity probe attempts DIRECT MPU-401
 * port access FIRST (~30 LOC); if it works, the wave-41 design can
 * simplify. If it times out at midi_send_TIMEOUT, the production path
 * must use DSP-mediated MIDI (per SDL/0047 patch). */
#define MPU_DATA    0x330  /* MPU-401 data port (MIDI byte writes) */
#define MPU_STATUS  0x331  /* MPU-401 status/command port */

/* ============================================================ */
/* Logging                                                       */
/* ============================================================ */

static FILE *g_log = NULL;

static void open_log(void)
{
    g_log = fopen("WBMIDI.LOG", "w");
    if (!g_log) g_log = fopen("C:\\WBMIDI.LOG", "w");
}

static void plog(const char *fmt, ...)
{
    char buf[256];
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

static double now_secs(void)
{
    return (double)uclock() / (double)UCLOCKS_PER_SEC;
}

/* ============================================================ */
/* MIDI byte send via MPU-401 UART mode                          */
/* ============================================================ */

/* Send one MIDI byte to MPU-401 data port. Polls status port bit 6
 * (TX-FULL flag: 1 = busy, 0 = ready) until clear, then writes.
 * Returns 0 on success, -1 on timeout. */
static int midi_send(uint8_t byte, const char *label)
{
    plog("[wbmidi STAGE=midi_send_begin label=%s byte=0x%02X]", label, byte);
    int timeout = 10000;
    while (timeout-- > 0) {
        /* MPU-401 status port bit 6: 1 = TX buffer full (busy), 0 = ready.
         * Some references invert this; the SB16 reference manual has bit 6
         * as TX-FULL active-high. */
        if ((inportb(MPU_STATUS) & 0x40) == 0) {
            outportb(MPU_DATA, byte);
            plog("[wbmidi STAGE=midi_send_ok label=%s]", label);
            return 0;
        }
    }
    plog("[wbmidi STAGE=midi_send_TIMEOUT label=%s byte=0x%02X timeout_iters=10000]",
         label, byte);
    return -1;
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    plog("=== wbmidi (wave-40 task #29: WaveBlaster MIDI sanity probe) ===");
    plog("DJGPP pure-C; target g2k DreamBlaster S2 on SB16 WaveBlaster header");
    plog("MPU-401 ports: data=0x%X status=0x%X", MPU_DATA, MPU_STATUS);
    plog("Mission: send Note On / wait 1 sec / Note Off; verify audible middle C");
    plog("Per single_lever_per_binary discipline: SINGLE MECHANISM (no IRQ/DMA/SDL)");
    plog("");
    plog("[wbmidi STAGE=startup_ok]");

    /* Read initial MPU-401 status for diagnostic context. */
    uint8_t status_initial = inportb(MPU_STATUS);
    plog("[wbmidi STAGE=mpu_status_read initial_status=0x%02X bit6_busy=%d bit7=%d]",
         status_initial,
         (status_initial >> 6) & 1,
         (status_initial >> 7) & 1);

    /* Note On: status byte 0x90 (Note On + channel 1), pitch 60 (middle C),
     * velocity 100. */
    plog("[wbmidi STAGE=note_on_begin]");
    int rc = 0;
    rc |= midi_send(0x90, "note_on_status_ch1");
    rc |= midi_send(60,   "note_on_pitch_60");
    rc |= midi_send(100,  "note_on_velocity_100");
    if (rc != 0) {
        plog("[wbmidi STAGE=note_on_FAILED rc=%d]", rc);
        plog("[wbmidi SUITE_DONE verdict=REFUTE_MPU_TIMEOUT]");
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 1;
    }
    plog("[wbmidi STAGE=note_on_ok]");

    /* Wait 1 sec for operator-audible window. */
    plog("[wbmidi STAGE=wait_begin secs=1.0]");
    double t0 = now_secs();
    while (now_secs() - t0 < 1.0) {
        /* Spin; no IRQ hook in this probe so just wall-clock. */
        (void)inportb(0x80);
    }
    double elapsed = now_secs() - t0;
    plog("[wbmidi STAGE=wait_ok elapsed=%.3f]", elapsed);

    /* Note Off: status byte 0x80 (Note Off + channel 1), pitch 60, velocity 0. */
    plog("[wbmidi STAGE=note_off_begin]");
    rc = 0;
    rc |= midi_send(0x80, "note_off_status_ch1");
    rc |= midi_send(60,   "note_off_pitch_60");
    rc |= midi_send(0,    "note_off_velocity_0");
    if (rc != 0) {
        plog("[wbmidi STAGE=note_off_FAILED rc=%d]", rc);
        plog("[wbmidi SUITE_DONE verdict=PARTIAL_NOTE_OFF_FAILED]");
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 1;
    }
    plog("[wbmidi STAGE=note_off_ok]");

    /* Final MPU-401 status read. */
    uint8_t status_final = inportb(MPU_STATUS);
    plog("[wbmidi STAGE=mpu_status_final_read final_status=0x%02X]", status_final);

    plog("");
    plog("[wbmidi SUITE_DONE verdict=PROBE_COMPLETED]");
    plog("");
    plog("Decomp guide:");
    plog("  verdict=PROBE_COMPLETED + operator-confirmed-audible-note");
    plog("    -> WaveBlaster hardware path validated; wave-41 Organya->MIDI");
    plog("       converter authoring proceeds.");
    plog("  verdict=PROBE_COMPLETED + operator-confirmed-SILENCE");
    plog("    -> MPU-401 accepted bytes but daughterboard not wired/powered;");
    plog("       hardware check needed before wave-41 authoring.");
    plog("  verdict=REFUTE_MPU_TIMEOUT");
    plog("    -> MPU-401 status bit 6 never cleared (W22-WB-F pattern;");
    plog("       production must use DSP-mediated MIDI per SDL/0047 patch).");
    plog("       wave-41 design pivots to DSP-mediated path.");

    plog("[SENTINEL_END]");
    if (g_log) fclose(g_log);
    return 0;
}
