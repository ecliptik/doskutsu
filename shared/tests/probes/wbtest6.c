/*
 * wbtest6.c -- WaveBlaster DOSMID-faithful direct-port polling probe (WBTEST-006).
 *
 * v1.0.2 task #16. Standalone DJGPP; no SDL, no engine. Parallel file to
 * wbtest.c (WBTEST-001) through wbtest4.c (WBTEST-004) per the bltpat /
 * bltpat-v2 precedent. (No wbtest5.c -- task #14 ran in-engine instrumentation
 * lane rather than probe lane.)
 *
 * MISSION: test whether MPU-401 status bit 6 (DRR = Data Receive Ready)
 * polling before every write is the missing piece that unlocks audible
 * wavetable MIDI on Vibra16S CT2490 + Dream SAM2695.
 *
 * Lead hypothesis after 5 prior probe iters (per flush-instr rev-8):
 *
 *   H20: Vibra16S CT2490 MPU-401 receive buffer overruns on back-to-back
 *        direct-port writes without DRR polling. Note-on events with
 *        ~1.5 sec spacing fit (single notes drain in time). CC / prog-change
 *        / sysex emit back-to-back without polling; the chip's tiny FIFO
 *        overruns; mid-message status bytes get lost; framing breaks; chip
 *        discards as malformed. This coherently explains:
 *          - PATH=D/DI silence (WBTEST-001/002b/003: direct path no-poll)
 *          - PATH=P/PI silence (WBTEST-002b: DSP-mediated also no-poll-on-MPU)
 *          - WBTEST-004 prog-change ignored (CC07 + C0 nn back-to-back)
 *          - WBTEST-003 Reset SysEx ignored (6-11 byte sysex back-to-back)
 *
 * Source-of-truth for the polling protocol: DOSMID (Tronix286 fork at
 * github.com/Tronix286/DOSMID, MPU401.C). DOSMID is a fully-functional DOS
 * MIDI player that DOES drive wavetable cards on real SB16 + WaveBlaster
 * hardware identical to g2k. Its mpu401_waitwrite() polls status bit 6
 * (DRR) until clear before EVERY outportb to the data port.
 *
 * Critical clarification on W22-WB-F's "don't poll status" guidance:
 * W22-WB-F observed that status bit 7 (TX-READY) never clears on this
 * chip family. That observation is correct and bit 7 polling should be
 * avoided. But the generalization "don't poll status" was overbroad --
 * bit 6 (DRR) is a DIFFERENT bit with DIFFERENT semantics, and per Roland
 * MPU-401 reference it IS reliable. DOSMID has been polling bit 6 in
 * production on Vibra16S boards for ~20 years without lockups.
 *
 * ==========================================================================
 * PROBE-FAITHFULNESS AUDIT TABLE
 * ==========================================================================
 *
 * | block                       | source                                           | faithful? |
 * |-----------------------------|--------------------------------------------------|-----------|
 * | mpu401_waitwrite            | DOSMID MPU401.C: poll status (mpu+1) bit 6 == 0  | YES       |
 * | mpu401_uart                 | DOSMID MPU401.C: waitwrite + outp(mpu+1, 0x3F)   | YES       |
 * | per-byte: waitwrite + outp  | DOSMID MPU401.C: waitwrite + outp(mpu+0, byte)   | YES       |
 * | poll loop wall-clock cap    | NEW; ~5ms cap; defensive against bit 6 also-lies | n/a       |
 * |                             | (which would be a separate problem class)        |           |
 * | GM Reset sysex + phrase     | wbtest3.c verbatim                               | YES       |
 * | Mixer reg dump (39 regs)    | wbtest2/3/4.c chain (mpuwbprobe.c sec.2)         | YES       |
 *
 * The single lever is "bit-6 DRR polling presence/absence". V1 polls
 * before every write (DOSMID-faithful). V2 skips the poll (same byte
 * stream, no waitwrite) -- verifies polling specifically is what unlocks
 * audibility. V3 polls + sends C0 20 program-change to test whether
 * prog-change reaches the voice engine via the polled path.
 *
 * ==========================================================================
 * 3-VARIANT DESIGN
 * ==========================================================================
 *
 *   V1 (control, DOSMID-faithful):
 *       init: mpu401_uart(mpu+1)        -- waitwrite + outp(mpu+1, 0x3F)
 *       per byte: mpu401_waitwrite + outp(mpu+0, byte)
 *       phrase: GM Reset sysex + CC07 vol max ch0+ch9 + prog C0 00 +
 *               C-E-G triad ch0 + ch9 note 36 drum + CC123 + CC120
 *       Expected if H20: piano + bass drum.
 *
 *   V2 (no-poll control):
 *       init: outp(mpu+1, 0x3F)         -- NO waitwrite (skip poll)
 *       per byte: outp(mpu+0, byte)     -- NO waitwrite
 *       phrase: identical to V1
 *       Expected if H20: organ + cowbell (matches WBTEST-002b/003/004 PATH=D
 *       and current SDL/0047 direct branch).
 *       Discriminator: V1 audible AND V2 silent means polling specifically
 *       is the cause; V2 audible too means polling is incidental.
 *
 *   V3 (polled + bass program):
 *       init + per byte: same as V1 (DOSMID-faithful polled)
 *       phrase: GM Reset sysex + CC07 vol max ch0 + prog C0 20 (Acoustic
 *               Bass) + C-E-G triad ch0 + CC123 + CC120 (no drum, isolate
 *               melody to prog-change discrimination)
 *       Expected if H20 + prog-change works through polled path:
 *               distinctly low-pitched bass timbre.
 *       Expected if H20 alone (polling unlocks audibility but prog-change
 *               still ignored): piano (same as V1) -- would re-open H16.
 *
 * Per-variant: ~6 sec audio (V1/V2 with drum) or ~5 sec (V3 melody-only).
 * 3 variants + 2 gaps = ~20 sec total under WBTEST6 A.
 *
 * ==========================================================================
 * argv MODE SELECT
 * ==========================================================================
 *
 *   WBTEST6 1   = V1 only (DOSMID polled; control)
 *   WBTEST6 2   = V2 only (no-poll same-bytes)
 *   WBTEST6 3   = V3 only (polled + bass program)
 *   WBTEST6 A   = all three sequentially (DEFAULT)
 *
 * ==========================================================================
 * OPERATOR PROTOCOL
 * ==========================================================================
 *
 * Per variant, ear-report:
 *   V1 piano + bass drum  -> H20 CONFIRMED. Bit-6 DRR polling is the
 *                            production fix. Add poll to vendor/SDL/src/
 *                            core/dos/SDL_dos_audio_synth.c at the
 *                            direct-port branch (Init L134 + WriteByte
 *                            L240) and flip SDL_HINT_DOS_AUDIO_WB_
 *                            DIRECT_PORT default to ON. Campaign succeeds.
 *   V1 silent / boops     -> direct-port silence is NOT just about polling;
 *                            deeper issue. Campaign closes per flush-instr
 *                            rev-7 outcome 4 (no working path identified).
 *
 *   V2 audible too        -> polling didn't matter; some other DOSMID-
 *                            specific quirk is the unlock. Surface the
 *                            other quirks via next iter.
 *
 *   V3 distinctly bass    -> prog-change works through the polled path.
 *                            Production fix is just the polling change;
 *                            the engine already sends correct C0 nn.
 *   V3 same as V1 (piano) -> prog-change still ignored even with polling.
 *                            Re-opens H16/H19; needs another iter.
 *
 * Any HANG          -> note WHICH banner is last in WBTEST6.LOG. Bit 6
 *                      bounded wallclock cap (5ms) should prevent infinite
 *                      hang, but log will show if poll hit cap repeatedly
 *                      (= chip bit 6 also lies; different problem class).
 *
 * 8.3 DOS filenames:
 *   Source: tests/probes/wbtest6.c   (host-side, gitignored)
 *   Binary: WBTEST6.EXE              (7+3)
 *   Log:    WBTEST6.LOG              (7+3)
 *   BAT:    WBTEST6.BAT              (7+3)
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
    g_log = fopen("WBTEST6.LOG", "w");
    if (!g_log) g_log = fopen("C:\\WBTEST6.LOG", "w");
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
/* BLASTER env parse (verbatim from wbtest4.c)                  */
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
/* SB16 mixer (verbatim from wbtest4.c) -- boot diag dump +     */
/* per-variant post-phrase IRQ-status read.                     */
/* ============================================================ */

