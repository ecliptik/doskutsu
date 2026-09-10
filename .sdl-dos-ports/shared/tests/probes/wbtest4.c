/*
 * wbtest4.c -- WaveBlaster program-change matrix probe (WBTEST-004).
 *
 * v1.0.2 task #12. Standalone DJGPP; no SDL, no engine. Parallel file to
 * wbtest.c (WBTEST-001) + wbtest2.c (WBTEST-002) + wbtest3.c (WBTEST-003)
 * per the bltpat / bltpat-v2 precedent.
 *
 * MISSION: discriminate whether MIDI program-change (status byte 0xCn) is
 * honored at all by the Dream SAM2695 daughterboard on g2k. WBTEST-003
 * established that none of the 5 standard Reset SysEx variants (GM / GS /
 * XG / GS+drum / XG+drum) shift the chip out of its power-up patch map
 * (organ on prog 0 + cowbell on note 36). flush-instr rev-6 refined the
 * hypothesis cluster to H16 (program-change silently dropped) as the
 * cheapest next discriminator: if 5 widely-different program numbers
 * still all sound identical, prog-change is being filtered chip-side.
 *
 * If prog-change IS honored (V2-V5 sound distinctly different from V1),
 * the next iter is a narrow-sweep to find which prog number maps to piano
 * in the chip's actual non-GM voice map.
 *
 * ==========================================================================
 * 5-VARIANT DESIGN
 * ==========================================================================
 *
 * Per-variant procedure:
 *   1. dsp_reset + dsp_write(0x34) UART entry (PATH=P init; byte-faithful
 *      to sb.c L568-580; copied verbatim from wbtest3.c).
 *   2. Phrase = CC07 ch0 vol max + program-change ch0 -> Vn value + C-E-G
 *      triad. NO Reset SysEx prefix (5 ignored sysexes is evidence-enough;
 *      adding another preamble dilutes the program-change signal). NO drum
 *      hit (isolate melody timbre discrimination; drums return in WBTEST-005
 *      once we know if prog-change works).
 *   3. Note Off all three + CC123 + CC120 cleanup.
 *   4. Post-phrase mixer[0x82] IRQ-status read.
 *
 * Variant table (programs chosen to span the GM tone palette so genuine
 * prog-change would produce maximally-distinguishable timbres):
 *
 *   V1 (control): C0 00 = GM Acoustic Grand Piano (prog 0).
 *                 Baseline on g2k = organ (per WBTEST-002b PATH=P + WBTEST-003).
 *
 *   V2:           C0 10 = GM Drawbar Organ (prog 16).
 *                 Same family as chip's apparent default; small change expected
 *                 if prog-change honored at all.
 *
 *   V3:           C0 20 = GM Acoustic Bass (prog 32).
 *                 Bass-register pitched-down timbre; should sound clearly
 *                 different from organ if prog-change works.
 *
 *   V4:           C0 40 = GM Soprano Sax (prog 64).
 *                 Bright reedy timbre, distinctly non-organ.
 *
 *   V5:           C0 60 = GM FX1 Rain (prog 96).
 *                 Atmospheric synth texture; non-pitched component should
 *                 stand out audibly against melodic baseline.
 *
 * Single lever per binary: this binary's one mechanism is "program-change
 * value"; the 5 variants explore that single axis. Byte transport (PATH=P
 * body) is unchanged from wbtest3.c and is not under test here.
 *
 * ==========================================================================
 * PROBE-FAITHFULNESS AUDIT TABLE
 * ==========================================================================
 *
 * | block                             | source             | faithful? | rationale                                                       |
 * |-----------------------------------|--------------------|-----------|-----------------------------------------------------------------|
 * | PATH=P init (dsp_reset+0x34)      | wbtest3.c          | YES       | copied verbatim; chain back to sb.c L508+L568 via wbtest2.c.    |
 * | PATH=P per-byte (0x38 + byte)     | wbtest3.c          | YES       | copied verbatim; mirrors sb.c L577-580 DSPMidiWriteByte.        |
 * | CC07 ch0 vol max                  | wbtest3.c          | YES       | identical byte sequence.                                        |
 * | Program-change byte (C0 vv)       | NEW per variant    | n/a       | vv = 0x00/10/20/40/60 per spec; chosen to span timbre palette.  |
 * | C-E-G triad + Note Offs           | wbtest3.c          | YES       | identical byte sequences + 1.5/1.5/2.0 sec holds.               |
 * | CC123 + CC120 cleanup             | wbtest3.c subset   | YES       | ch0 only (no ch9 since no drum hit this binary).                |
 * | Mixer reg dump (39 regs)          | wbtest3.c          | YES       | copied verbatim from mpuwbprobe.c sec.2 chain.                  |
 * | Reset SysEx prefix                | OMITTED            | n/a       | explicitly skipped per task #12 spec (WBTEST-003 evidence).     |
 * | Drum hit                          | OMITTED            | n/a       | explicitly skipped per task #12 spec; isolate melody timbre.    |
 *
 * Program-change value is the SOLE variable across variants.
 *
 * ==========================================================================
 * argv MODE SELECT
 * ==========================================================================
 *
 *   WBTEST4 1   = V1 only (prog 0 piano; control)
 *   WBTEST4 2   = V2 only (prog 16 organ)
 *   WBTEST4 3   = V3 only (prog 32 acoustic bass)
 *   WBTEST4 4   = V4 only (prog 64 soprano sax)
 *   WBTEST4 5   = V5 only (prog 96 FX rain)
 *   WBTEST4 A   = all five sequentially (DEFAULT)
 *
 * ==========================================================================
 * OPERATOR PROTOCOL
 * ==========================================================================
 *
 * Per variant, ear-report whether THIS variant sounds:
 *   identical to V1   = prog-change being silently dropped for this value
 *   different from V1 = prog-change honored at this value
 *   silence           = transport broken (shouldn't happen; PATH=P body is
 *                       unchanged from WBTEST-002b/003 which had audibility)
 *
 * Operator listens for relative timbre changes across V1-V5. Absolute
 * timbre names ("organ", "bass") are guidance from GM spec; the chip's
 * actual voice map may map these prog numbers to ANY timbre. Key question
 * is: does V2 sound DIFFERENT from V1? Does V3 sound DIFFERENT from V2?
 * If all 5 are indistinguishable, H16 confirmed.
 *
 * Expected runtime: ~30 sec under WBTEST4 A (5 x ~5 sec variant + 4 x ~1
 * sec gaps + boot diag).
 *
 * 8.3 DOS filenames:
 *   Source: tests/probes/wbtest4.c   (host-side, gitignored)
 *   Binary: WBTEST4.EXE              (7+3)
 *   Log:    WBTEST4.LOG              (7+3)
 *   BAT:    WBTEST4.BAT              (7+3)
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
    g_log = fopen("WBTEST4.LOG", "w");
    if (!g_log) g_log = fopen("C:\\WBTEST4.LOG", "w");
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

static double now_secs(void) { return (double)uclock() / (double)UCLOCKS_PER_SEC; }

static void sleep_secs(double s)
{
    double t_end = now_secs() + s;
    while (now_secs() < t_end) (void)inportb(0x80);
}

/* ============================================================ */
/* BLASTER env parse (verbatim from wbtest3.c)                  */
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
/* SB16 mixer + DSP I/O (verbatim from wbtest3.c)               */
/* ============================================================ */

