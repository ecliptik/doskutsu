/*
 * cs_opl2midi.c -- OPL2 (AdLib / YM3812) MIDI voice backend for SETUP (plain C).
 *
 * Plain-C port of vendor/nxengine-evo/src/sound/MidiBackendOpl2.cpp (Campaign 2,
 * patches 0234-0236): the same 8-patch placeholder bank (post patch-0171
 * release-rate bytes), the same GM family-bucket lookup, the same MIDI note ->
 * (fnum, block) table, a 9-voice single-bank LRU allocator, and
 * note-on/off/control-change/program-change. Same allocation policy, same patch
 * bytes, same note math as the engine's OPL2 backend -> the title theme sounds
 * the same on the OPL2 chip in SETUP as the game's AdLib backend plays it.
 *
 * The OPL2 twin of cs_opl3midi.c: identical structure, differing only in the
 * voice count (9 vs 18) and the chip lifecycle -- OPL2 uses SDL_DOSOpl2Detect /
 * InitChip / Shutdown (NO OPL3-mode 0x105, NO secondary bank 0x38A/0x38B);
 * voices 0-8 reuse the bank-agnostic SDL_DOSOpl3* transport, exactly as the
 * engine's MidiBackendOpl2 does. Single static instance. ASCII-only.
 */

#include "cs_opl2midi.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Real-chip primitives exported by libSDL3.a (SDL DOS core). The OPL2 chip
 * lifecycle is OPL2-specific (SDL/0110); the per-voice transport is the
 * bank-agnostic SDL_DOSOpl3* family (voice_id 0-8 = primary bank), reused
 * verbatim -- the same split the engine's MidiBackendOpl2.cpp uses. bool return
 * matches the SDL3 C ABI. */
extern bool     SDL_DOSOpl2Detect(void);
extern void     SDL_DOSOpl2InitChip(void);
extern void     SDL_DOSOpl2Shutdown(void);
extern void     SDL_DOSOpl3WriteRegister(uint16_t reg, uint8_t value);
extern void     SDL_DOSOpl3VoiceWritePatch(int voice_id, const uint8_t patch[12]);
extern void     SDL_DOSOpl3VoiceNoteOn(int voice_id, uint16_t freq, uint8_t block);
extern void     SDL_DOSOpl3VoiceNoteOff(int voice_id);

#define VOICES        9          /* OPL2 primary bank (vs 18 on OPL3) */
#define MIDI_CHANNELS 16
#define DRUM_CHANNEL  9

/* ---- 8-patch bank (MidiBackendOpl2.cpp; post patch-0171) -----------------*/

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

/* Runtime 128-program bank loaded from data/opl3bank.dat (the SAME DMXOPL bank
 * + file the game's OPL2 backend loads). g_bank_count == 0 means the file was
 * absent/short and program_to_patch uses the 8-patch placeholder below. */
#define OPL_BANK_PATH "data/opl3bank.dat"
static uint8_t g_bank[128][12];
static int     g_bank_count = 0;

/* Load data/opl3bank.dat (DOPL3v1: a 12-byte header -- 8-byte magic "DOPL3v1\n",
 * version byte, program-count byte, 2 reserved zero bytes -- followed by
 * count*12 patch bytes). Ported from MidiBackendOpl2::_try_load_opl2_bank.
 * Returns 1 on success; on ANY failure leaves g_bank_count = 0 so the 8-patch
 * placeholder is used (an incomplete install still previews). fopen "rb" --
 * DJGPP text mode would CRLF-corrupt the binary bank. OPL2 ignores the SBI
 * panning nibble; the loaded patches are used verbatim by the transport. */
static int load_bank(void)
{
  static const uint8_t magic[8] = { 'D', 'O', 'P', 'L', '3', 'v', '1', 0x0A };
  uint8_t  header[12];
  unsigned nprog, payload;
  FILE    *fp;

  g_bank_count = 0;
  fp = fopen(OPL_BANK_PATH, "rb");
  if (!fp)
    return 0;
  if (fread(header, 1, sizeof header, fp) != sizeof header ||
      memcmp(header, magic, 8) != 0)
  {
    fclose(fp);
    return 0;
  }
  if (header[8] != 0x01 || header[9] == 0 || header[9] > 128 ||
      header[10] != 0 || header[11] != 0)
  {
    fclose(fp);
    return 0;
  }
  nprog   = (unsigned)header[9];
  payload = nprog * 12u;                 /* <= 1536; no 32-bit overflow */
  if (fread(g_bank, 1, payload, fp) != payload)
  {
    fclose(fp);
    return 0;                            /* g_bank_count stays 0 -> placeholder */
  }
  fclose(fp);
  g_bank_count = (int)nprog;
  return 1;
}

int cs_opl2midi_bank_programs(void) { return g_bank_count; }

/* GM program (0-127) -> patch. A loaded opl3bank.dat program wins; otherwise the
 * 8-patch family bucket (MidiBackendOpl2.cpp). */
static const uint8_t *program_to_patch(int program)
{
  int bucket;
  if (program < 0 || program > 127)
    return PATCH_PIANO;
  if (program < g_bank_count)
    return g_bank[program];              /* runtime opl3bank.dat program */
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

/* MIDI note -> OPL (fnum, block), base table at block 4 (MidiBackendOpl2). */
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

/* ---- allocator (MidiBackendOpl2::_allocate_voice / _find_voice) ----------*/

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

/* ---- sink callbacks (MidiBackendOpl2::note_on / note_off / ...) -----------*/

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
   * CCs are ignored (the placeholder bank has pre-baked TL; the engine's OPL2
   * backend stores volume/expression but does not apply them either). */
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

int cs_opl2midi_init(void)
{
  if (!SDL_DOSOpl2Detect())
  {
    g_ready = 0;
    return 0;
  }
  SDL_DOSOpl2InitChip();
  load_bank();                           /* opl3bank.dat if present; else placeholder */
  memset(g_voices, 0, sizeof(g_voices));
  memset(g_channel_program, 0, sizeof(g_channel_program));
  g_voice_alloc_count = 0;
  g_ready = 1;
  return 1;
}

void cs_opl2midi_song_start(void)
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

void cs_opl2midi_get_sink(cs_smf_sink *out)
{
  if (!out)
    return;
  out->note_on        = cb_note_on;
  out->note_off       = cb_note_off;
  out->control_change = cb_control_change;
  out->program_change = cb_program_change;
  out->user           = NULL;
}

void cs_opl2midi_all_notes_off(void)
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

void cs_opl2midi_shutdown(void)
{
  int v;
  if (!g_ready)
    return;
  cs_opl2midi_all_notes_off();
  /* Mirror MidiBackendOpl2's dtor: clear the chip-wide rhythm/tremolo/vibrato
   * latch (0xBD) and KEY-OFF all 9 voices before the SDL OPL2 shutdown, so the
   * chip state is safe for the next process. */
  SDL_DOSOpl3WriteRegister(0xBD, 0x00);
  for (v = 0; v < VOICES; ++v)
    SDL_DOSOpl3VoiceNoteOff(v);
  SDL_DOSOpl2Shutdown();
  g_ready = 0;
}
