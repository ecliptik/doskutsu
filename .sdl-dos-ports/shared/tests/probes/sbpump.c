/*
 * sbpump.c -- SETUP.EXE audio-test HARD-FREEZE A/B confirmation probe (T20).
 *
 * Standalone DJGPP probe (no engine, no C++). Confirms ON REAL HARDWARE the
 * root cause sdl-engine diagnosed for the SETUP.EXE audio-test wedge (commit
 * 183704e / T14): SETUP opened the SB16 audio device -- which starts the
 * autoinit-DMA and hooks IRQ-5 -- then BLOCKED in libc getch() in the TUI menu.
 * getch() (BIOS INT 16h) neither tops up the SDL audio ring nor yields to the
 * SDL3-DOS cooperative scheduler, so the DMA engine + IRQ-5 stayed live while
 * NOTHING serviced the ring from the main thread. The engine never does this --
 * its main loop tops up the ring every tick via SDL_DOSAudioPump(). DOSBox-X
 * tolerates the unserviced live-IRQ/DMA; a real SB16 wedges the machine hard.
 * The SETUP fix tops up the ring with SDL_DOSAudioPump() throughout a bounded,
 * serviced test (audiotest_sdl.c pump_service()).
 *
 * Note (sdl-engine): the wedge is NOT plain ring underrun -- the SB16 ISR
 * self-fills silence on underrun. It is the live-DMA/IRQ + cooperative-thread/
 * lock regime when the main thread never pumps. Only the real SDL audio path
 * reproduces that, so this probe LINKS the SDL3 stack and drives the real SB16
 * backend; it is NOT a bare-metal SB16 probe (a hand-rolled DMA/IRQ probe would
 * exercise a different implementation and would not validate the actual fix).
 *
 * The A/B is reduced to its single variable -- "is the ring serviced or not" --
 * by reusing the EXACT skeleton of pump_service() in both modes; the ONLY
 * difference is the loop BODY:
 *
 *   mode A (no-service): open device + prime ring with a real tone, then spin
 *     for ~3 s with an EMPTY loop body -- NO SDL_DOSAudioPump, NO SDL_Delay, NO
 *     keyboard poll. The main thread services nothing and never yields, exactly
 *     the getch()-blocked condition. Expected on g2k: the machine WEDGES inside
 *     this window. The last per-line-fsync'd trace line survives the freeze.
 *
 *   mode B (serviced = the fix): identical open + prime, then run pump_service()'s
 *     loop verbatim for ~3 s -- `while (SDL_DOSAudioPump()) {}` to top the ring,
 *     SDL_Delay(5) to pace + yield (lets the ISR drain), kbhit/ESC poll so it
 *     stays interruptible, plus a ~320 ms heartbeat so a mid-service wedge is
 *     located. Expected on g2k: clean playback, clean exit.
 *
 * CELL MATRIX (disentangle servicing-vs-config -- the SETUP bug had BOTH a
 * no-service main loop AND a 44100/stereo/1024-frame device the game never
 * runs). Select the cell with argv[1]:
 *
 *   A1  mode A, 11025 Hz mono S16, frames=256   (game config, no service)
 *   B1  mode B, 11025 Hz mono S16, frames=256   (game config, serviced = fix)
 *   A2  mode A, 44100 Hz stereo S16, frames=default (original broken config, no service)
 *
 * Read (real HW, g2k):
 *   - A1 wedges + B1 clean         -> servicing IS the cause; the pump fix works.
 *   - A1 clean  + A2 wedges        -> the 44100/1024 CONFIG was the trigger, not
 *                                     servicing. Report that.
 *   - A1 clean  + B1 clean + A2 clean -> mechanism is NEITHER as modelled; the
 *                                     SETUP freeze is elsewhere. Report it; do
 *                                     NOT assert the fix is validated.
 *   ("A" / "B" aliases default to config 1, i.e. A1 / B1.)
 *
 * DOSBox-X note (REAL-HW-ONLY): per dosbox_not_proxy, DOSBox-X does NOT
 * reproduce the real-SB16 wedge -- mode A runs clean under DOSBox-X and prints
 * NO-WEDGE there. The DOSBox-X smoke proves ONLY structural correctness (binary
 * runs, device opens, ring primes >0, log parses, B exits clean). The wedge
 * confirmation is g2k-only. Do NOT read a clean DOSBox A-cell as "no repro".
 *
 * Faithfulness pins (sdl-engine T20 spec + Q2 confirmation):
 *   - Device open via raw SDL3 SDL_OpenAudioDeviceStream. sdl-engine confirmed
 *     this drives the SAME DOSSOUNDBLASTER_OpenDevice DMA-autoinit + IRQ-5
 *     program as the shipped SDL3_mixer path -- the DMA/IRQ state is
 *     parameterized by the negotiated device->spec, not by the producer -- and
 *     SDL_DOSAudioPump (producer-agnostic; runs SDL_PlaybackAudioThreadIterate)
 *     tops the ring from the bound SDL_AudioStream the same way it tops it from
 *     a mixer track. Single -lSDL3 link, matching the existing probe fleet.
 *   - Spec pinned per cell so the negotiated chunk_size / DMA half-buffer
 *     geometry matches the shipped open (the wedge timing is ring-depth-
 *     sensitive). frames=256 hint only for the game-config cells; A2 leaves the
 *     hint unset to reproduce the broken-config default.
 *   - mode-A TIME BOUND uses a read-only PIT channel-0 LATCH-READ (port 0x43
 *     control byte 0x00 + two reads of 0x40), the wave-19/cffsync pattern, NOT
 *     SDL_GetTicks() and NOT the BIOS tick at 0040:006C. Three reasons: (1)
 *     SDL_GetTicks -> uclock() reprograms PIT channel 0 on first call
 *     (vendor/SDL/src/timer/dos/SDL_systimer.c:30-38) -- a PIT control-path side
 *     effect inside the window the cell must prove is side-effect-free. (2) The
 *     latch-read reads the live HW down-counter directly, so it advances every
 *     PIT clock regardless of whether the INT8 ISR is serviced -- a tight
 *     no-yield loop still makes progress. (3) The BIOS-tick (0040:006C) read is
 *     a pure memory read with NO I/O trap; under DOSBox-X's core a tight loop of
 *     pure memory reads never hits a sync point, so the emulated timer never
 *     advances and the spin hangs (observed). The latch-read's port I/O forces a
 *     sync each iteration, so the counter advances under DOSBox too -- the spin
 *     terminates in both environments. The latch-read is still UNSERVICED: it
 *     pokes no audio port, runs no pump, never yields, never reprograms the PIT
 *     (latch control byte writes no mode/divisor). The mode-B bound keeps
 *     SDL_GetTicks (its SDL_Delay(5) already yields/services + warms uclock;
 *     immune to wrap-sampling). The clock is not the variable under test --
 *     servicing is -- so the two bounds differing is fine; only mode A REQUIRES
 *     the unserviced HW-counter clock.
 *
 * Output: LOGS\<TAG>PROBE.LOG honoring DOS_PORT_LOG_TAG (cap 3 chars ->
 * "<TAG>PROBE.LOG", 8.3-clean; unset -> PROBEDBG.LOG), same idiom + path as the
 * SETUP T14 trace so it rides realhw's existing LOGS\<TAG>*.LOG logback. Set a
 * DISTINCT tag per cell across the (separate, power-cycled) runs -- e.g.
 * DOS_PORT_LOG_TAG=PA1 / PB1 / PA2 -> PA1PROBE.LOG / PB1PROBE.LOG / PA2PROBE.LOG
 * -- so the three traces persist (the wedge cell never returns to clean up). The
 * cell id + config + build sha are also written INTO the log header for
 * provenance even if a tag is reused. Per-line fflush + fsync so the last line
 * survives a hard freeze.
 *
 * Usage:  SBPUMP A1   (game config, unserviced -- expect wedge on real SB16)
 *         SBPUMP B1   (game config, serviced   -- expect clean)
 *         SBPUMP A2   (broken config, unserviced)
 *
 * Build:  make probes-sbpump   (SDL3-linked, PROBES_SDL_* recipe, minstack 2048k)
 *
 * DOS/SDL constraints honored: no video, no engine, no C++, no std::thread,
 * static link only, -march=i486 (no SIMD). ASCII-only. License: MIT.
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_dosaudio_pump.h>  /* SDL_DOSAudioPump, SDL_DOSAudioRingFillFrames */

