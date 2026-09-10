/*
 * wbtest3.c -- WaveBlaster Reset-SysEx 5-variant probe (WBTEST-003).
 *
 * v1.0.2 task #9. Standalone DJGPP; no SDL, no engine. Parallel file to
 * wbtest.c (WBTEST-001) + wbtest2.c (WBTEST-002) per the bltpat / bltpat-v2
 * precedent: each iter's source kept on disk as historical reference.
 *
 * MISSION: identify the Reset SysEx that wakes the Dream SAM2695 daughterboard
 * into GM Capital Tone state. WBTEST-002b real-HW ear-report sharpened
 * (operator 2026-05-26): under PATH=P all bytes reach the chip with correct
 * pitches AND ch9 drum-channel routing works (the kick-drum command produces
 * a percussion sound, not silence), BUT:
 *   - ch0 notes play with ORGAN timbre, not piano (program 0 default is
 *     organ in the SAM2695 power-up patch map, not the GM-mandated Acoustic
 *     Grand Piano).
 *   - ch9 note 36 produces COWBELL, not bass drum (GM percussion key map
 *     puts kick at note 36; non-GM drum kits map note 36 to whatever the
 *     manufacturer chose, on SAM2695 the cowbell sample).
 *
 * H11 (flush-instr rev-5): Dream SAM2695 powers up in a non-GM-default mode
 * AND the Universal GM Reset sysex (F0 7E 7F 09 01 F7) sent by WBTEST-002b
 * is NOT switching it to GM Capital Tone state. The fix is a different /
 * stronger Reset SysEx pre-amble in production.
 *
 * H12 (byte truncation) DISPROVED: WriteSoundBlasterDSP (sb.c L518-525)
 * writes the full 8-bit byte (no high-bit mask); chip receiving 0x90 status
 * with high bit intact is required for the audible-at-correct-pitches
 * observation under WBTEST-002b. Bank/program selection is the variable,
 * not transport.
 *
 * Production fix (once a variant wins on g2k): single emit at
 * vendor/SDL/src/core/dos/SDL_dos_audio_synth.c L118 right after
 * SDL_DOSAudioSB_DSPMidiEnterUART() returns.
 *
 * ==========================================================================
 * 5-VARIANT DESIGN
 * ==========================================================================
 *
 * Per-variant procedure:
 *   1. dsp_reset + dsp_write(0x34) UART entry (PATH=P init; byte-faithful
 *      to sb.c L568-580; UNCHANGED from wbtest2.c PATH=P).
 *   2. Variant-specific Reset SysEx prefix (THE ONE VARIABLE).
 *   3. Phrase: CC07 ch0/ch9 vol max + prog ch0=0 (piano) + C-E-G triad
 *      + drum-kit kick on ch9 note 36 + CC123 + CC120. Byte-faithful to
 *      wbtest2.c PATH=P phrase. UNCHANGED.
 *   4. Post-phrase mixer[0x82] IRQ-status read.
 *
 * Prefix table:
 *   V1 (control): GM Reset only.
 *     F0 7E 7F 09 01 F7
 *     Expected on g2k: organ + cowbell (matches WBTEST-002b PATH=P).
 *
 *   V2: Roland GS Reset, then GM Reset.
 *     F0 41 10 42 12 40 00 7F 00 41 F7  (GS Reset)
 *     F0 7E 7F 09 01 F7                 (GM Reset)
 *
 *   V3: Yamaha XG Reset, then GM Reset.
 *     F0 43 10 4C 00 00 7E 00 F7        (XG Reset)
 *     F0 7E 7F 09 01 F7                 (GM Reset)
 *
 *   V4: GS Reset, then explicit ch9 GS drum bank select.
 *     F0 41 10 42 12 40 00 7F 00 41 F7  (GS Reset)
 *     B9 00 7F                          (CC0 Bank Select MSB = 127 on ch9)
 *     B9 20 00                          (CC32 Bank Select LSB = 0 on ch9)
 *     C9 00                             (program change ch9 -> prog 0 in
 *                                        drum bank)
 *
 *   V5: XG Reset, then ch9 XG drum bank select (same B/PC sequence as V4;
 *       XG and GS differ at sysex level not at bank-select).
 *     F0 43 10 4C 00 00 7E 00 F7        (XG Reset)
 *     B9 00 7F
 *     B9 20 00
 *     C9 00
 *
 * Single lever per binary: this binary's one mechanism is "Reset SysEx
 * variant"; the 5 variants explore that single axis. Byte transport
 * (PATH=P body) is unchanged from wbtest2.c and is not under test here.
 *
 * ==========================================================================
 * PROBE-FAITHFULNESS AUDIT TABLE
 * ==========================================================================
 *
 * | block                             | source             | faithful? | rationale                                                       |
 * |-----------------------------------|--------------------|-----------|-----------------------------------------------------------------|
 * | PATH=P init (dsp_reset+0x34)      | wbtest2.c L446-466 | YES       | copied verbatim; dsp_reset 100us hold matches sb.c L508.        |
 * | PATH=P per-byte (0x38 + byte)     | wbtest2.c L505-516 | YES       | copied verbatim; mirrors sb.c L577-580 DSPMidiWriteByte.        |
 * | Musical phrase                    | wbtest2.c L557-606 | YES       | CC07 + prog + C-E-G + drum + CC123/120 copied verbatim.         |
 * | Mixer reg dump (38 regs)          | wbtest2.c L734-768 | YES       | copied verbatim from mpuwbprobe.c sec.2 reg list.               |
 * | V1 prefix                         | (the control)      | n/a       | GM Reset only; matches WBTEST-002b PATH=P prefix.               |
 * | V2-V5 prefixes                    | NEW                | n/a       | byte-by-byte hex per the spec in task #9 description.           |
 *
 * Pre-phrase prefix is the SOLE variable across variants. Anything before
 * the prefix (DSP init) and after the prefix (musical phrase) is identical
 * to wbtest2.c PATH=P. This is the discriminator-design discipline.
 *
 * ==========================================================================
 * argv MODE SELECT
 * ==========================================================================
 *
 *   WBTEST3 1   = V1 only (control: GM Reset)
 *   WBTEST3 2   = V2 only (GS Reset + GM Reset)
 *   WBTEST3 3   = V3 only (XG Reset + GM Reset)
 *   WBTEST3 4   = V4 only (GS Reset + ch9 drum bank)
 *   WBTEST3 5   = V5 only (XG Reset + ch9 drum bank)
 *   WBTEST3 A   = all five sequentially (DEFAULT)
 *
 * ==========================================================================
 * OPERATOR PROTOCOL
 * ==========================================================================
 *
 * Per variant, ear-report:
 *   piano + kick = WIN. This Reset SysEx wakes SAM2695 into GM mode and
 *                  the ch9 drum map is GM-correct. SDL/0047 production fix
 *                  is emit this variant's prefix after UART entry.
 *   organ + cowbell = still-non-GM. This variant didn't switch the chip
 *                  out of power-up mode (matches V1 control).
 *   piano + cowbell = melodic side switched to GM (Capital Tone), drum
 *                  bank still non-GM (need explicit drum bank emit; check
 *                  if V4/V5 succeed where V2/V3 partially do).
 *   organ + kick = unusual but possible if the manufacturer's drum bank
 *                  happens to put kick at 36 while the melodic patch map
 *                  stays non-GM.
 *   silence = transport broken on this variant (shouldn't happen; PATH=P
 *                  body is unchanged from WBTEST-002b which had audibility).
 *
 * Expected runtime: ~35 sec under WBTEST3 A (5 x ~6 sec + 4 x ~1 sec gaps
 * + boot diag).
 *
 * 8.3 DOS filenames:
 *   Source: tests/probes/wbtest3.c   (host-side, gitignored)
 *   Binary: WBTEST3.EXE              (7+3)
 *   Log:    WBTEST3.LOG              (7+3)
 *   BAT:    WBTEST3.BAT              (7+3)
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
    g_log = fopen("WBTEST3.LOG", "w");
    if (!g_log) g_log = fopen("C:\\WBTEST3.LOG", "w");
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
/* BLASTER env parse (verbatim from wbtest2.c)                  */
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
/* SB16 / CT1745 mixer access (verbatim from wbtest2.c). Used   */
/* for boot-diag mixer reg dump + per-variant post-phrase 0x82  */
/* IRQ-status read. READ ONLY; 0x3C is sticky on CT2490.        */
/* ============================================================ */

