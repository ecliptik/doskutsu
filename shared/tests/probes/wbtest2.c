/*
 * wbtest2.c -- WaveBlaster MIDI 4-path probe (WBTEST-002).
 *
 * v1.0.2 task #4. Standalone DJGPP; no SDL, no engine. Parallel file to
 * wbtest.c (WBTEST-001) per the bltpat / bltpat-v2 precedent: each iter's
 * source kept on disk as historical reference.
 *
 * REVISION rationale: WBTEST-001 had a probe-faithfulness defect (PATH=D
 * skipped the 0x3F UART entry write that production SDL/0044+0047 makes at
 * vendor/SDL/src/core/dos/SDL_dos_audio_synth.c L182). g2k iter result
 * (PATH=D silent, PATH=P silent) was therefore uninformative for PATH=D
 * and IS informative for PATH=P (byte-faithful + silent = strong signal
 * that DSP cmd 0x34/0x38 sequence doesn't produce audible MIDI on Vibra16S
 * CT2490).
 *
 * Operator-authoritative context (2026-05-26):
 *   - WaveBlaster has NEVER actually produced wavetable music on g2k.
 *   - PicoGUS card physically REMOVED for this debug iter. Reinstalled (in
 *     USB mode) only once WB works. H1 (PicoGUS-as-root-cause) eliminated.
 *
 * Remaining hypothesis cluster (post-WBTEST-001 analysis by flush-instr):
 *   H2: Vibra16S CT2490 WB-header routing not enabled by default. Mixer reg
 *       dump reveals which bit gates the WB output. NOTE: reg 0x3C is
 *       EMPIRICALLY STICKY / write-protected on CT2490 per W22-WB-F; this
 *       probe DUMPS it (read-only) and does NOT attempt to write-flip.
 *   H3: S2 needs GM init beyond GM Reset sysex (Master Volume RPN +
 *       channel-volume CC07). Folded into per-path sequence.
 *   H4: PATH=D probe-faithfulness (CORRECTED in PATH=D below).
 *   H5: DSP cmd 0x34/0x38 don't work as Creative-documented on this clone.
 *       Tested by PATH=P (byte-faithful) + PATH=PI (with MPU reset prelude).
 *   H6: Standard MPU-401 power-up needs full reset + ACK dance to engage
 *       UART (blind 0x3F may write into intelligent-mode-command-FIFO).
 *       Tested by PATH=DI (full dance) vs PATH=D (blind only).
 *
 * IMPORTANT CAVEAT: W22-WB-F "works" verdicts on dispatch paths were
 * dispatch-without-timeout, NOT audible. Same failure class as WBTEST-001's
 * silent passes. Treat W22-WB-F as showing the chip ACCEPTS the byte
 * sequences, NOT that the WaveBlaster header actually voices them. The
 * operator's ear on real HW is the only audibility witness.
 *
 * ==========================================================================
 * PROBE-FAITHFULNESS AUDIT TABLE (traced against vendor/SDL POST-PATCH)
 * ==========================================================================
 *
 * | path | init byte stream                              | per-byte stream                       | faithful? | rationale                                                                                                       |
 * |------|-----------------------------------------------|---------------------------------------|-----------|-----------------------------------------------------------------------------------------------------------------|
 * |  D   | outportb(mpu+1, 0x3F)                         | outportb(mpu+0, byte)                 | YES       | matches synth.c L182 (default blind branch) + L260 (per-byte blind). NO status polling.                         |
 * |  DI  | outportb(mpu+1, 0xFF) + poll mpu+0 for 0xFE   | outportb(mpu+0, byte)                 | NO        | tests H6. Production SKIPS the inportb dance on Vibra16S per W22-WB-D post-mortem (the inportb stalls the bus). |
 * |      | + outportb(mpu+1, 0x3F) + poll mpu+0 for 0xFE |                                       |           | We use bounded ACK-byte-on-DATA-PORT polling (~5ms wall cap) instead of status-bit polling, per team-lead       |
 * |      |                                               |                                       |           | refinement: MPU status bit 7 lies on this chip; polling the DATA port directly for 0xFE byte is the alternative.|
 * |  P   | dsp_reset (100us pulse hold, sb.c L508)       | dsp_write(0x38) + dsp_write(byte)     | YES       | matches sb.c L568 SDL_DOSAudioSB_DSPMidiEnterUART + L577 DSPMidiWriteByte. dsp_reset stands in for              |
 * |      | + dsp_write(0x34)                             |                                       |           | DOSSOUNDBLASTER_OpenDevice's prior ResetSoundBlasterDSP (audio backend open precondition).                      |
 * |  PI  | outportb(mpu+1, 0xFF) + 5ms wait + dsp_reset  | dsp_write(0x38) + dsp_write(byte)     | NO        | tests H6 from DSP angle. Outportb-only MPU access on init (no inportb -> no stall risk).                        |
 * |      | + dsp_write(0x34)                             |                                       |           |                                                                                                                 |
 *
 * Deltas from WBTEST-001:
 *   - PATH=D ADDED outportb(mpu+1, 0x3F) at init (the documented defect).
 *   - PATH=P dsp_reset hold 10us -> 100us to match sb.c L508 production.
 *   - PATH=DI + PATH=PI new.
 *   - ACK polling switched from status-bit (mpu+1 bit 7) to direct DATA
 *     port (mpu+0) byte == 0xFE detection with ~5ms wall cap, per
 *     team-lead refinement.
 *
 * ==========================================================================
 * PER-PATH SEQUENCE (mirror across all four)
 * ==========================================================================
 *
 *   1. GM Universal Sysex Master Volume MAX: F0 7F 7F 04 01 7F 7F F7
 *      (LL=0x7F LSB MSB; master vol max). Tests H3.
 *   2. CC 0x07 channel volume MAX on ch 0:  B0 07 7F
 *   3. CC 0x07 channel volume MAX on ch 9:  B9 07 7F
 *   4. Program change ch 0 -> 0 (Acoustic Grand Piano):  C0 00
 *   5. Note On ch 0 C4 vel 100 (90 3C 64) -- hold 1.5s
 *   6. Note On ch 0 E4 vel 100 (90 40 64) -- hold 1.5s
 *   7. Note On ch 0 G4 vel 100 (90 43 64) -- hold 2.0s (full C-E-G triad)
 *   8. Note Off ch 0 C4/E4/G4 (80 3C 00 / 80 40 00 / 80 43 00)
 *   9. GM drum-kit test: Note On ch 9 note 36 vel 100 (99 24 64),
 *      hold 1.0s, Note Off ch 9 note 36 vel 0 (89 24 00).
 *      Isolates "voice engine on?" from "program/bank correct?".
 *  10. CC 123 (All Notes Off) on ch 0 + ch 9 (B0 7B 00 / B9 7B 00)
 *  11. CC 120 (All Sound Off) on ch 0 + ch 9 (B0 78 00 / B9 78 00)
 *  12. Read mixer reg 0x82 post-test (IRQ-status).
 *
 * Per-path runtime: ~6 sec. PATH=A total: ~28 sec.
 *
 * ==========================================================================
 * BOOT DIAGNOSTICS
 * ==========================================================================
 *
 *   - BLASTER env parse (A/I/D/H/T/P)
 *   - DSP reset + version query (warn if < 4.x)
 *   - MPU-401 status raw read (informational; per W22-WB-F bit 7 lies)
 *   - CT1745 mixer reg dump using mpuwbprobe.c's PROVEN reg list verbatim
 *     ([[grep_existing_before_new_design]]; cross-anchor reuse). 38 regs:
 *     0x00 0x04 0x06 0x08 0x0A 0x0C 0x0E 0x22 0x26 0x28 0x2E 0x30 0x31
 *     0x32 0x33 0x34 0x35 0x36 0x37 0x38 0x39 0x3A 0x3B 0x3C 0x3D 0x3E
 *     0x3F 0x40 0x41 0x42 0x43 0x44 0x45 0x46 0x47 0x80 0x81 0x82 0x83.
 *     READ-ONLY -- no mixer writes (0x3C is sticky/write-protected on
 *     CT2490 per W22-WB-F; flip attempts are someone else's lane after
 *     operator audibility identifies the bug class).
 *
 * ==========================================================================
 * argv MODE SELECT
 * ==========================================================================
 *
 *   WBTEST2 D   = PATH=D only  (byte-faithful direct-port blind)
 *   WBTEST2 I   = PATH=DI only (direct + full reset/ACK dance; HAZARD)
 *   WBTEST2 P   = PATH=P only  (byte-faithful DSP-mediated)
 *   WBTEST2 X   = PATH=PI only (DSP + MPU reset prelude)
 *   WBTEST2 A   = all four sequentially (DEFAULT)
 *
 * ==========================================================================
 * OPERATOR PROTOCOL
 * ==========================================================================
 *
 * Per path, ear-report:
 *   piano       = wavetable working + GM bank correct
 *   drum_only   = voice engine alive but program-change ignored / wrong bank
 *   boops/beeps = bytes reach synth but framing wrong
 *   silence     = chain broken before voice engine OR muted at routing
 *   HANG        = note which banner was last in WBTEST2.LOG; that names the byte
 *
 * 8.3 DOS filenames:
 *   Source: tests/probes/wbtest2.c  (host-side, gitignored)
 *   Binary: WBTEST2.EXE             (7+3)
 *   Log:    WBTEST2.LOG             (7+3)
 *   BAT:    WBTEST2.BAT             (7+3)
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
/* Logging                                                       */
/* ============================================================ */