#include <conio.h>      /* kbhit / getch -- B-mode ESC poll */
#include <pc.h>         /* inportb / outportb -- PIT channel-0 latch-read (mode A bound) */
#include <sys/stat.h>   /* mkdir() for LOGS\ */
#include <math.h>       /* sinf -- tone gen (init only, not in the timed loop) */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>     /* fsync -- DOS per-line durability (nx 0036 / SDL/0024) */

#ifndef SBPUMP_SHA12
#define SBPUMP_SHA12 "nostamp"
#endif
#define SBPUMP_VERSION   "v1"

/* A/B window length. Ring depth is ~372 ms in production; 3 s is several ring
 * drains of unserviced runtime -- ample for the real-SB16 wedge to assert. */
#define WINDOW_MS        3000
#define SERVICE_DELAY_MS 5      /* B-mode pacing yield, identical to pump_service() */
#define HEARTBEAT_MS     320    /* B-mode heartbeat cadence (locate a mid-service wedge) */

/* Primed tone -- clearly above an RMS-silence floor; not in the timed loop. */
#define TONE_HZ          440
#define TONE_AMP         0.30f

/* ---- cell table -----------------------------------------------------------*/

typedef struct {
  const char *name;   /* cell id (argv match) */
  char        mode;   /* 'A' = unserviced, 'B' = serviced */
  int         freq;
  int         channels;
  int         set_frames_hint; /* 1 -> pin AUDIO_DEVICE_SAMPLE_FRAMES=256 */
} cell_t;

