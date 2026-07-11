/*
 * cs_gusmidi.c -- SETUP.EXE game-accurate GUS music preview (task #4 v2).
 *
 * Plain-C reimplementation of NXE::Sound::MidiBackendGus for SETUP's music test.
 * See cs_gusmidi.h. MIDDLE cut: single nearest-middle-C sample per instrument,
 * instant note-off, full replicated game voice pool (respects num_voices).
 *
 * Ported functions (1:1 with vendor/nxengine-evo/src/sound/MidiBackendGus.cpp):
 *   MELODIC_NAMES/DRUM_NAMES tables, _drum_patch_name, rd_*le, pat_freq_to_note,
 *   _upload_pat (single-sample), _allocate_voice/_find_voice/_reap, note_on/off/
 *   cc/pc. Instrument collection uses cs_smf (SETUP has no MidiScheduler).
 */

#include "cs_gusmidi.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dosgus.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

/* ---- config ------------------------------------------------------------- */

#define GUS_MAX_VOICES     32
#define GUS_MIDI_CHANNELS  16
#define GUS_DRUM_CHANNEL   9      /* GM percussion channel (0-indexed) */
#define GUS_PRESCAN_MS     60000  /* whole-song instrument scan window */
#define GUS_PRESCAN_STEP   8      /* ms per prescan tick (timing irrelevant here) */

/* ---- trace hook (SETUP.LOG) --------------------------------------------- */

static void (*g_trace_fn)(const char *) = NULL;

void cs_gusmidi_set_trace(void (*fn)(const char *msg)) { g_trace_fn = fn; }

static void gtrace(const char *fmt, ...)
{
  char    buf[256];
  va_list ap;
  if (!g_trace_fn) return;
  va_start(ap, fmt);
  SDL_vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  g_trace_fn(buf);
}

/* ---- GM program / drum-note -> Gravis stock .pat 8.3 basename ------------ *
 * The canonical Gravis default-patchset name table (transcribed from the
 * TiMidity-compatible gravis.cfg), matching the operator's C:\ULTRASND\MIDI
 * set. Verbatim from MidiBackendGus.cpp. */

static const char *const MELODIC_NAMES[128] = {
  "acpiano",  "britepno", "synpiano", "honky",    "epiano1",  "epiano2",
  "hrpschrd", "clavinet", "celeste",  "glocken",  "musicbox", "vibes",
  "marimba",  "xylophon", "tubebell", "santur",   "homeorg",  "percorg",
  "rockorg",  "church",   "reedorg",  "accordn",  "harmonca", "concrtna",
  "nyguitar", "acguitar", "jazzgtr",  "cleangtr", "mutegtr",  "odguitar",
  "distgtr",  "gtrharm",  "acbass",   "fngrbass", "pickbass", "fretless",
  "slapbas1", "slapbas2", "synbass1", "synbass2", "violin",   "viola",
  "cello",    "contraba", "tremstr",  "pizzcato", "harp",     "timpani",
  "marcato",  "slowstr",  "synstr1",  "synstr2",  "choir",    "doo",
  "voices",   "orchhit",  "trumpet",  "trombone", "tuba",     "mutetrum",
  "frenchrn", "hitbrass", "synbras1", "synbras2", "sprnosax", "altosax",
  "tenorsax", "barisax",  "oboe",     "englhorn", "bassoon",  "clarinet",
  "piccolo",  "flute",    "recorder", "woodflut", "bottle",   "shakazul",
  "whistle",  "ocarina",  "sqrwave",  "sawwave",  "calliope", "chiflead",
  "charang",  "voxlead",  "lead5th",  "basslead", "fantasia", "warmpad",
  "polysyn",  "ghostie",  "bowglass", "metalpad", "halopad",  "sweeper",
  "aurora",   "soundtrk", "crystal",  "atmosphr", "freshair", "unicorn",
  "echovox",  "startrak", "sitar",    "banjo",    "shamisen", "koto",
  "kalimba",  "bagpipes", "fiddle",   "shannai",  "carillon", "agogo",
  "steeldrm", "woodblk",  "taiko",    "toms",     "syntom",   "revcym",
  "fx-fret",  "fx-blow",  "seashore", "jungle",   "telephon", "helicptr",
  "applause", "pistol",
};

