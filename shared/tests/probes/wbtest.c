/*
 * wbtest.c -- WaveBlaster MIDI A/B probe: direct-port-blind vs DSP-mediated.
 *
 * v1.0.2 task #1 (WBTEST-001). Standalone DJGPP; no SDL, no engine. Mission:
 * definitively answer which of the two MIDI dispatch paths that SDL patch
 * 0047 supports actually drives the DreamBlaster S2 daughterboard on g2k,
 * by sending each path a recognizable GM piano C-E-G chord and letting the
 * operator listen.
 *
 * HISTORICAL: This is the v1 probe. flush-instr's post-iter analysis
 * surfaced a probe-faithfulness defect (PATH=D omits the 0x3F UART entry
 * write that production SDL/0044+0047 makes at synth.c L182). Kept for
 * historical reference; the corrected v2 lives at tests/probes/wbtest2.c
 * per the bltpat / bltpat-v2 precedent.
 *
 * Operator-authoritative context (2026-05-26): WaveBlaster has NEVER actually
 * produced wavetable music in this codebase. The wave-42 "+8.12 fps WB
 * validated" was a misattributed silent OPL3 fallback.
 *
 * This probe runs the two SDL/0047 byte sequences in isolation, with NO
 * concurrent SDL audio thread, NO engine, just bare metal:
 *
 *   PATH=D (direct-port blind, opt-in via SDL_HINT_DOS_AUDIO_WB_DIRECT_PORT=1):
 *     init:        nothing (the WBTEST-001 omission -- production SDL/0047
 *                  direct branch writes 0x3F here; WBTEST-002 corrects this).
 *     per byte:    outportb(port_base+0, byte). NO status polling.
 *
 *   PATH=P (DSP-mediated, the SDL/0047 default):
 *     init:        WriteSoundBlasterDSP(0x34)  -- enter UART MIDI mode.
 *     per byte:    WriteSoundBlasterDSP(0x38) + WriteSoundBlasterDSP(byte).
 *
 * MUSICAL PHRASE (GM): each path plays a 5-second C-major triad on Acoustic
 * Grand Piano (program 0).
 *
 *   1. GM Reset sysex F0 7E 7F 09 01 F7
 *   2. Program change ch0 -> 0 (Acoustic Grand Piano): C0 00
 *   3. Note On  ch0 C4 vel100: 90 3C 64    -- hold 1.5 sec
 *   4. Note On  ch0 E4 vel100: 90 40 64    -- hold 1.5 sec
 *   5. Note On  ch0 G4 vel100: 90 43 64    -- hold 2.0 sec
 *   6. Note Off ch0 C4/E4/G4
 *   7. CC123 (all notes off) ch0: B0 7B 00
 *
 * argv mode select:
 *   WBTEST D   = PATH=D only
 *   WBTEST P   = PATH=P only
 *   WBTEST A   = both sequentially with ~1 sec silence gap (DEFAULT if no arg)
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

static FILE *g_log = NULL;

static void open_log(void)
{
    g_log = fopen("WBTEST.LOG", "w");
    if (!g_log) g_log = fopen("C:\\WBTEST.LOG", "w");
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

static double now_secs(void) { return (double)uclock() / (double)UCLOCKS_PER_SEC; }

static void sleep_secs(double s)
{
    double t_end = now_secs() + s;
    while (now_secs() < t_end) (void)inportb(0x80);
}

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
        char f = *p++;
        if (f >= 'a' && f <= 'z') f -= ('a' - 'A');
        switch (f) {
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
    int rp = audio_base + 0x6;
    outportb(rp, 1);
    double td = now_secs() + 0.00001; while (now_secs() < td) {} /* 10us hold (v1; v2 uses 100us) */
    outportb(rp, 0);
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

static int g_mpu_base = 0x330;
static int g_audio_base = 0x220;
static int g_path_p_init_failed = 0;

static void path_d_init(int mpu_base)
{
    g_mpu_base = mpu_base;
    /* WBTEST-001 OMISSION: production SDL/0047 writes outportb(mpu_base+1, 0x3F)
       here; this probe skips it (the documented defect). WBTEST-002 corrects. */
}

static void path_d_send_byte(uint8_t b) { outportb(g_mpu_base + 0, b); }

static int path_p_init(int audio_base)
{
    g_audio_base = audio_base;
    g_path_p_init_failed = 0;
    int rc = dsp_reset(audio_base);
    if (rc != 0) {
        plog("[wbtest path=P init] dsp_reset FAILED rc=%d", rc);
        g_path_p_init_failed = 1;
        return -1;
    }
    plog("[wbtest path=P init] dsp_reset OK (DSP responded 0xAA)");
    if (dsp_write_byte(audio_base, 0x34) != 0) {
        plog("[wbtest path=P init] DSP cmd 0x34 FAILED");
        g_path_p_init_failed = 1;
        return -1;
    }
    plog("[wbtest path=P init] DSP cmd 0x34 (UART entry) dispatched");
    return 0;
}

static void path_p_send_byte(uint8_t b)
{
    if (g_path_p_init_failed) return;
    (void)dsp_write_byte(g_audio_base, 0x38);
    (void)dsp_write_byte(g_audio_base, b);
}

typedef enum { PATH_D = 0, PATH_P = 1 } path_t;

static void send_byte(path_t p, uint8_t b)
{
    if (p == PATH_D) path_d_send_byte(b); else path_p_send_byte(b);
}