static const cell_t CELLS[] = {
  { "A1", 'A', 11025, 1, 1 },  /* game config, no service */
  { "B1", 'B', 11025, 1, 1 },  /* game config, serviced = the fix */
  { "A2", 'A', 44100, 2, 0 },  /* original broken config, no service */
};
#define CELL_COUNT ((int)(sizeof(CELLS) / sizeof(CELLS[0])))

/* ---- mode-A time bound: read-only PIT channel-0 latch-read -----------------*/

#define PIT_HZ 1193182UL  /* PIT input clock */

/* Latch + read PIT counter 0 (a 16-bit down-counter at PIT_HZ). Control byte
 * 0x00 = counter 0, latch command (writes NO mode/divisor -> no reprogram). The
 * port I/O forces an emulator sync each call (so the counter advances under
 * DOSBox-X) and reads the live HW counter (advances regardless of ISR service).
 * No yield, no ring service. */
static uint16_t pit_count(void)
{
  uint8_t lo, hi;
  outportb(0x43, 0x00);
  lo = inportb(0x40);
  hi = inportb(0x40);
  return (uint16_t)(lo | (hi << 8));
}

/* PIT ticks for a given ms span (PIT_HZ clocks). */
static unsigned long ms_to_pit(unsigned long ms)
{
  return (ms * PIT_HZ) / 1000UL;
}

/* Accumulated PIT ticks -> ms (survival reporting). */
static unsigned long pit_to_ms(unsigned long ticks)
{
  return (ticks * 1000UL) / PIT_HZ;
}

/* ---- trace (per-line fflush + fsync; last line survives a hard freeze) -----*/

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

/* Open LOGS\<TAG>PROBE.LOG honoring DOS_PORT_LOG_TAG (3-char cap), mirroring
 * the SETUP T14 trace path so realhw's LOGS\<TAG>*.LOG logback picks it up. */
static void trace_open(void)
{
  char base[16];
  char path[40];
  const char *tag;

  mkdir("LOGS", 0777); /* defensive; harmless if it exists */

  tag = getenv("DOS_PORT_LOG_TAG");
  if (tag && tag[0])
  {
    char t3[4];
    int i;
    for (i = 0; i < 3 && tag[i]; ++i) t3[i] = tag[i];
    t3[i] = '\0';
    snprintf(base, sizeof(base), "%sPROBE.LOG", t3);  /* e.g. PA1PROBE.LOG */
  }
  else
  {
    strcpy(base, "PROBEDBG.LOG");
  }
  snprintf(path, sizeof(path), "LOGS/%s", base);
  g_fp = fopen(path, "wb");
}

/* ---- main -----------------------------------------------------------------*/

