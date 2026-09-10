/*
 * wbhot.c -- WB-on-486 HOT MPU-401 access discriminator probe (T41 / task #16).
 *
 * DECIDES: is WaveBlaster MIDI salvageable on 486-class CPUs (g2k 486DX2-66)?
 *
 * CONFIRMED CONTEXT (g2k SETUP iter-3 logs + sdl-engine code analysis): on the
 * DX2-66, MPU-401 port access while the SB16 audio device is HOT (autoinit-DMA
 * running + IRQ-5 hooked) stalls the ISA bus and hard-freezes the machine --
 * the confirmed wedge line is the blind UART-entry COMMAND-port write
 * `outportb(0x331, 0x3F)` (vendor/SDL/src/core/dos/SDL_dos_audio_synth.c, the
 * SDL/0093 blind path), and the earlier T23/W22-WB-D evidence implicates the
 * STATUS-port read too. COLD MPU access (before any SB16 open) is provably
 * safe -- SETUP's profiler does a cold 0x331 read every boot. The same board
 * ran WB hot under a POD-83 CPU, so the stall is CPU-timing-dependent.
 *
 * THE UNKNOWN THIS PROBE DECIDES: can the DX2-66 complete a HOT MPU
 * DATA-port (0x330) write at all?
 *   - YES -> a cold-init reorder rescues WB on 486 (enter UART mode cold,
 *     blind-write data bytes hot, skip/reorder the hot 0xFF shutdown reset).
 *   - NO  -> WB is 486-unsupportable on the direct-port transport.
 * Plus the alternative transport: does the production DSP-mediated MIDI path
 * (DSP cmd 0x34 / 0x38 via the SB16 DSP write port, one bus contender by
 * construction) complete hot AND audibly drive the DreamBlaster S2?
 *
 * CELLS (argv[1]; each cell = its OWN BOOT; cell M deliberately risks a
 * hard freeze -- power-cycle after a wedge):
 *
 *   M  direct-MPU discriminator. Step order is the verdict encoding -- the
 *      DECISIVE access comes FIRST among hot MPU accesses so nothing earlier
 *      can contaminate it, and the KNOWN-wedge accesses run LAST as positive
 *      controls:
 *        m1  COLD UART entry: blind outportb(0x331, 0x3F)        [expect OK]
 *        m2  COLD note-on, 3 data bytes to 0x330 (C3 0x90 0x30 0x7F), ~0.8 s
 *            PIT-spin hold, then note-off. Operator HEARS the S2 cold = the
 *            MPU/S2 byte path works cold + chip is in UART mode for the hot
 *            steps (exactly the cold-init-reorder rescue shape).
 *        m3  SB16 brought HOT via the REAL SDL path (SDL_OpenAudioDeviceStream
 *            game config 11025/mono/S16/frames=256 -> DOSSOUNDBLASTER_OpenDevice
 *            autoinit-DMA + IRQ-5 hook, identical to the shipped open; ring
 *            primed via SDL_DOSAudioPump; fill>0 plausibility-gated). Faithful
 *            per the SBPUMP T20 precedent + sdl-engine Q2 confirmation.
 *        m4  THE DECISIVE ACCESS: HOT blind data writes to 0x330 -- note-on
 *            C4 (0x90 0x3C 0x7F), MARK pre/post around EVERY byte.
 *        m5  ~2.5 s SERVICED hold (pump + SDL_Delay + ring-fill heartbeat):
 *            sustained hot coexistence, and the operator listens for the C4
 *            wavetable note over the faint 440 Hz DAC tone.
 *        m6  HOT note-off data writes (0x80 0x3C 0x00), per-byte MARKs --
 *            6 data bytes total = a small stream sample, not a one-shot.
 *        m7  POSITIVE CONTROL 1: HOT status READ inportb(0x331) (the DRR-poll
 *            class of access; decides whether a rescue may include bit-6
 *            polling per WBTEST-006 H20 reliability findings).
 *        m8  POSITIVE CONTROL 2: HOT command WRITE outportb(0x331, 0xFF)
 *            (the production SDL_DOSMpu401Shutdown reset = the known-wedge
 *            access class; confirms the mechanism reproduces in-probe).
 *        m9  clean SDL teardown + exit 0.
 *
 *   D  DSP-mediated transport, HOT by construction. Calls the PRODUCTION
 *      functions exported from libSDL3.a (SDL_DOSAudioSB_DSPMidiEnterUART /
 *      SDL_DOSAudioSB_DSPMidiWriteByte -- DSP cmd 0x34, then 0x38+byte per
 *      MIDI byte, each gated on the DSP write-status port base+0xC bit 7,
 *      100000-iter cap), NOT a re-implementation:
 *        d1  SB16 hot bring-up (same as m3)
 *        d2  DSPMidiEnterUART (DSP cmd 0x34), MARK pre/post
 *        d3  note-on G4 (0x90 0x43 0x7F) via DSPMidiWriteByte, per-byte MARKs
 *        d4  ~2.5 s serviced hold w/ ring-fill heartbeat (ALSO observes
 *            whether cmd 0x34 UART mode kills the running DAC playback --
 *            watch the heartbeat fill + listen for the 440 Hz tone dying)
 *        d5  note-off via DSPMidiWriteByte, per-byte MARKs
 *        d6  clean teardown + exit 0.
 *      Distinct pitches (cold C3 / hot-direct C4 / DSP G4) so the operator's
 *      ear identifies WHICH path drove the S2 across cells.
 *
 * READ-THE-VERDICT TABLE (cell M; last surviving "MARK" line in the log):
 *   last MARK = "pre m4 hot_data b1"        -> hot DATA writes stall ->
 *                                              WB 486-UNSUPPORTABLE (direct).
 *   m4..m6 post-MARKs all present, last =
 *     "pre m7 ctrl_status_read"             -> hot data OK, hot STATUS READ
 *                                              stalls -> rescue VIABLE but
 *                                              blind-write only (no DRR poll).
 *     "pre m8 ctrl_cmd_write"               -> hot data + status read OK, hot
 *                                              COMMAND write stalls -> rescue
 *                                              VIABLE incl. DRR poll; shutdown
 *                                              reset must move cold.
 *   all MARKs + "done: closed cleanly"      -> NO repro in probe context ->
 *                                              mechanism is narrower than
 *                                              "any hot MPU access" (e.g.
 *                                              ISR-context interleave); report,
 *                                              do NOT declare rescue proven.
 *   CELL 9 (v3, iter-5 rev-3 sec 7.4 design -- the H-B mechanism + fix-shape
 *   differential in ONE boot; production-shape: SB16 first, NO cold MPU touch):
 *     p9a  SB16 hot via the production MIX path, then a fixed-beat BASELINE
 *          stall watch (~2 s, 8 beats at ~250 ms): each beat = top ring full,
 *          NO-pump 100 ms windows (up to 3 on delta==0 -- ISR chunk
 *          granularity is ~93 ms, one window can straddle zero drains),
 *          re-read fill; delta>=256 frames = beat ALIVE, delta==0 after
 *          300 ms = beat STALL (the p5 drain-witness pattern, repeated).
 *     p9b  HOT 0x3F UART entry, ACK DELIBERATELY UNDRAINED (the production
 *          shape common to poll + blind), then a 5 s / 20-beat stall watch.
 *          EXPECT if H-B real: sustained STALL run (fill pinned) or machine
 *          hard-freeze -- the fsync'd per-beat MARK trail carries the verdict
 *          (last MARK names the heartbeat). MECHANISM PROVEN.
 *     p9c  bounded drain of whatever is pending (DSR-capped + ONE
 *          unconditional data read on cap, the 0094 shape; logs every byte),
 *          then a 2 s / 8-beat RECOVERY watch (does draining RESCUE an
 *          already-stalled stream?), then a SECOND hot 0x3F IMMEDIATELY
 *          followed by the same bounded drain (the FIX shape), then a
 *          5 s / 20-beat watch. EXPECT: no stall (or fast recovery) =
 *          FIX SHAPE PROVEN.
 *          Watch labels: p9a baseline / p9b undrained / p9r recovery /
 *          p9d drained. B-before-C is deliberate: B may freeze the machine;
 *          C is the same boot's bonus if it survives.
 *
 *   (cell D: all d-MARKs + clean exit + operator hears G4 -> DSP transport is
 *   bus-safe + drives the S2. BUT the d4 "drain-witness" line then decides
 *   COEXISTENCE: "DAC STALLED" after the cmd-0x34 UART entry = the DSP path
 *   kills SB16 PCM playback -> DISQUALIFIED as the game transport (the game
 *   needs simultaneous SFX + music) even though bus-safe; "DAC ALIVE" = full
 *   candidate. m5 runs the same witness as the mechanism's positive control.
 *   Observe-don't-extrapolate: record what the witness shows; a non-UART
 *   polled-write variant (0x38 without 0x34 entry) is a NOTED follow-up,
 *   not asserted.)
 *
 * DOSBox-X note (REAL-HW-ONLY per dosbox_not_proxy): DOSBox-X does NOT
 * reproduce the ISA IOCHRDY stall -- every cell runs clean there. The DOSBox
 * smoke proves ONLY structural correctness (markers in order, log parses,
 * clean exit). The verdict is g2k-DX2-66-only.
 *
 * Output: LOGS\<TAG>PROBE.LOG honoring DOS_PORT_LOG_TAG (3-char cap; unset ->
 * PROBEDBG.LOG), per-line fflush+fsync so the LAST LINE SURVIVES a hard
 * freeze; same path idiom as SBPUMP/SETUP so realhw's LOGS\<TAG>*.LOG logback
 * picks it up. Distinct tag per cell (e.g. WM1 / WD1).
 *
 * Usage:  WBHOT M    (direct-MPU discriminator -- DELIBERATE FREEZE RISK)
 *         WBHOT D    (DSP-mediated transport   -- expected clean)
 *         WBHOT P    (polled direct-port differential)
 *         WBHOT 9    (P9: hot-0x3F undrained-vs-drained -- FREEZE RISK in p9b)
 *
 * Build:  make probes-wbhot   (SDL3-linked, PROBES_SDL_* recipe, minstack 2048k)
 *
 * DOS/SDL constraints honored: no video, no engine, no C++, no threads,
 * static link only, -march=i486 (no SIMD). ASCII-only. License: MIT.
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_dosaudio_pump.h>  /* SDL_DOSAudioPump, SDL_DOSAudioRingFillFrames */
#include <SDL3_mixer/SDL_mixer.h>    /* v2: cell-D production-faithful MIX bring-up */