static uint8_t mixer_read(int audio_base, uint8_t reg)
{
    outportb(audio_base + 0x4, reg);
    return inportb(audio_base + 0x5);
}

/* ============================================================ */
/* SB16 DSP I/O (verbatim from wbtest2.c)                       */
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
/* PATH=P state + per-byte sender (verbatim from wbtest2.c)     */
/* ============================================================ */

static int g_audio_base = 0x220;
static int g_path_init_failed = 0;

static int path_p_init(int audio_base)
{
    g_audio_base = audio_base;
    g_path_init_failed = 0;

    plog("[wbtest3 path=P init] dsp_reset (100us hold; matches sb.c L508)");
    int rc = dsp_reset(audio_base);
    if (rc != 0) {
        plog("[wbtest3 path=P init] dsp_reset FAILED rc=%d -- skipping variant", rc);
        g_path_init_failed = 1;
        return -1;
    }
    plog("[wbtest3 path=P init] dsp_reset OK (0xAA)");

    plog("[wbtest3 path=P init] dsp_write(0x34) -- UART entry"
         " (mirrors SDL_DOSAudioSB_DSPMidiEnterUART, sb.c L568)");
    if (dsp_write_byte(audio_base, 0x34) != 0) {
        plog("[wbtest3 path=P init] DSP cmd 0x34 FAILED");
        g_path_init_failed = 1;
        return -1;
    }
    plog("[wbtest3 path=P init] done");
    return 0;
}

