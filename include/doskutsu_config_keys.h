#ifndef DOSKUTSU_CONFIG_KEYS_H
#define DOSKUTSU_CONFIG_KEYS_H

/*
 * doskutsu_config_keys.h -- single source of truth for the user-facing
 * runtime settings that SETUP.EXE writes to DOSKUTSU.CFG and that
 * DOSKUTSU.EXE loads at startup (via the config-file setenv shim,
 * patch nxengine-evo/0216).
 *
 * Each entry maps a short, human-readable key as written in DOSKUTSU.CFG
 * (cfg_key) to the EXACT environment / SDL-hint name the engine already
 * reads (env_name). The engine reads these via getenv / SDL_getenv /
 * SDL_GetHint; in every case SDL_GetHint falls back to the env var of the
 * same name, so the shim can satisfy ALL of them with a single setenv of
 * env_name. The config file is therefore a NEW SOURCE for the SAME keys --
 * no existing lever changes.
 *
 * Precedence is env > file > built-in default: the shim uses
 * setenv(env_name, value, 0) (overwrite=0), so a real DOS `SET` still wins.
 *
 * The ~110 diagnostic / instrumentation / TAS / warp env vars are
 * DELIBERATELY not listed here -- only options a player should touch.
 *
 * ASCII-only; compiles as both C and C++ (shared by the engine and by the
 * standalone SETUP.EXE, which is C).
 */

#include <stddef.h> /* NULL */

#ifdef __cplusplus
extern "C" {
#endif

/* The header-only helpers below are used by some including TUs and not
 * others; suppress -Wunused-function where the compiler supports it. */
#if defined(__GNUC__)
#define DKT_MAYBE_UNUSED __attribute__((unused))
#else
#define DKT_MAYBE_UNUSED
#endif

typedef enum
{
  DKT_BOOL = 0, /* "0" / "1"                          */
  DKT_INT,      /* integer in [imin, imax]            */
  DKT_ENUM,     /* one of the strings in enum_vals[]  */
  DKT_STR       /* free-form string (e.g. BLASTER);   *
                 * SETUP composes/validates it, the   *
                 * loader passes it through verbatim   */
} dkt_type_t;

typedef enum
{
  DKC_SOUND = 0,
  DKC_INPUT,
  DKC_PERF,
  DKC_COMPAT,
  DKC_HARDWARE  /* sound-hardware (BLASTER) -- edited by SETUP's dedicated
                 * Sound Hardware screen, NOT the generic category editor */
} dkt_category_t;

/* Bit flags for dkt_key_t.flags. */
#define DKT_F_PRESENCE      0x1u /* engine PRESENCE-tests env_name (getenv()
                                  * != NULL is ON regardless of value); the
                                  * loader must NOT setenv a "0"/empty value
                                  * or it would wrongly enable the feature. */
#define DKT_F_AUTHORITATIVE 0x2u /* loader setenv's with overwrite=1 so the
                                  * config value WINS over an ambient env/
                                  * AUTOEXEC SET (file > env). Used for
                                  * BLASTER per operator decision: SETUP's
                                  * sound-hardware choice is authoritative.
                                  * All tuning keys leave this unset and
                                  * stay env > file (a real SET still wins). */

typedef struct
{
  const char    *cfg_key;   /* short key written in DOSKUTSU.CFG          */
  const char    *env_name;  /* exact env / hint name the engine reads     */
  dkt_type_t      type;
  dkt_category_t  category;
  const char    *def;       /* production default value (string)          */
  int             imin;     /* DKT_INT lower bound                        */
  int             imax;     /* DKT_INT upper bound                        */
  const char    **enum_vals;/* NULL-terminated; DKT_ENUM only             */
  const char    *label;     /* short UI label                            */
  const char    *help;      /* one-line help string                      */
  unsigned        flags;    /* bitmask of DKT_F_* (presence / authoritative);
                             * 0 = normal value-checked, env > file key.
                             * The loader (doskutsu_config.h) honors these. */
} dkt_key_t;

/* Enum value tables. "auto" means "let the engine auto-detect" -- SETUP
 * omits the key from DOSKUTSU.CFG in that case so the auto-detect chain
 * runs unchanged. */