/* Percussion: GM note 27..87 -> drum .pat. Index by (note - 27). */
static const char *const DRUM_NAMES[61] = {
  "highq",    "slap",     "scratch1", "scratch2", "sticks",   "sqrclick",
  "metclick", "metbell",  "kick1",    "kick2",    "stickrim", "snare1",
  "claps",    "snare2",   "tomlo2",   "hihatcl",  "tomlo1",   "hihatpd",
  "tommid2",  "hihatop",  "tommid1",  "tomhi2",   "cymcrsh1", "tomhi1",
  "cymride1", "cymchina", "cymbell",  "tamborin", "cymsplsh", "cowbell",
  "cymcrsh2", "vibslap",  "cymride2", "bongohi",  "bongolo",  "congahi1",
  "congahi2", "congalo",  "timbaleh", "timbalel", "agogohi",  "agogolo",
  "cabasa",   "maracas",  "whistle1", "whistle2", "guiro1",   "guiro2",
  "clave",    "woodblk1", "woodblk2", "cuica1",   "cuica2",   "triangl1",
  "triangl2", "shaker",   "jingles",  "belltree", "castinet", "surdo1",
  "surdo2",
};

static const char *drum_patch_name(int note)
{
  /* nx0261: clamp out-of-GM-range drum notes to the nearest defined percussion
   * (nearest-in-range beats silence for the rare edge notes). */
  if (note < 27) note = 27;
  else if (note > 87) note = 87;
  return DRUM_NAMES[note - 27];
}

/* ---- little-endian .pat readers ----------------------------------------- */

static Uint16 rd_u16le(const Uint8 *p) { return (Uint16)(p[0] | (p[1] << 8)); }
static Uint32 rd_u32le(const Uint8 *p)
{
  return (Uint32)p[0] | ((Uint32)p[1] << 8) | ((Uint32)p[2] << 16) | ((Uint32)p[3] << 24);
}
static Sint32 rd_s32le(const Uint8 *p) { return (Sint32)rd_u32le(p); }

/* Convert a .pat frequency field (milliHz or Hz, auto-detected) to a MIDI note,
 * `fallback` when zero/implausible. MAIN-LOOP only (log/FPU). */
static int pat_freq_to_note(Sint32 raw, int fallback)
{
  const double milli = (raw > 0) ? (raw / 1000.0) : 0.0;
  const double hz    = (raw > 0) ? (double)raw    : 0.0;
  double f;
  int    n;
  if (milli >= 8.0 && milli <= 12544.0)  f = milli;
  else if (hz >= 8.0 && hz <= 12544.0)   f = hz;
  else                                   return fallback;
  n = (int)(69.0 + 12.0 * (log(f / 440.0) / log(2.0)) + 0.5);
  if (n < 0)   n = 0;
  if (n > 127) n = 127;
  return n;
}

/* ---- synth state -------------------------------------------------------- */

/* One resident GM program / drum note (MIDDLE cut: single sample). */
typedef struct
{
  int    resident;
  Uint32 dram_addr;
  Uint32 data_size;
  Uint32 loop_start;   /* byte offset */
  Uint32 loop_end;     /* byte offset (== data_size if one-shot) */
  Uint32 sample_rate;  /* native Hz (drum rate / pitch base) */
  Uint32 g_fp;         /* (sample_rate/root_hz)<<16 fixed-point */
  Uint16 flags;        /* SDL_DOSGUS_LOOP / _16BIT / _BIDI */
} gus_pat;

/* Per-voice mirror (driver owns the hardware pool). */
typedef struct
{
  int    active;
  Uint8  midi_channel;
  Uint8  midi_note;
  Uint64 last_used;    /* LRU steal target */
} gus_voice;

