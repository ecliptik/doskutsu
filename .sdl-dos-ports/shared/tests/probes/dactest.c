/*
 * dactest.c -- VGA DAC pixel-mask (port 0x3C6) detect probe.
 *
 * S3-VIRGE campaign, team task #2. Standalone DJGPP probe; NO SDL, NO engine,
 * NO C++. Confirms (on real HW, one round-trip) the diagnosed root cause of
 * "SDL_Init: No available video device" after the Cirrus->S3 ViRGE/DX swap,
 * and validates the reset approach the SDL fix (sdl-engine task #1) will use.
 *
 * ROOT CAUSE (already diagnosed from source + a real-HW log -- this probe
 * CONFIRMS it, does not re-derive it): SDL's DOS backend DetectVGA() in
 * vendor/SDL/src/video/dos/SDL_dosmodes.c writes 0xA5 to the VGA DAC pixel-
 * mask register (port 0x3C6) and reads it back, expecting 0xA5. On the S3
 * internal SDAC, 0x3C6 overlays a HIDDEN DAC command register reached via
 * consecutive 0x3C6 reads, so the readback != 0xA5 and SDL wrongly concludes
 * "no VGA hardware" and rejects the card. On the Cirrus (and under DOSBox-X)
 * the pixel mask round-trips cleanly, so it was never hit before the swap.
 *
 * The SDL DetectVGA() sequence this probe replicates verbatim (Test 1):
 *     original = inportb(0x3C6);
 *     outportb(0x3C6, 0xA5);
 *     (void)inportb(0x80);            // small I/O delay
 *     readback = inportb(0x3C6);
 *     outportb(0x3C6, original);
 *     return (readback == 0xA5);
 *
 * FOUR TESTS (all pure port I/O; report RAW values so it works on ANY card --
 * Cirrus, S3, DOSBox-X -- with NO card-specific detection in the code):
 *   T1  BARE SDL-REPLICA: the exact sequence above, pristine (NO prior DAC
 *       port access in this probe), so it reflects the cold SDAC state. PASS
 *       iff readback==0xA5. (Hypothesis: FAIL on the S3 SDAC, PASS on Cirrus /
 *       DOSBox-X -- but see T1B: the bare cold sequence does only ONE 0x3C6
 *       read before the write, which may NOT have armed the overlay yet, so a
 *       cold T1 can PASS on the S3 even though SDL fails in situ.)
 *   T1B ARMED-PRECONDITION: read 0x3C6 four times first (arming the SDAC
 *       4-read overlay the way SDL's earlier BIOS/VBE init does), THEN run the
 *       SDL round-trip. Discriminates "bare sequence reproduces the bug" (T1
 *       FAIL) from "only the armed sequence does" (T1 PASS, T1B FAIL) -- the
 *       latter explains a real-HW SDL failure that a cold T1 would miss. This
 *       variant is ADDED beyond the original 3-test brief to de-risk a false
 *       NO_REPRO on the single real-HW round-trip (probe-engineer judgement).
 *   T2  RESET-VARIANT: first touch 0x3C8 (PEL write index) to reset the SDAC
 *       0x3C6 access state machine, THEN repeat the round-trip. PASS here when
 *       T1/T1B FAIL => a DAC-index reset is the fix (informs the SDL patch).
 *   T3  OBSERVE: read 0x3C6 SIX times consecutively + log all six (>=5 reads
 *       are required to actually surface the S3 SDAC hidden-command-register
 *       overlay -- four reads ARM it, the fifth+ access returns it), then touch
 *       0x3C8 and read once more; log it. Pure characterization, no PASS/FAIL.
 *
 * Test order T1 -> T1B -> T2 -> T3 is deliberate: T1 must run pristine (no
 * prior DAC port access in this probe) so it sees the cold state; T1B/T2/T3
 * each establish their own 0x3C6 access state at entry; T3 (which arms the
 * overlay hardest) runs LAST so it cannot contaminate the earlier tests. Every
 * write-bearing test restores the value it read, and the probe forces a known-
 * good pixel mask (0x3C8 reset + write) on the way out -- also via atexit, so
 * an early exit cannot leave the screen palette masked.
 *
 * Runs in TEXT MODE -- the DAC registers are core-VGA and accessible without a
 * mode set, so there is NO graphics-mode switch, NO physical mapping, NO hang
 * risk. Clean bounded exit. HAZARD: low -- identical port writes to SDL's own
 * production DetectVGA, plus a counter reset + known-good pixel-mask restore.
 *
 * Per [[dosbox_not_proxy]]: DOSBox-X's emulated DAC is a plain read/write
 * pixel-mask register with no S3 SDAC command-register overlay, so T1/T1B will
 * PASS under DOSBox-X. The DOSBox-X smoke is correctness-only (runs, exits,
 * writes a parseable log); the PASS/FAIL VERDICT is a g2k-with-S3 measurement.
 *
 * Output: DACTEST.LOG (CWD, fopen-direct; C:\ fallback), flush + fsync per
 * line, plus a stdout mirror. No env vars; no args.
 *
 * DOS constraints: -march=i486 -mtune=pentium -O2, no MMX/SSE. Pure DJGPP libc
 * + port I/O (<pc.h>). 8.3 DOS filename: DACTEST.EXE (7.3) -- fits.
 *
 * License: MIT.
 */

