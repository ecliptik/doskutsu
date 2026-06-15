/*
 * cs_pixtone.c -- standalone Cave Story Pixtone SFX synthesizer (plain C).
 *
 * Plain-C port of the engine's Pixtone synth core
 * (vendor/nxengine-evo/src/sound/Pixtone.cpp). The arithmetic, types, and
 * evaluation order are kept identical to the engine so the rendered effect is
 * byte-faithful to what the game produces. See cs_pixtone.h for the contract.
 *
 * The engine code is C++ (lambdas, std::vector, classes); SETUP builds as C99,
 * so this is a structural rewrite with the same math. ASCII-only.
 */

#include "cs_pixtone.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CS_PXT_CHANNELS 4

/* Six modulation models, indices matching the engine's enum
 * (MOD_SINE..MOD_NOISE). */
static int8_t g_wave[6][256];
static int    g_wave_inited = 0;

/* Build the 6 x 256-sample model tables once. Verbatim arithmetic from
 * Pixtone.cpp:454-463 (uint32_t counter + LCG seed -> int8_t truncation). */
static void wave_init(void)
{
  uint32_t seed = 0, i;
  if (g_wave_inited)
    return;
  for (i = 0; i < 256; ++i)
  {
    seed          = (seed * 214013) + 2531011;             /* LCG */
    g_wave[0][i]  = (int8_t)(0x40 * sin(i * 3.1416 / 0x80)); /* sine     */
    g_wave[1][i]  = (int8_t)(((0x40 + i) & 0x80) ? 0x80 - i : i); /* triangle */
    g_wave[2][i]  = (int8_t)(-0x40 + i / 2);                /* saw up   */
    g_wave[3][i]  = (int8_t)(0x40 - i / 2);                 /* saw down */
    g_wave[4][i]  = (int8_t)(0x40 - (i & 0x80));            /* square   */
    g_wave[5][i]  = (int8_t)((int8_t)(seed >> 16) / 2);     /* noise    */
  }
  g_wave_inited = 1;
}

/* ---- in-memory representation (mirrors Pixtone.h structs) -----------------*/

typedef struct
{
  const int8_t *wave;
  double        pitch;
  int32_t       level;
  int32_t       offset;
} cs_pxwave;

typedef struct
{
  int32_t initial;
  struct { int32_t time, val; } p[3];
} cs_pxenv;

typedef struct
{
  int          enabled;
  uint32_t     nsamples;
  cs_pxwave    carrier;
  cs_pxwave    frequency;
  cs_pxwave    amplitude;
  cs_pxenv     envelope;
  signed char *buffer;
} cs_pxchan;

typedef struct
{
  cs_pxchan channels[CS_PXT_CHANNELS];
} cs_pxsound;

/* ---- text param parser (Pixtone.cpp fgetv) -------------------------------*/

/* Load one numeric value from the text file; one per line. Skips empty lines,
 * then everything up to (and including) the first ':' before parsing. */
static double fgetv(FILE *fp)
{
  char  buf[4096], *p = buf;
  buf[4095] = '\0';
  if (!fgets(buf, sizeof(buf) - 1, fp))
    return 0.0;
  if (!buf[0] || buf[0] == '\r' || buf[0] == '\n')
    return fgetv(fp);
  while (*p && *p++ != ':')
  {
  }
  return strtod(p, NULL);
}

static int32_t fi(FILE *fp) { return (int32_t)fgetv(fp); }

static const int8_t *wave_for(int32_t model)
{
  int32_t m = model % 6;
  if (m < 0)
    m += 6;
  return g_wave[m];
}

/* Parse the 4 channels. fgetv calls are sequenced left-to-right to match the
 * engine's braced-init evaluation order (Pixtone.cpp:243-254):
 *   per channel: use, size, then 3 waves {model, freq, top, offset},
 *   then envelope {initial, (ax,ay), (bx,by), (cx,cy)}. */
static int cs_pxt_load(cs_pxsound *snd, const char *path)
{
  FILE *fp;
  int   ch;

  fp = fopen(path, "rb");
  if (!fp)
    return 0;

  for (ch = 0; ch < CS_PXT_CHANNELS; ++ch)
  {
    cs_pxchan *c = &snd->channels[ch];

    c->enabled  = (fi(fp) != 0);
    c->nsamples = (uint32_t)fgetv(fp);

    c->carrier.wave   = wave_for(fi(fp));
    c->carrier.pitch  = fgetv(fp);
    c->carrier.level  = fi(fp);
    c->carrier.offset = fi(fp);

    c->frequency.wave   = wave_for(fi(fp));
    c->frequency.pitch  = fgetv(fp);
    c->frequency.level  = fi(fp);
    c->frequency.offset = fi(fp);

    c->amplitude.wave   = wave_for(fi(fp));
    c->amplitude.pitch  = fgetv(fp);
    c->amplitude.level  = fi(fp);
    c->amplitude.offset = fi(fp);

    c->envelope.initial = fi(fp);
    c->envelope.p[0].time = fi(fp); c->envelope.p[0].val = fi(fp);
    c->envelope.p[1].time = fi(fp); c->envelope.p[1].val = fi(fp);
    c->envelope.p[2].time = fi(fp); c->envelope.p[2].val = fi(fp);

    c->buffer = NULL;
  }

  fclose(fp);
  return 1;
}