static int       g_ready       = 0;
static int       g_num_voices  = 28;
static Uint32    g_dram_size   = 0;
static gus_voice g_voices[GUS_MAX_VOICES];
static Uint64    g_voice_alloc_count = 0;

static gus_pat   g_mel[128];
static gus_pat   g_drum[128];

static Uint8     g_ch_program[GUS_MIDI_CHANNELS];
static Uint8     g_ch_volume[GUS_MIDI_CHANNELS];
static Uint8     g_ch_expr[GUS_MIDI_CHANNELS];
static Uint8     g_ch_pan[GUS_MIDI_CHANNELS];

static Uint32    g_note_freq[128];   /* equal-tempered Hz, rounded */
static int       g_note_freq_ready = 0;

/* ---- init --------------------------------------------------------------- */

int cs_gusmidi_init(void)
{
  SDL_DOSGusState st;
  int n, ch;

  if (!g_note_freq_ready)
  {
    for (n = 0; n < 128; ++n)
      g_note_freq[n] = (Uint32)(440.0 * pow(2.0, (n - 69) / 12.0) + 0.5);
    g_note_freq_ready = 1;
  }

  SDL_memset(g_voices, 0, sizeof(g_voices));
  SDL_memset(g_mel, 0, sizeof(g_mel));
  SDL_memset(g_drum, 0, sizeof(g_drum));
  for (ch = 0; ch < GUS_MIDI_CHANNELS; ++ch)
  {
    g_ch_program[ch] = 0;
    g_ch_volume[ch]  = 100;   /* GM default */
    g_ch_expr[ch]    = 127;
    g_ch_pan[ch]     = 64;    /* center */
  }
  g_voice_alloc_count = 0;

  g_num_voices = 28;
  g_dram_size  = 0;
  if (SDL_DOSGusGetState(&st) && st.valid)
  {
    g_num_voices = (st.num_voices > 0 && st.num_voices <= GUS_MAX_VOICES)
                     ? st.num_voices : 28;
    g_dram_size  = st.dram_size;
    g_ready      = 1;
  }
  else
  {
    g_ready = 0;
  }
  gtrace("gus midi: init ready=%d voices=%d dram=%luKB (voices == configured GUS_VOICES)",
         g_ready, g_num_voices, (unsigned long)(g_dram_size / 1024));
  return g_ready;
}

/* ---- .pat upload (single nearest-middle-C sample) ----------------------- */