/* Per-byte dispatch via DSP-mediated 0x38 framing (sb.c L577-580). */
static void send_byte_p(uint8_t byte)
{
    if (g_path_init_failed) return;
    (void)dsp_write_byte(g_audio_base, 0x38);
    (void)dsp_write_byte(g_audio_base, byte);
}

static void send_bytes(const uint8_t *bytes, int n, const char *event)
{
    char hexbuf[96];
    int off = 0;
    for (int i = 0; i < n && off < (int)sizeof(hexbuf) - 4; i++) {
        off += snprintf(hexbuf + off, sizeof(hexbuf) - off,
                        "%s%02X", i ? " " : "", bytes[i]);
    }
    plog("[wbtest3 EVENT=%s bytes=[%s]]", event, hexbuf);
    for (int i = 0; i < n; i++) send_byte_p(bytes[i]);
}

/* ============================================================ */
/* Reset SysEx tables                                           */
/* ============================================================ */

static const uint8_t SYSEX_GM_RESET[]  = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
static const uint8_t SYSEX_GS_RESET[]  = {
    0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7
};
static const uint8_t SYSEX_XG_RESET[]  = {
    0xF0, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E, 0x00, 0xF7
};
/* Drum-bank emit: CC0 Bank Select MSB = 127 on ch9; CC32 Bank Select LSB =
 * 0 on ch9; program change ch9 -> prog 0 in the selected bank. Same byte
 * sequence whether the prior Reset was GS or XG (both manufacturer dialects
 * use CC0+CC32+PC for bank+program selection at the MIDI byte layer; only
 * their sysex layer differs in defining which bank is the drum bank). */