static uint8_t mixer_read(int audio_base, uint8_t reg)
{
    outportb(audio_base + 0x4, reg);
    return inportb(audio_base + 0x5);
}

/* ============================================================ */
/* SB16 DSP (verbatim from wbtest4.c) -- only used in boot diag */
/* for DSP version detect. WBTEST-006 does not use DSP-mediated */
/* path; direct-port is the lever under test here.              */
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
/* PATH=DOSMID -- direct-port + bit-6 DRR polling.              */
/*                                                               */
/* Byte-faithful to DOSMID's MPU401.C:                          */
/*                                                               */
/*   void mpu401_waitwrite(int mpuport) {                       */
/*     for (;;) {                                                */
/*       buff = inp(mpuport+1);    // status port                */
/*       if ((buff & 0x40) == 0) break;  // bit 6 DRR clear      */
/*     }                                                         */
/*   }                                                           */
/*   void mpu401_uart(int mpuport) {                            */
/*     mpu401_waitwrite(mpuport);                                */
/*     outp(mpuport+1, 0x3F);                                    */
/*   }                                                           */
/*   // per byte: mpu401_waitwrite(); outp(mpuport+0, byte);    */
/*                                                               */
/* Our addition vs DOSMID: bounded wall-clock cap (~5ms) on     */
/* the waitwrite loop so the probe can't hang forever if bit 6  */
/* also-lies on this chip. DOSMID itself uses an unbounded loop */
/* and has been stable on Vibra16S for ~20 years, so the cap    */
/* should never fire in practice; if it does, the count is       */
/* logged as forensic evidence of a different problem class.    */
/* ============================================================ */