/* ---- synthesis (Pixtone.cpp stPXEnvelope::evaluate + stPXChannel::synth) --*/

static int32_t env_eval(const cs_pxenv *e, int32_t i)
{
  int32_t prevval = e->initial, prevtime = 0;
  int32_t nextval = 0, nexttime = 256;
  int32_t j;
  for (j = 2; j >= 0; --j)
    if (i < e->p[j].time)
    {
      nexttime = e->p[j].time;
      nextval  = e->p[j].val;
    }
  for (j = 0; j <= 2; ++j)
    if (i >= e->p[j].time)
    {
      prevtime = e->p[j].time;
      prevval  = e->p[j].val;
    }
  if (nexttime <= prevtime)
    return prevval;
  return (i - prevtime) * (nextval - prevval) / (nexttime - prevtime) + prevval;
}

static void chan_synth(cs_pxchan *c)
{
  double   mainpos   = c->carrier.offset;
  double   maindelta = 256.0 * c->carrier.pitch / c->nsamples;
  uint32_t i;

  if (!c->enabled)
    return;

  for (i = 0; i < c->nsamples; ++i)
  {
    double  base = 256.0 * (double)i / (double)c->nsamples; /* s(1) */
    int32_t freqval = c->frequency.wave[0xFF & (int)(c->frequency.offset + base * c->frequency.pitch)] * c->frequency.level;
    int32_t ampval  = c->amplitude.wave[0xFF & (int)(c->amplitude.offset + base * c->amplitude.pitch)] * c->amplitude.level;
    int32_t mainval = c->carrier.wave[0xFF & (int)mainpos] * c->carrier.level;

    c->buffer[i] = (signed char)(mainval * (ampval + 4096) / 4096 *
                                 env_eval(&c->envelope, (int32_t)base) / 4096);

    mainpos += maindelta * (1 + (freqval / (freqval < 0 ? 8192.0 : 2048.0)));
  }
}

/* ---- render + mix (Pixtone.cpp stPXSound::render) ------------------------*/

signed char *cs_pixtone_render(const char *path, uint32_t *out_len)
{
  cs_pxsound   snd;
  signed char *final_buffer = NULL;
  int16_t     *middle       = NULL;
  uint32_t     topbufsize   = 64;
  uint32_t     i, s;

  if (out_len)
    *out_len = 0;

  wave_init();

  memset(&snd, 0, sizeof(snd));
  if (!cs_pxt_load(&snd, path))
    return NULL;

  /* allocate per-enabled-channel buffers + track the widest channel */
  for (i = 0; i < CS_PXT_CHANNELS; ++i)
  {
    if (snd.channels[i].enabled)
    {
      snd.channels[i].buffer = (signed char *)malloc(snd.channels[i].nsamples);
      if (!snd.channels[i].buffer)
        goto fail;
      if (snd.channels[i].nsamples > topbufsize)
        topbufsize = snd.channels[i].nsamples;
    }
  }

  final_buffer = (signed char *)malloc(topbufsize);
  if (!final_buffer)
    goto fail;

  for (i = 0; i < CS_PXT_CHANNELS; ++i)
    if (snd.channels[i].enabled)
      chan_synth(&snd.channels[i]);

  /* mix channels into a 16-bit accumulator, then clamp to +/-127 */
  middle = (int16_t *)malloc((size_t)topbufsize * sizeof(int16_t));
  if (!middle)
    goto fail;
  memset(middle, 0, (size_t)topbufsize * sizeof(int16_t));

  for (i = 0; i < CS_PXT_CHANNELS; ++i)
    if (snd.channels[i].enabled)
      for (s = 0; s < snd.channels[i].nsamples; ++s)
        middle[s] += snd.channels[i].buffer[s];

  for (s = 0; s < topbufsize; ++s)
  {
    int16_t v = middle[s];
    if (v > 127)       v = 127;
    else if (v < -127) v = -127;
    final_buffer[s] = (signed char)v;
  }

  free(middle);
  for (i = 0; i < CS_PXT_CHANNELS; ++i)
    free(snd.channels[i].buffer);

  if (out_len)
    *out_len = topbufsize;
  return final_buffer;

fail:
  free(middle);
  free(final_buffer);
  for (i = 0; i < CS_PXT_CHANNELS; ++i)
    free(snd.channels[i].buffer);
  return NULL;
}
