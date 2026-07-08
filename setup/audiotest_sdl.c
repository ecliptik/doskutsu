/*
 * audiotest_sdl.c -- live, per-backend audio-test backend for SETUP.EXE.
 *
 * Compiled instead of audiotest_stub.c when AUDIOTEST_LINKED=1 (build via
 * `make setup AUDIOTEST=1`). Links the already-built static SDL3 + SDL3_mixer
 * stack and drives the REAL chip path for the configured music backend, so the
 * user can confirm each backend actually produces sound on their hardware:
 *
 *   - opl3     -> real YMF262 FM: a short note arpeggio via the SDL DOS core's
 *                 OPL3 register helpers (port 0x388).
 *   - wb       -> real MPU-401 / WaveBlaster MIDI: a short note arpeggio out
 *                 the MIDI port (BLASTER P-field, default 0x330).
 *   - organya  -> SB16 PCM: a generated tone (Organya's own software synth is
 *                 the game's job; this validates the PCM output path).
 *   - auto/PCM -> SB16 PCM generated tone.
 *
 * SFX is ALWAYS the SB16 PCM path (Pixtone SFX always plays on the SB16 DAC
 * regardless of the music backend), so the SFX test is a generated PCM blip in
 * every mode.
 *
 * Method (T9): fully IN-PROCESS. The OPL3 + MPU-401 primitives are exported by
 * libSDL3.a (SDL DOS core, patch SDL/0037+); we forward-declare them exactly
 * as the engine's MidiBackend{Opl3,WB}.cpp do (the internal SDL header
 * SDL_dos_audio_synth.h is not installed to the sysroot). No engine link, no
 * doskutsu.exe spawn, no SDL3_mixer MIDI (our mixer is built MIDI=OFF, and
 * ADLMIDI would be a software OPL that wouldn't test the real chip anyway).
 *
 * The SB16 device is opened via SDL3_mixer in every mode: it programs the
 * CT1745 analog mixer balance (master/voice/FM levels) from the configured
 * SB16_* hints -- the FM level matters for OPL3 audibility -- and provides the
 * PCM path for SFX. AUDIO_TIER2 sets the device rate/channels.
 *
 * DOSBox note: DOSBox-X emulates OPL3 (FM arpeggio is RMS-verifiable headless)
 * but does NOT emulate MPU-401, so the WB arpeggio is audible only on real
 * hardware with a wavetable attached. The MPU init is BLIND (no status-port
 * read), so it never hangs -- the historic real-S2 lockup trap is gone.
 *
 * T14 real-HW HARD-FREEZE fix (g2k, operator 2026-06-07): the original
 * design opened the SB16 device in audiotest_init() -- which starts the
 * autoinit-DMA + hooks IRQ-5 -- then returned to the TUI menu and BLOCKED in
 * libc getch(). getch() (BIOS INT 16h) neither pumps the SDL audio ring nor
 * yields to the SDL3-DOS cooperative scheduler, so the DMA engine + IRQ were
 * left live while NOTHING serviced the ring from the main thread -- a device-
 * driving model the game NEVER uses (the engine main loop tops up the ring
 * every tick via SDL_DOSAudioPump(), nxengine-evo/0162+0207). DOSBox-X
 * tolerates the unserviced live-IRQ/DMA; real SB16 hardware wedges hard
 * (dosbox_not_proxy class -- a clean DOSBox run proves nothing here).
 *
 * The fix has two parts:
 *   1. Mirror the engine's device config: AUDIO_TIER2 is read with the
 *      engine's default-ON killswitch semantics (strict-"0" disables, per
 *      SoundManager.cpp), so the test opens 11025 mono / 256-frame -- the
 *      same SB16 program the game runs -- not the test's old opt-in 44100
 *      stereo / 1024-frame (a config never validated on g2k).
 *   2. Open the device ONLY for the duration of a bounded, serviced test and
 *      close it again before returning to the (blocking-getch) menu, and
 *      service the ring throughout the test with SDL_DOSAudioPump() exactly
 *      like the engine main loop. The device is never live while the main
 *      thread is blocked. Each test is ESC-interruptible and always torn down.
 *
 * NOTE: this is a real-HW-only fix -- it CANNOT be validated under DOSBox-X.
 * Confirm on g2k (and/or via probe-engineer's isolated SB16 pump probe).
 *
 * DOS/SDL constraints honored: no video; no std::thread / SDL_CreateThread;
 * static link only; -march=i486, no SIMD. ASCII-only.
 */

#include "audiotest.h"

#if AUDIOTEST_LINKED

#include "cs_pixtone.h"            /* T28: standalone Pixtone synth -- real Polar Star SFX from the user's data/pxt/fx20.pxt */
#include "cs_smf.h"                /* T28: standalone SMF parser/scheduler -- real Title theme from data/midi/curly.mid */
#include "cs_opl3midi.h"           /* T28: OPL3 MIDI voice backend (18-voice allocator + GM bank) for the title theme */
#include "cs_opl2midi.h"           /* T80: OPL2 (AdLib) MIDI voice backend (9-voice) -- the music-only AdLib test path */
#include "cs_orgcache.h"           /* T36: reader for the engine's pre-rendered Organya PCM cache (organya-mode title snippet) */
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3/SDL_dosgus.h>        /* A2: native Gravis GF1 voice primitives (SDL/0112) -- the no-SB GUS test path */
#include <SDL3/SDL_dosaudio_pump.h> /* SDL/0067-v2: ring-service pump -- T14 drives the SB16 like the engine main loop */
#include <SDL3/SDL_dosmidi_tick.h>  /* T56 route A: SDL/0072 Lever-2b IRQ-5 MIDI tick (Register/IsrTickActive) */
#include <dpmi.h>                   /* T56 route A: _go32_dpmi_lock_data for the g_title_smf pointer the ISR thunk reads */
#include <conio.h>                  /* DJGPP kbhit/getch: non-blocking ESC poll during the serviced test (T14) */
#include <sys/farptr.h>             /* T42: _farpeekl for the BIOS tick reference clock */
#include <go32.h>                   /* T42: _dos_ds for the BIOS data-area peek */
#include <pc.h>                     /* T56: inportb/outportb for the CMOS/RTC read */
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>                  /* T14 trace: FILE*, fopen, vfprintf, fflush, fileno */
#include <stdarg.h>                 /* T14 trace: va_list */
#include <unistd.h>                 /* T14 trace: fsync -- DOS per-line durability (nx 0036 / SDL/0024 precedent) */
#include <sys/stat.h>              /* T14 trace: mkdir() for the LOGS/ subdir (SDL/0062 precedent) */

/*
 * Engine -> SDL extern: the SDL3-DOS SB16 backend (SDL/0071 Lever-3 IRQ-mix,
 * SDL_dosaudio_sb.c) references this counter, which the game DEFINES in
 * Pixtone.cpp (nxengine patch 0100). SETUP does not link the engine, so we
 * provide a stub definition (C linkage, matches the backend's volatile
 * uint32_t). SETUP plays no Pixtone SFX, so it stays 0.
 */
volatile uint32_t g_pixtone_active_count = 0;

/*
 * Real-chip primitives exported by libSDL3.a (SDL DOS core, SDL/0037+). The
 * declaring header SDL_dos_audio_synth.h is internal (not installed to the
 * sysroot), so -- like the engine's MidiBackend{Opl3,WB}.cpp -- we forward-
 * declare the prototypes we use. The symbols are global in the archive
 * (verified via nm); the linker resolves them from the SDL3 lib already linked.
 */
extern bool     SDL_DOSOpl3Detect(void);
extern void     SDL_DOSOpl3InitChip(void);
extern void     SDL_DOSOpl3VoiceWritePatch(int voice_id, const uint8_t patch[12]);
extern void     SDL_DOSOpl3VoiceNoteOn(int voice_id, uint16_t freq, uint8_t block);
extern void     SDL_DOSOpl3VoiceNoteOff(int voice_id);
extern void     SDL_DOSOpl3Shutdown(void);
extern bool     SDL_DOSMpu401Init(uint16_t port_base);
extern void     SDL_DOSMpu401WriteByte(uint8_t byte);
extern void     SDL_DOSMpu401Shutdown(void);
extern uint16_t SDL_DOSMpu401GetBLASTERPort(void);
/* T48 iter-8: SDL/0097 cold-init DRR-poll cap-hit witness (cap-during-reset). */
extern volatile uint32_t doskutsu_mpu_drr_cap_hits;

/* T48 iter-8: GS/GM/XG reset SysEx, VERBATIM mirror of the engine
 * (vendor/nxengine-evo/src/sound/MidiBackendWB.cpp patch 0219 WB_SYSEX_*). SETUP
 * is the deliverable, so WBS3 must exercise the COMPLETE fix (cold-init + poll +
 * voice-reset) in parity with the game's WPG cell -- otherwise SETUP's WB test
 * plays audibly but on the wrong (non-GM) Dream power-up voice map. Gated by the
 * SAME hint the engine uses (SDL_HINT_DOSKUTSU_AUDIO_WB_VOICE_RESET = gm|gs|xg),
 * default OFF. Emitted FIRST (before the program-change) via SDL_DOSMpu401WriteByte
 * so it rides the cold-init pacing (poll/delay). */