static FILE *g_log = NULL;

static void open_log(void)
{
    g_log = fopen("WBTEST2.LOG", "w");
    if (!g_log) g_log = fopen("C:\\WBTEST2.LOG", "w");
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

static void sleep_secs(double s)
{
    double t_end = now_secs() + s;
    while (now_secs() < t_end) (void)inportb(0x80);
}

/* ============================================================ */
/* BLASTER env parse                                            */
/* ============================================================ */

typedef struct {
    int audio_base, irq, dma_lo, dma_hi, mpu_base, sb_type;
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
    b->audio_base = 0x220; b->irq = 5; b->dma_lo = 1; b->dma_hi = 5;
    b->mpu_base = 0x330; b->sb_type = 6; b->raw[0] = 0;

    const char *env = getenv("BLASTER");
    if (!env) return;

    strncpy(b->raw, env, sizeof b->raw - 1);
    b->raw[sizeof b->raw - 1] = 0;

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
/* SB16 / CT1745 mixer access (audio_base + 0x4 = address,      */
/* +0x5 = data). Verbatim from mpuwbprobe.c -- same protocol     */
/* proven on CT2490 + DSP v4.13 in W22-WB-F. READ ONLY here;     */
/* per team-lead refinement, do NOT attempt to write-flip 0x3C   */
/* (empirically sticky/write-protected on CT2490).               */
/* ============================================================ */

static uint8_t mixer_read(int audio_base, uint8_t reg)
{
    outportb(audio_base + 0x4, reg);
    return inportb(audio_base + 0x5);
}

/* ============================================================ */
/* SB16 DSP I/O. Production sb.c ResetSoundBlasterDSP uses 100us */
/* reset pulse hold (L508); we match.                            */
/* ============================================================ */

static int dsp_wait_write(int audio_base)
{
    int port = audio_base + 0xC;
    double t0 = now_secs();
    for (int i = 0; i < 100000; i++) {
        if (!(inportb(port) & 0x80)) return 1;
        if ((i & 0xFF) == 0 && (now_secs() - t0) > 0.25) return 0;
    }
    return 0;
}

static int dsp_wait_read(int audio_base)
{
    int port = audio_base + 0xE;
    double t0 = now_secs();
    for (int i = 0; i < 100000; i++) {
        if (inportb(port) & 0x80) return 1;
        if ((i & 0xFF) == 0 && (now_secs() - t0) > 0.25) return 0;
    }
    return 0;
}

static int dsp_reset(int audio_base)
{
    int reset_port = audio_base + 0x6;
    outportb(reset_port, 1);
    /* 100us hold -- matches production ResetSoundBlasterDSP (sb.c L508). */
    double t_delay = now_secs() + 0.0001;
    while (now_secs() < t_delay) { /* spin */ }
    outportb(reset_port, 0);

    if (!dsp_wait_read(audio_base)) return -1;
    uint8_t b = inportb(audio_base + 0xA);
    return (b == 0xAA) ? 0 : -2;
}

static int dsp_write_byte(int audio_base, uint8_t cmd)
{
    if (!dsp_wait_write(audio_base)) return -1;
    outportb(audio_base + 0xC, cmd);
    return 0;
}

static int dsp_read_byte(int audio_base, uint8_t *out)
{
    if (!dsp_wait_read(audio_base)) return -1;
    *out = inportb(audio_base + 0xA);
    return 0;
}

/* ============================================================ */
/* MPU-401 ACK polling -- DATA PORT direct, per team-lead refine.*/
/*                                                               */
/* W22-WB-F established that the MPU status reg bit 7 is         */
/* unreliable on Vibra16S CT2490. Production SDL/0047 direct     */
/* branch dropped status-bit polling entirely for that reason.   */
/*                                                               */
/* For ACK detection (post-reset 0xFE), poll the DATA port       */
/* (mpu_base+0) directly with a bounded wall-clock cap of ~5ms.  */
/* When no byte is ready the data port typically returns 0xFF    */
/* (ISA open-bus pattern); when the chip dispatches the ACK we   */
/* read 0xFE. Either result is informative; we log iters + last  */
/* value seen.                                                   */
/*                                                               */
/* Returns: 1 = saw 0xFE (ACK), 0 = saw non-FE non-FF after      */
/*          some iter (odd chip state), -1 = timed out (only saw */
/*          0xFF for full 5ms = no chip response).               */
/* ============================================================ */

static int mpu_wait_ack_data_port(int mpu_base, int *out_iters, uint8_t *out_last)
{
    int data_port = mpu_base + 0;
    double t0 = now_secs();
    uint8_t last = 0xFF;
    int i;
    for (i = 0; i < 200000; i++) {
        uint8_t b = inportb(data_port);
        last = b;
        if (b == 0xFE) {
            if (out_iters) *out_iters = i;
            if (out_last) *out_last = b;
            return 1;
        }
        /* Some chips dispatch a non-FE response code; treat as informative. */
        if (b != 0xFF && b != 0xFE) {
            if (out_iters) *out_iters = i;
            if (out_last) *out_last = b;
            return 0;
        }
        /* Wall-clock cap: ~5ms (W22-WB-F observed ACK arrived within ~1ms
         * when it arrived at all; 5ms is 5x budget). */
        if ((i & 0x3F) == 0 && (now_secs() - t0) > 0.005) {
            if (out_iters) *out_iters = i;
            if (out_last) *out_last = last;
            return -1;
        }
    }
    if (out_iters) *out_iters = i;
    if (out_last) *out_last = last;
    return -1;
}

/* ============================================================ */
/* PATH state + per-path init dispatchers                       */
/* ============================================================ */

typedef enum { PATH_D = 0, PATH_DI = 1, PATH_P = 2, PATH_PI = 3 } path_t;

static int g_mpu_base = 0x330;
static int g_audio_base = 0x220;
static int g_path_init_failed = 0;

static const char *path_tag(path_t p)
{
    switch (p) {
        case PATH_D:  return "D";
        case PATH_DI: return "DI";
        case PATH_P:  return "P";
        case PATH_PI: return "PI";
    }
    return "?";
}

/* ---- PATH=D : byte-faithful direct-port (synth.c L182 + L260) ---- */
static int path_d_init(int mpu_base)
{
    g_mpu_base = mpu_base;
    g_path_init_failed = 0;
    plog("[wbtest2 path=D init] outportb(mpu+1=0x%03X, 0x3F) -- UART entry"
         " (faithful to synth.c L182)", mpu_base + 1);
    outportb(mpu_base + 1, 0x3F);
    plog("[wbtest2 path=D init] done; per-byte blind outportb to data 0x%03X",
         mpu_base + 0);
    return 0;
}

/* ---- PATH=DI : full reset + 0xFE ACK + UART + 0xFE ACK ---- */
static int path_di_init(int mpu_base)
{
    g_mpu_base = mpu_base;
    g_path_init_failed = 0;

    plog("[wbtest2 path=DI init] HAZARD: PATH=DI uses bounded-poll ACK on"
         " DATA port (no inportb on mpu+1 status reg -- per team-lead refine,"
         " polling data port for 0xFE byte sidesteps W22-WB-F status-bit-lies)");

    /* Reset MPU. */
    plog("[wbtest2 path=DI init] outportb(mpu+1=0x%03X, 0xFF) -- reset",
         mpu_base + 1);
    outportb(mpu_base + 1, 0xFF);

    /* Poll DATA port for 0xFE ACK (5ms wall cap). */
    plog("[wbtest2 path=DI init] polling mpu+0=0x%03X for 0xFE ACK (5ms cap)",
         mpu_base + 0);
    int iters = 0; uint8_t last = 0;
    int rc1 = mpu_wait_ack_data_port(mpu_base, &iters, &last);
    plog("[wbtest2 path=DI init] reset-ack rc=%d iters=%d last_byte=0x%02X"
         " (rc=1=saw 0xFE; rc=0=saw other; rc=-1=only 0xFF)",
         rc1, iters, last);

    /* UART entry. */
    plog("[wbtest2 path=DI init] outportb(mpu+1=0x%03X, 0x3F) -- UART entry",
         mpu_base + 1);
    outportb(mpu_base + 1, 0x3F);

    /* Poll DATA port for 0xFE UART-ack. */
    plog("[wbtest2 path=DI init] polling mpu+0 for UART-ack 0xFE (5ms cap)");
    iters = 0; last = 0;
    int rc2 = mpu_wait_ack_data_port(mpu_base, &iters, &last);
    plog("[wbtest2 path=DI init] uart-ack rc=%d iters=%d last_byte=0x%02X",
         rc2, iters, last);

    plog("[wbtest2 path=DI init] done; continuing to musical phrase regardless"
         " of ACK status -- the ACK info is forensic, not gating");
    return 0;
}

/* ---- PATH=P : byte-faithful DSP-mediated ---- */
static int path_p_init(int audio_base)
{
    g_audio_base = audio_base;
    g_path_init_failed = 0;

    plog("[wbtest2 path=P init] dsp_reset (100us hold; matches sb.c L508)");
    int rc = dsp_reset(audio_base);
    if (rc != 0) {
        plog("[wbtest2 path=P init] dsp_reset FAILED rc=%d -- skipping path", rc);
        g_path_init_failed = 1;
        return -1;
    }
    plog("[wbtest2 path=P init] dsp_reset OK (0xAA)");

    plog("[wbtest2 path=P init] dsp_write(0x34) -- UART entry"
         " (mirrors SDL_DOSAudioSB_DSPMidiEnterUART, sb.c L568)");
    if (dsp_write_byte(audio_base, 0x34) != 0) {
        plog("[wbtest2 path=P init] DSP cmd 0x34 FAILED");
        g_path_init_failed = 1;
        return -1;
    }
    plog("[wbtest2 path=P init] done");
    return 0;
}

/* ---- PATH=PI : MPU reset prelude + DSP-mediated ---- */
static int path_pi_init(int mpu_base, int audio_base)
{
    g_mpu_base = mpu_base;
    g_audio_base = audio_base;
    g_path_init_failed = 0;

    plog("[wbtest2 path=PI init] outportb-only MPU prelude (no inportb on"
         " mpu+1; no stall risk per W22-WB-D)");
    plog("[wbtest2 path=PI init] outportb(mpu+1=0x%03X, 0xFF) -- MPU reset"
         " prelude (H6 from DSP angle)", mpu_base + 1);
    outportb(mpu_base + 1, 0xFF);
    sleep_secs(0.005);

    plog("[wbtest2 path=PI init] dsp_reset (100us hold)");
    int rc = dsp_reset(audio_base);
    if (rc != 0) {
        plog("[wbtest2 path=PI init] dsp_reset FAILED rc=%d -- skipping", rc);
        g_path_init_failed = 1;
        return -1;
    }
    plog("[wbtest2 path=PI init] dsp_reset OK");

    plog("[wbtest2 path=PI init] dsp_write(0x34) -- UART entry");
    if (dsp_write_byte(audio_base, 0x34) != 0) {
        plog("[wbtest2 path=PI init] DSP cmd 0x34 FAILED");
        g_path_init_failed = 1;
        return -1;
    }
    plog("[wbtest2 path=PI init] done");
    return 0;
}

/* Path-agnostic per-byte sender. */
static void send_byte(path_t p, uint8_t byte)
{
    if (g_path_init_failed) return;
    switch (p) {
        case PATH_D:
        case PATH_DI:
            outportb(g_mpu_base + 0, byte);
            break;
        case PATH_P:
        case PATH_PI:
            (void)dsp_write_byte(g_audio_base, 0x38);
            (void)dsp_write_byte(g_audio_base, byte);
            break;
    }
}

static void send_bytes(path_t p, const uint8_t *bytes, int n, const char *event)
{
    char hexbuf[80];
    int off = 0;
    for (int i = 0; i < n && off < (int)sizeof(hexbuf) - 4; i++) {
        off += snprintf(hexbuf + off, sizeof(hexbuf) - off,
                        "%s%02X", i ? " " : "", bytes[i]);
    }
    plog("[wbtest2 path=%s EVENT=%s bytes=[%s]]", path_tag(p), event, hexbuf);
    for (int i = 0; i < n; i++) send_byte(p, bytes[i]);
}

/* ============================================================ */
/* Per-path musical phrase                                      */
/* ============================================================ */

static void run_path(path_t p, const blaster_t *b)
{
    const char *pt = path_tag(p);

    plog("");
    plog("===========================================================");
    plog("=== PATH=%-2s  ~6 sec audio (piano triad ~5s + drum ~1s) ===", pt);
    plog("===========================================================");
    plog("");
    sleep_secs(0.5);

    int init_rc = 0;
    switch (p) {
        case PATH_D:  init_rc = path_d_init(b->mpu_base); break;
        case PATH_DI: init_rc = path_di_init(b->mpu_base); break;
        case PATH_P:  init_rc = path_p_init(b->audio_base); break;
        case PATH_PI: init_rc = path_pi_init(b->mpu_base, b->audio_base); break;
    }
    if (init_rc != 0) {
        plog("[wbtest2 path=%s] init failed; skipping musical phrase", pt);
        return;
    }

    /* GM Master Volume MAX (Universal Sysex). */
    static const uint8_t gm_mv[] = {
        0xF0, 0x7F, 0x7F, 0x04, 0x01, 0x7F, 0x7F, 0xF7
    };
    send_bytes(p, gm_mv, sizeof gm_mv, "gm_master_volume_max");

    /* CC07 channel volume MAX on ch 0 + ch 9. */
    static const uint8_t cc07_c0[] = { 0xB0, 0x07, 0x7F };
    static const uint8_t cc07_c9[] = { 0xB9, 0x07, 0x7F };
    send_bytes(p, cc07_c0, sizeof cc07_c0, "cc07_ch0_vol_max");
    send_bytes(p, cc07_c9, sizeof cc07_c9, "cc07_ch9_vol_max");

    /* Program change ch 0 -> Acoustic Grand Piano. */
    static const uint8_t prog0[] = { 0xC0, 0x00 };
    send_bytes(p, prog0, sizeof prog0, "prog_change_ch0_piano");

    sleep_secs(0.05);

    /* C-E-G triad. */
    static const uint8_t n_c[] = { 0x90, 0x3C, 0x64 };
    static const uint8_t n_e[] = { 0x90, 0x40, 0x64 };
    static const uint8_t n_g[] = { 0x90, 0x43, 0x64 };
    send_bytes(p, n_c, sizeof n_c, "note_on_C4"); sleep_secs(1.5);
    send_bytes(p, n_e, sizeof n_e, "note_on_E4"); sleep_secs(1.5);
    send_bytes(p, n_g, sizeof n_g, "note_on_G4"); sleep_secs(2.0);

    static const uint8_t off_c[] = { 0x80, 0x3C, 0x00 };
    static const uint8_t off_e[] = { 0x80, 0x40, 0x00 };
    static const uint8_t off_g[] = { 0x80, 0x43, 0x00 };
    send_bytes(p, off_c, sizeof off_c, "note_off_C4");
    send_bytes(p, off_e, sizeof off_e, "note_off_E4");
    send_bytes(p, off_g, sizeof off_g, "note_off_G4");

    sleep_secs(0.2);

    /* GM drum-kit test: kick drum on ch 9 (GM percussion). */
    plog("[wbtest2 path=%s] drum-kit test: ch 9 note 36 (acoustic bass drum)", pt);
    static const uint8_t drum_on[]  = { 0x99, 0x24, 0x64 };
    static const uint8_t drum_off[] = { 0x89, 0x24, 0x00 };
    send_bytes(p, drum_on, sizeof drum_on, "drum_note_on_ch9_kick");
    sleep_secs(1.0);
    send_bytes(p, drum_off, sizeof drum_off, "drum_note_off_ch9_kick");

    sleep_secs(0.2);

    /* CC 123 + CC 120 belt + braces. */
    static const uint8_t cc123_c0[] = { 0xB0, 0x7B, 0x00 };
    static const uint8_t cc123_c9[] = { 0xB9, 0x7B, 0x00 };
    static const uint8_t cc120_c0[] = { 0xB0, 0x78, 0x00 };
    static const uint8_t cc120_c9[] = { 0xB9, 0x78, 0x00 };
    send_bytes(p, cc123_c0, sizeof cc123_c0, "cc123_ch0_all_notes_off");
    send_bytes(p, cc123_c9, sizeof cc123_c9, "cc123_ch9_all_notes_off");
    send_bytes(p, cc120_c0, sizeof cc120_c0, "cc120_ch0_all_sound_off");
    send_bytes(p, cc120_c9, sizeof cc120_c9, "cc120_ch9_all_sound_off");

    /* Post-test IRQ-status read. */
    uint8_t irq_status = mixer_read(b->audio_base, 0x82);
    plog("[wbtest2 path=%s post] mixer[0x82] = 0x%02X (expected ~0x30 baseline;"
         " UART never asserts MPU IRQ on SB16 family per W22-WB-F)",
         pt, irq_status);

    plog("[wbtest2 path=%s done]", pt);
}

/* ============================================================ */
/* Boot diagnostics + mixer reg dump (reuse mpuwbprobe.c list)   */
/* ============================================================ */

static void boot_diagnostics(blaster_t *b)
{
    plog("---- Boot diagnostics ----");

    parse_blaster(b);
    plog("BLASTER raw: \"%s\"", b->raw);
    plog("BLASTER parsed: A=0x%03X I=%d D=%d H=%d P=0x%03X T=%d",
         b->audio_base, b->irq, b->dma_lo, b->dma_hi, b->mpu_base, b->sb_type);
    plog("MPU-401 ports:  data=0x%03X status_or_cmd=0x%03X",
         b->mpu_base + 0, b->mpu_base + 1);
    plog("SB16 DSP ports: reset=0x%03X read=0x%03X write=0x%03X read_status=0x%03X",
         b->audio_base + 0x6, b->audio_base + 0xA,
         b->audio_base + 0xC, b->audio_base + 0xE);
    plog("(PicoGUS expected PHYSICALLY OUT for this iter per operator)");

    /* DSP detect. */
    plog("---- DSP detect ----");
    int rc = dsp_reset(b->audio_base);
    if (rc == 0) {
        plog("dsp_reset OK (chip responded 0xAA)");
        if (dsp_write_byte(b->audio_base, 0xE1) == 0) {
            uint8_t mb = 0, lb = 0;
            int r1 = dsp_read_byte(b->audio_base, &mb);
            int r2 = dsp_read_byte(b->audio_base, &lb);
            if (r1 == 0 && r2 == 0) {
                plog("DSP version: %d.%02d (Vibra16S CT2490 expected ~4.13)",
                     mb, lb);
                if (mb < 4) {
                    plog("WARN: DSP < 4.x; PATH=P/PI may fail.");
                }
            } else {
                plog("WARN: DSP version read incomplete (rd1=%d rd2=%d)", r1, r2);
            }
        } else {
            plog("WARN: DSP version cmd write failed");
        }
    } else {
        plog("WARN: DSP reset rc=%d (no SB16 at A=0x%03X);"
             " PATH=P/PI will skip themselves.", rc, b->audio_base);
    }

    /* MPU-401 status raw read (informational only). */
    plog("---- MPU-401 status (raw, informational; bit 7 lies per W22-WB-F) ----");
    uint8_t mpu_status = inportb(b->mpu_base + 1);
    plog("MPU-401 status raw: 0x%02X  (bit7_txready=%d  bit6_drr=%d)",
         mpu_status, (mpu_status >> 7) & 1, (mpu_status >> 6) & 1);

    /* CT1745 mixer reg dump -- reg list copied verbatim from mpuwbprobe.c
     * Section 2 (lines 482-509). 38 regs covering legacy SB Pro + SB16
     * master/voice/midi/cd/line/mic + EQ + input routing + IRQ/DMA/status.
     * READ ONLY -- no writes (0x3C is sticky/write-protected on CT2490). */
    plog("---- SB16 CT1745 mixer reg dump (H2 -- WB-header routing) ----");
    plog("(reg list verbatim from mpuwbprobe.c Section 2; READ-ONLY)");
    static const uint8_t mixer_regs[] = {
        0x00, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E,
        0x22, 0x26, 0x28, 0x2E,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
        0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E,
        0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
        0x80, 0x81, 0x82, 0x83
    };
    int n_mixer = (int)(sizeof(mixer_regs) / sizeof(mixer_regs[0]));
    for (int i = 0; i < n_mixer; i++) {
        uint8_t v = mixer_read(b->audio_base, mixer_regs[i]);
        plog("MIXER[0x%02X] = 0x%02X    %s", mixer_regs[i], v,
             mixer_regs[i] == 0x22 ? "master vol" :
             mixer_regs[i] == 0x26 ? "FM vol" :
             mixer_regs[i] == 0x30 ? "master L" :
             mixer_regs[i] == 0x31 ? "master R" :
             mixer_regs[i] == 0x32 ? "voice L (DAC)" :
             mixer_regs[i] == 0x33 ? "voice R (DAC)" :
             mixer_regs[i] == 0x34 ? "MIDI L (synth out)" :
             mixer_regs[i] == 0x35 ? "MIDI R (synth out)" :
             mixer_regs[i] == 0x3C ? "OUT mixer switch (b5=midiL b4=midiR ...) STICKY/WP" :
             mixer_regs[i] == 0x3D ? "IN  mixer switch L" :
             mixer_regs[i] == 0x3E ? "IN  mixer switch R" :
             mixer_regs[i] == 0x80 ? "IRQ select (b1=IRQ5)" :
             mixer_regs[i] == 0x81 ? "DMA select (b1=DMA1, b5=DMA5)" :
             mixer_regs[i] == 0x82 ? "IRQ status (b0=SB b2=MPU)" :
             "");
    }

    plog("");
}

/* ============================================================ */
/* Listen protocol                                              */
/* ============================================================ */

static void listen_protocol(int do_d, int do_di, int do_p, int do_pi)
{
    plog("---- Listen protocol ----");
    plog("Each path attempts the same musical phrase: GM-init + piano C-E-G");
    plog("triad on ch 0 + drum-kit hit on ch 9 + all-sound-off. ~6 sec per path.");
    plog("Inter-path silence gap ~1 sec.");
    plog("");
    plog("Per-path ear-report categories:");
    plog("  piano       = wavetable working + GM bank correct");
    plog("  drum_only   = voice engine alive but bank wrong");
    plog("  boops/beeps = bytes reach synth but framing wrong");
    plog("  silence     = chain broken before voice engine OR muted at routing");
    plog("  HANG        = note WHICH banner was last in WBTEST2.LOG");
    plog("");
    plog("Paths to run this invocation:");
    if (do_d)  plog("  PATH=D   byte-faithful direct-port blind (synth.c L182 + L260)");
    if (do_di) plog("  PATH=DI  full MPU reset + bounded ACK-on-DATA-port + UART + ACK");
    if (do_p)  plog("  PATH=P   byte-faithful DSP-mediated (sb.c L568 + L577)");
    if (do_pi) plog("  PATH=PI  MPU reset prelude + DSP-mediated");
    plog("");
    plog("CAVEAT: W22-WB-F audibility status on these byte sequences was NEVER");
    plog("ear-confirmed. Treat operator's ear on g2k as the SOLE truth here.");
    plog("");
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    open_log();
    plog("=== wbtest2 v1.0.2 task #4 (WBTEST-002): 4-path WB MIDI probe ===");
    plog("Standalone DJGPP; no SDL, no engine. Target: g2k DreamBlaster S2.");
    plog("Probe-faithfulness audit: PATH=D + PATH=P byte-faithful to current");
    plog("vendor/SDL/src/.../SDL_dos_audio_synth.c + SDL_dosaudio_sb.c POST-PATCH.");
    plog("PATH=DI + PATH=PI intentionally non-faithful -- test H6 (MPU power-up");
    plog("state may need full reset/ACK dance to engage UART).");
    plog("");

    int do_d = 0, do_di = 0, do_p = 0, do_pi = 0;
    if (argc < 2) {
        do_d = do_di = do_p = do_pi = 1;
        plog("MODE: A (default; all four paths sequentially)");
    } else {
        char m = argv[1][0];
        if (m >= 'a' && m <= 'z') m -= ('a' - 'A');
        switch (m) {
            case 'D': do_d  = 1; plog("MODE: D  (direct-port blind only)"); break;
            case 'I': do_di = 1; plog("MODE: I  (direct + full reset/ACK)"); break;
            case 'P': do_p  = 1; plog("MODE: P  (DSP-mediated only)"); break;
            case 'X': do_pi = 1; plog("MODE: X  (DSP + MPU reset prelude)"); break;
            default:
                do_d = do_di = do_p = do_pi = 1;
                plog("MODE: A (all four paths)");
                break;
        }
    }
    plog("");

    blaster_t b;
    boot_diagnostics(&b);
    listen_protocol(do_d, do_di, do_p, do_pi);

    plog("[wbtest2 pretest] 1-sec silence pad before first path...");
    sleep_secs(1.0);

    int first = 1;
    if (do_d) {
        if (!first) { plog(""); plog("---- inter-path silence gap (~1 sec) ----"); sleep_secs(1.0); }
        run_path(PATH_D, &b);
        first = 0;
    }
    if (do_di) {
        if (!first) { plog(""); plog("---- inter-path silence gap (~1 sec) ----"); sleep_secs(1.0); }
        run_path(PATH_DI, &b);
        first = 0;
    }
    if (do_p) {
        if (!first) { plog(""); plog("---- inter-path silence gap (~1 sec) ----"); sleep_secs(1.0); }
        run_path(PATH_P, &b);
        first = 0;
    }
    if (do_pi) {
        if (!first) { plog(""); plog("---- inter-path silence gap (~1 sec) ----"); sleep_secs(1.0); }
        run_path(PATH_PI, &b);
        first = 0;
    }

    plog("");
    plog("---- Verdict-mapping table ----");
    plog("Per-path operator ear-report -> hypothesis update:");
    plog("");
    plog("  PATH=D piano    -> SDL/0047 direct branch is FINE; bug elsewhere.");
    plog("                     (WBTEST-001 silence was the missing 0x3F init.)");
    plog("  PATH=D drum     -> direct-port works but PROG=0 wrong bank.");
    plog("  PATH=D silence  -> direct chain broken even with 0x3F init.");
    plog("");
    plog("  PATH=DI piano   -> Vibra16S NEEDS the full reset/ACK dance; SDL");
    plog("                     direct branch needs amendment.");
    plog("  PATH=DI drum    -> same as DI piano but with bank-select fault.");
    plog("  PATH=DI silence -> direct-port fundamentally not the path; check");
    plog("                     mixer dump for H2 (routing-muted), specifically");
    plog("                     reg 0x3C bits 5/4 + reg 0x34/0x35.");
    plog("");
    plog("  PATH=P piano    -> DSP-mediated default is FINE; in-game bug is");
    plog("                     SDL orchestration / engine init order.");
    plog("  PATH=P drum     -> DSP voice engine alive but bank wrong.");
    plog("  PATH=P silence  -> DSP cmd 0x34/0x38 doesn't engage WB on this");
    plog("                     chip. Check PATH=PI; if also silent, H5");
    plog("                     confirmed (DSP cmd path fundamentally wrong).");
    plog("");
    plog("  PATH=PI piano   -> DSP cmd path engages WB only when MPU was");
    plog("                     reset first. Production needs the prelude.");
    plog("  PATH=PI drum    -> same as PI piano + bank-select fault.");
    plog("  PATH=PI silence -> DSP cmd 0x34/0x38 fundamentally doesn't work");
    plog("                     on this Vibra16S clone (H5).");
    plog("");
    plog("  any HANG        -> note last WBTEST2.LOG banner; PATH=DI's bounded");
    plog("                     ACK polls are the prime hang-suspect even though");
    plog("                     they're on DATA port (no status-reg inportb).");
    plog("");
    plog("  H2 evidence: scan the mixer reg dump in boot diag. Specifically:");
    plog("    reg 0x3C bits 5/4 (MIDI L/R OUT switch; STICKY on CT2490 per");
    plog("    W22-WB-F so READ value is the evidence -- don't try to flip)");
    plog("    reg 0x34/0x35 (MIDI L/R synth out volume)");
    plog("    Any of those near-zero or with bits clear = routing-muted = H2.");

    plog("");
    plog("[wbtest2 SUITE_DONE verdict=PROBE_COMPLETED mode=%s]",
         (do_d && do_di && do_p && do_pi) ? "A" :
         (do_d  ? "D" : do_di ? "I" : do_p ? "P" : do_pi ? "X" : "?"));
    plog("[SENTINEL_END]");

    if (g_log) fclose(g_log);
    return 0;
}