static int upload_pat(const char *base_name, int is_drum, gus_pat *out)
{
  const char *ultradir;
  char        path[256];
  FILE       *fp;
  Uint8       hdr[129], ihdr[63], lhdr[47], srec[96];
  int         nsamp, s;
  Uint8      *best_pcm = NULL;
  int         best_dist = 1000;
  Uint32      best_data = 0, best_lstart = 0, best_lend = 0, best_rate = 0, best_gfp = 0;
  Uint16      best_flags = 0;
  Uint32      addr;

  out->resident = 0;
  if (!base_name || !out) return 0;

  ultradir = SDL_GetHint("SDL_HINT_DOSKUTSU_GUS_ULTRADIR");
  if (!ultradir || ultradir[0] == '\0') ultradir = "C:\\ULTRASND";

  /* <ULTRADIR>\<name>.pat then <ULTRADIR>\MIDI\<name>.pat. "rb" -- DJGPP text
   * mode would corrupt the binary .pat. */
  SDL_snprintf(path, sizeof(path), "%s\\%s.pat", ultradir, base_name);
  fp = fopen(path, "rb");
  if (!fp)
  {
    SDL_snprintf(path, sizeof(path), "%s\\MIDI\\%s.pat", ultradir, base_name);
    fp = fopen(path, "rb");
  }
  if (!fp)
  {
    gtrace("gus midi: .pat '%s' not found under '%s' -> silent", base_name, ultradir);
    return 0;
  }

  if (fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr) ||
      SDL_memcmp(hdr, "GF1PATCH1", 9) != 0)
  {
    gtrace("gus midi: .pat '%s' bad/short GF1 header -> silent", base_name);
    fclose(fp);
    return 0;
  }
  if (fread(ihdr, 1, sizeof(ihdr), fp) != sizeof(ihdr) ||
      fread(lhdr, 1, sizeof(lhdr), fp) != sizeof(lhdr))
  {
    gtrace("gus midi: .pat '%s' truncated instrument/layer header -> silent", base_name);
    fclose(fp);
    return 0;
  }

  nsamp = lhdr[6];
  if (nsamp < 1) nsamp = 1;
  if (nsamp > 64) nsamp = 64;

  /* Parse each sample; keep the one whose root note is nearest middle C (the
   * single-sample "patch 0245" behavior). Skip the rest of each unwanted sample's
   * PCM by seeking, so the file cursor stays aligned. */
  for (s = 0; s < nsamp; ++s)
  {
    Uint32 s_data, s_lstart, s_lend, s_rate, s_gfp, lend, lstart;
    Sint32 s_root;
    Uint8  s_modes;
    int    fmt16, is_unsigned, loop_on, bidi, use_loop, root_note, dist;
    double rh_milli, rh_hz, root_hz, g;

    if (fread(srec, 1, sizeof(srec), fp) != sizeof(srec)) break;
    s_data   = rd_u32le(srec + 8);
    s_lstart = rd_u32le(srec + 12);
    s_lend   = rd_u32le(srec + 16);
    s_rate   = rd_u16le(srec + 20);
    /* srec+22 low_freq / srec+26 high_freq: only needed for the FULL-cut
     * multisample key-range; the single-sample MIDDLE cut ignores them. */
    s_root   = rd_s32le(srec + 30);
    s_modes  = srec[55];
    if (s_data == 0 || (g_dram_size && s_data > g_dram_size) ||
        s_data > (2u * 1024u * 1024u))
      break;   /* implausible -> cannot safely advance the cursor */

    root_note = pat_freq_to_note(s_root, 60);
    dist      = (root_note > 60) ? (root_note - 60) : (60 - root_note);

    if (dist >= best_dist)
    {
      /* not better than the one we have -> skip its PCM */
      if (fseek(fp, (long)s_data, SEEK_CUR) != 0) break;
      continue;
    }

    /* new best -> read + normalize its PCM */
    {
      Uint8 *pcm = (Uint8 *)malloc(s_data);
      if (!pcm) { if (fseek(fp, (long)s_data, SEEK_CUR) != 0) break; continue; }
      if (fread(pcm, 1, s_data, fp) != s_data) { free(pcm); break; }

      fmt16       = (s_modes & 0x01) != 0;
      is_unsigned = (s_modes & 0x02) != 0;
      loop_on     = (s_modes & 0x04) != 0;
      bidi        = (s_modes & 0x08) != 0;

      /* Normalize to native GF1 DRAM format: 8-bit UNSIGNED / 16-bit SIGNED. */
      if (!fmt16)
      {
        if (!is_unsigned) { Uint32 i; for (i = 0; i < s_data; ++i) pcm[i] ^= 0x80; }
      }
      else
      {
        if (is_unsigned) { Uint32 i; for (i = 1; i < s_data; i += 2) pcm[i] ^= 0x80; }
      }

      use_loop = loop_on && !is_drum;
      lend = s_lend; lstart = s_lstart;
      if (lend == 0 || lend > s_data) lend = s_data;
      if (lstart > s_data) lstart = 0;

      rh_milli = (s_root > 0) ? (s_root / 1000.0) : 0.0;
      rh_hz    = (s_root > 0) ? (double)s_root    : 0.0;
      if (rh_milli >= 8.0 && rh_milli <= 12544.0)  root_hz = rh_milli;
      else if (rh_hz >= 8.0 && rh_hz <= 12544.0)   root_hz = rh_hz;
      else                                         root_hz = 440.0;
      g     = (double)s_rate / root_hz;
      s_gfp = (Uint32)(g * 65536.0 + 0.5);
      if (s_gfp == 0) s_gfp = 65536;

      free(best_pcm);
      best_pcm   = pcm;
      best_dist  = dist;
      best_data  = s_data;
      best_lstart= lstart;
      best_lend  = lend;
      best_rate  = s_rate ? s_rate : 22050;
      best_gfp   = s_gfp;
      best_flags = (Uint16)((use_loop ? SDL_DOSGUS_LOOP : 0) |
                            (fmt16 ? SDL_DOSGUS_16BIT : 0) |
                            (use_loop && bidi ? SDL_DOSGUS_BIDI : 0));
    }
  }
  fclose(fp);

  if (!best_pcm)
  {
    gtrace("gus midi: .pat '%s' no usable sample -> silent", base_name);
    return 0;
  }

  addr = SDL_DOSGusUploadSample(best_pcm, best_data, (best_flags & SDL_DOSGUS_16BIT) ? 1 : 0);
  free(best_pcm);
  if (addr == SDL_DOSGUS_BAD_ADDR)
  {
    gtrace("gus midi: .pat '%s' DRAM upload FAILED (full?) -> silent", base_name);
    return 0;
  }

  out->resident    = 1;
  out->dram_addr   = addr;
  out->data_size   = best_data;
  out->loop_start  = best_lstart;
  out->loop_end    = best_lend;
  out->sample_rate = best_rate;
  out->g_fp        = best_gfp;
  out->flags       = best_flags;
  gtrace("gus midi: .pat '%s' -> addr=0x%lX (%luB rate=%lu flags=0x%X %s)",
         base_name, (unsigned long)addr, (unsigned long)best_data,
         (unsigned long)best_rate, (unsigned)best_flags, is_drum ? "drum" : "mel");
  return 1;
}