static const uint8_t DRUMBANK_CH9_PROG0[] = {
    0xB9, 0x00, 0x7F,   /* CC0 = 127 (Bank MSB) */
    0xB9, 0x20, 0x00,   /* CC32 = 0  (Bank LSB) */
    0xC9, 0x00          /* program change ch9 -> 0 */
};

typedef struct {
    int          v_num;
    const char  *name;
    const char  *description;
} variant_meta_t;

static const variant_meta_t VARIANTS[] = {
    { 1, "V1", "control: GM Reset only" },
    { 2, "V2", "GS Reset + GM Reset" },
    { 3, "V3", "XG Reset + GM Reset" },
    { 4, "V4", "GS Reset + ch9 drum bank" },
    { 5, "V5", "XG Reset + ch9 drum bank" },
};

/* ============================================================ */
/* Per-variant Reset-SysEx prefix emit                          */
/* ============================================================ */

static void emit_prefix(int v)
{
    switch (v) {
        case 1:
            send_bytes(SYSEX_GM_RESET, sizeof SYSEX_GM_RESET, "sysex_gm_reset");
            break;
        case 2:
            send_bytes(SYSEX_GS_RESET, sizeof SYSEX_GS_RESET, "sysex_gs_reset");
            send_bytes(SYSEX_GM_RESET, sizeof SYSEX_GM_RESET, "sysex_gm_reset");
            break;
        case 3:
            send_bytes(SYSEX_XG_RESET, sizeof SYSEX_XG_RESET, "sysex_xg_reset");
            send_bytes(SYSEX_GM_RESET, sizeof SYSEX_GM_RESET, "sysex_gm_reset");
            break;
        case 4:
            send_bytes(SYSEX_GS_RESET, sizeof SYSEX_GS_RESET, "sysex_gs_reset");
            send_bytes(DRUMBANK_CH9_PROG0, sizeof DRUMBANK_CH9_PROG0,
                       "drumbank_ch9_msb127_lsb0_prog0");
            break;
        case 5:
            send_bytes(SYSEX_XG_RESET, sizeof SYSEX_XG_RESET, "sysex_xg_reset");
            send_bytes(DRUMBANK_CH9_PROG0, sizeof DRUMBANK_CH9_PROG0,
                       "drumbank_ch9_msb127_lsb0_prog0");
            break;
    }
    /* Brief settle so synth can ingest reset before phrase. SAM2695 takes
     * non-trivial time to apply a reset internally; 100ms is generous. */
    sleep_secs(0.1);
}

/* ============================================================ */
/* Musical phrase (byte-faithful copy from wbtest2.c PATH=P run) */
/* ============================================================ */