static void send_bytes(path_t p, const uint8_t *bytes, int n, const char *event)
{
    char hex[64]; int off = 0;
    for (int i = 0; i < n && off < (int)sizeof(hex) - 4; i++)
        off += snprintf(hex + off, sizeof(hex) - off, "%s%02X", i ? " " : "", bytes[i]);
    plog("[wbtest path=%c EVENT=%s bytes=[%s]]", (p == PATH_D) ? 'D' : 'P', event, hex);
    for (int i = 0; i < n; i++) send_byte(p, bytes[i]);
}

static void run_path(path_t p, const blaster_t *b)
{
    const char pt = (p == PATH_D) ? 'D' : 'P';
    plog("");
    plog("===========================================================");
    plog("=== PATH=%c   5-sec piano C-E-G chord starts NOW; listen! ==", pt);
    plog("===========================================================");
    plog("");
    sleep_secs(0.5);
    if (p == PATH_D) {
        path_d_init(b->mpu_base);
        plog("[wbtest path=D init] mpu_base=0x%03X (WBTEST-001 NO I/O at init)", b->mpu_base);
    } else {
        if (path_p_init(b->audio_base) != 0) { plog("[wbtest path=P] init failed"); return; }
    }
    static const uint8_t gm_reset[] = {0xF0,0x7E,0x7F,0x09,0x01,0xF7};
    static const uint8_t prog0[] = {0xC0,0x00};
    static const uint8_t n_c[] = {0x90,0x3C,0x64};
    static const uint8_t n_e[] = {0x90,0x40,0x64};
    static const uint8_t n_g[] = {0x90,0x43,0x64};
    static const uint8_t off_c[] = {0x80,0x3C,0x00};
    static const uint8_t off_e[] = {0x80,0x40,0x00};
    static const uint8_t off_g[] = {0x80,0x43,0x00};
    static const uint8_t cc123[] = {0xB0,0x7B,0x00};
    send_bytes(p, gm_reset, sizeof gm_reset, "gm_reset_sysex");
    send_bytes(p, prog0, sizeof prog0, "prog_change_ch0_piano");
    sleep_secs(0.05);
    send_bytes(p, n_c, sizeof n_c, "note_on_C4"); sleep_secs(1.5);
    send_bytes(p, n_e, sizeof n_e, "note_on_E4"); sleep_secs(1.5);
    send_bytes(p, n_g, sizeof n_g, "note_on_G4"); sleep_secs(2.0);
    send_bytes(p, off_c, sizeof off_c, "note_off_C4");
    send_bytes(p, off_e, sizeof off_e, "note_off_E4");
    send_bytes(p, off_g, sizeof off_g, "note_off_G4");
    send_bytes(p, cc123, sizeof cc123, "cc123_all_notes_off");
    uint8_t irq = mixer_read(b->audio_base, 0x82);
    plog("[wbtest path=%c post] mixer[0x82] = 0x%02X", pt, irq);
    plog("[wbtest path=%c done]", pt);
}

static void boot_diagnostics(blaster_t *b)
{
    plog("---- Boot diagnostics ----");
    parse_blaster(b);
    plog("BLASTER raw: \"%s\"", b->raw);
    plog("BLASTER parsed: A=0x%03X I=%d D=%d H=%d P=0x%03X T=%d",
         b->audio_base, b->irq, b->dma_lo, b->dma_hi, b->mpu_base, b->sb_type);
    int rc = dsp_reset(b->audio_base);
    if (rc == 0) {
        if (dsp_write_byte(b->audio_base, 0xE1) == 0) {
            uint8_t mb=0,lb=0;
            if (dsp_read_byte(b->audio_base,&mb)==0 && dsp_read_byte(b->audio_base,&lb)==0)
                plog("SB16 DSP version: %d.%02d", mb, lb);
        }
    } else {
        plog("WARN: DSP reset rc=%d", rc);
    }
    uint8_t s = inportb(b->mpu_base + 1);
    plog("MPU-401 status raw: 0x%02X", s);
    uint8_t irq = mixer_read(b->audio_base, 0x82);
    plog("Mixer[0x82] baseline: 0x%02X", irq);
    plog("");
}

int main(int argc, char **argv)
{
    open_log();
    plog("=== wbtest v1.0.2 task #1 (WBTEST-001): WB MIDI A/B probe ===");
    int do_d = 0, do_p = 0;
    if (argc < 2) { do_d = do_p = 1; plog("MODE: A (both)"); }
    else {
        char m = argv[1][0];
        if (m >= 'a' && m <= 'z') m -= ('a' - 'A');
        if (m == 'D') { do_d = 1; plog("MODE: D"); }
        else if (m == 'P') { do_p = 1; plog("MODE: P"); }
        else { do_d = do_p = 1; plog("MODE: A"); }
    }
    plog("");
    blaster_t b;
    boot_diagnostics(&b);
    sleep_secs(1.0);
    if (do_d) run_path(PATH_D, &b);
    if (do_d && do_p) { plog(""); plog("---- silence gap ----"); sleep_secs(1.0); }
    if (do_p) run_path(PATH_P, &b);
    plog("");
    plog("[wbtest SUITE_DONE verdict=PROBE_COMPLETED mode=%s]",
         (do_d && do_p) ? "A" : (do_d ? "D" : "P"));
    plog("[SENTINEL_END]");
    if (g_log) fclose(g_log);
    return 0;
}