/* ---- instrument pre-scan via cs_smf ------------------------------------- */

static Uint8 g_ps_prog[GUS_MIDI_CHANNELS];
static Uint8 g_ps_mel_used[128];
static Uint8 g_ps_drum_used[128];

static void ps_cb_pc(void *u, int ch, int program)
{
  (void)u;
  g_ps_prog[ch & 0x0F] = (Uint8)(program & 0x7F);
}
static void ps_cb_on(void *u, int ch, int note, int vel)
{
  (void)u;
  ch &= 0x0F; note &= 0x7F;
  if (vel <= 0) return;                       /* note_off */
  if (ch == GUS_DRUM_CHANNEL) g_ps_drum_used[note] = 1;
  else                        g_ps_mel_used[g_ps_prog[ch]] = 1;
}

int cs_gusmidi_song_prescan(cs_smf *m)
{
  cs_smf_sink sink;
  Uint32      t;
  int         i, uploaded = 0;
  SDL_DOSGusState st;

  if (!g_ready || !m) return 0;

  SDL_memset(g_ps_prog, 0, sizeof(g_ps_prog));
  SDL_memset(g_ps_mel_used, 0, sizeof(g_ps_mel_used));
  SDL_memset(g_ps_drum_used, 0, sizeof(g_ps_drum_used));

  SDL_memset(&sink, 0, sizeof(sink));
  sink.note_on        = ps_cb_on;
  sink.program_change = ps_cb_pc;
  cs_smf_set_sink(m, &sink);
  cs_smf_start(m, 0);
  for (t = 0; t <= GUS_PRESCAN_MS; t += GUS_PRESCAN_STEP)
    cs_smf_tick(m, t);

  g_ps_mel_used[0] = 1;   /* GM piano default for un-programmed channels */

  /* Fresh DRAM slate (SETUP has no persistent SFX bank; ResetDram is also
   * belt-and-braces since Init already zeroed the cursor). */
  SDL_DOSGusResetDram(0);
  for (i = 0; i < 128; ++i) { g_mel[i].resident = 0; g_drum[i].resident = 0; }

  for (i = 0; i < 128; ++i)
    if (g_ps_mel_used[i] && upload_pat(MELODIC_NAMES[i], 0, &g_mel[i])) ++uploaded;
  for (i = 0; i < 128; ++i)
    if (g_ps_drum_used[i] && upload_pat(drum_patch_name(i), 1, &g_drum[i])) ++uploaded;

  {
    unsigned long used_kb =
      (SDL_DOSGusGetState(&st) && st.valid) ? (unsigned long)(st.dram_used / 1024) : 0;
    gtrace("gus midi: prescan uploaded %d instruments into %luKB DRAM", uploaded, used_kb);
  }
  return uploaded;
}