static int g_mpu_base = 0x330;
static int g_no_poll  = 0;   /* V2 sets this to 1 to skip the poll */

/* Per-variant counters for waitwrite stats: total iterations across the
 * variant, max iterations seen in a single waitwrite call, count of
 * waitwrites that hit the wallclock cap. Reset at the start of each
 * variant's emit. */
static uint32_t g_ww_iters_total = 0;
static uint32_t g_ww_iters_max   = 0;
static uint32_t g_ww_cap_hits    = 0;
static uint32_t g_ww_calls       = 0;

static void mpu401_waitwrite(int mpu_base)
{
    if (g_no_poll) return;  /* V2 path */
    g_ww_calls++;
    double t0 = now_secs();
    uint32_t iters = 0;
    for (;;) {
        uint8_t s = inportb(mpu_base + 1);
        if ((s & 0x40) == 0) {
            /* DRR clear -- chip ready to receive. */
            break;
        }
        iters++;
        /* Bounded wall-clock cap: ~5ms per waitwrite call. DOSMID's
         * unbounded loop has been stable on Vibra16S for ~20 years; if
         * we ever hit this cap that's forensic evidence the chip's bit 6
         * also-lies (a separate problem class). */
        if ((iters & 0x3F) == 0 && (now_secs() - t0) > 0.005) {
            g_ww_cap_hits++;
            break;
        }
    }
    g_ww_iters_total += iters;
    if (iters > g_ww_iters_max) g_ww_iters_max = iters;
}

static void mpu401_uart(int mpu_base)
{
    plog("[wbtest6 path=DOSMID init] mpu401_uart: waitwrite + outp(mpu+1=0x%03X, 0x3F)",
         mpu_base + 1);
    mpu401_waitwrite(mpu_base);
    outportb(mpu_base + 1, 0x3F);
}

static void send_byte_dosmid(uint8_t byte)
{
    mpu401_waitwrite(g_mpu_base);
    outportb(g_mpu_base + 0, byte);
}

static void send_bytes(const uint8_t *bytes, int n, const char *event)
{
    char hexbuf[96];
    int off = 0;
    for (int i = 0; i < n && off < (int)sizeof(hexbuf) - 4; i++) {
        off += snprintf(hexbuf + off, sizeof(hexbuf) - off,
                        "%s%02X", i ? " " : "", bytes[i]);
    }
    plog("[wbtest6 EVENT=%s bytes=[%s]]", event, hexbuf);
    for (int i = 0; i < n; i++) send_byte_dosmid(bytes[i]);
}

/* ============================================================ */
/* Variant metadata                                              */
/* ============================================================ */

