/*
 * cs_opl3midi.c -- OPL3 MIDI voice backend for SETUP (plain C).
 *
 * Plain-C port of vendor/nxengine-evo/src/sound/MidiBackendOpl3.cpp: 8-patch
 * bank (with the patch-0171 release-rate fix already in the bytes), GM
 * family-bucket lookup, MIDI note -> (fnum, block) table, 18-voice LRU
 * allocator, note-on/off/control-change/program-change. Same allocation
 * policy, same patch bytes, same note math as the engine -> the title theme
 * sounds the same on the OPL3 chip in SETUP as in the game. Single static
 * instance. ASCII-only.
 */

#include "cs_opl3midi.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Real-chip primitives exported by libSDL3.a (SDL DOS core, SDL/0037+);
 * forward-declared exactly as the engine's MidiBackendOpl3.cpp does (bool
 * return matches the SDL3 C ABI). */
extern bool     SDL_DOSOpl3Detect(void);
extern void     SDL_DOSOpl3InitChip(void);
extern void     SDL_DOSOpl3VoiceWritePatch(int voice_id, const uint8_t patch[12]);
extern void     SDL_DOSOpl3VoiceNoteOn(int voice_id, uint16_t freq, uint8_t block);
extern void     SDL_DOSOpl3VoiceNoteOff(int voice_id);
extern void     SDL_DOSOpl3Shutdown(void);

#define VOICES        18
#define MIDI_CHANNELS 16
#define DRUM_CHANNEL  9

/* ---- 8-patch bank (MidiBackendOpl3.cpp; post patch-0171) -----------------*/

static const uint8_t PATCH_PIANO[12] = {
  0x01, 0x08, 0xE6, 0x48, 0x00, 0xF8, 0x01, 0x00, 0xF4, 0x48, 0x00, 0x00 };
static const uint8_t PATCH_MALLET[12] = {
  0x01, 0x10, 0xF0, 0x88, 0x00, 0xF8, 0x01, 0x00, 0xF0, 0xC8, 0x00, 0x00 };
static const uint8_t PATCH_ORGAN[12] = {
  0x01, 0x18, 0xF0, 0x08, 0x00, 0xFE, 0x01, 0x00, 0xF0, 0x08, 0x00, 0x00 };
static const uint8_t PATCH_GUITAR[12] = {
  0x21, 0x10, 0xE2, 0x18, 0x00, 0xFA, 0x01, 0x00, 0xF1, 0x68, 0x00, 0x00 };
static const uint8_t PATCH_BRASS[12] = {
  0x21, 0x18, 0x91, 0x18, 0x00, 0xFC, 0x21, 0x00, 0x71, 0x18, 0x00, 0x00 };
static const uint8_t PATCH_REED[12] = {
  0x21, 0x10, 0x71, 0x18, 0x00, 0xF8, 0x21, 0x00, 0x91, 0x68, 0x00, 0x00 };
static const uint8_t PATCH_PAD[12] = {
  0x21, 0x18, 0x71, 0x08, 0x01, 0xF6, 0x21, 0x00, 0x51, 0x08, 0x01, 0x00 };
static const uint8_t PATCH_DRUM[12] = {
  0x0F, 0x00, 0xFF, 0x0F, 0x07, 0xF0, 0x0F, 0x00, 0xFF, 0x0F, 0x07, 0x00 };

/* GM program (0-127) -> patch family bucket (MidiBackendOpl3.cpp). */
static const uint8_t *program_to_patch(int program)
{
  int bucket;
  if (program < 0 || program > 127)
    return PATCH_PIANO;
  bucket = program / 8;
  switch (bucket)
  {
    case 0:  return PATCH_PIANO;
    case 1:  return PATCH_MALLET;
    case 2:  return PATCH_ORGAN;
    case 3:  return PATCH_GUITAR;
    case 4:  return PATCH_GUITAR;
    case 5:  return PATCH_PAD;
    case 6:  return PATCH_PAD;
    case 7:  return PATCH_BRASS;
    case 8:  return PATCH_REED;
    case 9:  return PATCH_REED;
    case 10: return PATCH_GUITAR;
    case 11: return PATCH_PAD;
    case 12: return PATCH_PAD;
    case 13: return PATCH_GUITAR;
    case 14: return PATCH_MALLET;
    case 15: return PATCH_PIANO;
    default: return PATCH_PIANO;
  }
}

/* MIDI note -> OPL3 (fnum, block), base table at block 4 (MidiBackendOpl3). */
static const uint16_t base_fnum_table[12] = {
  363, 385, 408, 432, 458, 485, 514, 544, 577, 611, 647, 686 };

static void note_to_fnum_block(int note, uint16_t *out_fnum, uint8_t *out_block)
{
  int rel, octave_delta, semitone, block;
  if (note < 0)   note = 0;
  if (note > 127) note = 127;
  rel = note - 60;                          /* MIDI 60 = C4, reference block 4 */
  if (rel >= 0) { octave_delta = rel / 12;            semitone = rel % 12; }
  else          { octave_delta = -((-rel + 11) / 12); semitone = ((rel % 12) + 12) % 12; }
  block = 4 + octave_delta;
  if (block < 0) block = 0;
  if (block > 7) block = 7;
  *out_fnum  = base_fnum_table[semitone];
  *out_block = (uint8_t)block;
}

/* ---- state ---------------------------------------------------------------*/

typedef struct
{
  int      active;
  uint8_t  midi_channel;
  uint8_t  midi_note;
  uint64_t last_used;
} voice_t;