/* ---- voice pool (1:1 with MidiBackendGus) ------------------------------- */

static void reap_finished_voices(void)
{
  SDL_DOSGusState st;
  int v;
  if (!SDL_DOSGusGetState(&st) || !st.valid) return;
  for (v = 0; v < g_num_voices; ++v)
  {
    if (g_voices[v].active && ((st.voice_active_mask >> v) & 1u) == 0u)
    {
      SDL_DOSGusStopVoice(v);
      g_voices[v].active = 0;
    }
  }
}

static int allocate_voice(void)
{
  int v, i, lru;
  reap_finished_voices();            /* note-cadence reap (sdl-eng N1) */
  ++g_voice_alloc_count;

  v = SDL_DOSGusAllocVoice();
  if (v < 0 || v >= g_num_voices)
  {
    /* Music partition full -> LRU-steal the oldest ACTIVE music voice. Restrict
     * the search to ACTIVE voices (sdl-eng co-review): SETUP has no SFX source,
     * so the driver's reserved SFX slots are never active and their last_used
     * stays 0 forever -- a full-pool search would ALWAYS pick a reserved slot as
     * the LRU minimum, bleeding music into the reserved region (more polyphony
     * than the game = a less-accurate preview). AllocVoice==-1 after the reap
     * means every music voice is genuinely busy, so an active voice to steal
     * always exists (the lru<0 guard is purely defensive). */
    lru = -1;
    for (i = 0; i < g_num_voices; ++i)
      if (g_voices[i].active &&
          (lru < 0 || g_voices[i].last_used < g_voices[lru].last_used))
        lru = i;
    if (lru < 0) lru = 0;
    SDL_DOSGusStopVoice(lru);
    g_voices[lru].active = 0;
    v = SDL_DOSGusAllocVoice();
    if (v < 0 || v >= g_num_voices) v = lru;
  }
  g_voices[v].last_used = g_voice_alloc_count;
  return v;
}

static int find_voice(int channel, int note)
{
  int v;
  for (v = 0; v < g_num_voices; ++v)
    if (g_voices[v].active &&
        g_voices[v].midi_channel == (Uint8)channel &&
        g_voices[v].midi_note    == (Uint8)note)
      return v;
  return -1;
}

/* ---- dispatch (main-loop context; FPU OK) ------------------------------- */

static void gm_note_off(void *u, int channel, int note, int velocity);