#include <pc.h>          /* inportb / outportb */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>      /* atexit */
#include <unistd.h>      /* fsync */

/* VGA DAC ports (core VGA, accessible in any mode). */
#define DAC_PIXEL_MASK  0x3C6   /* read/write pixel mask; S3 SDAC cmd overlay */
#define DAC_WRITE_INDEX 0x3C8   /* PEL write index -- touching it resets the   */
                                /* SDAC 0x3C6 access state machine             */
#define IO_DELAY_PORT   0x80    /* dummy port for a short I/O settle (as SDL)  */
#define MAGIC           0xA5    /* the value SDL writes + checks               */
#define ARM_READS       4       /* consecutive 0x3C6 reads that arm the overlay */
#define OBS_READS       6       /* T3 reads -- >ARM_READS so the overlay shows  */
#define PEL_MASK_SAFE   0xFF    /* universal-safe pixel mask (all bits pass)    */

/* ============================================================ */
/* Logging -- fopen-direct DACTEST.LOG + stdout mirror, per-line */
/* flush + fsync (mirrors membw / blttile / dacprog convention). */
/* ============================================================ */

static FILE *g_log = NULL;

static void open_log(void)
{
    g_log = fopen("DACTEST.LOG", "w");
    if (!g_log) g_log = fopen("C:\\DACTEST.LOG", "w");
}

static void dlog(const char *fmt, ...)
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

static void io_delay(void) { (void)inportb(IO_DELAY_PORT); }

/* Reset the SDAC 0x3C6 read state machine (any 0x3C7/8/9 access does it). */
static void dac_reset_index(void)
{
    outportb(DAC_WRITE_INDEX, 0x00);
    io_delay();
}

/* The trustworthy pixel-mask value to restore on exit. Captured from T3's
 * post-reset read (a read taken with the counter known-reset reads the real
 * pixel mask, not the overlay). Defaults to the universal-safe 0xFF. */
static uint8_t g_pixel_mask_canonical = PEL_MASK_SAFE;

/* Force a known-good pixel mask: reset the counter so the write addresses the
 * pixel mask (not the overlay), then write the canonical value. Idempotent;
 * safe to run from both the linear exit path and atexit. */
static void restore_pixel_mask_safe(void)
{
    dac_reset_index();
    outportb(DAC_PIXEL_MASK, g_pixel_mask_canonical);
}

/* The SDL DetectVGA() round-trip, factored so T1 and T1B share it exactly.
 * Reads + restores the value it finds; returns 1 iff readback==MAGIC. */