#include <conio.h>      /* kbhit / getch -- hold-window ESC poll */
#include <pc.h>         /* inportb / outportb -- MPU ports + PIT latch-read */
#include <sys/stat.h>   /* mkdir() for LOGS\ */
#include <math.h>       /* sinf -- prime tone gen (init only) */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>     /* fsync -- per-line durability */

/* Production DSP-mediated MIDI helpers -- exported from libSDL3.a (verified
 * `nm`: T _SDL_DOSAudioSB_*), declared in the non-installed private header
 * vendor/SDL/src/core/dos/SDL_dos_audio_synth.h. Cell D calls the REAL
 * production code; prototypes mirrored here verbatim. */
extern bool SDL_DOSAudioSB_IsInitialized(void);
extern void SDL_DOSAudioSB_DSPMidiEnterUART(void);
extern void SDL_DOSAudioSB_DSPMidiWriteByte(uint8_t byte);

#ifndef WBHOT_SHA12
#define WBHOT_SHA12 "nostamp"
#endif
/* v2 (2026-06-11, post-iter-4): cell-D bring-up switched from raw
 * SDL_OpenAudioDeviceStream to the PRODUCTION MIX path (MIX_CreateMixerDevice
 * + looping sine track), mirroring setup/audiotest_sdl.c device_open() --
 * iter-4 cell D returned INVALID (fill=0) because the raw-stream open does
 * not prime at 11025 mono on real g2k-class HW (SBPUMP precedent: only
 * 22050-stereo primed raw), while the MIX open primes fine at the production
 * config on the same hardware. Cell M is UNCHANGED (its iter-4 verdict is in:
 * the machine wedged INSIDE the m3 SB16 open after cold MPU access -- cell D
 * same-boot-shape control proved the open completes without MPU touch ->
 * MPU+SB16 coexistence wedges in EITHER order -> cold-init-reorder rescue
 * DEAD; the m4 question is moot). Version line bumped so a stale v1
 * WD1PROBE.LOG cannot masquerade as the v2 verdict (S3BLT lesson). */
/* v3 (2026-06-12, post-iter-5 run-2): NEW cell 9 (P9) per the rev-3 analysis
 * sec 7.4 -- the H-B hypothesis differential: production-shape HOT 0x3F
 * UART entry with the 0xFE ACK left UNDRAINED while SB16 autoinit DMA runs
 * (phase B; expect stall/wedge if H-B real), then bounded-drain + a second
 * hot 0x3F + immediate drain (phase C, the fix shape; expect clean). Cells
 * P / M / D UNCHANGED (regression reference). New log tag WP9 -> WP9PROBE.LOG
 * so a stale WP1PROBE.LOG cannot masquerade as the v3 verdict. */
#define WBHOT_VERSION "v3"

/* MPU-401 ports (BLASTER P field default; g2k = P330). */
#define MPU_DATA   0x330
#define MPU_STAT   0x331   /* status (read) / command (write) */

/* Game audio config (the shipped open; the wedge timing is config-sensitive). */
#define GAME_FREQ      11025
#define GAME_CHANNELS  1

/* Hold windows + prime tone. */
#define COLD_HOLD_MS   800
#define HOT_HOLD_MS    2500
#define HEARTBEAT_MS   320
#define TONE_HZ        440
#define TONE_AMP       0.15f   /* faint -- the S2 note must be audible OVER it */

/* Distinct pitches per access path (operator's-ear witness). */
#define NOTE_COLD  0x30  /* C3 -- cell M cold direct-port (blind) */
#define NOTE_HOT   0x3C  /* C4 -- cell M HOT direct-port (blind decisive bytes) */
#define NOTE_DSP   0x43  /* G4 -- cell D DSP-mediated */
#define NOTE_POLL  0x40  /* E4 -- cell P polled direct-port (v2) */