static const char *dkt_backend_vals[] =
  { "auto", "wb", "opl3", "organya", "adlib", "gus", "none", NULL };

/* SETUP-side discriminator for the two cards that BOTH map to AUDIO_BACKEND=wb
 * (the engine sees only `wb` + the MPU-401 port and behaves identically): a
 * "General MIDI" external module vs a "WaveBlaster" daughterboard. SETUP keys
 * the Music-card label/picker off this when backend==wb. The engine ignores
 * MIDI_DEV entirely (#9). */
static const char *dkt_midi_dev_vals[] =
  { "genmidi", "waveblaster", NULL };

/* GUS active-voice presets. The native GF1 backend's DAC output rate is
 * 617400/voices, so the voice count IS the music sample rate: 14=44100Hz
 * (highest fidelity), 16=38587, 20=30870 (the default -- best balance of
 * fidelity and polyphony), 24=25725, 28=22050 (the PicoGUS firmware-rescale
 * silence -- 28ch is forced to 44.1k, hence SILENT and not the default), 32=19293.
 * (Rates are the integer-truncated 617400/voices the GF1 driver computes.)
 * The SDL3-DOS GF1 driver clamps the hint to [14,32]; these 6 are the
 * curated presets SETUP offers (#39). */
static const char *dkt_gus_voice_vals[] =
  { "14", "16", "20", "24", "28", "32", NULL };

/* System Speed preset classes (plan 3.4). "notset" is the provenance sentinel
 * shown as "(not set)" until a class is chosen or Auto-detect runs. */
static const char *dkt_speed_vals[] =
  { "notset", "slow", "normal", "fast", "veryfast", NULL };