static uint8_t mixer_read(int audio_base, uint8_t reg)
{
    outportb(audio_base + 0x4, reg);
    return inportb(audio_base + 0x5);
}

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
    double t_delay = now_secs() + 0.0001;  /* 100us hold; matches sb.c L508 */
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
/* PATH=P state + per-byte sender (verbatim from wbtest3.c)     */
/* ============================================================ */

static int g_audio_base = 0x220;
static int g_path_init_failed = 0;

static int path_p_init(int audio_base)
{
    g_audio_base = audio_base;
    g_path_init_failed = 0;

    plog("[wbtest4 path=P init] dsp_reset (100us hold; matches sb.c L508)");
    int rc = dsp_reset(audio_base);
    if (rc != 0) {
        plog("[wbtest4 path=P init] dsp_reset FAILED rc=%d -- skipping variant", rc);
        g_path_init_failed = 1;
        return -1;
    }
    plog("[wbtest4 path=P init] dsp_reset OK (0xAA)");

    plog("[wbtest4 path=P init] dsp_write(0x34) -- UART entry"
         " (mirrors SDL_DOSAudioSB_DSPMidiEnterUART, sb.c L568)");
    if (dsp_write_byte(audio_base, 0x34) != 0) {
        plog("[wbtest4 path=P init] DSP cmd 0x34 FAILED");
        g_path_init_failed = 1;
        return -1;
    }
    plog("[wbtest4 path=P init] done");
    return 0;
}

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
    plog("[wbtest4 EVENT=%s bytes=[%s]]", event, hexbuf);
    for (int i = 0; i < n; i++) send_byte_p(bytes[i]);
}