static int sdl_detectvga_roundtrip(uint8_t *out_original, uint8_t *out_readback)
{
    uint8_t original = (uint8_t)inportb(DAC_PIXEL_MASK);
    outportb(DAC_PIXEL_MASK, MAGIC);
    io_delay();
    uint8_t readback = (uint8_t)inportb(DAC_PIXEL_MASK);
    outportb(DAC_PIXEL_MASK, original);          /* restore what we read */
    if (out_original) *out_original = original;
    if (out_readback) *out_readback = readback;
    return (readback == MAGIC);
}

/* T1 -- the exact SDL DetectVGA() round-trip, pristine (no pre-reset). */
static int test1_bare_sdl_replica(void)
{
    uint8_t original = 0, readback = 0;
    int pass = sdl_detectvga_roundtrip(&original, &readback);
    dlog("[dactest] T1_BARE original=0x%02X wrote=0x%02X readback=0x%02X -> %s",
         original, MAGIC, readback, pass ? "PASS" : "FAIL");
    dlog("[dactest] T1_NOTE PASS=pixel-mask round-trips (Cirrus/DOSBox); "
         "FAIL=0x3C6 overlay rejected the write (S3 SDAC) -> SDL aborts "
         "\"No VGA card detected\"");
    return pass;
}

/* T1B -- arm the 4-read overlay first (as SDL's earlier init does in situ),
 * THEN run the SDL round-trip. Catches the case where the cold bare T1 passes
 * but the in-situ (armed) sequence -- which is what SDL actually runs -- fails. */
static int test1b_armed_precondition(void)
{
    uint8_t arm[ARM_READS];
    for (int i = 0; i < ARM_READS; i++) arm[i] = (uint8_t)inportb(DAC_PIXEL_MASK);
    uint8_t original = 0, readback = 0;
    int pass = sdl_detectvga_roundtrip(&original, &readback);
    dlog("[dactest] T1B_ARMED arm_reads(0x3C6 x%d)=0x%02X 0x%02X 0x%02X 0x%02X "
         "then original=0x%02X wrote=0x%02X readback=0x%02X -> %s",
         ARM_READS, arm[0], arm[1], arm[2], arm[3],
         original, MAGIC, readback, pass ? "PASS" : "FAIL");
    dlog("[dactest] T1B_NOTE T1 PASS + T1B FAIL => the bug needs the 0x3C6 "
         "read-counter armed (SDL's BIOS/VBE init arms it) -- a cold bare test "
         "alone would miss it");
    return pass;
}

/* T2 -- reset the SDAC 0x3C6 state machine via a 0x3C8 touch, then repeat. */
static int test2_reset_variant(void)
{
    dac_reset_index();                           /* reset 0x3C6 access state */
    uint8_t original = 0, readback = 0;
    int pass = sdl_detectvga_roundtrip(&original, &readback);
    dlog("[dactest] T2_RESET touch=0x3C8 original=0x%02X wrote=0x%02X "
         "readback=0x%02X -> %s", original, MAGIC, readback,
         pass ? "PASS" : "FAIL");
    dlog("[dactest] T2_NOTE PASS-here-when-T1/T1B-FAILed => a 0x3C8 (PEL-index) "
         "reset before the pixel-mask test is the fix for the SDL patch");
    return pass;
}

/* T3 -- characterize: OBS_READS consecutive 0x3C6 reads expose the overlay
 * (the value changes once >ARM_READS reads have armed it); then a 0x3C8 touch
 * + one read returns the real pixel mask again, which we adopt as canonical. */