static void play_phrase(void)
{
    /* CC07 channel volume MAX on ch 0 + ch 9 (belt+braces against any
     * default-low channel volume on the chip). */
    static const uint8_t cc07_c0[] = { 0xB0, 0x07, 0x7F };
    static const uint8_t cc07_c9[] = { 0xB9, 0x07, 0x7F };
    send_bytes(cc07_c0, sizeof cc07_c0, "cc07_ch0_vol_max");
    send_bytes(cc07_c9, sizeof cc07_c9, "cc07_ch9_vol_max");

    /* Program change ch 0 -> 0 (Acoustic Grand Piano in GM Capital Tone). */
    static const uint8_t prog0[] = { 0xC0, 0x00 };
    send_bytes(prog0, sizeof prog0, "prog_change_ch0_piano");

    sleep_secs(0.05);

    /* C-E-G triad. */
    static const uint8_t n_c[] = { 0x90, 0x3C, 0x64 };
    static const uint8_t n_e[] = { 0x90, 0x40, 0x64 };
    static const uint8_t n_g[] = { 0x90, 0x43, 0x64 };
    send_bytes(n_c, sizeof n_c, "note_on_C4"); sleep_secs(1.5);
    send_bytes(n_e, sizeof n_e, "note_on_E4"); sleep_secs(1.5);
    send_bytes(n_g, sizeof n_g, "note_on_G4"); sleep_secs(2.0);

    static const uint8_t off_c[] = { 0x80, 0x3C, 0x00 };
    static const uint8_t off_e[] = { 0x80, 0x40, 0x00 };
    static const uint8_t off_g[] = { 0x80, 0x43, 0x00 };
    send_bytes(off_c, sizeof off_c, "note_off_C4");
    send_bytes(off_e, sizeof off_e, "note_off_E4");
    send_bytes(off_g, sizeof off_g, "note_off_G4");

    sleep_secs(0.2);

    /* GM drum-kit test: kick drum on ch 9 note 36 (GM percussion). */
    plog("[wbtest3] drum-kit test: ch 9 note 36 (GM kick / SAM2695 cowbell)");
    static const uint8_t drum_on[]  = { 0x99, 0x24, 0x64 };
    static const uint8_t drum_off[] = { 0x89, 0x24, 0x00 };
    send_bytes(drum_on, sizeof drum_on, "drum_note_on_ch9_36");
    sleep_secs(1.0);
    send_bytes(drum_off, sizeof drum_off, "drum_note_off_ch9_36");

    sleep_secs(0.2);

    /* CC 123 + CC 120 belt+braces. */
    static const uint8_t cc123_c0[] = { 0xB0, 0x7B, 0x00 };
    static const uint8_t cc123_c9[] = { 0xB9, 0x7B, 0x00 };
    static const uint8_t cc120_c0[] = { 0xB0, 0x78, 0x00 };
    static const uint8_t cc120_c9[] = { 0xB9, 0x78, 0x00 };
    send_bytes(cc123_c0, sizeof cc123_c0, "cc123_ch0_all_notes_off");
    send_bytes(cc123_c9, sizeof cc123_c9, "cc123_ch9_all_notes_off");
    send_bytes(cc120_c0, sizeof cc120_c0, "cc120_ch0_all_sound_off");
    send_bytes(cc120_c9, sizeof cc120_c9, "cc120_ch9_all_sound_off");
}

/* ============================================================ */
/* Per-variant run                                              */
/* ============================================================ */

static void run_variant(const variant_meta_t *v, const blaster_t *b)
{
    plog("");
    plog("===========================================================");
    plog("=== %s STARTING (%s); listen NOW ===", v->name, v->description);
    plog("===========================================================");
    plog("");
    sleep_secs(1.0);  /* operator banner-to-audio correlation pad */

    if (path_p_init(b->audio_base) != 0) {
        plog("[wbtest3 %s] PATH=P init failed; skipping variant", v->name);
        return;
    }

    plog("[wbtest3 %s] Reset SysEx prefix emit", v->name);
    emit_prefix(v->v_num);

    plog("[wbtest3 %s] musical phrase emit", v->name);
    play_phrase();

    /* Post-phrase mixer[0x82] IRQ-status read. */
    uint8_t irq_status = mixer_read(b->audio_base, 0x82);
    plog("[wbtest3 %s post] mixer[0x82] = 0x%02X", v->name, irq_status);
    plog("[wbtest3 %s done]", v->name);
}

/* ============================================================ */
/* Boot diagnostics + mixer reg dump (verbatim from wbtest2.c)  */
/* ============================================================ */