/* ============================================================ */
/* Variant metadata: 5 program-change values + GM name labels   */
/* ============================================================ */

typedef struct {
    int         v_num;
    const char *name;
    uint8_t     prog_value;
    const char *gm_name;
} variant_meta_t;

static const variant_meta_t VARIANTS[] = {
    { 1, "V1", 0x00, "GM Acoustic Grand Piano (control)" },
    { 2, "V2", 0x10, "GM Drawbar Organ" },
    { 3, "V3", 0x20, "GM Acoustic Bass" },
    { 4, "V4", 0x40, "GM Soprano Sax" },
    { 5, "V5", 0x60, "GM FX1 Rain" },
};

/* ============================================================ */
/* Per-variant phrase                                           */
/*                                                               */
/* Spec: prog-change + CC07 vol max + C-E-G triad ONLY. Skip    */
/* drum hit (isolate melody timbre discrimination). Cleanup     */
/* CC123 + CC120 on ch 0 only at end.                            */
/* ============================================================ */

static void play_phrase(uint8_t prog_value)
{
    /* CC07 channel volume MAX on ch 0 (belt+braces against default-low
     * channel vol; SAM2695 power-up state is unknown). */
    static const uint8_t cc07_c0[] = { 0xB0, 0x07, 0x7F };
    send_bytes(cc07_c0, sizeof cc07_c0, "cc07_ch0_vol_max");

    /* Program change ch 0 -> per-variant value (THE ONE VARIABLE). */
    uint8_t prog_change[2] = { 0xC0, prog_value };
    char event[64];
    snprintf(event, sizeof event, "prog_change_ch0_0x%02X", prog_value);
    send_bytes(prog_change, sizeof prog_change, event);

    /* Brief settle so chip can apply program change before notes arrive. */
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

    /* Cleanup: CC 123 (All Notes Off) + CC 120 (All Sound Off) on ch 0.
     * No ch 9 emit -- this binary doesn't hit drums. */
    static const uint8_t cc123_c0[] = { 0xB0, 0x7B, 0x00 };
    static const uint8_t cc120_c0[] = { 0xB0, 0x78, 0x00 };
    send_bytes(cc123_c0, sizeof cc123_c0, "cc123_ch0_all_notes_off");
    send_bytes(cc120_c0, sizeof cc120_c0, "cc120_ch0_all_sound_off");
}

/* ============================================================ */
/* Per-variant run                                              */
/* ============================================================ */

static void run_variant(const variant_meta_t *v, const blaster_t *b)
{
    plog("");
    plog("===========================================================");
    plog("=== %s STARTING (prog=0x%02X %s); listen NOW ===",
         v->name, v->prog_value, v->gm_name);
    plog("===========================================================");
    plog("");
    sleep_secs(1.0);  /* banner-to-audio correlation pad */

    if (path_p_init(b->audio_base) != 0) {
        plog("[wbtest4 %s] PATH=P init failed; skipping variant", v->name);
        return;
    }

    plog("[wbtest4 %s] phrase emit (prog=0x%02X + CC07 + C-E-G triad)",
         v->name, v->prog_value);
    play_phrase(v->prog_value);

    /* Post-phrase mixer[0x82] IRQ-status read. */
    uint8_t irq_status = mixer_read(b->audio_base, 0x82);
    plog("[wbtest4 %s post] mixer[0x82] = 0x%02X", v->name, irq_status);
    plog("[wbtest4 %s done]", v->name);
}