int main(int argc, char **argv)
{
  const cell_t *cell = NULL;
  SDL_AudioSpec spec;
  SDL_AudioStream *stream = NULL;
  int16_t *tone = NULL;
  int tone_frames, tone_samples, i;
  int prime_fill;

  /* --- parse cell --- */
  if (argc >= 2 && argv[1][0])
  {
    for (i = 0; i < CELL_COUNT; ++i)
      if (strcasecmp(argv[1], CELLS[i].name) == 0) { cell = &CELLS[i]; break; }
    /* "A"/"B" aliases -> config-1 cells A1/B1 */
    if (!cell && (argv[1][0] == 'a' || argv[1][0] == 'A') && !argv[1][1]) cell = &CELLS[0];
    if (!cell && (argv[1][0] == 'b' || argv[1][0] == 'B') && !argv[1][1]) cell = &CELLS[1];
  }

  trace_open();
  if (!cell)
  {
    trace("sbpump %s (sha %s): ERROR -- missing/invalid cell arg", SBPUMP_VERSION, SBPUMP_SHA12);
    trace("usage: SBPUMP A1 | SBPUMP B1 | SBPUMP A2   (A=A1, B=B1)");
    if (g_fp) { fclose(g_fp); g_fp = NULL; }
    return 2;
  }

  trace("sbpump %s (sha %s): cell=%s mode=%c", SBPUMP_VERSION, SBPUMP_SHA12, cell->name, cell->mode);
  trace("config: %d Hz %s S16 frames=%s window=%d ms tone=%d Hz",
        cell->freq, (cell->channels == 1) ? "mono" : "stereo",
        cell->set_frames_hint ? "256" : "default", WINDOW_MS, TONE_HZ);
  trace("NOTE: real-HW only -- mode A wedges on real SB16, runs clean under DOSBox-X");

  /* --- SDL audio bring-up (frames hint BEFORE open, per cell) --- */
  if (cell->set_frames_hint)
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256", SDL_HINT_OVERRIDE);
  if (!SDL_Init(SDL_INIT_AUDIO))
  {
    trace("FATAL: SDL_Init(AUDIO) failed: %s", SDL_GetError());
    if (g_fp) { fclose(g_fp); g_fp = NULL; }
    return 1;
  }
  trace("init: SDL_Init(AUDIO) ok");

  SDL_zero(spec);
  spec.format   = SDL_AUDIO_S16;
  spec.channels = cell->channels;
  spec.freq     = cell->freq;

  /* Raw-stream open -> runs DOSSOUNDBLASTER_OpenDevice (DMA autoinit + IRQ-5
   * hook) identically to the mixer path. After this returns, the device is LIVE. */
  trace("open: SDL_OpenAudioDeviceStream (starts autoinit-DMA + hooks IRQ-5)");
  stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
  if (!stream)
  {
    trace("FATAL: SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
    SDL_Quit();
    if (g_fp) { fclose(g_fp); g_fp = NULL; }
    return 1;
  }
  trace("open: device live, stream bound");

  /* --- generate + queue the prime tone (sized to window + margin) --- */
  tone_frames  = cell->freq * ((WINDOW_MS / 1000) + 2);   /* >= window, continuous in B */
  tone_samples = tone_frames * cell->channels;            /* interleaved */
  tone = (int16_t *)malloc((size_t)tone_samples * sizeof(int16_t));
  if (!tone)
  {
    trace("FATAL: tone malloc failed (%d samples)", tone_samples);
    SDL_DestroyAudioStream(stream);
    SDL_Quit();
    if (g_fp) { fclose(g_fp); g_fp = NULL; }
    return 1;
  }
  for (i = 0; i < tone_frames; ++i)
  {
    float s = sinf((float)(2.0 * M_PI * TONE_HZ) * (float)i / (float)cell->freq);
    int16_t v = (int16_t)(s * TONE_AMP * 32767.0f);
    int ch;
    for (ch = 0; ch < cell->channels; ++ch)
      tone[i * cell->channels + ch] = v;
  }
  if (!SDL_PutAudioStreamData(stream, tone, tone_samples * (int)sizeof(int16_t)))
    trace("warn: SDL_PutAudioStreamData failed: %s", SDL_GetError());
  if (!SDL_ResumeAudioStreamDevice(stream))
    trace("warn: SDL_ResumeAudioStreamDevice failed: %s", SDL_GetError());
  trace("tone: queued %d frames (%d samples), stream resumed", tone_frames, tone_samples);

  /* --- prime the ring to full (pump's own top-up loop) --- */
  while (SDL_DOSAudioPump()) { /* top the SB16 ring */ }
  prime_fill = SDL_DOSAudioRingFillFrames();
  trace("prime: ring fill = %d frames", prime_fill);
  /* Sentinel / plausibility check: if the ring did not fill, the device is not
   * actually streaming -> the cell is INVALID (no live DMA/IRQ to wedge, no
   * audio to service). Make that loud so a NO-WEDGE mode-A is not misread as
   * "mechanism absent" when really the device never came up. */
  if (prime_fill <= 0)
    trace("INVALID: ring did NOT prime (fill<=0) -- device not streaming; "
          "cell result MEANINGLESS this run (re-check BLASTER / device open)");
  else
    trace("prime: OK -- DMA+IRQ-5 live with real audio in the ring");

  /* --- the A/B window --- */
  if (cell->mode == 'A')
  {
    /* UNSERVICED: the loop body is only the PIT latch-read time bound. No pump,
     * no SDL_Delay, no kbhit -- the main thread never tops the ring and never
     * yields, the getch()-blocked condition. On a real SB16 the machine is
     * expected to WEDGE inside this loop; the line above is the last survivable
     * trace. Elapsed is accumulated from the PIT down-counter (wrap at the
     * standard 65536 reload); exact window length is not load-bearing -- the
     * unserviced property + termination are. */
    unsigned long elapsed = 0;
    unsigned long target  = ms_to_pit((unsigned long)WINDOW_MS);
    uint16_t last = pit_count();
    trace("A: entering UNSERVICED window ~%d ms (NO pump, NO yield) -- "
          "expect WEDGE on real SB16 HERE", WINDOW_MS);
    for (;;)
    {
      uint16_t c = pit_count();
      elapsed += (c <= last) ? (unsigned long)(last - c)
                             : (unsigned long)(last + (65536UL - c)); /* wrap */
      last = c;
      if (elapsed >= target)
        break;
    }
    /* Reaching here on real HW => the unserviced ring did NOT wedge. */
    trace("A: SURVIVED unserviced window (~%lu ms) -- NO WEDGE (post-window ring fill = %d)",
          pit_to_ms(elapsed), SDL_DOSAudioRingFillFrames());
    trace("A: NO-WEDGE => this cell did NOT reproduce the freeze "
          "(expected under DOSBox-X; on g2k it points the mechanism elsewhere)");
  }
  else /* mode == 'B' */
  {
    int esc = 0;
    Uint64 start   = SDL_GetTicks();
    Uint64 next_hb = (Uint64)HEARTBEAT_MS;
    Uint64 now;
    trace("B: entering SERVICED window ~%d ms "
          "(while(pump){} + SDL_Delay(%d) + ESC poll + %d ms heartbeat)",
          WINDOW_MS, SERVICE_DELAY_MS, HEARTBEAT_MS);
    for (;;)
    {
      while (SDL_DOSAudioPump()) { /* top the ring to full */ }

      if (kbhit())
      {
        int k = getch();
        if (k == 0 || k == 0xE0) { (void)getch(); k = 27; } /* extended -> stop */
        if (k == 27) { esc = 1; break; }
      }

      now = SDL_GetTicks() - start;
      if (now >= next_hb)
      {
        trace("B: heartbeat t=%lu ms ring=%d frames",
              (unsigned long)now, SDL_DOSAudioRingFillFrames());
        next_hb += (Uint64)HEARTBEAT_MS;
      }
      if (now >= (Uint64)WINDOW_MS)
        break;

      SDL_Delay(SERVICE_DELAY_MS); /* pace + cooperative-scheduler yield (ISR drains) */
    }
    trace("B: serviced window complete%s -- CLEAN (ring fill = %d frames)",
          esc ? " (ESC)" : "", SDL_DOSAudioRingFillFrames());
  }

  /* --- teardown --- */
  trace("teardown: pausing + closing device");
  SDL_PauseAudioStreamDevice(stream);
  SDL_DestroyAudioStream(stream);
  free(tone);
  SDL_Quit();
  trace("done: closed cleanly, exit 0");

  if (g_fp) { fclose(g_fp); g_fp = NULL; }
  return 0;
}