static void boot_diagnostics(blaster_t *b)
{
    plog("---- Boot diagnostics ----");

    parse_blaster(b);
    plog("BLASTER raw: \"%s\"", b->raw);
    plog("BLASTER parsed: A=0x%03X I=%d D=%d H=%d P=0x%03X T=%d",
         b->audio_base, b->irq, b->dma_lo, b->dma_hi, b->mpu_base, b->sb_type);
    plog("SB16 DSP ports: reset=0x%03X read=0x%03X write=0x%03X read_status=0x%03X",
         b->audio_base + 0x6, b->audio_base + 0xA,
         b->audio_base + 0xC, b->audio_base + 0xE);
    plog("(PicoGUS expected PHYSICALLY OUT for this iter per operator)");

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
                if (mb < 4) plog("WARN: DSP < 4.x; PATH=P may fail.");
            } else {
                plog("WARN: DSP version read incomplete (rd1=%d rd2=%d)", r1, r2);
            }
        } else {
            plog("WARN: DSP version cmd write failed");
        }
    } else {
        plog("WARN: DSP reset rc=%d (no SB16 at A=0x%03X); PATH=P will skip itself.",
             rc, b->audio_base);
    }

    /* CT1745 mixer reg dump -- reg list verbatim from mpuwbprobe.c sec.2
     * (also carried in wbtest2.c). 39 regs covering legacy SB Pro + SB16
     * master/voice/midi/cd/line/mic + EQ + input routing + IRQ/DMA/status.
     * READ ONLY -- 0x3C is sticky/write-protected on CT2490. */
    plog("---- SB16 CT1745 mixer reg dump (READ-ONLY; reg list verbatim"
         " from mpuwbprobe.c sec.2) ----");
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
/* Listen protocol + verdict-mapping table                      */
/* ============================================================ */

static void listen_protocol(int do_v[5])
{
    plog("---- Listen protocol ----");
    plog("Each variant attempts the SAME musical phrase (C-E-G triad on ch 0");
    plog("+ kick drum on ch 9 note 36). The pre-phrase Reset SysEx is the SOLE");
    plog("variable. WBTEST-002b PATH=P baseline = organ + cowbell.");
    plog("");
    plog("Per-variant ear-report categories:");
    plog("  piano + kick      = WIN. This variant's prefix is the production fix.");
    plog("  organ + cowbell   = still-non-GM. Variant didn't switch chip out of");
    plog("                      power-up mode (matches V1 control).");
    plog("  piano + cowbell   = melodic side switched to GM (Capital Tone), drum");
    plog("                      bank still non-GM. V4 / V5 should fix the drum.");
    plog("  organ + kick      = unusual; drum bank GM-correct but melodic isn't.");
    plog("  silence           = transport broken (shouldn't happen; PATH=P body");
    plog("                      identical to WBTEST-002b which was audible).");
    plog("");
    plog("Variants to run this invocation:");
    for (int i = 0; i < 5; i++) {
        if (do_v[i])
            plog("  %s   %s", VARIANTS[i].name, VARIANTS[i].description);
    }
    plog("");
}