static int      g_ready = 0;
static voice_t  g_voices[VOICES];
static uint64_t g_voice_alloc_count = 0;
static uint8_t  g_channel_program[MIDI_CHANNELS];

/* ---- allocator (MidiBackendOpl3::_allocate_voice / _find_voice) ----------*/

static int allocate_voice(void)
{
  int free_voice = -1, lru_voice = 0, v, chosen;
  ++g_voice_alloc_count;
  for (v = 0; v < VOICES; ++v)
  {
    if (!g_voices[v].active && free_voice < 0)
      free_voice = v;
    if (g_voices[v].last_used < g_voices[lru_voice].last_used)
      lru_voice = v;
  }
  chosen = (free_voice >= 0) ? free_voice : lru_voice;
  if (free_voice < 0)
    SDL_DOSOpl3VoiceNoteOff(chosen);   /* LRU steal: silence prior occupant */
  g_voices[chosen].last_used = g_voice_alloc_count;
  return chosen;
}

static int find_voice(int channel, int note)
{
  int v;
  for (v = 0; v < VOICES; ++v)
    if (g_voices[v].active &&
        g_voices[v].midi_channel == (uint8_t)channel &&
        g_voices[v].midi_note    == (uint8_t)note)
      return v;
  return -1;
}

static void silence_voice(int voice_id)
{
  if (voice_id < 0 || voice_id >= VOICES)
    return;
  SDL_DOSOpl3VoiceNoteOff(voice_id);
  g_voices[voice_id].active = 0;
}

/* ---- sink callbacks (MidiBackendOpl3::note_on / note_off / ...) -----------*/

static void cb_note_off(void *u, int channel, int note, int velocity)
{
  int voice;
  (void)u; (void)velocity;
  if (!g_ready)
    return;
  voice = find_voice(channel, note);
  if (voice >= 0)
    silence_voice(voice);
}

static void cb_note_on(void *u, int channel, int note, int velocity)
{
  int      voice;
  uint16_t fnum  = 0;
  uint8_t  block = 4;
  const uint8_t *patch;
  (void)u;
  if (!g_ready)
    return;
  if (velocity == 0)               /* GM: note-on vel 0 == note-off */
  {
    cb_note_off(NULL, channel, note, 0);
    return;
  }
  voice = allocate_voice();
  g_voices[voice].active       = 1;
  g_voices[voice].midi_channel = (uint8_t)(channel & 0x0F);
  g_voices[voice].midi_note    = (uint8_t)(note    & 0x7F);

  patch = (channel == DRUM_CHANNEL)
            ? PATCH_DRUM
            : program_to_patch(g_channel_program[channel & 0x0F]);
  SDL_DOSOpl3VoiceWritePatch(voice, patch);

  note_to_fnum_block(note, &fnum, &block);
  SDL_DOSOpl3VoiceNoteOn(voice, fnum, block);
}

static void cb_control_change(void *u, int channel, int controller, int value)
{
  (void)u; (void)value;
  if (!g_ready)
    return;
  /* All Sound Off (120) / All Notes Off (123): silence this channel. Other
   * CCs are ignored (the placeholder bank has pre-baked TL; the engine slot
   * 0103 leaves volume/expression unhonored too). */
  if (controller == 120 || controller == 123)
  {
    int v;
    for (v = 0; v < VOICES; ++v)
      if (g_voices[v].active && g_voices[v].midi_channel == (uint8_t)(channel & 0x0F))
        silence_voice(v);
  }
}

static void cb_program_change(void *u, int channel, int program)
{
  (void)u;
  if (!g_ready)
    return;
  g_channel_program[channel & 0x0F] = (uint8_t)(program & 0x7F);
}

/* ---- public API ----------------------------------------------------------*/

int cs_opl3midi_init(void)
{
  if (!SDL_DOSOpl3Detect())
  {
    g_ready = 0;
    return 0;
  }
  SDL_DOSOpl3InitChip();
  memset(g_voices, 0, sizeof(g_voices));
  memset(g_channel_program, 0, sizeof(g_channel_program));
  g_voice_alloc_count = 0;
  g_ready = 1;
  return 1;
}

void cs_opl3midi_song_start(void)
{
  int v, ch;
  if (!g_ready)
    return;
  for (v = 0; v < VOICES; ++v)
    if (g_voices[v].active)
    {
      SDL_DOSOpl3VoiceNoteOff(v);
      g_voices[v].active = 0;
    }
  for (ch = 0; ch < MIDI_CHANNELS; ++ch)
    g_channel_program[ch] = 0;          /* GM default = Acoustic Grand Piano */
}

void cs_opl3midi_get_sink(cs_smf_sink *out)
{
  if (!out)
    return;
  out->note_on        = cb_note_on;
  out->note_off       = cb_note_off;
  out->control_change = cb_control_change;
  out->program_change = cb_program_change;
  out->user           = NULL;
}

void cs_opl3midi_all_notes_off(void)
{
  int v;
  if (!g_ready)
    return;
  for (v = 0; v < VOICES; ++v)
    if (g_voices[v].active)
    {
      SDL_DOSOpl3VoiceNoteOff(v);
      g_voices[v].active = 0;
    }
}

void cs_opl3midi_shutdown(void)
{
  if (!g_ready)
    return;
  cs_opl3midi_all_notes_off();
  SDL_DOSOpl3Shutdown();
  g_ready = 0;
}