static const dkt_key_t DKT_KEYS[] =
{
  /* ---- Sound -------------------------------------------------------- */
  { "AUDIO_BACKEND", "SDL_HINT_DOSKUTSU_AUDIO_BACKEND",
    DKT_ENUM, DKC_SOUND, "auto", 0, 0, dkt_backend_vals,
    "Music backend",
    "auto=detect, wb=WaveBlaster, opl3=FM synth, organya=Organya, adlib=OPL2 FM, gus=Gravis Ultrasound, none=no music", 0 },

  { "AUDIO_OFF", "DOSKUTSU_NO_AUDIO",
    DKT_BOOL, DKC_SOUND, "0", 0, 0, NULL,
    "Sound disabled",
    "1 disables all audio (no SFX, no music)", DKT_F_PRESENCE /* engine
      reads getenv("DOSKUTSU_NO_AUDIO") for mere presence, so AUDIO_OFF=0
      must NOT setenv it (would disable audio) */ },

  { "AUDIO_TIER2", "SDL_HINT_DOSKUTSU_AUDIO_TIER2",
    DKT_BOOL, DKC_SOUND, "1", 0, 0, NULL,
    "Audio quality",
    "1=11025 Hz mono master rate (default); 0=legacy higher rate", 0 },

  { "SB16_VOICE_VOL", "SDL_HINT_DOSKUTSU_SB16_VOICE_VOL",
    DKT_INT, DKC_SOUND, "28", 0, 31, NULL,
    "SB16 voice volume",
    "SB16 mixer voice (PCM/SFX) level, 0-31", 0 },

  { "SB16_FM_VOL", "SDL_HINT_DOSKUTSU_SB16_FM_VOL",
    DKT_INT, DKC_SOUND, "28", 0, 31, NULL,
    "SB16 FM volume",
    "SB16 mixer FM (OPL3 music) level, 0-31", 0 },

  { "ORG_PRERENDER", "SDL_HINT_DOSKUTSU_ORG_PRERENDER",
    DKT_BOOL, DKC_SOUND, "1", 0, 0, NULL,
    "Organya pre-render",
    "1 pre-renders Organya music to a disk PCM cache (less demanding on CPU at playback)", 0 },

  /* ---- Input -------------------------------------------------------- */
  { "USE_JOYSTICK", "DOSKUTSU_USE_JOYSTICK",
    DKT_BOOL, DKC_INPUT, "0", 0, 0, NULL,
    "Joystick / gamepad",
    "1 enables the gameport (costs ~80 ms/frame BIOS poll if no stick attached)", 0 },

  /* ---- Performance / display --------------------------------------- */
  { "PERF_MODE", "SDL_HINT_DOSKUTSU_PERF_MODE",
    DKT_INT, DKC_PERF, "0", 0, 2, NULL,
    "Performance mode",
    "0=faithful, 1=smooth (drop decorative detail), 2=fast", 0 },

  { "FIXED_TIMESTEP", "SDL_HINT_DOSKUTSU_FIXED_TIMESTEP",
    DKT_BOOL, DKC_PERF, "1", 0, 0, NULL,
    "Fixed 50 Hz timestep",
    "1=game runs at authored 50 Hz regardless of render fps (default)", 0 },

  /* ---- Compatibility / troubleshooting (Advanced submenu) ---------- */
  { "AUDIO_WB_DIRECT_PORT", "SDL_HINT_DOSKUTSU_AUDIO_WB_DIRECT_PORT",
    DKT_BOOL, DKC_COMPAT, "1", 0, 0, NULL,
    "WaveBlaster direct port",
    "1=direct MPU-401 port writes (default); 0=DSP-mediated fallback", 0 },

  { "DIRTY_RECTS", "SDL_HINT_DOSKUTSU_DIRTY_RECTS",
    DKT_BOOL, DKC_COMPAT, "1", 0, 0, NULL,
    "Dirty-rect rendering",
    "0 force-disables dirty-rect rendering (only if you see artifacts)", 0 },

  { "PIXEL_FORMAT_8", "SDL_HINT_DOSKUTSU_PIXEL_FORMAT_8",
    DKT_BOOL, DKC_COMPAT, "1", 0, 0, NULL,
    "8bpp indexed mode",
    "0 force-disables indexed 8bpp mode (only if you see color regressions)", 0 },

  { "FORCE_PUMP_YIELD", "SDL_HINT_DOSKUTSU_FORCE_PUMP_YIELD",
    DKT_BOOL, DKC_COMPAT, "0", 0, 0, NULL,
    "Force per-pump yield",
    "1 restores the original per-pump cooperative yield (only if audio stutters)", 0 },

  { "THRASH_FULLCOVER", "SDL_HINT_DOSKUTSU_THRASH_FULLCOVER",
    DKT_BOOL, DKC_COMPAT, "1", 0, 0, NULL,
    "Backdrop full-cover",
    "1=fixed backdrop coverage on motion (default); 0=legacy clip", 0 },

  /* ---- Sound hardware (BLASTER) ------------------------------------ */
  /* The whole sound-hardware selection (base port / IRQ / 8-bit DMA /
   * 16-bit DMA / MPU-401 MIDI port / card type) rides on the one standard
   * BLASTER variable the SDL3-DOS backend already parses at init
   * (A/I/D/H/P/T fields; P = MPU-401 MIDI port). SETUP's Sound Hardware
   * screen composes this string from its fields; the loader passes it
   * through. AUTHORITATIVE (overwrite=1): a SETUP-written BLASTER overrides
   * an ambient AUTOEXEC `SET BLASTER` (operator decision: the setup config
   * is the authoritative source, BLASTER is merely the auto-detect default).
   * Default "" -> SETUP omits the line so the ambient SET BLASTER is used. */
  { "BLASTER", "BLASTER",
    DKT_STR, DKC_HARDWARE, "", 0, 0, NULL,
    "Sound hardware",
    "SB base/IRQ/DMA/MIDI-port (A/I/D/H/P/T); SETUP's value overrides AUTOEXEC SET BLASTER",
    DKT_F_AUTHORITATIVE },

  /* ---- SETUP-only (engine-IGNORED) --------------------------------- */
  /* APPEND-ONLY: new keys go at the END of this table. SPEED_CLASS records
   * which System Speed preset the user last applied (plan 3.4). It is a
   * SETUP-only provenance string: env_name is NULL, so the engine loader
   * (doskutsu_config.h) SKIPS it (never setenv -- a NULL name would also be a
   * crash). The preset's real effect is written through the existing
   * PERF_MODE / FIXED_TIMESTEP / DIRTY_RECTS / PIXEL_FORMAT_8 keys; this only
   * remembers the label for the profile panel + the System Speed screen's
   * "(current)" tag. "notset" renders as "(not set)" until a class is chosen
   * or Auto-detect runs. */
  { "SPEED_CLASS", NULL,
    DKT_ENUM, DKC_PERF, "notset", 0, 0, dkt_speed_vals,
    "Speed class",
    "SETUP-only record of the chosen System Speed preset; ignored by the engine", 0 },

  /* MIDI music-set choice (backlog #39). env_name is the hint the engine reads
   * once at init to pick the MIDI source set (SoundManager.cpp:545-640). The
   * engine maps a CLOSED SET of LOGICAL values, NOT directory names:
   *   "" / "wiimidi" -> data/midi/   (WiiWare arrangements; the default)
   *   "orgmid"       -> data/orgmid/ (ORGMID note-for-note transcription)
   *   anything else  -> data/midi/   (unrecognized -> fallback + a warn log)
   * Default is "wiimidi" -- the byte-neutral value (== the engine's no-hint
   * behavior, data/midi/) that does NOT trip the unrecognized-fallback warning
   * a literal "midi" would. DKT_STR (the value space is logical, not a free
   * directory). SETUP's Music screen offers only the known sets actually
   * present on disk (setup/midiset.c); the orgmid1/orgmid2 GM_VARIANT dev sets
   * are deliberately NOT exposed (Q-A2). Only meaningful for a MIDI backend
   * (wb / opl3); Organya ignores it. */
  { "MIDI_SET", "SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE",
    DKT_STR, DKC_SOUND, "wiimidi", 0, 0, NULL,
    "MIDI music set",
    "Which MIDI music set the MIDI backend plays: wiimidi (WiiWare) or orgmid (ORGMID)", 0 },

  /* ---- Input bindings (Phase 3 / #40 -- patch nxengine-evo/0227) ----------
   * Per-action keyboard / gameport-button remap. The engine's BIND_* loader
   * (input.cpp input_apply_cfg_bindings) reads each DOSKUTSU_BIND_<ACTION> env
   * var AFTER settings_load and overlays it onto the live mappings, so a
   * SETUP-written binding WINS over settings.dat. Value grammar:
   * "k:<sdlkeycode>[,b:<jbut>]" (k = SDL3 keycode, b = optional gameport button
   * index). DKT_STR: SETUP composes / validates the string (a scancode->SDL
   * keycode table lives in SETUP); the loader passes it through verbatim.
   * Default "" -> SETUP omits the line so the action keeps its settings.dat /
   * built-in default binding (killswitch: absent all BIND_* == today's
   * controls). The 11 remappable player actions match input.h INPUTS
   * LEFTKEY..MAPSYSTEMKEY, in that order. APPEND-ONLY: these sit at the end of
   * the table so existing positional indices are unchanged. */
  { "BIND_LEFT", "DOSKUTSU_BIND_LEFT",
    DKT_STR, DKC_INPUT, "", 0, 0, NULL,
    "Move Left", "Key/button bound to Move Left (k:<sdlkeycode>[,b:<jbut>])", 0 },
  { "BIND_RIGHT", "DOSKUTSU_BIND_RIGHT",
    DKT_STR, DKC_INPUT, "", 0, 0, NULL,
    "Move Right", "Key/button bound to Move Right (k:<sdlkeycode>[,b:<jbut>])", 0 },
  { "BIND_UP", "DOSKUTSU_BIND_UP",
    DKT_STR, DKC_INPUT, "", 0, 0, NULL,
    "Look Up", "Key/button bound to Up (k:<sdlkeycode>[,b:<jbut>])", 0 },
  { "BIND_DOWN", "DOSKUTSU_BIND_DOWN",
    DKT_STR, DKC_INPUT, "", 0, 0, NULL,
    "Crouch / Down", "Key/button bound to Down (k:<sdlkeycode>[,b:<jbut>])", 0 },
  { "BIND_JUMP", "DOSKUTSU_BIND_JUMP",
    DKT_STR, DKC_INPUT, "", 0, 0, NULL,
    "Jump", "Key/button bound to Jump (k:<sdlkeycode>[,b:<jbut>])", 0 },
  { "BIND_FIRE", "DOSKUTSU_BIND_FIRE",
    DKT_STR, DKC_INPUT, "", 0, 0, NULL,
    "Fire", "Key/button bound to Fire (k:<sdlkeycode>[,b:<jbut>])", 0 },
  { "BIND_STRAFE", "DOSKUTSU_BIND_STRAFE",
    DKT_STR, DKC_INPUT, "", 0, 0, NULL,
    "Strafe", "Key/button bound to Strafe (k:<sdlkeycode>[,b:<jbut>])", 0 },
  { "BIND_PREVWPN", "DOSKUTSU_BIND_PREVWPN",
    DKT_STR, DKC_INPUT, "", 0, 0, NULL,
    "Prev Weapon", "Key/button bound to Previous Weapon (k:<sdlkeycode>[,b:<jbut>])", 0 },
  { "BIND_NEXTWPN", "DOSKUTSU_BIND_NEXTWPN",
    DKT_STR, DKC_INPUT, "", 0, 0, NULL,
    "Next Weapon", "Key/button bound to Next Weapon (k:<sdlkeycode>[,b:<jbut>])", 0 },
  { "BIND_INVENTORY", "DOSKUTSU_BIND_INVENTORY",
    DKT_STR, DKC_INPUT, "", 0, 0, NULL,
    "Inventory", "Key/button bound to Inventory (k:<sdlkeycode>[,b:<jbut>])", 0 },
  { "BIND_MAP", "DOSKUTSU_BIND_MAP",
    DKT_STR, DKC_INPUT, "", 0, 0, NULL,
    "Map", "Key/button bound to Map System (k:<sdlkeycode>[,b:<jbut>])", 0 },

  /* Stored gameport joystick calibration (Phase 3 / #40). Consumed by the
   * SDL3-DOS backend, NOT the engine: input-sdl's SDL patch reads
   * SDL_HINT_DOSKUTSU_JOY_CAL via SDL_GetHint (which falls back to the env var
   * the shim sets) at joystick init, so the player needn't re-swirl the stick
   * each run. Value grammar (team-lead decision -- 6-value explicit per-axis
   * centre, because a spring-return flightstick's electrical centre is often
   * not the geometric midpoint): "xmin,xcenter,xmax,ymin,ycenter,ymax". The
   * SDL3-DOS backend parses it (SDL_sscanf "%d,%d,%d,%d,%d,%d"); the engine
   * never touches it. DKT_STR; SETUP's 3-step Calibrate screen composes it.
   * Default "" -> SETUP omits the line -> SDL uses its built-in / auto
   * calibration (behavior-neutral). RESERVED here so the SDL patch has a
   * registered key without editing this table (engine-side owns the registry;
   * see feat/input-remap contract). */
  { "JOY_CAL", "SDL_HINT_DOSKUTSU_JOY_CAL",
    DKT_STR, DKC_INPUT, "", 0, 0, NULL,
    "Joystick calibration",
    "Stored gameport calibration xmin,xcenter,xmax,ymin,ycenter,ymax (set by SETUP Calibrate)", 0 },

  /* Invert the gameport Y axis (Phase 3 / #40 -- patch nxengine-evo/0229). A
   * spring-return flightstick's pitch axis frequently reads opposite the
   * platformer convention (push-forward = climb vs dive), so this lets the
   * player flip UP <-> DOWN without re-wiring. Consumed by the engine BIND_*
   * loader (input.cpp input_apply_cfg_bindings): AFTER settings_load it negates
   * the axis1 (Y) sign on the live UP/DOWN mappings, so it wins over
   * settings.dat like the BIND_* overlay. Default "0" (no inversion). Plain
   * value-checked bool (loader setenv's "0"/"1"; engine tests == "1") -- NOT a
   * presence key, so "0" correctly means off. Only meaningful with a joystick;
   * the X axis is intentionally not invertible (left/right rarely needs it). */
  { "JOY_INVERT_Y", "DOSKUTSU_JOY_INVERT_Y",
    DKT_BOOL, DKC_INPUT, "0", 0, 0, NULL,
    "Invert joystick Y",
    "1 swaps the gameport stick up/down (for flightsticks with inverted pitch)", 0 },

  /* Gravis Ultrasound active-voice count (#39 -- patch SDL/0112 GF1 backend).
   * env_name is the hint the SDL3-DOS GF1 driver reads ONCE at device open
   * (gus_resolve_voices, SDL_dosaudio_gus.c): the GF1 DAC output rate is
   * 617400/voices, so fewer voices = a higher (better-fidelity) sample rate.
   * The driver clamps to [14,32]; SETUP offers the curated presets in
   * dkt_gus_voice_vals (14=44100Hz highest fidelity .. 32=19293Hz). Default
   * "20" (30870 Hz) is the g2k-validated best-sounding balance of fidelity and
   * polyphony; it also avoids the 28-voice/22050Hz PicoGUS firmware-rescale
   * silence (28ch is forced to 44.1k). SETUP always writes this key (DKT_ENUM with no
   * "auto" omission), so the cfg value reaches the driver verbatim -- the
   * driver's own GUS_DEF_VOICES fallback only applies with no cfg/env at all.
   * Only meaningful for AUDIO_BACKEND=gus; other backends ignore the hint (the
   * driver only reads it when the GF1 device opens). APPEND-ONLY: sits at the
   * end so existing positional indices are unchanged. */
  { "GUS_VOICES", "SDL_HINT_DOSKUTSU_GUS_VOICES",
    DKT_ENUM, DKC_SOUND, "20", 0, 0, dkt_gus_voice_vals,
    "GUS voices",
    "Gravis Ultrasound active voices; rate=617400/voices (20=30870Hz default, 14=44100Hz best, 28=22050Hz may be silent)", 0 },

  /* ---- Sound-redesign keys (#9 -- dual Music-card + SFX-device pickers) ----
   * SETUP's Sound flow encodes the user's two device picks as INTENT keys:
   *   - AUDIO_BACKEND=none  -> "No Music"  (the music dispatch is off)
   *   - SFX_DEVICE=none     -> "No Sound FX" (the SFX dispatch is off)
   * Both are the clearest single-key encodings of the engine's independent
   * music/SFX on-off (patches nxengine-evo 0240/0241). SFX_DEVICE is a DKT_STR
   * with default "" so SETUP OMITS the line when SFX rides the music card's
   * native DAC (engine derives it -- SB DAC for an SB-family card, GF1 for the
   * Gravis card); only an explicit "No Sound FX" writes SFX_DEVICE=none. The
   * engine strict-matches the literal "none" (SoundManager dispatch gate). */
  { "SFX_DEVICE", "SDL_HINT_DOSKUTSU_SFX_DEVICE",
    DKT_STR, DKC_SOUND, "", 0, 0, NULL,
    "Sound FX device",
    "none = no sound effects; unset = effects ride the music card's native DAC", 0 },

  /* MUSIC_OFF / SFX_OFF: engine-internal equivalents of the two intent keys
   * above (SoundManager accepts either encoding). SETUP does NOT toggle these
   * through the UI -- it writes the AUDIO_BACKEND=none / SFX_DEVICE=none intent
   * instead, to avoid double-encoding one state. They are REGISTERED so a
   * hand-edited DOSKUTSU.CFG with MUSIC_OFF=1 / SFX_OFF=1 still reaches the
   * engine through the loader. VALUE-checked, not presence: the engine (patch
   * nxengine-evo 0240) strict-matches "1" via SDL_GetHint (_mo[0]=='1' &&
   * _mo[1]=='\0'), so a setenv'd "0" correctly reads as OFF (music/SFX stay on).
   * Default "0". APPEND-ONLY: at the end so positional indices are unchanged. */
  { "MUSIC_OFF", "SDL_HINT_DOSKUTSU_MUSIC_OFF",
    DKT_BOOL, DKC_SOUND, "0", 0, 0, NULL,
    "Music disabled",
    "1 disables music only (sound effects keep playing); alt to AUDIO_BACKEND=none", 0 },

  { "SFX_OFF", "SDL_HINT_DOSKUTSU_SFX_OFF",
    DKT_BOOL, DKC_SOUND, "0", 0, 0, NULL,
    "Sound FX disabled",
    "1 disables sound effects only (music keeps playing); alt to SFX_DEVICE=none", 0 },

  /* Gravis Ultrasound high-fidelity (multi-sample) mixing (#9 / #12 -- GF1 MIDI
   * bank rendering; engine patch nxengine-evo 0255). When ON the GF1 backend
   * uploads the full multi-sample .pat set per instrument for best fidelity +
   * polyphony; OFF falls back to a single nearest-middle-C sample (the low-DRAM
   * state). Only meaningful for AUDIO_BACKEND=gus. Confirmed with pat-bank:
   * cfg_key GUS_HIFI -> SDL hint SDL_HINT_DOSKUTSU_GUS_MULTISAMPLE, read once by
   * MidiBackendGus's ctor via SDL_GetHint. POLARITY: the engine DEFAULTS ON --
   * it strict-matches "0" as the killswitch; unset OR any non-"0" value = ON.
   * So this registry default is "1" (matching the engine default): SETUP writes
   * GUS_HIFI=1 (hint "1" -> on), and only an explicit Off writes GUS_HIFI=0
   * (hint "0" -> the single-sample fallback). The loader passthru SETs the full
   * hint name from this key (same shim as GUS_VOICES). */
  { "GUS_HIFI", "SDL_HINT_DOSKUTSU_GUS_MULTISAMPLE",
    DKT_BOOL, DKC_SOUND, "1", 0, 0, NULL,
    "GUS high fidelity",
    "1 = full multi-sample fidelity (default); 0 = single-sample low-memory fallback", 0 },

  /* MIDI device discriminator for the AUDIO_BACKEND=wb path (#9). SETUP-ONLY:
   * the two Music-card rows "General MIDI" and "WaveBlaster" both write
   * AUDIO_BACKEND=wb (identical engine behavior -- MPU-401 out); this key only
   * tells SETUP which label/picker state to show and which MPU-401 default to
   * suggest. The engine never reads it (the hint name exists only so the
   * loader's generic passthru has somewhere to put it; SDL ignores an unknown
   * hint). genmidi = external General MIDI module (operator sets the MPU port);
   * waveblaster = daughterboard on the SB header (default MPU port 0x330).
   * Default "waveblaster" preserves the historical meaning of a bare wb config.
   * APPEND-ONLY: at the end so positional indices are unchanged. */
  { "MIDI_DEV", "SDL_HINT_DOSKUTSU_MIDI_DEV",
    DKT_ENUM, DKC_SOUND, "waveblaster", 0, 0, dkt_midi_dev_vals,
    "MIDI device",
    "SETUP-only (AUDIO_BACKEND=wb): genmidi=external GM module via MPU-401, waveblaster=daughterboard on the SB header", 0 },
};

#define DKT_KEY_COUNT ((int)(sizeof(DKT_KEYS) / sizeof(DKT_KEYS[0])))

/* Case-insensitive ASCII compare of a NUL-terminated key against a
 * possibly-not-NUL-terminated token of length n. Returns 1 on match. */
static DKT_MAYBE_UNUSED int dkt_key_ieq(const char *key, const char *tok, int n)
{
  int i;
  for (i = 0; i < n; ++i)
  {
    char a = key[i];
    char b = tok[i];
    if (a == '\0') return 0;
    if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
    if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
    if (a != b) return 0;
  }
  return key[n] == '\0';
}

/* Look up a registry entry by its cfg_key (case-insensitive). tok is the
 * key token from the config line; n is its length. Returns NULL if the
 * key is not a recognized user-facing setting. */
static DKT_MAYBE_UNUSED const dkt_key_t *dkt_lookup(const char *tok, int n)
{
  int i;
  for (i = 0; i < DKT_KEY_COUNT; ++i)
    if (dkt_key_ieq(DKT_KEYS[i].cfg_key, tok, n))
      return &DKT_KEYS[i];
  return NULL;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOSKUTSU_CONFIG_KEYS_H */