static void verdict_table(void)
{
    plog("---- Verdict-mapping table ----");
    plog("");
    plog("Strategy: walk V1->V5 in order; the FIRST variant that produces");
    plog("piano + kick is the winning Reset SysEx. Operator's ear is the only");
    plog("authoritative judge (per W22-WB-F caveat: 'dispatched' != 'audible').");
    plog("");
    plog("  V1 piano+kick -> impossible -- WBTEST-002b already established V1's");
    plog("                   prefix (GM Reset) yields organ+cowbell on g2k. If");
    plog("                   V1 produces piano+kick here, something between");
    plog("                   WBTEST-002b and WBTEST-003 changed silently (engine");
    plog("                   state, mixer pollution, etc.); treat as anomaly.");
    plog("  V1 organ+cowbell -> CONTROL CONFIRMED. Baseline matches WBTEST-002b.");
    plog("");
    plog("  V2 piano+kick -> WIN. GS Reset wakes SAM2695 into GM mode and the");
    plog("                   GM Reset that follows installs Capital Tone +");
    plog("                   GM-correct drum bank simultaneously. Production fix");
    plog("                   = emit GS Reset + GM Reset at SDL/0047 L118.");
    plog("  V2 piano+cowbell -> GS Reset switches melodic to GM but drum bank");
    plog("                   stays non-GM. Need V4 (GS + explicit drum bank).");
    plog("  V2 organ+cowbell -> GS Reset didn't take. Try V3 (XG).");
    plog("");
    plog("  V3 piano+kick -> WIN. XG Reset is the production fix.");
    plog("  V3 piano+cowbell -> XG melodic switch worked; need V5.");
    plog("  V3 organ+cowbell -> XG Reset didn't take either; SAM2695 may need");
    plog("                   GS Reset specifically (V4) or a different sequence.");
    plog("");
    plog("  V4 piano+kick -> WIN. GS Reset + explicit drum bank is the fix.");
    plog("  V4 piano+cowbell -> drum bank emit didn't take effect; SAM2695 may");
    plog("                   want a different bank-MSB value than 127, or the");
    plog("                   drum sample-map is fixed in hardware.");
    plog("  V4 organ+cowbell -> GS Reset still didn't take.");
    plog("");
    plog("  V5 piano+kick -> WIN. XG Reset + drum bank is the fix.");
    plog("  V5 piano+cowbell -> drum bank didn't take under XG either.");
    plog("  V5 organ+cowbell -> none of GM/GS/XG works on SAM2695; deeper");
    plog("                   investigation needed (vendor-specific sysex,");
    plog("                   firmware version check, etc.).");
    plog("");
    plog("Tiebreaker if both GS-side (V2 or V4) AND XG-side (V3 or V5) win:");
    plog("  Prefer the simpler one (GS without drum bank > GS with > XG without");
    plog("  > XG with). Fewer bytes at SDL/0047 L118 = less to maintain.");
    plog("");
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    open_log();
    plog("=== wbtest3 v1.0.2 task #9 (WBTEST-003): 5-variant Reset SysEx probe ===");
    plog("Standalone DJGPP; no SDL, no engine. Target: g2k Dream SAM2695.");
    plog("Single lever: 'Reset SysEx variant'. PATH=P byte-transport unchanged");
    plog("from wbtest2.c; only the pre-phrase prefix differs across V1-V5.");
    plog("");

    int do_v[5] = { 0, 0, 0, 0, 0 };
    if (argc < 2) {
        for (int i = 0; i < 5; i++) do_v[i] = 1;
        plog("MODE: A (default; all five variants sequentially)");
    } else {
        char m = argv[1][0];
        if (m >= '1' && m <= '5') {
            do_v[m - '1'] = 1;
            plog("MODE: %c  (single variant V%c)", m, m);
        } else if (m == 'a' || m == 'A') {
            for (int i = 0; i < 5; i++) do_v[i] = 1;
            plog("MODE: A (all five variants)");
        } else {
            for (int i = 0; i < 5; i++) do_v[i] = 1;
            plog("MODE: A (unknown arg '%c'; defaulting to all)", m);
        }
    }
    plog("");

    blaster_t b;
    boot_diagnostics(&b);
    listen_protocol(do_v);

    plog("[wbtest3 pretest] 1-sec silence pad before first variant...");
    sleep_secs(1.0);

    int first = 1;
    for (int i = 0; i < 5; i++) {
        if (!do_v[i]) continue;
        if (!first) {
            plog("");
            plog("---- inter-variant silence gap (~1 sec) ----");
            sleep_secs(1.0);
        }
        run_variant(&VARIANTS[i], &b);
        first = 0;
    }

    plog("");
    verdict_table();

    /* Mode tag for SUITE_DONE: 'A' if multi, '1'-'5' if single. modetag
     * sized generously to silence -Wformat-truncation worst-case-int
     * analysis even though active_one is always in [1,5]. */
    int active_count = 0, active_one = 0;
    for (int i = 0; i < 5; i++) { if (do_v[i]) { active_count++; active_one = i + 1; } }
    char modetag[16];
    if (active_count == 5) snprintf(modetag, sizeof modetag, "A");
    else if (active_count == 1) snprintf(modetag, sizeof modetag, "%d", active_one);
    else snprintf(modetag, sizeof modetag, "?");

    plog("[wbtest3 SUITE_DONE verdict=PROBE_COMPLETED mode=%s]", modetag);
    plog("[SENTINEL_END]");

    if (g_log) fclose(g_log);
    return 0;
}