static const uint8_t WB_SYSEX_GM[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
static const uint8_t WB_SYSEX_GS[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40,
                                       0x00, 0x7F, 0x00, 0x41, 0xF7 };
static const uint8_t WB_SYSEX_XG[] = { 0xF0, 0x43, 0x10, 0x4C, 0x00, 0x00,
                                       0x7E, 0x00, 0xF7 };

/* ---- tunables -------------------------------------------------------------*/

/* T28: the REAL Polar Star shot. SND_POLAR_STAR_L1_2 = 32 (SoundManager.h);
 * Pixtone names its files in hex -> fx20.pxt. The engine resolves SFX from
 * <basepath>/data/pxt/; SETUP runs in the game dir, so the relative path
 * matches (DOS FAT is case-insensitive -> DATA\PXT\FX20.PXT on the CF card).
 * Synthesized once from the user's own data at runtime (cs_pixtone_render);
 * NOT bundled or committed. Absent/unreadable -> fall back to the tone blip. */
#define SFX_POLAR_STAR_PXT  "data/pxt/fx20.pxt"
#define SFX_PXT_RATE        22050   /* .pxt native storage rate (S8 mono) */

/* SFX PCM blip (always SB16): one-shot, clearly above an RMS-silence floor.
 * Used as the graceful fallback when the real .pxt is unavailable. */
#define SFX_HZ        880
#define SFX_AMP       0.50f
#define SFX_MS        160
/* Generated PCM music tone (organya / auto / PCM fallback): loops until stop. */
#define MUSIC_HZ      440
#define MUSIC_AMP     0.32f
#define MUSIC_MS      900

/* T28: the REAL Title theme. The default title screen plays music(24), which
 * the engine resolves to data/midi/<name>.mid via _music_names[24] = "curly"
 * (the WiiWare arrangement set, docs/ASSETS.md step 4.5). Played through the
 * configured synth backend (opl3 / wb); organya/pcm modes keep the arpeggio
 * (the org software synth is out of SETUP's scope). Read at runtime from the
 * user's data; NOT bundled. Absent/unreadable -> fall back to the arpeggio. */
#define TITLE_MID     "data/midi/curly.mid"
#define TITLE_MS      15000   /* bounded title-theme window (loops; until-key, capped) */

/* T36: in Organya mode, the music test plays a short snippet of the real Title
 * theme from the engine's pre-rendered PCM cache (CACHE/<rate>_<ch>/CURLY.PCM,
 * written by the game when Organya pre-render is enabled). Read at runtime from
 * the user's disk; nothing committed/shipped. Absent -> fall back to the tone.
 * The song is "curly" (music(24)); the cache basename is the uppercase 8.3 org
 * basename = "CURLY". */
#define ORG_TITLE_SONG  "CURLY"
#define ORG_SNIPPET_MS  5000   /* first ~5 s of the pre-rendered title theme */

/* Note arpeggio for the synth backends: C4 E4 G4 C5 (MIDI 60/64/67/72). */
static const int  ARP_NOTES[] = { 60, 64, 67, 72 };
#define ARP_COUNT     ((int)(sizeof(ARP_NOTES) / sizeof(ARP_NOTES[0])))
#define ARP_ON_MS     280   /* note held */
#define ARP_GAP_MS    40    /* gap between notes */
#define WB_CHANNEL    0
#define WB_PROGRAM    19    /* GM 19 = Church Organ: present on any GM wavetable */
#define WB_VELOCITY   100

/* OPL3 voice + patch. PATCH_ORGAN (12 bytes) lifted verbatim from the engine's
 * inline OPL3 bank (MidiBackendOpl3.cpp, post patch-0171 release-rate fix):
 * sustained, sine-like -> steady RMS energy. Layout per the helper contract:
 * op0[0..4], channel FB/CONN at [5], op1[6..10], [11] engine-side fine-tune. */
#define OPL3_VOICE    0
static const uint8_t PATCH_ORGAN[12] = {
  0x01, 0x18, 0xF0, 0x08, 0x00,  /* op0 */
  0xFE,                          /* FB/CONN: stereo + FB=7 + CON=0 */
  0x01, 0x00, 0xF0, 0x08, 0x00,  /* op1 */
  0x00,                          /* fine-tune (unused by helper) */
};

/* MIDI-note -> OPL3 (fnum, block), mirroring the engine's table + math. */
static const uint16_t OPL3_BASE_FNUM[12] =
  { 363, 385, 408, 432, 458, 485, 514, 544, 577, 611, 647, 686 };

static void opl3_note_to_fnum_block(int note, uint16_t *out_fnum, uint8_t *out_block)
{
  int rel, octave_delta, semitone, block;   /* MIDI 60 = C4, reference block 4 */
  if (note < 0)   note = 0;
  if (note > 127) note = 127;
  rel = note - 60;
  if (rel >= 0) { octave_delta = rel / 12;            semitone = rel % 12; }
  else          { octave_delta = -((-rel + 11) / 12); semitone = ((rel % 12) + 12) % 12; }
  block = 4 + octave_delta;
  if (block < 0) block = 0;
  if (block > 7) block = 7;
  *out_fnum  = OPL3_BASE_FNUM[semitone];
  *out_block = (uint8_t)block;
}

/* ---- state ----------------------------------------------------------------*/

enum { MUS_PCM = 0, MUS_OPL3, MUS_WB, MUS_OPL2, MUS_GUS };

/* A2: native Gravis GF1 test path. Like AdLib (MUS_OPL2), a GUS-only box has NO
 * Sound Blaster -- SETUP does SDL_Init(0) (no audio device, no mixer) and drives
 * the GF1 wavetable directly through the SDL_DOSGus* primitives (SDL/0112). The
 * music test plays a C-E-G-C arpeggio: one looping square-wave sample uploaded
 * to card DRAM, replayed at a per-note sample rate so the GF1 pitches it (the
 * wavetable trick), which end-to-end proves DRAM upload + voice + DAC output --
 * the exact thing an operator needs to confirm their card makes sound in the
 * game. The SFX test plays the real Polar Star (Pixtone) one-shot on a GF1
 * voice (S8 render -> U8 for the GF1's unsigned 8-bit DAC). Full GM .pat MIDI
 * synth (the game's MidiBackendGus) is out of scope for a hardware test button.
 * A dedicated later task can add the real title theme on GUS. */
#define GUS_VOL         200           /* 0..255 linear voice volume (headroom)   */
/* CRITICAL (g2k GUS music-test silence fix): pitch the arpeggio by the uploaded
 * sample PERIOD at a FIXED native playback rate, NOT by varying the GF1 playback
 * rate. Every GF1 path proven audible on the g2k PicoGUS drives SetVoiceFreq at
 * the native 22050 Hz -- the SDL/0112 driver's own gus_emit_test_tone AND this
 * file's GUS SFX test (gus_play_pcm8 @ SFX_PXT_RATE=22050). The earlier arpeggio
 * drove non-native pitch-computed rates (13-26 kHz); those produced NO output on
 * the PicoGUS (consistent with the documented #39-class firmware quirks, where
 * arithmetically-valid register values still yield silence). Playing every note
 * at 22050 and setting pitch via the period removes that sole variable. */
#define GUS_NATIVE_HZ   22050         /* the ONLY playback rate validated on g2k  */
/* Per-note square-wave period in samples: output pitch = 22050/period ~= note.
 * Parallel to ARP_NOTES {60,64,67,72} = C4/E4/G4/C5 (262/330/392/523 Hz):
 * 22050/84=262, /67=329, /56=394, /42=525. */
static const int GUS_ARP_PERIOD[ARP_COUNT] = { 84, 67, 56, 42 };
/* One-shot sample length: >= ARP_ON_MS at 22050 (280 ms -> 6174 samples) so a
 * note sustains its whole hold as a one-shot; 6720 = ~305 ms. */
#define GUS_NOTE_LEN    6720
static int g_gus_ok = 0;              /* GF1 detected + brought up (device_open) */

static MIX_Mixer *g_mixer    = NULL;
static MIX_Audio *g_sfx_aud  = NULL;   /* sine blip (always created; SFX fallback) */
static MIX_Audio *g_mus_aud  = NULL;   /* PCM music tone */
static MIX_Track *g_sfx_trk  = NULL;
static MIX_Track *g_mus_trk  = NULL;

/* T28 real SFX (Polar Star). The expensive part -- parsing + synthesizing
 * fx20.pxt -- is device-independent, so it is rendered ONCE into an 8-bit
 * mono 22050 PCM buffer (g_sfx_pcm) and cached for the SETUP session. The
 * per-device MIX_Audio (g_sfx_real) is cheap and rebuilt each device_open
 * (it belongs to the mixer, which is torn down in device_close). When the
 * .pxt is missing/unreadable the render is attempted once, fails gracefully,
 * and the SFX test uses the sine blip. */
static int          g_real_sfx     = 1;      /* killswitch (default ON; "0" disables) */
static signed char *g_sfx_pcm      = NULL;   /* cached Pixtone render (S8 mono 22050) */
static uint32_t     g_sfx_pcm_len  = 0;
static int          g_sfx_render_tried = 0;  /* render attempted (success or fail) */
static MIX_Audio   *g_sfx_real     = NULL;   /* per-device MIX_Audio from g_sfx_pcm */
static int          g_sfx_used_real = 0;     /* did the last SFX test play the real effect */

/* T28 real title theme (curly.mid). Parsed once and cached for the session;
 * played through the OPL3 voice backend (cs_opl3midi) or the WB MPU-401 byte
 * path. When the .mid is missing/unreadable the parse is attempted once,
 * fails gracefully, and the music test uses the existing arpeggio. */
static int          g_real_music     = 1;    /* killswitch (default ON; "0" disables) */
static int          g_midi_biosclk   = 0;    /* T42 A/B: drive the MIDI scheduler from the BIOS clock (opt-in "1") */
static cs_smf      *g_title_smf      = NULL; /* parsed title theme (NULL = unavailable) */
static int          g_title_tried    = 0;    /* parse attempted (success or fail) */
static volatile int g_smf_isr_driven = 0;    /* T56 route A: title theme is dispatched from the SB16 IRQ-5 tick (1) vs the main loop (0) */
static int          g_music_used_real = 0;   /* did the last music test play the real theme */

/* T36 Organya-mode pre-rendered title snippet. Loaded once from the engine's
 * PCM cache, capped to ORG_SNIPPET_MS; cached for the session like the SFX/MIDI
 * data. NULL = no cache (fall back to the tone). */
static int          g_is_organya     = 0;    /* configured backend == "organya" */
static int16_t     *g_org_pcm        = NULL; /* cached snippet (S16, g_org_rate/ch) */
static uint32_t     g_org_samples    = 0;    /* int16 count in g_org_pcm */
static int          g_org_rate       = 0;
static int          g_org_ch         = 0;
static int          g_org_tried      = 0;    /* load attempted (success or fail) */
static int        g_inited   = 0;    /* SDL+MIX up + config resolved (device may be CLOSED) */
static int        g_dev_open  = 0;    /* T14: device + tracks live (only during a serviced test) */

static int        g_music_mode = MUS_PCM;
static int        g_tier2       = 1;   /* resolved engine Tier-2 (default-ON killswitch) */

/* T24 progress seam: the backend owns "how far through the whole bounded test
 * am I" (elapsed/planned), pump_service() forwards it to audiotest_progress()
 * in permille; the popup (setup/main.c) just renders the bar. */
static Uint64     g_test_t0       = 0;  /* wall-clock start of the current test */
static int        g_test_total_ms = 0;  /* planned total ms for the permille map */
static int        g_opl3_ok    = 0;    /* OPL3 detected + chip initialized */
static int        g_opl2_ok    = 0;    /* T80: OPL2 (AdLib) detected + chip initialized (music-only path) */
static int        g_wb_ok      = 0;    /* MPU-401 (blind) initialized */
static uint16_t   g_wb_port    = 0x330;

static char g_msg[200] = "audio test ready";

/* ---- config helpers -------------------------------------------------------*/

static int cfg_is_on(const scfg_t *c, const char *cfg_key)
{
  int idx = scfg_index(cfg_key);
  const char *v;
  if (idx < 0) return 0;
  v = scfg_get(c, idx);
  return (v && v[0] == '1' && v[1] == '\0');   /* strict "1" */
}

static const char *cfg_val(const scfg_t *c, const char *cfg_key, const char *fallback)
{
  int idx = scfg_index(cfg_key);
  const char *v;
  if (idx < 0) return fallback;
  v = scfg_get(c, idx);
  return (v && v[0]) ? v : fallback;
}

/* Push every SDL_HINT_* registry key from the config into SDL's hint store so
 * the SDL3-DOS backend programs the SAME device rate + SB16 mixer balance the
 * game would. Must run before the device opens. */
static void apply_hints(const scfg_t *c)
{
  int i;
  for (i = 0; i < DKT_KEY_COUNT; ++i)
  {
    const char *env = DKT_KEYS[i].env_name;
    if (env && strncmp(env, "SDL_HINT_", 9) == 0)
      SDL_SetHint(env, scfg_get(c, i));
  }
}

/* Apply the configured BLASTER (T10 DKC_HARDWARE key, env_name "BLASTER" -- not
 * an SDL hint, so apply_hints() skips it). The SDL3-DOS backend resolves the
 * SB16 base port AND the MPU-401 MIDI port (P-field) from this at device open /
 * WB init, so it must be set BEFORE SDL_Init. main.c's screen_audiotest()
 * already setenv's it, but we repeat it here so audiotest_init is self-contained
 * for any caller (the standalone probe, future entry points). AUTHORITATIVE
 * semantics: a non-empty config value overrides the ambient AUTOEXEC SET
 * BLASTER (overwrite=1); an empty value leaves the ambient env intact so the
 * auto-detect default is used. */
static void apply_blaster(const scfg_t *c)
{
  const char *v = cfg_val(c, "BLASTER", "");
  if (v && v[0])
    SDL_setenv_unsafe("BLASTER", v, 1);
}

/* A2: bridge the operator's real ULTRASND / ULTRADIR env vars into the SDL GUS
 * hints, EXACTLY as the engine does (main.cpp patch 0238): SDL_DOSGusInit reads
 * SDL_HINT_DOSKUTSU_GUS_ULTRASND (via SDL_GetHint, whose env fallback is the
 * SAME hint NAME, not "ULTRASND"), so without this bridge the GF1 detect fails
 * with "no ULTRASND hint" even on a correctly-configured Gravis box. GUS_VOICES
 * already rides apply_hints() (its cfg env_name IS SDL_HINT_DOSKUTSU_GUS_VOICES).
 * Must run before SDL_DOSGusInit; safe before SDL_Init (the hint store is
 * independent). No-op on a non-GUS box (the vars are absent). */
static void apply_ultrasnd(void)
{
  const char *us = SDL_getenv("ULTRASND");
  const char *ud = SDL_getenv("ULTRADIR");
  if (us && us[0])
    SDL_SetHintWithPriority("SDL_HINT_DOSKUTSU_GUS_ULTRASND", us, SDL_HINT_OVERRIDE);
  if (ud && ud[0])
    SDL_SetHintWithPriority("SDL_HINT_DOSKUTSU_GUS_ULTRADIR", ud, SDL_HINT_OVERRIDE);
}

/* ---- T14 freeze trace -----------------------------------------------------
 *
 * SETUP writes NO log of its own, so the g2k hard-freeze left no survivable
 * trace -- only a screenshot. This breadcrumb fixes that: every step of the
 * audio-test path is written to LOGS\<TAG>SETUP.LOG with a PER-LINE flush +
 * fsync (the DOS durability idiom from nx 0036 / SDL/0024 -- fflush only
 * reaches the OS; DOS file buffers + the CF card's write cache survive a hang,
 * so a hard freeze can lose the tail without the fsync). When the machine
 * wedges, the LAST flushed line is the exact wedge point.
 *
 * Filename honors DOSKUTSU_LOG_TAG exactly like the engine (main.cpp) + SDL
 * (SDL/0024/0062): "<TAG>SETUP.LOG" with TAG, "SETUPDBG.LOG" without. The
 * next iter sets a distinct tag per backend per power-cycle (OPL/WB/ORG ->
 * OPLSETUP.LOG / WBSETUP.LOG / ORGSETUP.LOG, all 8.3-clean), so all three
 * freeze traces persist for logback to pull in one pass. TAG is capped at 3
 * chars when composing the basename so "<TAG>SETUP" stays inside DOS 8.3.
 *
 * Permanent diagnostic infra: only the audio screen writes it, and only while
 * a test is on screen -- cheap, and the survivable-trace value is high. */
static FILE *g_trace_fp = NULL;

static void trace_open(void)
{
  char base[16];
  char path[40];
  const char *tag;

  if (g_trace_fp)
    return;

  /* Relative LOGS\ subdir == CWD\LOGS (SETUP ships + runs in the game dir);
   * matches the engine's CWD\LOGS convention + realhw's logback probe path.
   * mkdir is defensive -- harmless if it already exists. */
  mkdir("LOGS", 0777);

  tag = SDL_getenv("DOSKUTSU_LOG_TAG");
  if (tag && tag[0])
  {
    char t3[4];
    int i;
    for (i = 0; i < 3 && tag[i]; ++i) t3[i] = tag[i];
    t3[i] = '\0';
    SDL_snprintf(base, sizeof(base), "%sSETUP.LOG", t3);  /* e.g. OPLSETUP.LOG */
  }
  else
  {
    SDL_strlcpy(base, "SETUPDBG.LOG", sizeof(base));
  }
  SDL_snprintf(path, sizeof(path), "LOGS/%s", base);

  /* T49: APPEND, not truncate. The original "wb" truncated on every SETUP
   * launch under the same tag -- so in a multi-launch cell (operator re-enters
   * SETUP, or the CHECK flow exits + re-launches), the LAST launch erased the
   * session that actually ran the music test, losing the [tempo] witness even
   * though the test ran. Append keeps every launch's trace; a session banner
   * delimits them so the analyst reads the last "session start" block. realhw
   * clears LOGS\ per iter, so cross-iter staleness is bounded. */
  g_trace_fp = fopen(path, "ab");
  if (g_trace_fp)
  {
    fprintf(g_trace_fp, "\n==== SETUP audio-test session start ====\n");
    fflush(g_trace_fp);
    fsync(fileno(g_trace_fp));
  }
}

/* One breadcrumb line, durably committed before returning (see header). */
static void trace(const char *fmt, ...)
{
  va_list ap;
  if (!g_trace_fp)
    return;
  va_start(ap, fmt);
  vfprintf(g_trace_fp, fmt, ap);
  va_end(ap);
  fputc('\n', g_trace_fp);
  fflush(g_trace_fp);          /* libc -> OS */
  fsync(fileno(g_trace_fp));   /* OS + DOS buffers + CF write cache -> disk */
}

static void trace_close(void)
{
  if (g_trace_fp)
  {
    trace("trace: close");
    fclose(g_trace_fp);
    g_trace_fp = NULL;
  }
}

/* ---- public API -----------------------------------------------------------*/

int audiotest_available(void) { return 1; }
const char *audiotest_error(void) { return g_msg; }

int audiotest_init(const scfg_t *c)
{
  const char *backend;
  const char *t2;

  if (g_inited)
    return 0;

  if (cfg_is_on(c, "AUDIO_OFF"))
  {
    SDL_strlcpy(g_msg, "Audio is disabled in the config (AUDIO_OFF=1).", sizeof(g_msg));
    return 1;
  }

  apply_hints(c);
  apply_blaster(c);
  apply_ultrasnd();   /* A2: real ULTRASND/ULTRADIR env -> GUS hints (before SDL_DOSGusInit) */

  /* T80: resolve the music path from the configured backend BEFORE SDL_Init so
   * the AdLib (OPL2) path can skip the SB audio device entirely. The OPL chip
   * is direct port I/O at 0x388 (SDL_DOSOpl2* helpers) and needs neither
   * SDL_INIT_AUDIO nor SDL3_mixer; opening the SB device on a no-SB AdLib box
   * is exactly the "Not a SoundBlaster at port 0x220" failure this fixes. This
   * resolution reads the config struct (not an SDL hint), so it is safe to run
   * before SDL_Init. Every other backend keeps the SB device + mixer bring-up
   * unchanged. (backend/g_music_mode/g_is_organya are re-used by the trace +
   * status text below.) */
  backend = cfg_val(c, "AUDIO_BACKEND", "auto");
  if (strcmp(backend, "opl3") == 0)
    g_music_mode = MUS_OPL3;
  else if (strcmp(backend, "adlib") == 0)
    g_music_mode = MUS_OPL2;
  else if (strcmp(backend, "gus") == 0)
    g_music_mode = MUS_GUS;
  else if (strcmp(backend, "wb") == 0)
    g_music_mode = MUS_WB;
  else
    g_music_mode = MUS_PCM;   /* organya / auto / pcm / off-list */
  g_is_organya = (strcmp(backend, "organya") == 0);

  if (g_music_mode == MUS_OPL2 || g_music_mode == MUS_GUS)
  {
    /* AdLib (OPL) + Gravis (GF1) drive their own synth chip with NO Sound
     * Blaster: no SB device, no Mixer. SDL_Init(0) still brings up the base
     * library so SDL_GetHint / SDL_GetTicks / the hints apply_hints() pushed
     * all work. (AdLib is music-only; GUS also plays GF1-native SFX.) */
    if (!SDL_Init(0))
    {
      SDL_snprintf(g_msg, sizeof(g_msg), "SDL_Init failed: %s", SDL_GetError());
      return 1;
    }
  }
  else
  {
    if (!SDL_Init(SDL_INIT_AUDIO))
    {
      SDL_snprintf(g_msg, sizeof(g_msg), "SDL_Init(AUDIO) failed: %s", SDL_GetError());
      return 1;
    }
    if (!MIX_Init())
    {
      SDL_snprintf(g_msg, sizeof(g_msg), "MIX_Init failed: %s", SDL_GetError());
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
      SDL_Quit();
      return 1;
    }
  }

  /* T24: silence SDL INFO/WARN console logging (the SB16 device-open banners)
   * so the audio-test popup + progress bar render cleanly over the TUI -- the
   * operator's "too busy, overwrites the screen" complaint. Errors still
   * print; the durable device-param witness is LOGS\<TAG>SETUP.LOG, not the
   * console. Set after SDL_Init so the log subsystem exists. */
  SDL_SetLogPriorities(SDL_LOG_PRIORITY_ERROR);

  trace_open();
  trace("init: SDL_Init(AUDIO) + MIX_Init ok");

  /* T14: resolve AUDIO_TIER2 with the engine's killswitch semantics -- read
   * the SDL hint apply_hints() just pushed, default-ON, strict-"0" disables
   * (identical to SoundManager.cpp:243-246). The old code read it opt-in
   * (strict-"1"), so an empty/absent cfg value opened 44100 stereo -- a
   * device config the game never runs on g2k. Default-ON => 11025 mono. */
  t2 = SDL_GetHint("SDL_HINT_DOSKUTSU_AUDIO_TIER2");
  g_tier2 = !(t2 && t2[0] == '0' && t2[1] == 0);

  /* T28: real-SFX killswitch. Default-ON; strict "0" forces the tone blip.
   * SDL_GetHint falls back to the same-named environment variable, so the
   * operator can disable the real Polar Star with
   * SET SDL_HINT_DOSKUTSU_SETUP_REAL_SFX=0 at the SETUP launch if a bad/odd
   * .pxt ever misbehaves on a given machine. */
  {
    const char *rs = SDL_GetHint("SDL_HINT_DOSKUTSU_SETUP_REAL_SFX");
    g_real_sfx = !(rs && rs[0] == '0' && rs[1] == 0);
  }

  /* T28: real-music killswitch (the title theme). Default-ON; strict "0"
   * forces the test arpeggio. SDL_GetHint falls back to the env var. */
  {
    const char *rm = SDL_GetHint("SDL_HINT_DOSKUTSU_SETUP_REAL_MUSIC");
    g_real_music = !(rm && rm[0] == '0' && rm[1] == 0);
  }

  /* T42 A/B (opt-in, strict "1"): drive the MIDI scheduler from the BIOS
   * 18.2 Hz clock instead of SDL_GetTicks. Diagnostic for the real-HW
   * half-tempo report -- if the SDL_GetTicks ms clock reads half on this
   * hardware, this cell plays at the correct tempo (the witness then shows
   * d_ev/d_real full vs the default cell's half). Default OFF. */
  {
    const char *bc = SDL_GetHint("SDL_HINT_DOSKUTSU_SETUP_MIDI_BIOSCLK");
    g_midi_biosclk = (bc && bc[0] == '1' && bc[1] == 0);
  }

  /* T80: g_music_mode / g_is_organya were resolved above (before SDL_Init) so
   * the AdLib path could skip SDL_INIT_AUDIO. The chip bring-up (OPL2/OPL3
   * detect+init, MPU-401 init) is still deferred to device_open() so the device
   * is live only during a serviced test (T14), never while the menu blocks in
   * getch(). */
  trace("init: tier2=%d backend=%s mode=%d organya=%d biosclk=%d (0=pcm 1=opl3 2=wb 3=opl2 4=gus)",
        g_tier2, backend, g_music_mode, g_is_organya, g_midi_biosclk);

  /* Review-3 item 13 (T28 status-text rewrite): plain natural prose describing
   * both tests, no Hz/port jargon. This is the pre-test ABOUT-box text the
   * chooser shows (main.c renders audiotest_error()); after a test runs, the
   * post-play g_msg replaces it with the precise real-vs-fallback result. */
  switch (g_music_mode)
  {
    case MUS_OPL3:
      SDL_strlcpy(g_msg,
        "The Sound Effects test plays the Polar Star shot. The Music test "
        "plays the title theme on the sound card's FM synth (OPL3).",
        sizeof(g_msg));
      break;
    case MUS_OPL2:
      SDL_strlcpy(g_msg,
        "The Music test plays the title theme on the sound card's FM synth "
        "(AdLib / OPL2). AdLib has no sound effects (music only).",
        sizeof(g_msg));
      break;
    case MUS_GUS:
      SDL_strlcpy(g_msg,
        "The Music test plays a note arpeggio on the Gravis UltraSound (GF1) "
        "wavetable. The Sound Effects test plays the Polar Star shot on the "
        "GF1.", sizeof(g_msg));
      break;
    case MUS_WB:
      SDL_strlcpy(g_msg,
        "The Sound Effects test plays the Polar Star shot. The Music test "
        "plays the title theme on the WaveBlaster -- silent if none is "
        "attached.", sizeof(g_msg));
      break;
    default:
      SDL_strlcpy(g_msg,
        "The Sound Effects test plays the Polar Star shot. The Music test "
        "plays a short test tone.", sizeof(g_msg));
      break;
  }

  g_inited = 1;
  return 0;
}

/* ---- device lifecycle + ring servicing (T14 freeze fix) ------------------*/

static void device_close(void);   /* fwd: device_open() unwinds via it on error */
static int  play_gus_sfx(void);    /* A2 fwd: audiotest_play_sfx (below) uses it */
static void gus_play_pcm8(const Uint8 *u8, Uint32 len, Uint32 rate, int ms_cap); /* rc4 fwd: play_gus_arpeggio routes each note through it */

/* T28: synthesize the real Polar Star effect once per SETUP session
 * (device-independent: always S8 mono 22050). Sets g_sfx_pcm/_len on success,
 * leaves them NULL on any failure. The single attempt is gated by
 * g_sfx_render_tried so a missing .pxt is not re-probed on every test. */
static void ensure_sfx_render(void)
{
  if (g_sfx_render_tried)
    return;
  g_sfx_render_tried = 1;
  g_sfx_pcm = cs_pixtone_render(SFX_POLAR_STAR_PXT, &g_sfx_pcm_len);
  if (g_sfx_pcm && g_sfx_pcm_len > 0)
    trace("sfx: rendered real Polar Star from %s (%lu samples)",
          SFX_POLAR_STAR_PXT, (unsigned long)g_sfx_pcm_len);
  else
  {
    g_sfx_pcm     = NULL;   /* render returns NULL + len 0 on failure */
    g_sfx_pcm_len = 0;
    trace("sfx: real Polar Star unavailable (%s missing/unreadable) -> tone blip",
          SFX_POLAR_STAR_PXT);
  }
}

/* Open the SB16 device + test tones + the selected synth chip. Called at the
 * START of each bounded test and torn down by device_close() at the end, so
 * the autoinit-DMA + IRQ-5 are live ONLY while pump_service() is feeding the
 * ring -- never while the main thread blocks in the menu's getch(). */
static int device_open(void)
{
  SDL_AudioSpec spec;
  /* T48 / SDL/0096 cold-init: when the WB cold-init hint is set we arm the
   * MPU-401 BEFORE MIX_CreateMixerDevice (below) and must NOT re-init it in the
   * post-MIX chip bring-up (a second SDL_DOSMpu401Init would issue the UART
   * entry HOT = the DX2-66 wedge). These carry the cold result across the
   * g_wb_ok = 0 reset that the chip bring-up does. */
  int wb_cold_inited = 0;
  int wb_cold_ok = 0;

  if (!g_inited) return 1;
  if (g_dev_open) return 0;

  /* T80: AdLib (OPL2) music path -- direct OPL chip I/O at 0x388, with NO SB
   * device and NO SDL3_mixer (none was inited; see audiotest_init). Detect +
   * bring up the OPL2 chip and return early, skipping the entire
   * MIX_CreateMixerDevice + SFX/tone/track block below -- there is no audio
   * device to open, and attempting it is the "Not a SoundBlaster" failure this
   * path avoids. Music dispatches from the SDL_GetTicks SMF clock (route B), the
   * same cooperative service loop the OPL3 test uses (no SB IRQ-5 tick). AdLib
   * is music-only: a DAC-less OPL chip has no PCM sound effects. */
  if (g_music_mode == MUS_OPL2)
  {
    g_opl2_ok = 0;
    trace("device_open: adlib/opl2 detect+init @0x388 (no SB device; music-only)");
    if (cs_opl2midi_init())
    {
      g_opl2_ok = 1;
      trace("device_open: opl2 ready (9-voice single-bank; SDL_GetTicks clock)");
    }
    else
    {
      trace("device_open: opl2 detect=0 (no OPL chip at 0x388)");
    }
    g_dev_open = 1;
    trace("device_open: done (opl2 path, no mixer)");
    return 0;
  }

  /* A2: Gravis GF1 music/SFX path -- native wavetable, NO SB device and NO
   * SDL3_mixer (none was inited; see audiotest_init). SDL_DOSGusInit (SDL/0112)
   * parses the ULTRASND hint apply_ultrasnd() bridged, resets the GF1, DRAM-
   * detects, and enables the DAC; it hooks no IRQ and starts no pump, so it is
   * self-contained for SETUP. Return early, skipping the SB16 + mixer + SFX/tone
   * block below (there is no SB device to open on a Gravis box). Voices are
   * commanded directly (SetVoiceFreq/Vol/Pan + StartVoice); pacing/ESC uses the
   * same pump_service loop (its SDL_DOSAudioPump no-ops with no SB device). */
  if (g_music_mode == MUS_GUS)
  {
    g_gus_ok = 0;
    trace("device_open: gus GF1 init (SDL_DOSGusInit; no SB device; native wavetable)");
    if (SDL_DOSGusInit())
    {
      SDL_DOSGusState st;
      g_gus_ok = 1;
      if (SDL_DOSGusGetState(&st))
        trace("device_open: gus ready (base=0x%X voices=%d rate=%dHz dram=%dKB)",
              (unsigned)st.base_port, st.num_voices, (int)st.output_rate,
              (int)(st.dram_size / 1024));
      else
        trace("device_open: gus ready (state unavailable)");
    }
    else
    {
      SDL_snprintf(g_msg, sizeof(g_msg),
        "No Gravis UltraSound found: %s", SDL_GetError());
      trace("device_open: gus init FAILED: %s", SDL_GetError());
    }
    g_dev_open = 1;
    trace("device_open: done (gus path, no mixer)");
    return 0;
  }

  /* T48 / SDL/0096: DOOM-faithful COLD-INIT reorder for the WB music test --
   * enter MPU UART mode on the COLD ISA bus (before the SB16 autoinit-DMA +
   * IRQ-5 go live) -- mirrors the engine SoundManager reorder. The SDL cold
   * path (patch 0096) does 0xFF reset + drain + 0x3F UART entry + entry-ACK
   * drain and keeps per-byte writes blind.
   *
   * T73: DEFAULT-ON (strict-"0" killswitch), matching the game's T68/0220 flip.
   * Was opt-in (strict-"1"), which meant a NORMAL menu launch of SETUP.EXE (no
   * env) ran the WB test on a HOT bus = the iter-6 freeze risk -- the deliverable
   * wasn't actually fixed for a real operator (iter-8 WBS3 only played because
   * the BAT set COLD_INIT=1). Default-ON + SDL/0099's bus-hot guard = the WB test
   * cold-inits + arms by default = wedge-proof. SDL_HINT_DOSKUTSU_AUDIO_WB_COLD
   * _INIT=0 reverts to the old hot-order path. */
  if (g_music_mode == MUS_WB)
  {
    /* T74: default the per-byte TX pacing to 320 us (TIMED-DELAY -- freeze-proof,
     * the 31250-baud MIDI wire-byte time) if-unset, BEFORE the cold-init below
     * reads it. Behavioral no-op (the SDL cold-init already defaults tx_delay=320
     * when unset, so SETUP already armed TIMED-DELAY; the game relies on that same
     * default) -- but pinning it explicitly here makes the pace banner honest and
     * survives any future SDL-default drift. Operator =0 still wins (strict-"0"
     * killswitch -> the SDL path falls to the bit-6 DRR poll). */
    if (!SDL_GetHint("SDL_HINT_DOSKUTSU_AUDIO_WB_TX_DELAY"))
      SDL_SetHint("SDL_HINT_DOSKUTSU_AUDIO_WB_TX_DELAY", "320");
    const char *cold = SDL_GetHint("SDL_HINT_DOSKUTSU_AUDIO_WB_COLD_INIT");
    const bool cold_on = !(cold && cold[0] == '0' && cold[1] == 0);
    if (cold_on)
    {
      g_wb_port = SDL_DOSMpu401GetBLASTERPort();
      trace("device_open: wb COLD-INIT port=0x%03X BEFORE SB16 open "
            "(T73 DEFAULT-ON; killswitch SDL_HINT_DOSKUTSU_AUDIO_WB_COLD_INIT=0; "
            "DOOM-faithful reorder -- MPU UART entered on the cold bus, hot writes "
            "stay blind)",
            (unsigned)g_wb_port);
      wb_cold_ok = SDL_DOSMpu401Init(g_wb_port) ? 1 : 0;
      wb_cold_inited = 1;
      trace("device_open: wb cold-init=%d (survived -- no wedge; SB16 not yet open)",
            wb_cold_ok);
      /* T48 iter-8: mirror the SDL-side pace-mode banner into the SETUP trace
       * (SDL_Log is NUL'd in SETUP, so this is the only WBS3 log witness for
       * which per-byte flow-control mode ran). Re-read the same hints the SDL
       * cold-init path resolved. */
      {
        const char *txd = SDL_GetHint("SDL_HINT_DOSKUTSU_AUDIO_WB_TX_DELAY");
        const char *drr = SDL_GetHint("SDL_HINT_DOSKUTSU_AUDIO_WB_DRR_POLL");
        /* T74: TX_DELAY is defaulted to "320" at the top of this MUS_WB block (if
         * unset), so this banner now reports the true pace (TIMED-DELAY). It
         * formerly misread DRR-POLL on an unset hint while the SDL cold-init path
         * actually armed its 320 default -- a witness confound, not a real pace
         * gap (iter-9 carried no freeze risk). [grep_anchor_confound] */
        const char *pace = (txd && txd[0] && SDL_atoi(txd) > 0) ? "TIMED-DELAY(freeze-proof)"
                         : (drr && drr[0] == '0' && drr[1] == 0) ? "BLIND(off)"
                         : "DRR-POLL(ON)";
        trace("device_open: wb cold-init pace=%s (per-byte PLAY flow control; "
              "SDL/0097 H20 garble fix)", pace);
      }
    }
  }

  /* Pin device sample frames to 256 in Tier-2, mirroring SoundManager::init
   * (SDL_HINT_OVERRIDE) so the SB16 IRQ rate matches the game (~43 Hz) rather
   * than SDL's default 512/1024-frame heuristic. */
  if (g_tier2)
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256", SDL_HINT_OVERRIDE);

  /* T56 route A: default the SDL/0072 Lever-2b IRQ-5 MIDI tick ON for SETUP's
   * MIDI preview so the title theme dispatches at ~43 Hz (like the in-game
   * MidiScheduler) instead of the ~2-3 Hz cooperative TUI loop -- the cadence
   * root cause of the operator's "draggy/slower" report. Resolved at the SB16
   * OpenDevice below, so it MUST be set first. Normal priority + the unset
   * guard preserve an explicit operator killswitch: SDL_HINT_DOSKUTSU_MIDI_ISR
   * _TICK=0 (env or scfg) still wins -> falls back to the main-line tick. */
  if (!SDL_GetHint("SDL_HINT_DOSKUTSU_MIDI_ISR_TICK"))
  {
    SDL_SetHint("SDL_HINT_DOSKUTSU_MIDI_ISR_TICK", "1");
    trace("device_open: SDL_HINT_DOSKUTSU_MIDI_ISR_TICK defaulted ON (route A; "
          "operator =0 still overrides)");
  }
  else
  {
    /* T74 witness: operator set the hint explicitly -- log the resolved route so
     * the iter-10 TPB control cell (MIDI_ISR_TICK=0) shows the killswitch at init. */
    const char *it = SDL_GetHint("SDL_HINT_DOSKUTSU_MIDI_ISR_TICK");
    trace("device_open: SDL_HINT_DOSKUTSU_MIDI_ISR_TICK=%s (operator-set; %s)",
          it ? it : "(null)",
          (it && it[0] == '0' && it[1] == 0) ? "route B -- ISR tick DISABLED" : "route A");
  }

  SDL_zero(spec);
  spec.format   = SDL_AUDIO_S16;
  spec.channels = g_tier2 ? 1 : 2;
  spec.freq     = g_tier2 ? 11025 : 22050;   /* match engine: Tier-2 11025 mono / Tier-1 22050 stereo */

  trace("device_open: MIX_CreateMixerDevice freq=%d ch=%d S16 frames=%s "
        "(opens SB16: DSP reset + DMA program + IRQ hook)",
        spec.freq, spec.channels, g_tier2 ? "256" : "default");
  g_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
  if (!g_mixer)
  {
    SDL_snprintf(g_msg, sizeof(g_msg), "Could not open audio device: %s", SDL_GetError());
    trace("device_open: MIX_CreateMixerDevice FAILED: %s", SDL_GetError());
    return 1;
  }
  trace("device_open: mixer up");

  /* SFX (always SB16 PCM) + the PCM music tone (organya/auto/fallback). */
  g_sfx_aud = MIX_CreateSineWaveAudio(g_mixer, SFX_HZ, SFX_AMP, SFX_MS);
  g_mus_aud = MIX_CreateSineWaveAudio(g_mixer, MUSIC_HZ, MUSIC_AMP, MUSIC_MS);
  g_sfx_trk = MIX_CreateTrack(g_mixer);
  g_mus_trk = MIX_CreateTrack(g_mixer);
  if (!g_sfx_aud || !g_mus_aud || !g_sfx_trk || !g_mus_trk)
  {
    SDL_snprintf(g_msg, sizeof(g_msg), "Could not create test tones: %s", SDL_GetError());
    trace("device_open: tone/track create FAILED: %s", SDL_GetError());
    device_close();
    return 1;
  }
  /* T28: bind the SFX track to the REAL Polar Star when it rendered, else the
   * sine blip. g_sfx_aud is always kept as the graceful fallback. The mixer
   * resamples the S8/22050 master to the device format on playback. */
  g_sfx_used_real = 0;
  if (g_real_sfx)
  {
    ensure_sfx_render();
    if (g_sfx_pcm && g_sfx_pcm_len > 0)
    {
      SDL_AudioSpec px;
      px.format = SDL_AUDIO_S8; px.channels = 1; px.freq = SFX_PXT_RATE;
      g_sfx_real = MIX_LoadRawAudio(g_mixer, g_sfx_pcm, g_sfx_pcm_len, &px);
      if (g_sfx_real)
      {
        g_sfx_used_real = 1;
        trace("device_open: SFX = real Polar Star (MIX_LoadRawAudio ok)");
      }
      else
        trace("device_open: MIX_LoadRawAudio FAILED: %s -> tone blip", SDL_GetError());
    }
  }
  MIX_SetTrackAudio(g_sfx_trk, g_sfx_used_real ? g_sfx_real : g_sfx_aud);
  MIX_SetTrackAudio(g_mus_trk, g_mus_aud);
  /* NOTE: MIX_SetTrackLoops on a STOPPED track is a no-op -- MIX_PlayTrack
   * resets the loop count from its (default 0) options. So the PCM-tone loop
   * is set AFTER MIX_PlayTrack in audiotest_play_music, not here. (Caught by
   * probe-engineer while mirroring this device_open for the WBHOT MIX bring-up:
   * the music tone was stopping at MUSIC_MS instead of looping the test window.) */
  trace("device_open: tones + tracks created (sfx_real=%d)", g_sfx_used_real);

  /* Bring up the selected synth chip (resets per-open; cleared in close). */
  g_opl3_ok = 0;
  g_wb_ok   = 0;
  if (g_music_mode == MUS_OPL3)
  {
    trace("device_open: opl3 detect @0x388");
    if (SDL_DOSOpl3Detect())
    {
      trace("device_open: opl3 detect=1, init chip");
      SDL_DOSOpl3InitChip();
      trace("device_open: opl3 write patch");
      SDL_DOSOpl3VoiceWritePatch(OPL3_VOICE, PATCH_ORGAN);
      g_opl3_ok = 1;
      trace("device_open: opl3 ready");
    }
    else
    {
      trace("device_open: opl3 detect=0 (no chip)");
    }
  }
  else if (g_music_mode == MUS_WB)
  {
    if (wb_cold_inited)
    {
      /* T48 / SDL/0096: the MPU was already armed COLD before the SB16 open --
       * do NOT re-init it (a hot UART entry here is the wedge). Just carry the
       * cold result across the g_wb_ok = 0 reset above. */
      g_wb_ok = wb_cold_ok;
      trace("device_open: wb already cold-inited before SB16 open -- skipping "
            "the hot MPU init (cold_ok=%d)", g_wb_ok);
    }
    else
    {
      const char *drr = SDL_GetHint("SDL_HINT_DOSKUTSU_AUDIO_WB_DRR_POLL");
      const char *dp  = SDL_GetHint("SDL_HINT_DOSKUTSU_AUDIO_WB_DIRECT_PORT");
      g_wb_port = SDL_DOSMpu401GetBLASTERPort();
      /* T58: log the resolved transport hints BEFORE the (potentially wedging)
       * init, so the iter log shows whether the poll/direct-port env actually
       * reached SETUP -- the last surviving line tells us if a hang is on the
       * blind path (no poll) or the polled path. */
      trace("device_open: wb mpu-401 init port=0x%03X drr_poll=%s direct_port=%s",
            (unsigned)g_wb_port,
            (drr && drr[0] == '1' && drr[1] == 0) ? "1" : "0(blind)",
            (dp && dp[0] == '0' && dp[1] == 0) ? "0(dsp)" : "1");
      g_wb_ok = SDL_DOSMpu401Init(g_wb_port) ? 1 : 0;
      trace("device_open: wb init=%d (survived -- no wedge)", g_wb_ok);
    }
  }

  g_dev_open = 1;
  trace("device_open: done");
  return 0;
}

/* Quiesce + tear down everything device_open() brought up. Safe to call when
 * the device is already closed (idempotent). Leaves SDL_Init/MIX_Init alive. */
static void device_close(void)
{
  trace("device_close: begin (opl3=%d opl2=%d wb=%d gus=%d)", g_opl3_ok, g_opl2_ok, g_wb_ok, g_gus_ok);
  if (g_opl3_ok) { SDL_DOSOpl3VoiceNoteOff(OPL3_VOICE); SDL_DOSOpl3Shutdown(); g_opl3_ok = 0; }
  /* T80: OPL2 (AdLib) path -- all-notes-off + 0xBD clear + SDL_DOSOpl2Shutdown.
   * The mixer/track/audio handles below are all NULL on this path (never
   * created), so their guarded destroys no-op. */
  if (g_opl2_ok) { cs_opl2midi_shutdown(); g_opl2_ok = 0; }
  /* A2: Gravis GF1 path -- stop every voice, then shut the card down (parks the
   * GF1 in reset). Like OPL2, the mixer/track/audio handles below are all NULL
   * on this path so their guarded destroys no-op. */
  if (g_gus_ok) { SDL_DOSGusStopAllVoices(); SDL_DOSGusShutdown(); g_gus_ok = 0; }
  if (g_wb_ok)
  {
    /* All Sound Off (CC 120) + All Notes Off (CC 123) before MPU shutdown. */
    SDL_DOSMpu401WriteByte((uint8_t)(0xB0 | WB_CHANNEL));
    SDL_DOSMpu401WriteByte(120);
    SDL_DOSMpu401WriteByte(0x00);
    SDL_DOSMpu401WriteByte((uint8_t)(0xB0 | WB_CHANNEL));
    SDL_DOSMpu401WriteByte(123);
    SDL_DOSMpu401WriteByte(0x00);
    SDL_DOSMpu401Shutdown();
    g_wb_ok = 0;
  }
  if (g_sfx_trk) { MIX_DestroyTrack(g_sfx_trk); g_sfx_trk = NULL; }
  if (g_mus_trk) { MIX_DestroyTrack(g_mus_trk); g_mus_trk = NULL; }
  if (g_sfx_aud)  { MIX_DestroyAudio(g_sfx_aud);  g_sfx_aud  = NULL; }
  if (g_sfx_real) { MIX_DestroyAudio(g_sfx_real); g_sfx_real = NULL; } /* T28: per-device real-SFX audio (g_sfx_pcm survives) */
  if (g_mus_aud)  { MIX_DestroyAudio(g_mus_aud);  g_mus_aud  = NULL; }
  if (g_mixer)   { MIX_DestroyMixer(g_mixer);   g_mixer   = NULL; }
  g_dev_open = 0;
  trace("device_close: done");
}

/* Service the SB16 ring exactly like the engine main loop while a test plays:
 * top the ring to full with SDL_DOSAudioPump() (same primitive + while-loop
 * pattern as nxengine-evo/0162's load_stage seams), pace with SDL_Delay()
 * (which yields to the cooperative scheduler so the audio thread also runs +
 * the ISR drains the ring), and poll the keyboard non-blockingly so the test
 * is always interruptible and the main thread never blocks while the device
 * is streaming.
 *
 *   ms < 0            -> run until a key is pressed (no time bound)
 *   stop_on_any_key   -> any key ends the wait; otherwise only ESC does
 *
 * Returns the key consumed (ESC == 27), or 0 on timeout. */
/* T24: begin a test's progress span. total_ms is the planned wall-clock of the
 * whole bounded test (all arpeggio notes / the PCM window / the SFX blip), so
 * pump_service() can report OVERALL permille even though it runs per-segment. */
static void test_progress_begin(int total_ms)
{
  g_test_t0       = SDL_GetTicks();
  g_test_total_ms = (total_ms > 0) ? total_ms : 0;
  audiotest_progress(0);
}

static int pump_service(const char *what, int ms, int stop_on_any_key)
{
  Uint64 start = SDL_GetTicks();
  int      iters = 0;         /* outer-loop iterations (~one per SDL_Delay(5))    */
  unsigned long serviced = 0; /* # SDL_DOSAudioPump() calls that DID work (ring)  */
  trace("pump[%s]: begin ms=%d (first SDL_DOSAudioPump next -- locks device + iterates)", what, ms);
  for (;;)
  {
    while (SDL_DOSAudioPump()) { ++serviced; /* top the SB16 ring to full */ }

    /* T24: forward OVERALL test progress (elapsed-since-begin / planned). */
    if (g_test_total_ms > 0)
    {
      int pm = (int)(((SDL_GetTicks() - g_test_t0) * 1000) / (Uint64)g_test_total_ms);
      if (pm < 0)    pm = 0;
      if (pm > 1000) pm = 1000;
      audiotest_progress(pm);
    }
    ++iters;

    if (kbhit())
    {
      int k = getch();
      if (k == 0 || k == 0xE0) { (void)getch(); k = 27; } /* extended key -> treat as stop */
      if (k == 27 /*ESC*/ || stop_on_any_key)
      {
        trace("pump[%s]: key=%d -> end (iters=%d serviced=%lu)", what, k, iters, serviced);
        return k;
      }
    }

    if (ms >= 0 && (SDL_GetTicks() - start) >= (Uint64)ms)
    {
      /* rc4 diag: report iters + ACTUAL ring-servicing count at EVERY return,
       * unconditionally -- the old heartbeat only printed at iter 64/128/... so a
       * sub-320ms pump (e.g. a 280ms arpeggio note) logged NO servicing line and
       * looked unserviced when it was fine. On the GUS path there is no SB device
       * (SDL_Init(0)), so serviced is EXPECTED 0 -- the GF1 plays autonomously
       * from DRAM and needs no ring pump; iters>0 proves the loop ran. */
      trace("pump[%s]: timeout -> end (iters=%d serviced=%lu)", what, iters, serviced);
      return 0;
    }

    SDL_Delay(5);   /* pace + cooperative-scheduler yield (lets the ISR drain) */
  }
}

int audiotest_play_sfx(void)
{
  if (!g_inited) return 1;
  trace("play_sfx: enter");
  /* T80: AdLib (OPL2) is music-only -- a DAC-less OPL chip has no PCM sound
   * effects, and there is no SB device / mixer open on this path. Report it
   * cleanly instead of touching the (NULL) SFX track. SETUP's SFX picker
   * already offers only "No Sound FX" for AdLib; this is a defensive guard in
   * case the SFX test is reached anyway. */
  if (g_music_mode == MUS_OPL2)
  {
    SDL_strlcpy(g_msg, "AdLib has no sound effects (music only).", sizeof(g_msg));
    trace("play_sfx: adlib/opl2 -- no PCM SFX (music only); skipped");
    return 0;
  }
  /* A2: Gravis GF1 SFX -- the Polar Star plays on a GF1 voice (native wavetable
   * DAC), NOT the SB16 (a GUS-only box has none). Bring the GF1 up via
   * device_open (no mixer/SFX-track on this path) and play the real Pixtone
   * one-shot, else a GF1 square blip. */
  if (g_music_mode == MUS_GUS)
  {
    int real;
    if (device_open() != 0) return 1;
    if (!g_gus_ok) { device_close(); return 1; }   /* g_msg set by device_open */
    test_progress_begin(SFX_MS + 300);
    real = play_gus_sfx();
    audiotest_progress(1000);
    SDL_strlcpy(g_msg, real
                  ? "Real Polar Star shot on the Gravis UltraSound (GF1)."
                  : "Test blip -- Cave Story SFX data not found (data/pxt/fx20.pxt).",
                sizeof(g_msg));
    device_close();
    trace("play_sfx: done gus (real=%d)", real);
    return 0;
  }
  if (device_open() != 0) return 1;
  /* SFX is always the SB16 PCM path regardless of music backend. */
  trace("play_sfx: MIX_PlayTrack");
  if (!MIX_PlayTrack(g_sfx_trk, 0)) { trace("play_sfx: MIX_PlayTrack FAILED"); device_close(); return 1; }
  test_progress_begin(SFX_MS + 300);
  pump_service("sfx", SFX_MS + 300, 0);   /* blip duration + a short tail; ESC aborts */
  audiotest_progress(1000);
  /* T28: record what actually played so the popup can name it (nx-engine wires
   * the wording in main.c). Set on success only -- failure paths keep their
   * own device-error message in g_msg. */
  SDL_strlcpy(g_msg, g_sfx_used_real
                ? "Real Polar Star shot (data/pxt/fx20.pxt)."
                : "Test tone -- Cave Story SFX data not found (data/pxt/fx20.pxt).",
              sizeof(g_msg));
  device_close();
  trace("play_sfx: done (real=%d)", g_sfx_used_real);
  return 0;
}

/* Play a short arpeggio on the real OPL3 chip while servicing the SB16 ring.
 * ESC aborts mid-arpeggio. Assumes device_open() ran. */
static int play_opl3_arpeggio(void)
{
  int i;
  if (!g_opl3_ok) return 1;
  for (i = 0; i < ARP_COUNT; ++i)
  {
    uint16_t fnum; uint8_t block;
    opl3_note_to_fnum_block(ARP_NOTES[i], &fnum, &block);
    trace("opl3 arp: note %d on", ARP_NOTES[i]);
    SDL_DOSOpl3VoiceNoteOn(OPL3_VOICE, fnum, block);
    if (pump_service("opl3", ARP_ON_MS, 0) == 27) { SDL_DOSOpl3VoiceNoteOff(OPL3_VOICE); break; }
    SDL_DOSOpl3VoiceNoteOff(OPL3_VOICE);
    if (pump_service("opl3", ARP_GAP_MS, 0) == 27) break;
  }
  return 0;
}

/* T80: play the same short arpeggio on the real OPL2 (AdLib) chip, driven
 * through the cs_opl2midi 9-voice backend (its sink callbacks) so it exercises
 * the real allocator + patch path -- the AdLib fallback when data/midi/curly.mid
 * is absent. No SB ring to service (music-only path); pump_service no-ops the
 * SDL_DOSAudioPump but still paces + polls the keyboard for ESC. */
static int play_opl2_arpeggio(void)
{
  cs_smf_sink sink;
  int i;
  if (!g_opl2_ok) return 1;
  cs_opl2midi_song_start();
  cs_opl2midi_get_sink(&sink);
  for (i = 0; i < ARP_COUNT; ++i)
  {
    trace("opl2 arp: note %d on", ARP_NOTES[i]);
    sink.note_on(sink.user, 0, ARP_NOTES[i], 100);
    if (pump_service("opl2", ARP_ON_MS, 0) == 27) { sink.note_off(sink.user, 0, ARP_NOTES[i], 0); break; }
    sink.note_off(sink.user, 0, ARP_NOTES[i], 0);
    if (pump_service("opl2", ARP_GAP_MS, 0) == 27) break;
  }
  cs_opl2midi_all_notes_off();
  return 0;
}

/* A2: play a C-E-G-C arpeggio on the real Gravis GF1. Pitch is set by the
 * uploaded sample PERIOD, with EVERY note played at the fixed native 22050 Hz
 * (GUS_NATIVE_HZ) -- the only GF1 playback rate validated audible on the g2k
 * PicoGUS (the driver's gus_emit_test_tone + this file's GUS SFX both use it).
 * The prior version varied the playback rate per note (13-26 kHz) and was SILENT
 * on g2k while the SFX test worked; see the GUS_NATIVE_HZ note above. The four
 * period-varied samples are uploaded UP FRONT (no DRAM pokes during playback,
 * PicoGUS-safe), then each is played as a ONE-SHOT (flags=0, not a loop -- a
 * forever-loop voice can wedge the PicoGUS). Order per the driver (Freq -> Vol ->
 * Pan -> StartVoice); StopVoice between notes quiesces before the next;
 * device_close StopAllVoices before Shutdown. No SB ring; pump_service paces +
 * polls ESC (SDL_DOSAudioPump no-ops with no SB device). device_open() ran. */
static int play_gus_arpeggio(void)
{
  static Uint8 wave[GUS_NOTE_LEN];   /* static: keep it off the stack */
  int    i, j;

  /* rc4 INSTRUMENTATION (task #14, 3rd attempt): the SETUP GUS MUSIC test is
   * still silent on g2k while the GUS SFX test (gus_play_pcm8) + in-game GUS
   * music both work. Two prior fixes (loop-rate, then native-22050) missed
   * because nothing logged what the music path actually does vs the SFX path.
   * This trace mirrors gus_play_pcm8's instrumentation so the g2k log can be
   * DIFFed. SETUP-side values only (no GF1 status reads after StartVoice -- that
   * contends with the just-started voice and can wedge the PicoGUS, gus-8). */
  trace("gus arp: ENTER g_gus_ok=%d ARP_COUNT=%d NOTE_LEN=%d native=%dHz",
        g_gus_ok, ARP_COUNT, GUS_NOTE_LEN, GUS_NATIVE_HZ);
  if (!g_gus_ok) { trace("gus arp: g_gus_ok=0 -> BAIL (silent, GF1 not up)"); return 1; }

  /* rc5 FIX: route EACH note through gus_play_pcm8 -- the EXACT code path the
   * WORKING GUS SFX test uses (fresh upload + fresh AllocVoice + SetFreq/Vol/Pan
   * + StartVoice + serviced pump + StopVoice, one self-contained sound). The rc4
   * g2k trace diff showed the silent music path and the audible SFX path issued
   * identical, valid GF1 commands (same base/voices/rate, uploads OK, AllocVoice
   * v=0, valid START params) -- the pump "services 0 vs 64" was a heartbeat-
   * threshold logging artifact (64 iters ~= 320ms > a 280ms note; and on the GUS
   * path SDL_DOSAudioPump is a no-op, the GF1 plays autonomously). The only real
   * divergence was STRUCTURAL: the old arpeggio did 4 UP-FRONT uploads + reused
   * ONE voice across 4 StartVoice/StopVoice cycles, none of which the proven SFX
   * path does. Reusing gus_play_pcm8 per note eliminates every structural
   * difference at once. Each note's pitch is in the uploaded square's PERIOD,
   * played at the native GUS_NATIVE_HZ (the SFX rate). */
  for (i = 0; i < ARP_COUNT; ++i)
  {
    int p = GUS_ARP_PERIOD[i];
    for (j = 0; j < GUS_NOTE_LEN; ++j)
      wave[j] = ((j % p) < (p / 2)) ? 0xFF : 0x00;
    trace("gus arp: note %d period=%d -> gus_play_pcm8 (SFX-identical path)", i, p);
    gus_play_pcm8(wave, (Uint32)GUS_NOTE_LEN, GUS_NATIVE_HZ, ARP_ON_MS);
    if (pump_service("gus-gap", ARP_GAP_MS, 0) == 27) break;
  }
  trace("gus arp: DONE (all %d notes issued)", ARP_COUNT);
  return 0;
}

/* A2: play a one-shot PCM sample on a single GF1 voice at `rate` Hz, capped to
 * ms_cap. `u8` is unsigned 8-bit (GF1 native DAC encoding). Serviced/ESC-able.
 * Uploads into DRAM (freed from DRAM on the next device_close's Shutdown). */
static void gus_play_pcm8(const Uint8 *u8, Uint32 len, Uint32 rate, int ms_cap)
{
  Uint32 addr;
  int    v, dur;
  /* rc4 INSTRUMENTATION (task #14): this is the WORKING SFX path -- trace it in
   * the SAME shape as play_gus_arpeggio so the g2k log can be DIFFed to find why
   * the (structurally identical) music path is silent. */
  trace("gus pcm: ENTER g_gus_ok=%d len=%luB rate=%dHz", g_gus_ok, (unsigned long)len, (int)rate);
  {
    SDL_DOSGusState st;
    if (SDL_DOSGusGetState(&st))
      trace("gus pcm: state base=0x%X voices=%d rate=%dHz dram=%luKB used=%luB mask=0x%lX",
            (unsigned)st.base_port, st.num_voices, (int)st.output_rate,
            (unsigned long)(st.dram_size / 1024), (unsigned long)st.dram_used,
            (unsigned long)st.voice_active_mask);
  }
  addr = SDL_DOSGusUploadSample(u8, len, 0);
  trace("gus pcm: upload (%luB) -> addr=0x%lX%s", (unsigned long)len, (unsigned long)addr,
        addr == SDL_DOSGUS_BAD_ADDR ? " *** BAD_ADDR ***" : "");
  if (addr == SDL_DOSGUS_BAD_ADDR) { trace("gus pcm: DRAM upload FAILED"); return; }
  v = SDL_DOSGusAllocVoice();
  trace("gus pcm: AllocVoice -> v=%d", v);
  if (v < 0) { trace("gus pcm: no free voice"); return; }
  SDL_DOSGusSetVoiceFreq(v, rate);
  SDL_DOSGusSetVoiceVol(v, GUS_VOL);
  SDL_DOSGusSetVoicePan(v, 128);
  SDL_DOSGusStartVoice(v, addr, addr + len - 1, addr, 0);   /* one-shot, no loop */
  dur = (int)(((Uint64)len * 1000) / (Uint64)rate);
  if (dur > ms_cap) dur = ms_cap;
  trace("gus pcm: START v=%d addr=0x%lX end=0x%lX freq=%d vol=%d pan=128 flags=0 dur=%dms",
        v, (unsigned long)addr, (unsigned long)(addr + len - 1), (int)rate, GUS_VOL, dur);
  pump_service("gus-sfx", dur + 200, 0);
  SDL_DOSGusStopVoice(v);
  trace("gus pcm: STOP (played %dms)", dur);
}

/* A2: GUS SFX test -- the real Polar Star on the GF1. The Pixtone render is S8
 * mono 22050 (device-independent, cached); convert to the GF1's unsigned 8-bit
 * and play it one-shot. Returns 1 if it played the real effect, 0 if it played
 * the fallback square blip (no .pxt data). Either way it produces sound so the
 * operator can confirm the GF1 SFX path. Assumes device_open() ran. */
static int play_gus_sfx(void)
{
  if (!g_gus_ok) return 0;
  ensure_sfx_render();
  if (g_sfx_pcm && g_sfx_pcm_len > 0)
  {
    Uint8   *u8 = (Uint8 *)malloc(g_sfx_pcm_len);
    if (u8)
    {
      Uint32 i;
      for (i = 0; i < g_sfx_pcm_len; ++i)
        u8[i] = (Uint8)((int)g_sfx_pcm[i] + 128);   /* S8 -> U8 (GF1 DAC) */
      gus_play_pcm8(u8, g_sfx_pcm_len, SFX_PXT_RATE, 4000);
      free(u8);
      trace("gus sfx: real Polar Star played (%lu samples)", (unsigned long)g_sfx_pcm_len);
      return 1;
    }
  }
  /* Fallback: a short square blip so the SFX path still makes sound. */
  {
    static Uint8 blip[3000];   /* ~136 ms at 22050 */
    int i, period = (int)(SFX_PXT_RATE / SFX_HZ);   /* 22050/880 ~= 25 */
    if (period < 2) period = 2;
    for (i = 0; i < (int)sizeof(blip); ++i)
      blip[i] = ((i % period) < (period / 2)) ? 0xF0 : 0x10;
    gus_play_pcm8(blip, (Uint32)sizeof(blip), SFX_PXT_RATE, 1000);
    trace("gus sfx: real Polar Star unavailable -> square blip");
  }
  return 0;
}

/* Play a short arpeggio out the MPU-401 / WaveBlaster MIDI port while
 * servicing the SB16 ring. ESC aborts. Assumes device_open() ran. */
static int play_wb_arpeggio(void)
{
  int i;
  if (!g_wb_ok) return 1;

  /* T48 iter-8: opt-in voice-reset SysEx FIRST (before the program-change),
   * mirroring the engine MidiBackendWB on_song_start. Gated by the SAME hint
   * (SDL_HINT_DOSKUTSU_AUDIO_WB_VOICE_RESET = gm|gs|xg), default OFF. Witness to
   * the SETUP trace (NOT SDL_Log -- SETUP freopens stdio to NUL, so the SDL-side
   * banner never reaches the log; the WBS3 config + landed witnesses must come
   * through trace()). Records the bytes + the cap-hits delta ACROSS the reset
   * (the multi-byte SysEx is the worst case for the SAM2695 overrun, so its
   * cap-during-reset cost is the strongest log proxy that the bytes were paced
   * through, not dropped). */
  {
    const char *vr = SDL_GetHint("SDL_HINT_DOSKUTSU_AUDIO_WB_VOICE_RESET");
    const uint8_t *sx = NULL;
    int sxn = 0;
    const char *name = "none";
    if (vr && SDL_strcmp(vr, "gm") == 0)      { sx = WB_SYSEX_GM; sxn = (int)sizeof(WB_SYSEX_GM); name = "GM"; }
    else if (vr && SDL_strcmp(vr, "gs") == 0) { sx = WB_SYSEX_GS; sxn = (int)sizeof(WB_SYSEX_GS); name = "GS"; }
    else if (vr && SDL_strcmp(vr, "xg") == 0) { sx = WB_SYSEX_XG; sxn = (int)sizeof(WB_SYSEX_XG); name = "XG"; }
    if (sx)
    {
      uint32_t cap0 = doskutsu_mpu_drr_cap_hits;
      int b;
      for (b = 0; b < sxn; ++b)
        SDL_DOSMpu401WriteByte(sx[b]);   /* raw SysEx -- no 7-bit mask on framing bytes */
      trace("wb voice-reset: %s SysEx sent (%d bytes) cap_hits_during_reset=%lu "
            "(0=bytes paced clean; >0=poll capped mid-SysEx -> overrun risk)",
            name, sxn, (unsigned long)(doskutsu_mpu_drr_cap_hits - cap0));
    }
    else
    {
      trace("wb voice-reset: OFF (SDL_HINT_DOSKUTSU_AUDIO_WB_VOICE_RESET unset; "
            "WB test plays on the chip's power-up voice map)");
    }
  }

  /* Program change: select a wavetable patch on the test channel. */
  trace("wb arp: program change %d", WB_PROGRAM);
  SDL_DOSMpu401WriteByte((uint8_t)(0xC0 | WB_CHANNEL));
  SDL_DOSMpu401WriteByte((uint8_t)WB_PROGRAM);
  for (i = 0; i < ARP_COUNT; ++i)
  {
    trace("wb arp: note %d on", ARP_NOTES[i]);
    SDL_DOSMpu401WriteByte((uint8_t)(0x90 | WB_CHANNEL));     /* note on  */
    SDL_DOSMpu401WriteByte((uint8_t)ARP_NOTES[i]);
    SDL_DOSMpu401WriteByte((uint8_t)WB_VELOCITY);
    if (pump_service("wb", ARP_ON_MS, 0) == 27)
    {
      SDL_DOSMpu401WriteByte((uint8_t)(0x80 | WB_CHANNEL));   /* note off on abort */
      SDL_DOSMpu401WriteByte((uint8_t)ARP_NOTES[i]);
      SDL_DOSMpu401WriteByte(0x00);
      break;
    }
    SDL_DOSMpu401WriteByte((uint8_t)(0x80 | WB_CHANNEL));     /* note off */
    SDL_DOSMpu401WriteByte((uint8_t)ARP_NOTES[i]);
    SDL_DOSMpu401WriteByte(0x00);
    if (pump_service("wb", ARP_GAP_MS, 0) == 27) break;
  }
  return 0;
}

/* ---- T28 real title theme (SMF -> OPL3 voices / WB MPU-401 bytes) ---------*/

/* WaveBlaster sink: the scheduler dispatches events here; each forwards raw
 * MIDI bytes to the MPU-401 and the wavetable synthesizes. CRITICAL: these
 * only WRITE -- they NEVER read the MPU status register, so the blind-write
 * invariant (SDL/0093, load-bearing on real HW) holds for the title theme too. */
static void wb_cb_note_on(void *u, int ch, int note, int vel)
{
  (void)u;
  SDL_DOSMpu401WriteByte((uint8_t)(0x90 | (ch & 0x0F)));
  SDL_DOSMpu401WriteByte((uint8_t)(note & 0x7F));
  SDL_DOSMpu401WriteByte((uint8_t)(vel & 0x7F));
}
static void wb_cb_note_off(void *u, int ch, int note, int vel)
{
  (void)u;
  SDL_DOSMpu401WriteByte((uint8_t)(0x80 | (ch & 0x0F)));
  SDL_DOSMpu401WriteByte((uint8_t)(note & 0x7F));
  SDL_DOSMpu401WriteByte((uint8_t)(vel & 0x7F));
}
static void wb_cb_cc(void *u, int ch, int cc, int val)
{
  (void)u;
  SDL_DOSMpu401WriteByte((uint8_t)(0xB0 | (ch & 0x0F)));
  SDL_DOSMpu401WriteByte((uint8_t)(cc & 0x7F));
  SDL_DOSMpu401WriteByte((uint8_t)(val & 0x7F));
}
static void wb_cb_pc(void *u, int ch, int prog)
{
  (void)u;
  SDL_DOSMpu401WriteByte((uint8_t)(0xC0 | (ch & 0x0F)));
  SDL_DOSMpu401WriteByte((uint8_t)(prog & 0x7F));
}
static void wb_get_sink(cs_smf_sink *s)
{
  s->note_on = wb_cb_note_on; s->note_off = wb_cb_note_off;
  s->control_change = wb_cb_cc; s->program_change = wb_cb_pc; s->user = NULL;
}
/* All Notes Off (CC 123) + All Sound Off (CC 120) on all 16 channels. */
static void wb_all_notes_off(void)
{
  int ch;
  for (ch = 0; ch < 16; ++ch)
  {
    SDL_DOSMpu401WriteByte((uint8_t)(0xB0 | ch)); SDL_DOSMpu401WriteByte(120); SDL_DOSMpu401WriteByte(0x00);
    SDL_DOSMpu401WriteByte((uint8_t)(0xB0 | ch)); SDL_DOSMpu401WriteByte(123); SDL_DOSMpu401WriteByte(0x00);
  }
}

/* Parse the title theme once per session. Returns 1 if available. */
static int ensure_title_loaded(void)
{
  if (!g_title_tried)
  {
    g_title_tried = 1;
    g_title_smf = cs_smf_open(TITLE_MID);
    if (g_title_smf)
      trace("title: parsed %s (%lu events)", TITLE_MID,
            (unsigned long)cs_smf_event_count(g_title_smf));
    else
      trace("title: %s missing/unreadable -> arpeggio fallback", TITLE_MID);
  }
  return g_title_smf != NULL;
}

/* T42: BIOS 18.2065 Hz tick counter at 0040:006C -- a real-time reference
 * independent of uclock's PIT-channel-0 reprogramming (DJGPP's uclock chains
 * INT 8, so the BIOS tick keeps advancing at 18.2 Hz). Cross-checking
 * SDL_GetTicks (uclock-derived) against this reveals whether the ms clock the
 * MIDI scheduler reads tracks real time on this hardware -- the decisive
 * discriminator for the real-HW half-tempo report (DOSBox can't show it). */
static unsigned long bios_ticks(void)
{
  return _farpeekl(_dos_ds, 0x46C);
}

/* T56: CMOS/RTC time-of-day in seconds (minute*60 + second). The RTC is driven
 * by its OWN 32.768 kHz crystal, INDEPENDENT of the 8253 PIT (and therefore of
 * uclock/SDL_GetTicks AND the BIOS-tick path, which are both PIT-derived). So
 * this is the one real-time anchor that breaks the circularity: if the scheduler
 * position advances half as fast as the RTC, the timebase is provably half-rate
 * (works in DOSBox + on real HW). Reads CMOS regs 0x00 (sec) + 0x02 (min) BCD,
 * guarded against the update-in-progress flag (status-A bit 7). */
static unsigned cmos_read(unsigned reg)
{
  outportb(0x70, (unsigned char)reg);
  return inportb(0x71);
}
static unsigned long rtc_seconds(void)
{
  unsigned sec, min;
  int cap = 100000;
  while ((cmos_read(0x0A) & 0x80) && --cap) { /* wait out update-in-progress */ }
  sec = cmos_read(0x00);
  min = cmos_read(0x02);
  sec = (sec >> 4) * 10 + (sec & 0x0F);   /* BCD -> binary */
  min = (min >> 4) * 10 + (min & 0x0F);
  return (unsigned long)min * 60UL + sec; /* seconds within the hour */
}

/* T56 per-event dispatch witness: log the first dozen dispatched events with
 * their scheduled time (ev_us) vs the scheduler position (pos_us) so the iter
 * log shows on-schedule-ness per note on real HW (lag_us = pos-ev; small +
 * stable = on time). type 0=note_on 1=note_off 2=cc 3=program 4=tempo. */
static int g_disp_logged = 0;
static void disp_log(void *u, int type, uint64_t event_us, uint64_t position_us,
                     int ch, int d1, int d2)
{
  (void)u;
  if (g_disp_logged >= 12)
    return;
  g_disp_logged++;
  trace("[disp] #%d type=%d ch=%d d1=%d d2=%d ev_us=%lu pos_us=%lu lag_us=%ld",
        g_disp_logged, type, ch, d1, d2,
        (unsigned long)event_us, (unsigned long)position_us,
        (long)((long long)position_us - (long long)event_us));
}

/* Drive the parsed title theme through the configured synth backend for the
 * bounded window while servicing the SB16 ring + ticking the scheduler on the
 * main loop (exactly like the engine's non-ISR tick()). ESC / any key ends it.
 * Assumes device_open() ran + the backend chip is up. */

/* T56 route A: the SB16 IRQ-5 MIDI tick callback (SDL/0072). Invoked from IRQ
 * context at ~43 Hz; forwards the OpenDevice-relative now_ms to the ISR-safe
 * fixed-point scheduler tick. ISR-CONTEXT: no FPU/malloc/SDL/IO -- cs_smf_tick
 * _isr honors that, and the bound sinks (OPL3 integer F-num / WB blind MPU
 * writes) are ISR-safe. The data it reads (g_title_smf, *g_title_smf, the events
 * array) is DPMI-locked before this is registered; code residency relies on
 * CWSDPMI no-paging, matching the shipped engine's MidiScheduler (data-lock
 * only, no code-lock). */
static void setup_smf_isr_thunk(Uint64 now_ms)
{
  cs_smf_tick_isr(g_title_smf, now_ms);
}

static int play_title_smf(void)
{
  cs_smf_sink sink;
  Uint64      start;
  int         hb = 0;

  if (g_music_mode == MUS_OPL3)
  {
    int bp;
    if (!g_opl3_ok) return 1;
    cs_opl3midi_init();        /* idempotent -- chip already up via device_open */
    bp = cs_opl3midi_bank_programs();
    trace("title: opl3 bank = %s (%d programs)",
          bp > 0 ? "opl3bank.dat" : "8-patch placeholder", bp);
    cs_opl3midi_song_start();
    cs_opl3midi_get_sink(&sink);
  }
  else if (g_music_mode == MUS_OPL2)
  {
    int bp;
    if (!g_opl2_ok) return 1;
    cs_opl2midi_init();        /* T80: idempotent -- chip already up via device_open */
    bp = cs_opl2midi_bank_programs();
    trace("title: opl2 bank = %s (%d programs)",
          bp > 0 ? "opl3bank.dat" : "8-patch placeholder", bp);
    cs_opl2midi_song_start();
    cs_opl2midi_get_sink(&sink);
  }
  else /* MUS_WB */
  {
    if (!g_wb_ok) return 1;
    wb_get_sink(&sink);
  }

  /* T56: attach the per-event dispatch witness (the sink getters don't set it).
   * NOTE: cs_smf_tick_isr never calls .dispatch (no witness in the ISR), so this
   * only fires on the main-line fallback path -- harmless under route A. */
  sink.dispatch = disp_log;
  g_disp_logged = 0;
  cs_smf_set_sink(g_title_smf, &sink);

  /* T56 route A: when the SDL/0072 Lever-2b IRQ-5 tick is engaged AND the
   * scheduler state DPMI-locks cleanly, drive dispatch from the ISR at ~43 Hz
   * (the in-game cadence) instead of this cooperative loop's ~2-3 Hz. Lifecycle
   * (nx's cs_smf contract): open -> isr_lock -> start -> Register; the parse-time
   * events buffer is read-only during playback so no cli/sti is needed. Arm with
   * now_ms=0 so the first ISR tick reseeds last_tick from the OpenDevice-relative
   * clock the ISR feeds (a different epoch than SDL_GetTicks); song starts ~1
   * tick (~23 ms) late -- benign. On lock-fail, fall back to the main-line tick. */
  g_smf_isr_driven = 0;
  if (SDL_DOSMidiIsrTickActive() && cs_smf_isr_lock(g_title_smf))
  {
    _go32_dpmi_lock_data((void *)&g_title_smf, (unsigned long)sizeof(g_title_smf));
    cs_smf_start(g_title_smf, 0);
    SDL_DOSMidiTickRegister(setup_smf_isr_thunk);
    g_smf_isr_driven = 1;
    trace("title: route A -- ISR-driven dispatch ENGAGED (SDL/0072 ~43 Hz)");
  }
  else
  {
    cs_smf_start(g_title_smf, SDL_GetTicks());
    /* T74 witness: explicit route-B banner mirroring "route A ... ENGAGED" so the
     * iter-10 control cell (MIDI_ISR_TICK=0) objectively shows the killswitch took. */
    trace("title: route B -- ISR-tick DISABLED (%s); main-loop dispatch (~2-3 Hz cooperative)",
          SDL_DOSMidiIsrTickActive() ? "DPMI lock FAILED -- fell back" : "MIDI_ISR_TICK=0 / ISR inactive");
  }
  trace("title: start (mode=%d)", g_music_mode);

  start = SDL_GetTicks();
  /* T42 tempo witness: every ~1 s emit the SDL_GetTicks delta vs the BIOS-clock
   * delta (real ms) + events dispatched, so the real-HW log shows whether the
   * scheduler's ms clock tracks real time and at what event rate the music
   * actually plays. d_wall ~= d_real => clock OK; d_wall ~= d_real/2 => the ms
   * clock reads half (root cause); d_ev/d_real => effective tempo. */
  {
    const unsigned long sched_bios0 = bios_ticks();  /* T42: BIOSCLK scheduler epoch */
    const unsigned long w_rtc0 = rtc_seconds();      /* T56: PIT-independent real-time epoch */
    Uint64        w_wall = start;
    unsigned long w_bios = sched_bios0;
    uint64_t      w_ev   = cs_smf_dispatched(g_title_smf);
    Uint64        w_emit = start;
    int           w_hb   = hb;   /* T56: loop-iteration (== cs_smf_tick call) baseline */
    (void)w_wall; (void)w_bios; (void)w_ev; (void)w_emit; (void)w_hb;

  trace("title: scheduler clock = %s", g_midi_biosclk ? "BIOS-18.2Hz (A/B)" : "SDL_GetTicks");
  for (;;)
  {
    Uint64 elapsed;
    /* T42 A/B: default scheduler clock is SDL_GetTicks; the opt-in cell uses
     * the BIOS 18.2 Hz tick (real-time, independent of uclock) so we can tell
     * whether the ms clock reads half on this hardware. cs_smf catch-up makes
     * the coarse 55 ms BIOS step tempo-correct (bursty but right rate). */
    Uint64 sched_now = g_midi_biosclk
      ? (Uint64)(((uint64_t)(bios_ticks() - sched_bios0) * 5493ULL) / 100ULL)
      : SDL_GetTicks();
    /* T56 route A: under ISR dispatch the IRQ-5 tick owns the scheduler -- do NOT
     * also tick from the main loop (cs_smf contract: EITHER tick OR tick_isr,
     * never both). The pump below still runs so the SB16 DMA keeps generating
     * the IRQ-5 that drives the ISR tick. */
    if (!g_smf_isr_driven)
      cs_smf_tick(g_title_smf, sched_now);           /* dispatch due events (main-line fallback) */
    while (SDL_DOSAudioPump()) { /* top the SB16 ring */ }

    elapsed = SDL_GetTicks() - start;
    audiotest_progress((int)((elapsed * 1000) / (Uint64)TITLE_MS > 1000
                              ? 1000 : (elapsed * 1000) / (Uint64)TITLE_MS));

    {
      Uint64 now = SDL_GetTicks();
      if (now - w_emit >= 1000)
      {
        unsigned long bnow   = bios_ticks();
        uint64_t      evnow  = cs_smf_dispatched(g_title_smf);
        unsigned long d_bios = bnow - w_bios;               /* BIOS ticks (18.2 Hz) */
        unsigned long d_real = (unsigned long)((d_bios * 5493UL) / 100UL); /* -> ms */
        {
          unsigned long rtc = rtc_seconds();
          unsigned long rtc_ds = (rtc >= w_rtc0) ? (rtc - w_rtc0) : (rtc + 3600UL - w_rtc0);
          /* DECISIVE: pos (SDL-clock song position) vs rtc_ds (PIT-INDEPENDENT
           * real seconds). pos_ms ~= rtc_ds*1000 => clock real; pos_ms ~=
           * rtc_ds*500 (pos advances HALF as fast as the RTC) => timebase is
           * half-rate == the half-tempo root cause, proven independent of PIT. */
          trace("[tempo] d_wall=%lums d_real=%lums d_ev=%lu ticks=%d pos=%lums rtc_s=%lu upk1k=%lu tempo_us=%lu div=%u "
                "(pos_ms vs rtc_s*1000: equal=clock-real, pos~=half=HALF-RATE-TIMEBASE; "
                "upk1k ~=787916 ok)",
                (unsigned long)(now - w_wall), d_real,
                (unsigned long)(evnow - w_ev),
                hb - w_hb,
                (unsigned long)cs_smf_position_ms(g_title_smf),
                rtc_ds,
                (unsigned long)cs_smf_us_per_tick_x1000(g_title_smf),
                (unsigned long)cs_smf_tempo_us(g_title_smf),
                (unsigned)cs_smf_division(g_title_smf));
        }
        w_wall = now; w_bios = bnow; w_ev = evnow; w_emit = now; w_hb = hb;
      }
    }

    if ((++hb & 127) == 0)
      trace("title: hb=%d serviced (elapsed=%lums)", hb, (unsigned long)elapsed);

    if (kbhit())
    {
      int k = getch();
      if (k == 0 || k == 0xE0) { (void)getch(); }
      trace("title: key -> end");
      break;
    }
    if (elapsed >= (Uint64)TITLE_MS)
    {
      trace("title: window elapsed -> end");
      break;
    }
    SDL_Delay(5);   /* pace + cooperative-scheduler yield */
  }
  }  /* T42 witness scope */

  /* T56 route A: stop the ISR tick BEFORE silencing/draining so the IRQ-5 can't
   * dispatch into a scheduler we're about to quiesce (or, later, free). */
  if (g_smf_isr_driven)
  {
    SDL_DOSMidiTickRegister(NULL);
    g_smf_isr_driven = 0;
  }

  /* Silence whatever is still sounding before the device tears down. */
  if (g_music_mode == MUS_OPL3)      cs_opl3midi_all_notes_off();
  else if (g_music_mode == MUS_OPL2) cs_opl2midi_all_notes_off();
  else                               wb_all_notes_off();
  pump_service("title-drain", 60, 0);   /* brief serviced drain */
  return 0;
}

/* ---- T36 Organya-mode pre-rendered title snippet -------------------------*/

/* Load the title snippet from the engine's PCM cache once per session
 * (device-independent). Returns 1 if available. */
static int ensure_org_snippet_loaded(void)
{
  if (!g_org_tried)
  {
    g_org_tried = 1;
    g_org_pcm = cs_orgcache_load(ORG_TITLE_SONG,
                                 g_tier2 ? 11025 : 22050, g_tier2 ? 1 : 2,
                                 ORG_SNIPPET_MS, &g_org_samples, &g_org_rate, &g_org_ch);
    if (g_org_pcm)
      trace("org: loaded %s snippet (%lu samples, %d Hz, %d ch)",
            ORG_TITLE_SONG, (unsigned long)g_org_samples, g_org_rate, g_org_ch);
    else
      trace("org: no pre-rendered cache for %s -> tone fallback", ORG_TITLE_SONG);
  }
  return g_org_pcm != NULL;
}

/* Play the cached snippet once via the MIX raw-PCM path (the mixer resamples /
 * remixes the cache's rate+channels to the device). ESC-interruptible,
 * pump-serviced. Returns 1 if it played the real snippet, 0 to fall back. */
static int play_org_snippet(void)
{
  SDL_AudioSpec spec;
  MIX_Audio    *aud;
  int           dur_ms;

  if (!ensure_org_snippet_loaded())
    return 0;

  spec.format   = SDL_AUDIO_S16;
  spec.channels = g_org_ch;
  spec.freq     = g_org_rate;
  aud = MIX_LoadRawAudio(g_mixer, g_org_pcm,
                         (size_t)g_org_samples * sizeof(int16_t), &spec);
  if (!aud)
  {
    trace("org: MIX_LoadRawAudio FAILED: %s -> tone fallback", SDL_GetError());
    return 0;
  }
  MIX_SetTrackAudio(g_mus_trk, aud);
  /* MIX_PlayTrack's default options play once (loops 0) -- exactly what the
   * snippet wants, so no SetTrackLoops is needed (it would be a no-op pre-play). */
  if (!MIX_PlayTrack(g_mus_trk, 0))
  {
    trace("org: MIX_PlayTrack FAILED -> tone fallback");
    MIX_DestroyAudio(aud);
    MIX_SetTrackAudio(g_mus_trk, g_mus_aud);   /* rebind the tone audio */
    return 0;
  }

  /* snippet duration = samples / channels / rate (ms). */
  dur_ms = (int)(((Uint64)g_org_samples * 1000) / ((Uint64)g_org_rate * (Uint64)g_org_ch));
  trace("org: playing %d ms snippet", dur_ms);
  pump_service("org", dur_ms + 200, 0);   /* snippet + short tail; ESC aborts */
  MIX_StopTrack(g_mus_trk, 0);
  pump_service("org-drain", 60, 0);
  MIX_SetTrackAudio(g_mus_trk, g_mus_aud); /* rebind the tone audio before destroy */
  MIX_DestroyAudio(aud);
  return 1;
}

int audiotest_play_music(void)
{
  int rc;
  int use_smf;
  int use_org;
  if (!g_inited) return 1;
  trace("play_music: enter mode=%d", g_music_mode);
  if (device_open() != 0) return 1;

  /* T28: the real title theme plays when enabled, parsed, and the configured
   * synth chip is up (opl3/wb). T36: in Organya mode the real title theme
   * comes from the engine's pre-rendered PCM cache instead. */
  g_music_used_real = 0;
  use_smf = 0;
  use_org = 0;
  if (g_real_music && (g_music_mode == MUS_OPL3 || g_music_mode == MUS_OPL2 ||
                       g_music_mode == MUS_WB))
  {
    if (ensure_title_loaded())
      use_smf = (g_music_mode == MUS_OPL3) ? g_opl3_ok
              : (g_music_mode == MUS_OPL2) ? g_opl2_ok
              : g_wb_ok;
  }
  else if (g_real_music && g_is_organya)
  {
    use_org = ensure_org_snippet_loaded();
  }

  /* OVERALL span for the progress bar. */
  test_progress_begin(use_smf
                        ? TITLE_MS
                        : use_org
                            ? (ORG_SNIPPET_MS + 200)
                            : (g_music_mode == MUS_PCM
                                 ? 10000
                                 : ARP_COUNT * (ARP_ON_MS + ARP_GAP_MS)));

  if (use_smf)
  {
    g_music_used_real = 1;
    rc = play_title_smf();
  }
  else if (use_org && play_org_snippet())
  {
    g_music_used_real = 1;
    rc = 0;
  }
  else
  {
    switch (g_music_mode)
    {
      case MUS_OPL3: rc = play_opl3_arpeggio(); break;
      case MUS_OPL2: rc = play_opl2_arpeggio(); break;   /* T80 */
      case MUS_GUS:  rc = play_gus_arpeggio();  break;   /* A2 */
      case MUS_WB:   rc = play_wb_arpeggio();   break;
      default:
        trace("play_music: MIX_PlayTrack (pcm)");
        if (!MIX_PlayTrack(g_mus_trk, 0)) { trace("play_music: MIX_PlayTrack FAILED"); rc = 1; break; }
        /* Loop the tone for the test window. MIX_SetTrackLoops must be called
         * AFTER MIX_PlayTrack (it has no effect on a stopped track; PlayTrack's
         * default options reset loops to 0). Without this the tone played once
         * (~MUSIC_MS) then went silent for the rest of the window. */
        MIX_SetTrackLoops(g_mus_trk, -1);
        /* Loop until a key, capped at 10 s so a dead keyboard can't strand the
         * test with the device live. */
        pump_service("music", 10000, 1);
        MIX_StopTrack(g_mus_trk, 0);
        pump_service("music-drain", 80, 0);   /* brief serviced drain so the stop lands cleanly */
        rc = 0;
        break;
    }
  }

  audiotest_progress(1000);
  /* T28: record what actually played for the popup wording (set on success). */
  if (rc == 0)
  {
    if (g_music_used_real && g_is_organya)
      SDL_strlcpy(g_msg, "Real Title theme on Organya (pre-rendered).", sizeof(g_msg));
    else if (g_music_used_real)
      SDL_snprintf(g_msg, sizeof(g_msg),
        "Real Title theme (data/midi/curly.mid) via %s.",
        g_music_mode == MUS_OPL3 ? "OPL3"
        : g_music_mode == MUS_OPL2 ? "AdLib (OPL2)"   /* T80 */
        : "WaveBlaster");
    else if (g_music_mode == MUS_GUS)
      SDL_strlcpy(g_msg, "Note arpeggio on the Gravis UltraSound (GF1).",
                  sizeof(g_msg));
    else if (g_music_mode == MUS_OPL3 || g_music_mode == MUS_OPL2 ||
             g_music_mode == MUS_WB)
      SDL_strlcpy(g_msg, "Test arpeggio -- Cave Story MIDI data not found "
                         "(data/midi/curly.mid).", sizeof(g_msg));
    else if (g_is_organya)
      SDL_strlcpy(g_msg, "Test tone -- no pre-rendered Organya music found.",
                  sizeof(g_msg));
  }
  device_close();
  trace("play_music: done rc=%d (real=%d)", rc, g_music_used_real);
  return rc;
}

/* Round-6 item 6/8: per-phase ABOUT text for the chooser, resolved to what
 * will actually play. The data probes (ensure_sfx_render / ensure_title_loaded)
 * are device-independent + cached + idempotent, so calling them here is cheap
 * and the result is reused by the actual play. Each string is <= ~110 chars
 * and uses no ';' (round-6 punctuation rule). */
const char *audiotest_about(int phase)
{
  if (phase == 0)   /* Sound Effects test */
  {
    if (g_music_mode == MUS_GUS)
    {
      if (g_real_sfx)
      {
        ensure_sfx_render();
        if (g_sfx_pcm)
          return "Plays Polar Star sound effect on the Gravis GF1";
        return "Plays a test blip on the Gravis GF1 - game data not found";
      }
      return "Plays a test blip on the Gravis GF1";
    }
    if (g_real_sfx)
    {
      ensure_sfx_render();
      if (g_sfx_pcm)
        return "Plays Polar Star sound effect";
      return "Plays a test sound - game data not found";
    }
    return "Plays a test sound";
  }

  /* A2: Gravis GF1 -- the music test is a native wavetable arpeggio (no GM .pat
   * MIDI synth in SETUP; that is a later task). */
  if (g_music_mode == MUS_GUS)
    return "Plays a note arpeggio on the Gravis UltraSound (GF1)";

  /* Music test. Real title theme only for the synth backends; organya/pcm and
   * the killswitch-off / data-missing cases describe the tone/tune fallback. */
  if (g_music_mode == MUS_OPL3 || g_music_mode == MUS_OPL2 ||
      g_music_mode == MUS_WB)
  {
    if (g_real_music && ensure_title_loaded())
      return (g_music_mode == MUS_OPL3) ? "Plays Title Theme Music on OPL3"
           : (g_music_mode == MUS_OPL2) ? "Plays Title Theme Music on AdLib"  /* T80 */
           : "Plays Title Theme Music on WaveBlaster";
    if (g_real_music)
      return "Plays a test tone - game data not found";
    return "Plays a test tone";
  }
  /* T36: Organya mode -- the real title theme from the engine's pre-rendered
   * PCM cache, else a tone that points the operator at how to populate it.
   * (Period-free to match the operator's round-8 item-8 wording across the box.) */
  if (g_is_organya)
  {
    if (g_real_music && ensure_org_snippet_loaded())
      return "Plays Title Theme Music on Organya (pre-rendered)";
    return "Plays a test tone - no pre-rendered music found (enable Organya "
           "pre-render and run the game once)";
  }
  return "Plays a test tone";   /* pcm / auto */
}

void audiotest_stop_music(void)
{
  /* T14: each test is bounded and self-tearing-down (device_close at the end
   * of every play), so nothing is normally streaming here. Close defensively
   * in case a caller reaches this while a device is somehow still open. */
  if (g_dev_open) device_close();
}

void audiotest_shutdown(void)
{
  trace("shutdown: begin");
  device_close();
  /* T28: free the cached Pixtone render (device_close already released the
   * per-device MIX_Audio that borrowed-by-copy from it). */
  if (g_sfx_pcm) { free(g_sfx_pcm); g_sfx_pcm = NULL; g_sfx_pcm_len = 0; }
  g_sfx_render_tried = 0;
  /* T56 route A: defensive deregister in case play_title_smf was interrupted
   * before its own deregister (device_close above already stopped the SB16 IRQ,
   * so the cb can't fire here, but never free a scheduler the ISR could hold). */
  SDL_DOSMidiTickRegister(NULL);
  g_smf_isr_driven = 0;
  /* T28: free the cached title-theme event list. */
  if (g_title_smf) { cs_smf_close(g_title_smf); g_title_smf = NULL; }
  g_title_tried = 0;
  /* T36: free the cached Organya snippet PCM. */
  if (g_org_pcm) { free(g_org_pcm); g_org_pcm = NULL; }
  g_org_samples = 0; g_org_tried = 0;
  /* T80/A2: the AdLib (OPL2) + Gravis (GF1) paths inited SDL with SDL_Init(0) --
   * no audio subsystem and no MIX_Init -- so do NOT MIX_Quit /
   * SDL_QuitSubSystem(AUDIO) (there is nothing to quit). SDL_Quit() tears down
   * the base library on every path. */
  if (g_music_mode != MUS_OPL2 && g_music_mode != MUS_GUS)
  {
    MIX_Quit();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }
  SDL_Quit();
  SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);   /* T24: restore default verbosity */
  g_inited = 0;
  trace("shutdown: SDL torn down");
  trace_close();
}

#endif /* AUDIOTEST_LINKED */