static void test3_observe(void)
{
    uint8_t r[OBS_READS];
    for (int i = 0; i < OBS_READS; i++) r[i] = (uint8_t)inportb(DAC_PIXEL_MASK);
    dlog("[dactest] T3_READS 0x3C6 x%d = 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X "
         "0x%02X (plain VGA: all equal/the pixel mask; S3 SDAC: read >=5 "
         "returns the hidden command register)",
         OBS_READS, r[0], r[1], r[2], r[3], r[4], r[5]);

    /* report whether the readout drifted (overlay surfaced) vs stayed flat. */
    int drifted = 0;
    for (int i = 1; i < OBS_READS; i++) if (r[i] != r[0]) drifted = 1;
    dlog("[dactest] T3_DRIFT overlay_surfaced=%s (the six reads %s)",
         drifted ? "YES" : "NO",
         drifted ? "are NOT all equal -- 0x3C6 returns >1 register (SDAC overlay)"
                 : "are all equal -- a plain pixel-mask register (no overlay)");

    dac_reset_index();                           /* reset the read counter */
    uint8_t after = (uint8_t)inportb(DAC_PIXEL_MASK);
    dlog("[dactest] T3_AFTER_RESET 0x3C6 (post 0x3C8 touch) = 0x%02X "
         "(the real pixel mask -- typically 0xFF)", after);

    /* adopt the post-reset read as the canonical restore value (a counter-
     * reset read addresses the real pixel mask). Guard against an absurd 0x00
     * which would blank the palette -- fall back to the safe 0xFF. */
    g_pixel_mask_canonical = (after != 0x00) ? after : PEL_MASK_SAFE;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    /* atexit insurance: even an early exit leaves a known-good pixel mask. */
    atexit(restore_pixel_mask_safe);

    open_log();
    dlog("=== DACTEST -- VGA DAC pixel-mask (0x3C6) detect probe ===");
    dlog("DACTEST-BEGIN");
    dlog("purpose: confirm the S3 SDAC 0x3C6 DetectVGA failure + validate the "
         "0x3C8-reset fix (S3-VIRGE campaign task #2)");
    dlog("build: DJGPP -march=i486 -mtune=pentium -O2; pure port I/O, text mode");
    dlog("NOTE: no DAC port is touched before T1 -- T1 sees the cold SDAC state.");
    dlog("");

    int t1  = test1_bare_sdl_replica();
    dlog("");
    int t1b = test1b_armed_precondition();
    dlog("");
    int t2  = test2_reset_variant();
    dlog("");
    test3_observe();

    /* Leave a known-good pixel mask + reset read state for the post-exit shell
     * (atexit will repeat this harmlessly). */
    restore_pixel_mask_safe();

    dlog("");
    dlog("[dactest] SUMMARY T1_bare=%s T1B_armed=%s T2_reset=%s",
         t1 ? "PASS" : "FAIL", t1b ? "PASS" : "FAIL", t2 ? "PASS" : "FAIL");

    /* Verdict prioritizes the actionable outcome for sdl-engine's task #1.
     * A reproduced failure is "T1 OR T1B FAIL"; the fix is validated by T2. */
    int reproduced = (!t1 || !t1b);
    if (reproduced && t2) {
        dlog("[dactest] VERDICT=CONFIRMED_SDAC_BUG_RESET_FIXES (the 0x3C6 "
             "round-trip FAILs %s, and a 0x3C8 index reset before it makes the "
             "round-trip succeed -- the SDL fix should reset the DAC index "
             "before DetectVGA, or fall back to VBE-presence)",
             (!t1 && !t1b) ? "cold AND armed"
                           : (!t1 ? "cold (bare)" : "only when armed"));
    } else if (!reproduced && t2) {
        dlog("[dactest] VERDICT=NO_REPRO_PIXEL_MASK_OK (T1+T1B+T2 all PASS: this "
             "card's 0x3C6 round-trips cold and armed -- expected on Cirrus / "
             "DOSBox-X; not the S3 failure path. See T3_DRIFT for overlay state)");
    } else if (!t2) {
        dlog("[dactest] VERDICT=RESET_INSUFFICIENT (0x3C6 round-trip FAILs even "
             "after a 0x3C8 reset -- the SDL fix needs the VBE-presence fallback, "
             "not just a DAC-index reset; see T3 raw reads + T3_DRIFT)");
    } else {
        dlog("[dactest] VERDICT=ANOMALOUS (unexpected T1/T1B/T2 combination; "
             "inspect the T3 raw reads + T3_DRIFT)");
    }
    dlog("DACTEST-DONE");

    if (g_log) fclose(g_log);
    return 0;
}