static void gm_note_on(void *u, int channel, int note, int velocity)
{
  int          ch, is_drum, v, lin, vol255, pan255;
  const gus_pat *ps;
  Uint32       hz, start, end, loopstart;
  (void)u;
  if (!g_ready) return;
  if (velocity == 0) { gm_note_off(u, channel, note, 0); return; }
  ch = channel & 0x0F;
  note &= 0x7F;

  is_drum = (ch == GUS_DRUM_CHANNEL);
  ps = is_drum ? &g_drum[note] : &g_mel[g_ch_program[ch] & 0x7F];
  if (!ps->resident) return;   /* not uploaded -> silent (no file I/O here) */

  v = allocate_voice();
  g_voices[v].active       = 1;
  g_voices[v].midi_channel = (Uint8)ch;
  g_voices[v].midi_note    = (Uint8)note;

  if (is_drum) hz = ps->sample_rate;   /* note selects the drum, no transpose */
  else         hz = (Uint32)(((Uint64)ps->g_fp * g_note_freq[note]) >> 16);
  if (hz == 0) hz = ps->sample_rate;

  lin    = velocity;                                 /* 0..127 */
  lin    = (lin * g_ch_volume[ch]) / 127;            /* CC7 */
  lin    = (lin * g_ch_expr[ch]) / 127;              /* CC11 */
  vol255 = (lin * 255) / 127;
  if (vol255 > 255) vol255 = 255;

  pan255 = (int)g_ch_pan[ch] << 1;                   /* CC10 0..127 -> 0..255 */
  if (pan255 > 255) pan255 = 255;

  start     = ps->dram_addr;
  end       = ps->dram_addr + ps->loop_end;
  loopstart = ps->dram_addr + ps->loop_start;

  SDL_DOSGusSetVoiceFreq(v, hz);
  SDL_DOSGusSetVoiceVol(v, vol255);
  SDL_DOSGusSetVoicePan(v, pan255);
  SDL_DOSGusStartVoice(v, start, end, loopstart, ps->flags);
}

static void gm_note_off(void *u, int channel, int note, int velocity)
{
  int v;
  (void)u; (void)velocity;
  if (!g_ready) return;
  v = find_voice(channel & 0x0F, note & 0x7F);
  if (v < 0) return;
  SDL_DOSGusStopVoice(v);           /* MIDDLE cut: instant stop (no release ramp) */
  g_voices[v].active = 0;
}

static void gm_cc(void *u, int channel, int controller, int value)
{
  int ch, v;
  (void)u;
  if (!g_ready) return;
  ch = channel & 0x0F;
  switch (controller)
  {
    case 7:  g_ch_volume[ch] = (Uint8)(value & 0x7F); break;
    case 10: g_ch_pan[ch]    = (Uint8)(value & 0x7F); break;
    case 11: g_ch_expr[ch]   = (Uint8)(value & 0x7F); break;
    case 120:  /* All Sound Off */
    case 123:  /* All Notes Off */
      for (v = 0; v < g_num_voices; ++v)
        if (g_voices[v].active && g_voices[v].midi_channel == (Uint8)ch)
        {
          SDL_DOSGusStopVoice(v);
          g_voices[v].active = 0;
        }
      break;
    default: break;
  }
}

static void gm_pc(void *u, int channel, int program)
{
  (void)u;
  if (!g_ready) return;
  g_ch_program[channel & 0x0F] = (Uint8)(program & 0x7F);
}

void cs_gusmidi_get_sink(cs_smf_sink *out)
{
  if (!out) return;
  SDL_memset(out, 0, sizeof(*out));
  out->note_on        = gm_note_on;
  out->note_off       = gm_note_off;
  out->control_change = gm_cc;
  out->program_change = gm_pc;
}

void cs_gusmidi_song_start(void)
{
  int v, ch;
  if (!g_ready) return;
  SDL_DOSGusStopAllVoices();
  for (v = 0; v < GUS_MAX_VOICES; ++v) g_voices[v].active = 0;
  for (ch = 0; ch < GUS_MIDI_CHANNELS; ++ch)
  {
    g_ch_program[ch] = 0;
    g_ch_volume[ch]  = 100;
    g_ch_expr[ch]    = 127;
    g_ch_pan[ch]     = 64;
  }
  gtrace("gus midi: song_start (voices cleared, %d-voice pool)", g_num_voices);
}

void cs_gusmidi_all_notes_off(void)
{
  int v;
  if (!g_ready) return;
  for (v = 0; v < g_num_voices; ++v)
  {
    if (g_voices[v].active) { SDL_DOSGusStopVoice(v); g_voices[v].active = 0; }
  }
}

void cs_gusmidi_shutdown(void)
{
  int v;
  for (v = 0; v < GUS_MAX_VOICES; ++v) g_voices[v].active = 0;
  g_ready = 0;
}