typedef enum { V_POLLED_PIANO = 1, V_NOPOLL_PIANO = 2, V_POLLED_BASS = 3 } variant_t;

typedef struct {
    int          v_num;
    const char  *name;
    const char  *description;
    int          no_poll;     /* 1 = V2 skip waitwrite; else 0 */
    uint8_t      prog_value;  /* C0 nn program-change value */
    int          include_drum;
} variant_meta_t;

static const variant_meta_t VARIANTS[] = {
    { 1, "V1", "DOSMID-faithful polled, prog=piano + drum",     0, 0x00, 1 },
    { 2, "V2", "NO-poll same-bytes control, prog=piano + drum", 1, 0x00, 1 },
    { 3, "V3", "DOSMID-faithful polled, prog=bass + no drum",   0, 0x20, 0 },
};

/* ============================================================ */
/* Per-variant phrase                                           */
/*                                                               */
/* Standard phrase from WBTEST-003/wbtest3.c verbatim, parameterized */
/* by prog value + drum-inclusion flag. The polling discipline is */
/* baked into send_bytes() via g_no_poll.                          */
/* ============================================================ */

static void play_phrase(uint8_t prog_value, int include_drum)
{
    /* GM Reset sysex. */
    static const uint8_t gm_reset[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
    send_bytes(gm_reset, sizeof gm_reset, "sysex_gm_reset");
    sleep_secs(0.1);

    /* CC07 channel volume MAX on ch 0 (and ch 9 if drums included). */
    static const uint8_t cc07_c0[] = { 0xB0, 0x07, 0x7F };
    send_bytes(cc07_c0, sizeof cc07_c0, "cc07_ch0_vol_max");
    if (include_drum) {
        static const uint8_t cc07_c9[] = { 0xB9, 0x07, 0x7F };
        send_bytes(cc07_c9, sizeof cc07_c9, "cc07_ch9_vol_max");
    }

    /* Program change ch 0 -> prog_value. */
    uint8_t prog_change[2] = { 0xC0, prog_value };
    char event[64];
    snprintf(event, sizeof event, "prog_change_ch0_0x%02X", prog_value);
    send_bytes(prog_change, sizeof prog_change, event);
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

    if (include_drum) {
        plog("[wbtest6] drum-kit test: ch 9 note 36");
        static const uint8_t drum_on[]  = { 0x99, 0x24, 0x64 };
        static const uint8_t drum_off[] = { 0x89, 0x24, 0x00 };
        send_bytes(drum_on, sizeof drum_on, "drum_note_on_ch9_36");
        sleep_secs(1.0);
        send_bytes(drum_off, sizeof drum_off, "drum_note_off_ch9_36");
        sleep_secs(0.2);
    }

    /* CC 123 + CC 120 cleanup. */
    static const uint8_t cc123_c0[] = { 0xB0, 0x7B, 0x00 };
    static const uint8_t cc120_c0[] = { 0xB0, 0x78, 0x00 };
    send_bytes(cc123_c0, sizeof cc123_c0, "cc123_ch0_all_notes_off");
    send_bytes(cc120_c0, sizeof cc120_c0, "cc120_ch0_all_sound_off");
    if (include_drum) {
        static const uint8_t cc123_c9[] = { 0xB9, 0x7B, 0x00 };
        static const uint8_t cc120_c9[] = { 0xB9, 0x78, 0x00 };
        send_bytes(cc123_c9, sizeof cc123_c9, "cc123_ch9_all_notes_off");
        send_bytes(cc120_c9, sizeof cc120_c9, "cc120_ch9_all_sound_off");
    }
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
    plog("  no_poll=%d  prog=0x%02X  include_drum=%d",
         v->no_poll, v->prog_value, v->include_drum);
    plog("");
    sleep_secs(1.0);  /* banner-to-audio correlation pad */

    /* Configure polling discipline for this variant. */
    g_no_poll = v->no_poll;
    g_mpu_base = b->mpu_base;

    /* Reset waitwrite counters at start of variant. */
    g_ww_iters_total = 0;
    g_ww_iters_max   = 0;
    g_ww_cap_hits    = 0;
    g_ww_calls       = 0;

    /* UART entry. V2 (no_poll) still emits the 0x3F write but without
     * the preceding waitwrite -- this matches what SDL/0047 + WBTEST-001
     * to WBTEST-005 all do. */
    mpu401_uart(b->mpu_base);

    /* Phrase. */
    plog("[wbtest6 %s] phrase emit (polling discipline %s)",
         v->name, v->no_poll ? "OFF (V2 control)" : "ON (DOSMID-faithful)");
    play_phrase(v->prog_value, v->include_drum);

    /* Post-variant: emit waitwrite stats + IRQ status reg 0x82. */
    plog("[wbtest6 %s waitwrite stats] calls=%lu  iters_total=%lu"
         "  iters_max=%lu  cap_hits=%lu",
         v->name,
         (unsigned long)g_ww_calls,
         (unsigned long)g_ww_iters_total,
         (unsigned long)g_ww_iters_max,
         (unsigned long)g_ww_cap_hits);
    if (g_ww_cap_hits > 0) {
        plog("[wbtest6 %s] WARNING: %lu waitwrite calls hit the 5ms wallclock"
             " cap. If this is non-zero on g2k, status bit 6 also-lies on this"
             " chip and PATH=DOSMID can't be the production path either"
             " (different problem class -- escalate to flush-instr).",
             v->name, (unsigned long)g_ww_cap_hits);
    }

    uint8_t irq_status = mixer_read(b->audio_base, 0x82);
    plog("[wbtest6 %s post] mixer[0x82] = 0x%02X", v->name, irq_status);
    plog("[wbtest6 %s done]", v->name);
}

/* ============================================================ */
/* Boot diagnostics + mixer reg dump (verbatim chain from        */
/* wbtest2.c -> wbtest3.c -> wbtest4.c)                          */
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

    plog("---- DSP detect (informational only; WBTEST-006 uses direct-port,"
         " not DSP-mediated) ----");
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
            }
        }
    } else {
        plog("WARN: DSP reset rc=%d (no SB16 at A=0x%03X) -- DSP detect"
             " informational only; PATH=DOSMID does not require DSP.",
             rc, b->audio_base);
    }

    /* MPU status raw read (informational; bit 7 lies, bit 6 is what we'll
     * be polling). */
    plog("---- MPU-401 status raw (pre-test) ----");
    uint8_t mpu_status = inportb(b->mpu_base + 1);
    plog("MPU-401 status raw: 0x%02X  (bit7_txready=%d=lies  bit6_drr=%d=DOSMID poll target)",
         mpu_status, (mpu_status >> 7) & 1, (mpu_status >> 6) & 1);

    plog("---- SB16 CT1745 mixer reg dump (verbatim chain mpuwbprobe.c"
         " sec.2 -> wbtest2 -> wbtest3 -> wbtest4 -> wbtest6) ----");
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