/* Bounded-poll caps (v2 cell P). DRR cap mirrors production SDL/0080
 * (10000 iters; empirical max 238 on POD-83, cap_hits=0). ACK-drain cap is
 * generous-but-bounded; the drain logs WHAT it read so a non-0xFE byte is
 * data, not a hang. */
#define DRR_POLL_CAP   10000
#define ACK_DRAIN_CAP  20000

/* v3 cell-9 stall-watch geometry. Fixed BEAT COUNTS (not wall-clock loops) so
 * the smoke can verify the exact emit structure. Each beat: top the ring to
 * full, then NO-pump windows of 100 ms (up to 3 -- the extension exists
 * because the ISR drains the ring in chunk granularity up to 1024 frames =
 * ~93 ms at 11025 Hz, so a SINGLE 100 ms window can legitimately straddle
 * zero drain events; measured as a spurious STALL beat in the first DOSBox
 * smoke of this cell). delta >= 256 frames (one chunk; v2 drain-witness
 * threshold) = ALIVE; delta still 0 after all 3 windows (300 ms) = STALL;
 * in between = PARTIAL (logged raw). 300 ms unpumped from a full ring is
 * safe: ring depth ~371 ms and SDL_Delay still yields (v2 rationale).
 * Beat period ~250 ms per the rev-3 p9 design (a STALL beat stretches to
 * ~300 ms; when stalled nothing drains, so pacing fidelity is moot). */
#define P9_BEAT_MS        250
#define P9_MEASURE_MS     100
#define P9_MEASURE_WINS   3
#define P9_ALIVE_DELTA    256
#define P9_BASE_BEATS     8    /* phase A baseline  ~2 s */
#define P9_WATCH_BEATS    20   /* phases B + C       ~5 s each */
#define P9_RECOV_BEATS    8    /* phase C recovery  ~2 s */
#define P9_DRAIN_MAX_BYTES 8   /* bounded pending-drain byte cap */
#define P9_DSR_CAP        60000 /* first-byte DSR poll cap (0094 shape) */
#define P9_DSR_CAP_NEXT   2000  /* subsequent-byte DSR poll cap */

/* ---- PIT channel-0 latch-read (read-only, reprogram-free; SBPUMP idiom) ----*/

#define PIT_HZ 1193182UL

static uint16_t pit_count(void)
{
  uint8_t lo, hi;
  outportb(0x43, 0x00);  /* latch counter 0 -- writes NO mode/divisor */
  lo = inportb(0x40);
  hi = inportb(0x40);
  return (uint16_t)(lo | (hi << 8));
}

/* Spin for ~ms via PIT latch-reads. Used ONLY for the COLD hold (no SDL yet);
 * hot holds are SERVICED (pump + SDL_Delay) instead. Port I/O per iteration
 * keeps DOSBox-X's emulated counter advancing (SBPUMP rationale (3)). */
static void pit_spin_ms(unsigned long ms)
{
  unsigned long elapsed = 0;
  unsigned long target = (ms * PIT_HZ) / 1000UL;
  uint16_t last = pit_count();
  while (elapsed < target)
  {
    uint16_t c = pit_count();
    elapsed += (c <= last) ? (unsigned long)(last - c)
                           : (unsigned long)(last + (65536UL - c));
    last = c;
  }
}

/* ---- trace (per-line fflush + fsync; last line survives a hard freeze) ----*/

static FILE *g_fp = NULL;

static void trace(const char *fmt, ...)
{
  va_list ap;
  if (!g_fp) return;
  va_start(ap, fmt);
  vfprintf(g_fp, fmt, ap);
  va_end(ap);
  fputc('\n', g_fp);
  fflush(g_fp);
  fsync(fileno(g_fp));
}

/* MARK lines are the verdict encoding. Unique line FORMAT ("MARK pre|post
 * <step>") that no banner/help text contains, per the grep-anchor-confound
 * rule -- parsers anchor on "^MARK ". */
static void mark(const char *prepost, const char *step)
{
  trace("MARK %s %s", prepost, step);
}

static void trace_open(void)
{
  char base[16];
  char path[40];
  const char *tag;

  mkdir("LOGS", 0777);

  tag = getenv("DOS_PORT_LOG_TAG");
  if (tag && tag[0])
  {
    char t3[4];
    int i;
    for (i = 0; i < 3 && tag[i]; ++i) t3[i] = tag[i];
    t3[i] = '\0';
    snprintf(base, sizeof(base), "%sPROBE.LOG", t3);  /* e.g. WM1PROBE.LOG */
  }
  else
  {
    strcpy(base, "PROBEDBG.LOG");
  }
  snprintf(path, sizeof(path), "LOGS/%s", base);
  g_fp = fopen(path, "wb");
}

/* ---- SB16 hot bring-up via the REAL SDL path (SBPUMP-faithful) ------------*/

static SDL_AudioStream *g_stream = NULL;
static int16_t *g_tone = NULL;

/* v2 cell-D MIX objects (production bring-up). */
static MIX_Mixer *g_mixer = NULL;
static MIX_Audio *g_aud   = NULL;
static MIX_Track *g_trk   = NULL;