/* ============================================================ */
/* Boot diagnostics + mixer reg dump (verbatim from wbtest3.c)  */
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

    plog("---- SB16 CT1745 mixer reg dump (READ-ONLY; reg list verbatim"
         " from mpuwbprobe.c sec.2 via wbtest3.c) ----");
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
    plog("Each variant attempts the SAME phrase (CC07 vol max + program-change");
    plog("ch 0 -> Vn + C-E-G triad on ch 0). The program-change byte is the SOLE");
    plog("variable. WBTEST-002b/003 baseline on V1 (prog 0) = organ on g2k.");
    plog("");
    plog("Per-variant ear-report: does THIS variant sound different from V1?");
    plog("  identical to V1   = prog-change being silently dropped at this value");
    plog("  different from V1 = prog-change honored at this value");
    plog("  silence           = transport broken (shouldn't happen; PATH=P body");
    plog("                      unchanged from WBTEST-003 which was audible)");
    plog("");
    plog("Variants to run this invocation:");
    for (int i = 0; i < 5; i++) {
        if (do_v[i])
            plog("  %s   prog=0x%02X  %s",
                 VARIANTS[i].name, VARIANTS[i].prog_value, VARIANTS[i].gm_name);
    }
    plog("");
}

static void verdict_table(void)
{
    plog("---- Verdict-mapping table ----");
    plog("");
    plog("Outcome -> next-iter direction:");
    plog("");
    plog("  V1 == V2 == V3 == V4 == V5 (ALL identical)");
    plog("    -> H16 CONFIRMED: program-change being silently dropped.");
    plog("       Next iter = Doom-init replicate (test if any DOS game/driver");
    plog("       wakes SAM2695 into responsive mode; if yes, replicate that");
    plog("       exact byte sequence in SDL/0047 production fix).");
    plog("");
    plog("  V1 != V2 != V3 != V4 != V5 (ALL distinctly different)");
    plog("    -> prog-change works on this chip. The chip's voice map is");
    plog("       non-GM but each prog number produces a unique timbre.");
    plog("       Next iter = narrow-sweep across prog 0-127 to find which");
    plog("       value maps to piano in the chip's actual voice map.");
    plog("");
    plog("  PARTIAL: some V_n distinct, others identical");
    plog("    -> Bank-MSB / LSB dependent. The chip honors prog-change within");
    plog("       certain banks but not others. Next iter = Bank Select CC0/CC32");
    plog("       sweep with prog-change held constant.");
    plog("");
    plog("  V1 silent but V_n audible (or vice-versa)");
    plog("    -> Anomalous. Re-run iter; check log for any path-init errors.");
    plog("       Should be deterministic since PATH=P body is byte-faithful.");
    plog("");
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    open_log();
    plog("=== wbtest4 v1.0.2 task #12 (WBTEST-004): 5-variant prog-change matrix ===");
    plog("Standalone DJGPP; no SDL, no engine. Target: g2k Dream SAM2695.");
    plog("Single lever: 'program-change value'. PATH=P byte-transport unchanged");
    plog("from wbtest3.c; only the C0 vv byte differs across V1-V5.");
    plog("NO Reset SysEx prefix (WBTEST-003 found all 5 variants ignored).");
    plog("NO drum hit (isolate melody timbre discrimination).");
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

    plog("[wbtest4 pretest] 1-sec silence pad before first variant...");
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

    int active_count = 0, active_one = 0;
    for (int i = 0; i < 5; i++) { if (do_v[i]) { active_count++; active_one = i + 1; } }
    char modetag[16];
    if (active_count == 5) snprintf(modetag, sizeof modetag, "A");
    else if (active_count == 1) snprintf(modetag, sizeof modetag, "%d", active_one);
    else snprintf(modetag, sizeof modetag, "?");

    plog("[wbtest4 SUITE_DONE verdict=PROBE_COMPLETED mode=%s]", modetag);
    plog("[SENTINEL_END]");

    if (g_log) fclose(g_log);
    return 0;
}
