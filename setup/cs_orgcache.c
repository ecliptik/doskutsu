/*
 * cs_orgcache.c -- reader for the engine's pre-rendered Organya PCM cache.
 *
 * Mirrors the on-disk contract authored by vendor/nxengine-evo/src/sound/
 * Organya.cpp (patches 0208/0209/0215): a 64-byte OrgPcmHeader followed by raw
 * int16 PCM. See cs_orgcache.h for the SETUP-side rationale (no sha check).
 * ASCII-only.
 */

#include "cs_orgcache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* On-disk header -- byte-identical layout to Organya.cpp's OrgPcmHeader.
 * char[4]+char[16]+char[16] = 36 bytes, then 7 x uint32 (4-aligned at 36) = 64
 * bytes total, no padding (same DJGPP gcc ABI as the engine). */
typedef struct
{
  char     magic[4];        /* "OPC1" */
  char     build[16];       /* game build sha -- NOT validated by SETUP */
  char     song[16];        /* org basename */
  uint32_t sample_rate;
  uint32_t internal_rate;
  uint32_t loop_start;
  uint32_t loop_end;
  uint32_t loopstart_frame;
  uint32_t nframes;         /* int16 sample count in the payload */
  uint32_t channels;        /* 1 = mono / 2 = stereo */
} cs_org_header;

/* Try one candidate tier subdir. Returns a malloc'd buffer on success. */
static int16_t *try_one(int rate, int channels, const char *song, int max_ms,
                        uint32_t *out_samples, int *out_rate, int *out_channels)
{
  char          path[64];
  FILE         *fp;
  cs_org_header h;
  uint64_t      cap;
  uint32_t      want;
  int16_t      *buf;
  size_t        got;

  if (rate <= 0 || (channels != 1 && channels != 2))
    return NULL;

  /* CACHE/<rate>_<channels>/<song>.PCM, relative to CWD (SETUP runs in the
   * game dir; DOS FAT is case-insensitive). */
  snprintf(path, sizeof(path), "CACHE/%d_%d/%s.PCM", rate, channels, song);

  fp = fopen(path, "rb");
  if (!fp)
    return NULL;

  if (fread(&h, 1, sizeof(h), fp) != sizeof(h)) { fclose(fp); return NULL; }

  /* Validate magic + sanity only (NOT the build sha -- different binary). */
  if (h.magic[0] != 'O' || h.magic[1] != 'P' || h.magic[2] != 'C' || h.magic[3] != '1' ||
      h.sample_rate < 4000 || h.sample_rate > 48000 ||
      (h.channels != 1 && h.channels != 2) ||
      h.nframes == 0)
  {
    fclose(fp);
    return NULL;
  }

  /* Cap to max_ms worth of int16 samples: ms * rate * channels / 1000. */
  cap  = ((uint64_t)max_ms * h.sample_rate * h.channels) / 1000ULL;
  want = (h.nframes < cap) ? h.nframes : (uint32_t)cap;
  if (want == 0) { fclose(fp); return NULL; }

  buf = (int16_t *)malloc((size_t)want * sizeof(int16_t));
  if (!buf) { fclose(fp); return NULL; }

  got = fread(buf, sizeof(int16_t), want, fp);
  fclose(fp);
  if (got != want) { free(buf); return NULL; }

  if (out_samples)  *out_samples  = want;
  if (out_rate)     *out_rate     = (int)h.sample_rate;
  if (out_channels) *out_channels = (int)h.channels;
  return buf;
}

int16_t *cs_orgcache_load(const char *song, int prefer_rate, int prefer_channels,
                          int max_ms, uint32_t *out_samples,
                          int *out_rate, int *out_channels)
{
  /* Candidate tiers: preferred first, then the two canonical sets. Duplicates
   * are harmless (the first hit returns). */
  static const int cand[][2] = { {11025, 1}, {22050, 2} };
  int16_t *b;
  int      i;

  if (out_samples)  *out_samples  = 0;
  if (out_rate)     *out_rate     = 0;
  if (out_channels) *out_channels = 0;
  if (!song || !song[0])
    return NULL;

  b = try_one(prefer_rate, prefer_channels, song, max_ms,
              out_samples, out_rate, out_channels);
  if (b)
    return b;

  for (i = 0; i < (int)(sizeof(cand) / sizeof(cand[0])); ++i)
  {
    if (cand[i][0] == prefer_rate && cand[i][1] == prefer_channels)
      continue; /* already tried */
    b = try_one(cand[i][0], cand[i][1], song, max_ms,
                out_samples, out_rate, out_channels);
    if (b)
      return b;
  }
  return NULL;
}