/* Returns 0 on success; ring-fill plausibility-gated. */
static int sb16_bring_hot(void)
{
  SDL_AudioSpec spec;
  int tone_frames, tone_samples, i, prime_fill;

  SDL_SetHintWithPriority(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256", SDL_HINT_OVERRIDE);
  if (!SDL_Init(SDL_INIT_AUDIO))
  {
    trace("FATAL: SDL_Init(AUDIO) failed: %s", SDL_GetError());
    return 1;
  }
  trace("init: SDL_Init(AUDIO) ok");

  SDL_zero(spec);
  spec.format   = SDL_AUDIO_S16;
  spec.channels = GAME_CHANNELS;
  spec.freq     = GAME_FREQ;

  trace("open: SDL_OpenAudioDeviceStream (starts autoinit-DMA + hooks IRQ-5)");
  g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
  if (!g_stream)
  {
    trace("FATAL: SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
    return 1;
  }
  trace("open: device live, stream bound");

  /* Faint continuous tone: audible proof the DAC path is live, quiet enough
   * that the S2 wavetable note is distinguishable over it. */
  tone_frames  = GAME_FREQ * ((HOT_HOLD_MS / 1000) + 4);
  tone_samples = tone_frames * GAME_CHANNELS;
  g_tone = (int16_t *)malloc((size_t)tone_samples * sizeof(int16_t));
  if (!g_tone)
  {
    trace("FATAL: tone malloc failed (%d samples)", tone_samples);
    return 1;
  }
  for (i = 0; i < tone_frames; ++i)
  {
    float s = sinf((float)(2.0 * M_PI * TONE_HZ) * (float)i / (float)GAME_FREQ);
    g_tone[i] = (int16_t)(s * TONE_AMP * 32767.0f);
  }
  if (!SDL_PutAudioStreamData(g_stream, g_tone, tone_samples * (int)sizeof(int16_t)))
    trace("warn: SDL_PutAudioStreamData failed: %s", SDL_GetError());
  if (!SDL_ResumeAudioStreamDevice(g_stream))
    trace("warn: SDL_ResumeAudioStreamDevice failed: %s", SDL_GetError());

  while (SDL_DOSAudioPump()) { /* top the SB16 ring */ }
  prime_fill = SDL_DOSAudioRingFillFrames();
  trace("prime: ring fill = %d frames", prime_fill);
  if (prime_fill <= 0)
  {
    trace("INVALID: ring did NOT prime (fill<=0) -- device not streaming; "
          "the device is NOT hot -> cell result MEANINGLESS this run "
          "(re-check BLASTER / device open)");
    return 1;
  }
  trace("prime: OK -- DMA+IRQ-5 live with real audio in the ring (HOT)");
  return 0;
}

/* v2: production-faithful SB16 bring-up via the MIX path, mirroring
 * setup/audiotest_sdl.c device_open() -- MIX_CreateMixerDevice at the
 * production Tier-2 config + a faint LOOPING sine track so the DMA genuinely
 * streams. Fixes the iter-4 INVALID: the raw SDL_OpenAudioDeviceStream open
 * does not prime at 11025 mono on real g2k-class HW, the MIX open does.
 * Returns 0 on success; ring-fill plausibility-gated like the raw path. */
static int sb16_bring_hot_mix(void)
{
  SDL_AudioSpec spec;
  int prime_fill;

  if (!SDL_Init(SDL_INIT_AUDIO))
  {
    trace("FATAL: SDL_Init(AUDIO) failed: %s", SDL_GetError());
    return 1;
  }
  if (!MIX_Init())
  {
    trace("FATAL: MIX_Init failed: %s", SDL_GetError());
    return 1;
  }
  trace("init: SDL_Init(AUDIO) + MIX_Init ok");

  /* Pin frames=256 mirroring SoundManager::init / SETUP Tier-2. */
  SDL_SetHintWithPriority(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256", SDL_HINT_OVERRIDE);

  SDL_zero(spec);
  spec.format   = SDL_AUDIO_S16;
  spec.channels = GAME_CHANNELS;
  spec.freq     = GAME_FREQ;

  trace("open: MIX_CreateMixerDevice %d/%s/S16 frames=256 "
        "(opens SB16: DSP reset + DMA program + IRQ hook)",
        GAME_FREQ, (GAME_CHANNELS == 1) ? "mono" : "stereo");
  g_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
  if (!g_mixer)
  {
    trace("FATAL: MIX_CreateMixerDevice failed: %s", SDL_GetError());
    return 1;
  }

  /* Faint looping tone (audible DAC-alive witness; S2 note distinguishable
   * over it). 8000 ms covers the whole cell window even loop-less
   * (belt-and-braces), and loops=-1 is set AFTER MIX_PlayTrack: PlayTrack's
   * options=0 picks MIX_PROP_PLAY_LOOPS_NUMBER default 0, REPLACING any
   * pre-play MIX_SetTrackLoops (its contract: "change loops of a
   * currently-playing track"). Set-before-play silently loses the loop --
   * measured in this probe's first MIX smoke (ring drained to 0 at the
   * 1000 ms audio end and never refilled). NOTE: setup/audiotest_sdl.c
   * device_open uses the set-before-play order; flagged to sdl-engine. */
  g_aud = MIX_CreateSineWaveAudio(g_mixer, TONE_HZ, TONE_AMP, 8000);
  g_trk = MIX_CreateTrack(g_mixer);
  if (!g_aud || !g_trk)
  {
    trace("FATAL: tone/track create failed: %s", SDL_GetError());
    return 1;
  }
  MIX_SetTrackAudio(g_trk, g_aud);
  if (!MIX_PlayTrack(g_trk, 0))
  {
    trace("FATAL: MIX_PlayTrack failed: %s", SDL_GetError());
    return 1;
  }
  MIX_SetTrackLoops(g_trk, -1);  /* AFTER play -- see comment above */
  trace("open: mixer up, looping tone track playing");

  while (SDL_DOSAudioPump()) { /* top the SB16 ring */ }
  prime_fill = SDL_DOSAudioRingFillFrames();
  trace("prime: ring fill = %d frames", prime_fill);
  if (prime_fill <= 0)
  {
    trace("INVALID: ring did NOT prime (fill<=0) -- device not streaming; "
          "the device is NOT hot -> cell result MEANINGLESS this run "
          "(MIX path too; re-check BLASTER / device open)");
    return 1;
  }
  trace("prime: OK -- DMA+IRQ-5 live with real audio in the ring (HOT, MIX path)");
  return 0;
}

/* Serviced hold: pump + yield + heartbeat (ring fill locates a mid-hold wedge
 * + observes whether the DAC stream survives). ESC aborts early.
 *
 * DRAIN WITNESS (one-shot, mid-hold; sdl-engine sign-off addition): pump the
 * ring to full, then SDL_Delay(100) WITHOUT pumping and re-read the fill.
 * Pure SDL-side (ZERO port accesses -- not a wedge candidate). The DMA drains
 * ~1102 frames per 100 ms at 11025 Hz, so a drop >= 256 (one chunk) = the DAC
 * is consuming ("DAC ALIVE"); unchanged fill = the PCM stream is dead ("DAC
 * STALLED"). In cell D this DECIDES coexistence: if DSP cmd-0x34 UART mode
 * stalls the DAC, the DSP transport cannot run alongside SB16 PCM SFX and is
 * DISQUALIFIED as the game transport even if bus-safe + audible. Cell M's m5
 * runs the same witness as a positive control for the witness mechanism
 * itself (direct-port data writes must not stop the DAC). The 100 ms unpumped
 * window is safe: ring depth is ~390 ms and SDL_Delay still YIELDS to the
 * cooperative scheduler (the SBPUMP wedge condition was no-pump AND no-yield). */
static void serviced_hold(const char *label, unsigned long ms)
{
  Uint64 start = SDL_GetTicks();
  Uint64 next_hb = (Uint64)HEARTBEAT_MS;
  Uint64 now;
  int drain_done = 0;
  trace("%s: serviced hold ~%lu ms (pump + yield + heartbeat)", label, ms);
  for (;;)
  {
    while (SDL_DOSAudioPump()) { }
    if (kbhit())
    {
      int k = getch();
      if (k == 0 || k == 0xE0) { (void)getch(); k = 27; }
      if (k == 27) { trace("%s: hold aborted by ESC", label); break; }
    }
    now = SDL_GetTicks() - start;
    if (!drain_done && now >= (Uint64)(ms / 2))
    {
      int f0, f1;
      while (SDL_DOSAudioPump()) { }  /* top to full first */
      f0 = SDL_DOSAudioRingFillFrames();
      SDL_Delay(100);                 /* NO pump -- let the DMA drain */
      f1 = SDL_DOSAudioRingFillFrames();
      trace("%s: drain-witness fill %d -> %d over ~100 ms (%s)", label, f0, f1,
            (f0 - f1 >= 256) ? "DAC ALIVE -- stream consuming"
                             : "DAC STALLED -- stream NOT consuming");
      drain_done = 1;
    }
    if (now >= next_hb)
    {
      trace("%s: heartbeat t=%lu ms ring=%d frames",
            label, (unsigned long)now, SDL_DOSAudioRingFillFrames());
      next_hb += (Uint64)HEARTBEAT_MS;
    }
    if (now >= (Uint64)ms)
      break;
    SDL_Delay(5);
  }
  trace("%s: hold complete (ring fill = %d frames)", label, SDL_DOSAudioRingFillFrames());
}

static void teardown_clean(void)
{
  trace("teardown: pausing + closing device");
  if (g_stream)
  {
    SDL_PauseAudioStreamDevice(g_stream);
    SDL_DestroyAudioStream(g_stream);
    g_stream = NULL;
  }
  /* v2 MIX objects (production device_close order: tracks, audio, mixer). */
  if (g_trk)   { MIX_DestroyTrack(g_trk);   g_trk = NULL; }
  if (g_aud)   { MIX_DestroyAudio(g_aud);   g_aud = NULL; }
  if (g_mixer) { MIX_DestroyMixer(g_mixer); g_mixer = NULL; }
  free(g_tone);
  g_tone = NULL;
  SDL_Quit();
  trace("done: closed cleanly, exit 0");
}

/* One marker-bracketed blind MPU data-port write. */
static void mpu_data_write_marked(const char *step, uint8_t byte)
{
  char buf[48];
  snprintf(buf, sizeof(buf), "%s 0x%02X", step, (unsigned)byte);
  mark("pre", buf);
  outportb(MPU_DATA, byte);
  mark("post", buf);
}

/* One marker-bracketed production DSPMidiWriteByte. */
static void dsp_midi_write_marked(const char *step, uint8_t byte)
{
  char buf[48];
  snprintf(buf, sizeof(buf), "%s 0x%02X", step, (unsigned)byte);
  mark("pre", buf);
  SDL_DOSAudioSB_DSPMidiWriteByte(byte);
  mark("post", buf);
}

/* ---- v2 polled MPU helpers (cell P -- the v1.0.3/SDL-0080 access pattern) --*/

/* Bounded bit-6 DRR poll on the status port, mirroring production
 * SDL_DOSMpu401WaitDRR (SDL/0080: wait DRR clear before EVERY MPU write;
 * iter cap 10000; fail-soft = caller writes anyway). Returns iters used,
 * or -1 on cap. */
static int mpu_wait_drr(void)
{
  int iter;
  for (iter = 0; iter < DRR_POLL_CAP; iter++)
  {
    if ((inportb(MPU_STAT) & 0x40) == 0)
      return iter;
  }
  return -1;
}

/* Bounded UART-entry ACK drain: poll DSR (bit 7 low = data ready) and read
 * the data port, logging what came back. Task-#23 suspect mechanism: blind
 * mode leaves the 0xFE ACK unread, which may wedge the next SB16 bring-up.
 * This drain is the corrective half of the polled pattern. Logs every byte
 * read (expect exactly one 0xFE on a healthy chip). */
static void mpu_drain_ack(const char *step)
{
  int iter, got = 0;
  for (iter = 0; iter < ACK_DRAIN_CAP; iter++)
  {
    if ((inportb(MPU_STAT) & 0x80) == 0)
    {
      uint8_t b = inportb(MPU_DATA);
      trace("%s: drained byte 0x%02X at iter %d%s", step, (unsigned)b, iter,
            (b == 0xFE) ? " (ACK)" : " (NOT the 0xFE ACK -- note it)");
      got++;
      if (b == 0xFE)
        return;
    }
  }
  trace("%s: drain done, %d byte(s), no 0xFE within %d iters%s", step, got,
        ACK_DRAIN_CAP, got ? "" : " (nothing pending -- note it)");
}

/* One marker-bracketed POLLED MPU data write: DRR poll, then the byte.
 * The poll result is logged AFTER the post-MARK (the MARKs bracket the full
 * polled-write unit -- a wedge inside the poll names this step). */
static void mpu_data_write_polled_marked(const char *step, uint8_t byte)
{
  char buf[48];
  int it;
  snprintf(buf, sizeof(buf), "%s 0x%02X", step, (unsigned)byte);
  mark("pre", buf);
  it = mpu_wait_drr();
  outportb(MPU_DATA, byte);
  mark("post", buf);
  if (it < 0)
    trace("%s: DRR cap hit (wrote anyway, fail-soft per production)", buf);
}

/* ---- cell M: direct-MPU discriminator -------------------------------------*/

static int cell_m(void)
{
  uint8_t status;

  /* m1: COLD UART entry -- blind command-port write, exactly the production
   * SDL/0093 blind path byte. Provably-safe class (cold). */
  mark("pre", "m1 cold_uart_entry");
  outportb(MPU_STAT, 0x3F);
  mark("post", "m1 cold_uart_entry");

  /* m2: COLD note-on / hold / note-off -- data path works cold + chip now in
   * UART mode (the cold-init-reorder rescue shape). Operator hears C3. */
  mpu_data_write_marked("m2 cold_note_on b1", 0x90);
  mpu_data_write_marked("m2 cold_note_on b2", NOTE_COLD);
  mpu_data_write_marked("m2 cold_note_on b3", 0x7F);
  trace("m2: cold hold ~%d ms (PIT spin; listen for C3 from the S2)", COLD_HOLD_MS);
  pit_spin_ms(COLD_HOLD_MS);
  mpu_data_write_marked("m2 cold_note_off b1", 0x80);
  mpu_data_write_marked("m2 cold_note_off b2", NOTE_COLD);
  mpu_data_write_marked("m2 cold_note_off b3", 0x00);
  trace("m2: cold direct-port MIDI complete -- COLD access class OK");

  /* m3: SB16 HOT bring-up (real SDL path). */
  mark("pre", "m3 sb16_hot_bringup");
  if (sb16_bring_hot() != 0)
  {
    teardown_clean();
    return 1;
  }
  mark("post", "m3 sb16_hot_bringup");

  /* m4: THE DECISIVE ACCESS -- first hot MPU access of the run. */
  trace("m4: DECISIVE -- hot blind DATA writes to 0x%03X follow; on a wedge "
        "the last pre-MARK names the stalling byte", MPU_DATA);
  mpu_data_write_marked("m4 hot_data b1", 0x90);
  mpu_data_write_marked("m4 hot_data b2", NOTE_HOT);
  mpu_data_write_marked("m4 hot_data b3", 0x7F);
  trace("m4: HOT DATA WRITES COMPLETED -- direct-port rescue candidate ALIVE");

  /* m5: sustained hot coexistence + audible witness (C4 over the 440 tone). */
  serviced_hold("m5", HOT_HOLD_MS);

  /* m6: hot note-off -- stream sample, not a one-shot. */
  mpu_data_write_marked("m6 hot_data_off b1", 0x80);
  mpu_data_write_marked("m6 hot_data_off b2", NOTE_HOT);
  mpu_data_write_marked("m6 hot_data_off b3", 0x00);

  /* m7: POSITIVE CONTROL 1 -- hot STATUS read (DRR-poll access class). */
  trace("m7: positive controls follow -- KNOWN-WEDGE access classes; a freeze "
        "from here on still leaves the m4-m6 verdict intact");
  mark("pre", "m7 ctrl_status_read");
  status = inportb(MPU_STAT);
  mark("post", "m7 ctrl_status_read");
  trace("m7: hot status read OK (status=0x%02X)", (unsigned)status);

  /* m8: POSITIVE CONTROL 2 -- hot COMMAND write (production shutdown 0xFF). */
  mark("pre", "m8 ctrl_cmd_write");
  outportb(MPU_STAT, 0xFF);
  mark("post", "m8 ctrl_cmd_write");
  trace("m8: hot command write OK -- NO wedge reproduced this run; mechanism "
        "is narrower than 'any hot MPU access' (report, do not extrapolate)");

  /* m9 */
  teardown_clean();
  return 0;
}

/* ---- cell P (v2): POLLED direct-port -- the regression differential -------*/

/* Probe-scale equivalent of the task-#23 differential cell (b): the
 * v1.0.3/SDL-0080 access pattern that stock v1.0.8.1 runs daily on this CPU
 * (DRR poll before every write + UART-entry ACK drained), with the SB16
 * brought hot via the PRODUCTION MIX path. Cell M (blind) is the failing
 * reference (c). P fully clean where M wedged at m3 = blind pattern IS the
 * regression at probe scale; P wedging anywhere = mechanism is NOT (only)
 * blind-vs-polled -- escalate before any fix ships. */
static int cell_p(void)
{
  int it;
  uint8_t status;

  /* p1: COLD POLLED UART entry. Cold status reads are provably safe (the
   * SETUP profiler does a cold 0x331 read every boot). */
  mark("pre", "p1 cold_uart_entry_polled");
  it = mpu_wait_drr();
  outportb(MPU_STAT, 0x3F);
  mark("post", "p1 cold_uart_entry_polled");
  trace("p1: DRR pre-entry iters=%d%s", it, (it < 0) ? " (CAP HIT)" : "");
  mpu_drain_ack("p1 ack_drain");

  /* p2: COLD polled note-on/off E4 (the third distinct pitch). */
  mpu_data_write_polled_marked("p2 cold_note_on b1", 0x90);
  mpu_data_write_polled_marked("p2 cold_note_on b2", NOTE_POLL);
  mpu_data_write_polled_marked("p2 cold_note_on b3", 0x7F);
  trace("p2: cold hold ~%d ms (PIT spin; listen for E4 from the S2)", COLD_HOLD_MS);
  pit_spin_ms(COLD_HOLD_MS);
  mpu_data_write_polled_marked("p2 cold_note_off b1", 0x80);
  mpu_data_write_polled_marked("p2 cold_note_off b2", NOTE_POLL);
  mpu_data_write_polled_marked("p2 cold_note_off b3", 0x00);
  trace("p2: cold POLLED direct-port MIDI complete");

  /* p3: SB16 HOT via the PRODUCTION MIX path. THE step where blind cell M
   * wedged on iter 4 -- if the polled+drained cold init leaves the bus
   * healthy, this open must complete. */
  mark("pre", "p3 sb16_hot_bringup_mix");
  if (sb16_bring_hot_mix() != 0)
  {
    teardown_clean();
    return 1;
  }
  mark("post", "p3 sb16_hot_bringup_mix");

  /* p4: HOT POLLED data writes -- what stock does on every WB note, daily.
   * Each DRR poll is a HOT 0x331 read; each write a HOT 0x330 write. */
  trace("p4: hot POLLED data writes follow (the production v1.0.3/0080 "
        "pattern); on a wedge the last pre-MARK names the stalling unit");
  mpu_data_write_polled_marked("p4 hot_data b1", 0x90);
  mpu_data_write_polled_marked("p4 hot_data b2", NOTE_POLL);
  mpu_data_write_polled_marked("p4 hot_data b3", 0x7F);
  trace("p4: HOT POLLED WRITES COMPLETED");

  /* p5: sustained coexistence + audible witness + drain witness. */
  serviced_hold("p5", HOT_HOLD_MS);

  /* p6: hot polled note-off. */
  mpu_data_write_polled_marked("p6 hot_data_off b1", 0x80);
  mpu_data_write_polled_marked("p6 hot_data_off b2", NOTE_POLL);
  mpu_data_write_polled_marked("p6 hot_data_off b3", 0x00);

  /* p7: hot status read + production shutdown reset (0xFF), polled -- stock
   * executes this write at every quit and survives. */
  mark("pre", "p7 ctrl_status_read");
  status = inportb(MPU_STAT);
  mark("post", "p7 ctrl_status_read");
  trace("p7: hot status read OK (status=0x%02X)", (unsigned)status);
  mark("pre", "p8 ctrl_cmd_write_reset");
  it = mpu_wait_drr();
  outportb(MPU_STAT, 0xFF);
  mark("post", "p8 ctrl_cmd_write_reset");
  trace("p8: hot polled 0xFF reset OK (DRR iters=%d) -- FULL polled pattern "
        "survived hot; blind-vs-polled differential is decisive vs cell M", it);

  /* p9 */
  teardown_clean();
  return 0;
}

/* ---- cell 9 (v3): hot-0x3F undrained-vs-drained differential ---------------*/

/* Fixed-beat stall watch (the p5 drain-witness pattern, repeated). Per beat:
 * MARK pre -> pump ring to full -> read f0 -> SDL_Delay(P9_MEASURE_MS) with NO
 * pump -> read f1 -> MARK post -> verdict trace. Every line is fsync'd, so on
 * a hard freeze the last MARK names the heartbeat (the rev-3 requirement).
 * The beat body issues ZERO port accesses itself (pump + fill reads are
 * SDL-side); the only wedge candidate is the machine state under test.
 * Returns the number of STALL beats; fills out counters via pointers. */
static int stall_watch(const char *ph, int beats,
                       int *out_alive, int *out_partial, int *out_max_streak)
{
  int k, alive = 0, partial = 0, stall = 0, streak = 0, max_streak = 0;
  Uint64 start = SDL_GetTicks();

  trace("%s: stall watch -- %d beats x ~%d ms (no-pump window %d ms x up to "
        "%d, ALIVE = delta >= %d frames, STALL = delta 0 after all windows)",
        ph, beats, P9_BEAT_MS, P9_MEASURE_MS, P9_MEASURE_WINS, P9_ALIVE_DELTA);
  for (k = 1; k <= beats; k++)
  {
    char step[24];
    int f0, f1, delta;
    Uint64 t, beat_end;

    int w;

    snprintf(step, sizeof(step), "%s hb%02d", ph, k);
    mark("pre", step);
    while (SDL_DOSAudioPump()) { }          /* top to full */
    f0 = SDL_DOSAudioRingFillFrames();
    /* NO-pump windows; extend on delta==0 up to P9_MEASURE_WINS so the ISR's
     * ~93 ms chunk-drain granularity cannot fake a STALL (see geometry note). */
    f1 = f0;
    delta = 0;
    for (w = 1; w <= P9_MEASURE_WINS; w++)
    {
      SDL_Delay(P9_MEASURE_MS);             /* NO pump -- let the DMA drain */
      f1 = SDL_DOSAudioRingFillFrames();
      delta = f0 - f1;
      if (delta != 0)
        break;
    }
    if (w > P9_MEASURE_WINS) w = P9_MEASURE_WINS;
    mark("post", step);

    t = SDL_GetTicks() - start;
    if (delta < 0 || delta > 16384)
    {
      /* Plausibility bound (SDLPROBE lesson): fill cannot grow unpumped and
       * cannot drain more than any plausible ring depth in 100 ms. */
      trace("%s t=%lu fill %d->%d delta=%d win=%d WITNESS-DEFECT (implausible delta)",
            step, (unsigned long)t, f0, f1, delta, w);
      partial++;  /* count it somewhere so the sum check holds */
      streak = 0;
      continue;
    }
    trace("%s t=%lu fill %d->%d delta=%d win=%d/%d beat=%s", step,
          (unsigned long)t, f0, f1, delta, w, P9_MEASURE_WINS,
          (delta >= P9_ALIVE_DELTA) ? "ALIVE" : (delta == 0) ? "STALL" : "PARTIAL");
    if (delta >= P9_ALIVE_DELTA) { alive++; streak = 0; }
    else if (delta == 0)
    {
      stall++;
      streak++;
      if (streak > max_streak) max_streak = streak;
    }
    else { partial++; streak = 0; }

    if (kbhit())
    {
      int c = getch();
      if (c == 0 || c == 0xE0) { (void)getch(); c = 27; }
      if (c == 27) { trace("%s: watch aborted by ESC at beat %d", ph, k); break; }
    }
    /* Pad to the beat boundary, serviced (pump + yield). */
    beat_end = (Uint64)k * P9_BEAT_MS;
    while ((SDL_GetTicks() - start) < beat_end)
    {
      while (SDL_DOSAudioPump()) { }
      SDL_Delay(5);
    }
  }
  if (alive + partial + stall != beats)
    trace("%s: NOTE beat sum %d+%d+%d != %d (ESC abort or defect -- "
          "interpret counts, not totals)", ph, alive, partial, stall, beats);
  trace("%s: watch done beats=%d alive=%d partial=%d stall=%d "
        "max_stall_streak=%d beats (~%d ms)", ph, beats, alive, partial,
        stall, max_streak, max_streak * P9_BEAT_MS);
  if (out_alive)      *out_alive = alive;
  if (out_partial)    *out_partial = partial;
  if (out_max_streak) *out_max_streak = max_streak;
  return stall;
}

/* Bounded drain of whatever is pending in the MPU output buffer -- the 0094
 * (WB-INIT-DIFF) shape: DSR(bit 7)-capped poll per byte, and if NOTHING
 * arrives via DSR (the documented Vibra16S "bit-7 lie"), ONE unconditional
 * data-port read so the pending ACK cannot survive a lying status bit (data-
 * port reads are bus-safe per the DOOM/DMX reference + the 0080 win). Logs
 * EVERY byte drained with its path (via=DSR / via=uncond). Hard-bounded:
 * P9_DRAIN_MAX_BYTES bytes max, capped polls -- the drain itself cannot spin
 * forever. (The port reads ARE hot-access wedge candidates on the machine
 * state under test; the caller MARK-brackets the whole unit.) */
static void mpu_drain_pending(const char *step)
{
  int n, iter, cap = P9_DSR_CAP;

  for (n = 0; n < P9_DRAIN_MAX_BYTES; n++)
  {
    for (iter = 0; iter < cap; iter++)
    {
      if ((inportb(MPU_STAT) & 0x80) == 0)
        break;
    }
    if (iter >= cap)
      break;
    {
      uint8_t b = inportb(MPU_DATA);
      trace("%s: drained 0x%02X via=DSR iter=%d%s", step, (unsigned)b, iter,
            (b == 0xFE) ? " (ACK)" : " (NOT the 0xFE ACK -- note it)");
    }
    cap = P9_DSR_CAP_NEXT;  /* later bytes: short cap */
  }
  if (n == 0)
  {
    uint8_t b = inportb(MPU_DATA);
    trace("%s: nothing via DSR within %d iters -> ONE unconditional read: "
          "0x%02X via=uncond%s (bit-7-lie path, 0094 shape)", step,
          P9_DSR_CAP, (unsigned)b, (b == 0xFE) ? " (ACK)" : "");
  }
  else
  {
    trace("%s: drain done, %d byte(s) via DSR", step, n);
  }
}

/* Cell 9 = the rev-3 sec-7.4 differential. Production shape: SB16 comes up
 * FIRST (full MIX open, DMA + IRQ-5 live), the FIRST MPU touch of the boot is
 * the HOT 0x3F -- exactly the WBB/WBC wedge window. NO cold MPU phase.
 *   p9a  baseline watch (8 beats) -- ALIVE beats expected; any STALL here
 *        invalidates the cell (device/witness not healthy pre-MPU).
 *   p9b  hot 0x3F, ACK UNDRAINED (production shape) + 20-beat watch.
 *        MECHANISM PROVEN if a sustained STALL run (or a freeze) appears.
 *   p9c  bounded pending-drain + 8-beat RECOVERY watch (does draining rescue
 *        an already-stalled stream?), then hot 0x3F + IMMEDIATE drain (fix
 *        shape) + 20-beat watch ("p9d"). FIX SHAPE PROVEN if p9d is clean
 *        where p9b stalled.
 * Order B-before-C is deliberate: B may hard-freeze the machine; C is the
 * same boot's bonus if it survives. Every step is hard-bounded; the per-beat
 * fsync'd MARK trail carries the verdict through a freeze. */
static int cell_9(void)
{
  int a_stall, b_stall, r_stall, d_stall;
  int a_alive, b_alive, r_alive, d_alive;
  int a_part,  b_part,  r_part,  d_part;
  int a_strk,  b_strk,  r_strk,  d_strk;
  int it;

  /* p9a: SB16 hot FIRST (production order), then baseline. */
  mark("pre", "p9a sb16_hot_bringup_mix");
  if (sb16_bring_hot_mix() != 0)
  {
    teardown_clean();
    return 1;
  }
  mark("post", "p9a sb16_hot_bringup_mix");

  a_stall = stall_watch("p9a", P9_BASE_BEATS, &a_alive, &a_part, &a_strk);
  if (a_stall > 0)
    trace("INVALID: baseline (pre-MPU) shows %d STALL beat(s) -- device or "
          "witness not healthy; p9b/p9c verdicts UNINTERPRETABLE this run",
          a_stall);

  /* p9b: THE MECHANISM -- hot 0x3F, ACK deliberately undrained. */
  /* NOTE: no "beat=" literal in this banner -- the smoke's STALL guard
   * anchors on the hb-line format and banner text must not collide
   * ([[grep_anchor_confound]]; this exact line tripped it once). */
  trace("p9b: EXPECT if H-B real: sustained STALL beats (fill pinned) or "
        "machine HARD-FREEZE -- power-cycle; the last MARK hb line in this "
        "log IS the verdict. EXPECT if H-B wrong: beats stay ALIVE.");
  mark("pre", "p9b hot_uart_entry_undrained");
  it = mpu_wait_drr();
  outportb(MPU_STAT, 0x3F);
  mark("post", "p9b hot_uart_entry_undrained");
  trace("p9b: hot 0x3F written (DRR pre-write iters=%d%s) -- ACK DELIBERATELY "
        "UNDRAINED, the production WBB/WBC shape", it,
        (it < 0) ? " CAP HIT, wrote anyway" : "");

  b_stall = stall_watch("p9b", P9_WATCH_BEATS, &b_alive, &b_part, &b_strk);

  /* p9c: THE FIX SHAPE -- reached only if p9b left the machine alive. */
  trace("p9c: phase C (fix shape) -- bounded drain of pending MPU bytes, "
        "recovery watch, then second hot 0x3F + IMMEDIATE drain + watch. "
        "EXPECT: no stall (or fast recovery) = fix shape proven.");
  mark("pre", "p9c drain_pending");
  mpu_drain_pending("p9c drain_pending");
  mark("post", "p9c drain_pending");

  r_stall = stall_watch("p9r", P9_RECOV_BEATS, &r_alive, &r_part, &r_strk);

  mark("pre", "p9c hot_uart_entry_drained");
  it = mpu_wait_drr();
  outportb(MPU_STAT, 0x3F);
  mark("post", "p9c hot_uart_entry_drained");
  trace("p9c: second hot 0x3F written (DRR iters=%d%s) -- drain follows "
        "IMMEDIATELY", it, (it < 0) ? " CAP HIT, wrote anyway" : "");
  mark("pre", "p9c entry_ack_drain");
  mpu_drain_pending("p9c entry_ack_drain");
  mark("post", "p9c entry_ack_drain");

  d_stall = stall_watch("p9d", P9_WATCH_BEATS, &d_alive, &d_part, &d_strk);

  /* One-line differential for flush-instr (observe, don't extrapolate --
   * the interpretation table lives in the header + WB-INIT-DIFF note). */
  trace("p9 SUMMARY: baseline stall=%d/%d streak=%d | undrained stall=%d/%d "
        "streak=%d | post-drain-recovery stall=%d/%d streak=%d | drained "
        "stall=%d/%d streak=%d",
        a_stall, P9_BASE_BEATS, a_strk, b_stall, P9_WATCH_BEATS, b_strk,
        r_stall, P9_RECOV_BEATS, r_strk, d_stall, P9_WATCH_BEATS, d_strk);

  teardown_clean();
  return 0;
}

/* ---- cell D: DSP-mediated transport ---------------------------------------*/

static int cell_d(void)
{
  /* d1: SB16 HOT bring-up via the PRODUCTION MIX path (v2 -- the iter-4 raw
   * open completed but never primed at 11025 mono; MIX primes). */
  mark("pre", "d1 sb16_hot_bringup");
  if (sb16_bring_hot_mix() != 0)
  {
    teardown_clean();
    return 1;
  }
  mark("post", "d1 sb16_hot_bringup");
  if (!SDL_DOSAudioSB_IsInitialized())
  {
    trace("INVALID: SDL_DOSAudioSB_IsInitialized()=false after open -- DSP "
          "path precondition missing; cell MEANINGLESS this run");
    teardown_clean();
    return 1;
  }

  /* d2: production UART entry (DSP cmd 0x34, write-status-gated). NOTE: this
   * mirrors production INCLUDING its 0x34-then-0x38 sequence; d4's heartbeat
   * watches whether cmd-0x34 UART mode kills the running DAC stream. */
  mark("pre", "d2 dsp_uart_entry");
  SDL_DOSAudioSB_DSPMidiEnterUART();
  mark("post", "d2 dsp_uart_entry");

  /* d3: note-on G4 via the production per-byte path (0x38 + byte). */
  dsp_midi_write_marked("d3 dsp_note_on b1", 0x90);
  dsp_midi_write_marked("d3 dsp_note_on b2", NOTE_DSP);
  dsp_midi_write_marked("d3 dsp_note_on b3", 0x7F);
  trace("d3: DSP-mediated note-on completed -- listen for G4 from the S2");

  /* d4: hold -- audible witness + DAC-survival observation. */
  serviced_hold("d4", HOT_HOLD_MS);

  /* d5: note-off. */
  dsp_midi_write_marked("d5 dsp_note_off b1", 0x80);
  dsp_midi_write_marked("d5 dsp_note_off b2", NOTE_DSP);
  dsp_midi_write_marked("d5 dsp_note_off b3", 0x00);

  /* d6 */
  teardown_clean();
  return 0;
}

/* ---- main ------------------------------------------------------------------*/

int main(int argc, char **argv)
{
  char cell = 0;

  if (argc >= 2 && argv[1][0] && !argv[1][1])
  {
    if (argv[1][0] == 'm' || argv[1][0] == 'M') cell = 'M';
    if (argv[1][0] == 'd' || argv[1][0] == 'D') cell = 'D';
    if (argv[1][0] == 'p' || argv[1][0] == 'P') cell = 'P';
    if (argv[1][0] == '9')                      cell = '9';
  }

  trace_open();
  if (!cell)
  {
    trace("wbhot %s (sha %s): ERROR -- missing/invalid cell arg", WBHOT_VERSION, WBHOT_SHA12);
    trace("usage: WBHOT P (POLLED direct-port differential) | WBHOT M (blind "
          "failing reference, FREEZE RISK) | WBHOT D (DSP transport) | "
          "WBHOT 9 (hot-0x3F undrained-vs-drained, FREEZE RISK in p9b)");
    if (g_fp) { fclose(g_fp); g_fp = NULL; }
    return 2;
  }

  trace("wbhot %s (sha %s): cell=%c", WBHOT_VERSION, WBHOT_SHA12, cell);
  trace("config: mpu=0x%03X/0x%03X audio=%d Hz mono S16 frames=256 "
        "cold-hold=%d ms hot-hold=%d ms tone=%d Hz",
        MPU_DATA, MPU_STAT, GAME_FREQ, COLD_HOLD_MS, HOT_HOLD_MS, TONE_HZ);
  trace("NOTE: real-HW-only verdict -- DOSBox-X does not reproduce the ISA "
        "IOCHRDY stall; cell %c structural smoke only there", cell);
  if (cell == 'M')
    trace("NOTE: cell M DELIBERATELY risks a hard freeze (own boot; "
          "power-cycle after a wedge; the log keeps the verdict)");
  if (cell == 'P')
    trace("NOTE: cell P = POLLED pattern (v1.0.3/0080, stock-daily-working); "
          "expected CLEAN -- a wedge here refutes blind-vs-polled as the "
          "whole mechanism (own boot regardless)");
  if (cell == '9')
  {
    trace("NOTE: cell 9 = H-B mechanism + fix-shape differential (rev-3 sec "
          "7.4). Phase p9b DELIBERATELY leaves the hot 0x3F ACK undrained "
          "(production shape) and MAY HARD-FREEZE the machine -- power-cycle "
          "after a wedge; the fsync'd MARK trail keeps the verdict.");
    printf("WBHOT 9: phase p9b may HARD-FREEZE this machine (expected if the\n"
           "mechanism is real). Power-cycle after a freeze -- the log keeps\n"
           "the verdict. Total runtime if it survives: ~20 sec.\n");
  }

  {
    int rc = (cell == 'M') ? cell_m() : (cell == 'P') ? cell_p()
           : (cell == '9') ? cell_9() : cell_d();
    if (g_fp) { fclose(g_fp); g_fp = NULL; }
    return rc;
  }
}