static void listen_protocol(int do_v[3])
{
    plog("---- Listen protocol ----");
    plog("Single lever: MPU-401 status bit 6 (DRR) polling presence/absence.");
    plog("DOSMID source: github.com/Tronix286/DOSMID, MPU401.C. Polls bit 6 == 0");
    plog("before every write (UART entry + every MIDI byte). Production-stable");
    plog("on Vibra16S SB16 cards for ~20 years.");
    plog("");
    plog("Variants to run this invocation:");
    for (int i = 0; i < 3; i++) {
        if (do_v[i])
            plog("  %s   %s", VARIANTS[i].name, VARIANTS[i].description);
    }
    plog("");
    plog("Operator: emergency wallclock cap on the poll loop is 5ms per call;");
    plog("waitwrite stats are emitted post-variant. cap_hits should be 0 on a");
    plog("DOSMID-friendly chip; non-zero means bit 6 also-lies (escalate).");
    plog("");
}

static void verdict_table(void)
{
    plog("---- Verdict-mapping table ----");
    plog("");
    plog("V1 (DOSMID-faithful polled) ear-report:");
    plog("  piano + bass drum -> H20 CONFIRMED. Bit-6 DRR polling is the");
    plog("                       production fix.");
    plog("                       Production patch: add poll before each");
    plog("                       outportb in vendor/SDL/src/core/dos/SDL_dos_");
    plog("                       audio_synth.c at L134 (Init_DirectPort,");
    plog("                       before outportb mpu+1 0x3F) and L240");
    plog("                       (WriteByte direct-port branch, before");
    plog("                       outportb mpu+0 byte). Flip");
    plog("                       SDL_HINT_DOS_AUDIO_WB_DIRECT_PORT default");
    plog("                       to ON. Set this as the new SDL/0047 direct-");
    plog("                       port branch standard.");
    plog("  organ + cowbell   -> direct-port silence is NOT just about bit-6");
    plog("                       polling. Campaign closes per flush-instr");
    plog("                       rev-7 outcome 4 (no working path identified).");
    plog("  silence / boops   -> polling unlocks transport but framing is");
    plog("                       wrong somehow (sysex truncation? CC ignored?).");
    plog("                       Needs another iter.");
    plog("");
    plog("V2 (no-poll same bytes) ear-report:");
    plog("  audible (any timbre) -> polling didn't matter; some other DOSMID-");
    plog("                          specific quirk is the unlock. The bit-6");
    plog("                          hypothesis is wrong; investigate other");
    plog("                          deltas (DOSMID also sets mixer regs?");
    plog("                          ISR hook? Bank Select on init?).");
    plog("  silent / non-GM     -> matches WBTEST-001/002b/003 PATH=D");
    plog("                          baseline. Polling specifically is the");
    plog("                          discriminator (V1 audible AND V2 silent");
    plog("                          == polling is the cause).");
    plog("");
    plog("V3 (polled + bass program) ear-report:");
    plog("  distinctly low-pitched bass -> prog-change works via polled path.");
    plog("                                 Production fix is JUST the polling");
    plog("                                 change.");
    plog("  same as V1 (piano)          -> prog-change still ignored even with");
    plog("                                 polling. Re-opens H16/H19; needs");
    plog("                                 another iter.");
    plog("  silent / non-GM             -> matches WBTEST-004 V3 result; both");
    plog("                                 polling and program-change broken.");
    plog("");
    plog("Cross-check waitwrite stats (post-variant lines):");
    plog("  cap_hits == 0  -> DRR polling works on this chip; results trustworthy.");
    plog("  cap_hits >  0  -> bit 6 also-lies; PATH=DOSMID isn't viable either;");
    plog("                    escalate to flush-instr (different problem class).");
    plog("");
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    open_log();
    plog("=== wbtest6 v1.0.2 task #16 (WBTEST-006): DOSMID polled direct-port ===");
    plog("Standalone DJGPP; no SDL, no engine. Target: g2k Vibra16S + SAM2695.");
    plog("Single lever: MPU bit-6 DRR polling. Faithful to DOSMID's MPU401.C");
    plog("polling protocol (mpu401_waitwrite + mpu401_uart + per-byte send).");
    plog("");

    int do_v[3] = { 0, 0, 0 };
    if (argc < 2) {
        for (int i = 0; i < 3; i++) do_v[i] = 1;
        plog("MODE: A (default; all three variants sequentially)");
    } else {
        char m = argv[1][0];
        if (m >= '1' && m <= '3') {
            do_v[m - '1'] = 1;
            plog("MODE: %c  (single variant V%c)", m, m);
        } else if (m == 'a' || m == 'A') {
            for (int i = 0; i < 3; i++) do_v[i] = 1;
            plog("MODE: A (all three variants)");
        } else {
            for (int i = 0; i < 3; i++) do_v[i] = 1;
            plog("MODE: A (unknown arg '%c'; defaulting to all)", m);
        }
    }
    plog("");

    blaster_t b;
    boot_diagnostics(&b);
    listen_protocol(do_v);

    plog("[wbtest6 pretest] 1-sec silence pad before first variant...");
    sleep_secs(1.0);

    int first = 1;
    for (int i = 0; i < 3; i++) {
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
    for (int i = 0; i < 3; i++) { if (do_v[i]) { active_count++; active_one = i + 1; } }
    char modetag[16];
    if (active_count == 3) snprintf(modetag, sizeof modetag, "A");
    else if (active_count == 1) snprintf(modetag, sizeof modetag, "%d", active_one);
    else snprintf(modetag, sizeof modetag, "?");

    plog("[wbtest6 SUITE_DONE verdict=PROBE_COMPLETED mode=%s]", modetag);
    plog("[SENTINEL_END]");

    if (g_log) fclose(g_log);
    return 0;
}
