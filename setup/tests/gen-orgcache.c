/*
 * gen-orgcache.c -- generate a synthetic Organya pre-render PCM cache file so
 * the SETUP audio test's organya-mode REAL-snippet path can be exercised in a
 * DOSBox-X / E2E walk without running the game to populate the cache.
 *
 * HOST tool (build with cc, run on the build host before staging). It writes a
 * valid 64-byte OPC1 header + a few seconds of an audible mono tone, matching
 * the on-disk contract in vendor/nxengine-evo/src/sound/Organya.cpp (the build
 * sha is a placeholder -- SETUP does not validate it, by design).
 *
 * This is a TEST FIXTURE generator: the tone is synthetic, NOT Cave Story
 * content. Nothing Cave-Story-derived is produced or shipped.
 *
 * Usage:
 *   cc -std=gnu99 -o gen-orgcache setup/tests/gen-orgcache.c
 *   ./gen-orgcache <stage-root>     # writes <stage-root>/CACHE/11025_1/CURLY.PCM
 *
 * Then SETUP in organya mode (Tier-2 11025 mono) finds it and plays the first
 * ~5 s. ASCII-only.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Byte-identical to Organya.cpp's OrgPcmHeader (64 bytes). */
typedef struct
{
  char     magic[4];
  char     build[16];
  char     song[16];
  uint32_t sample_rate;
  uint32_t internal_rate;
  uint32_t loop_start;
  uint32_t loop_end;
  uint32_t loopstart_frame;
  uint32_t nframes;
  uint32_t channels;
} OrgPcmHeader;

int main(int argc, char **argv)
{
  const char  *root = (argc > 1) ? argv[1] : ".";
  const int    rate = 11025, ch = 1, seconds = 6;
  const uint32_t nframes = (uint32_t)(rate * seconds);
  char         buf[1024], path[1024];
  OrgPcmHeader h;
  FILE        *fp;
  uint32_t     i;
  int16_t     *pcm;

  if (strlen(root) > 800) { fprintf(stderr, "root path too long\n"); return 1; }
  snprintf(buf, sizeof(buf), "%s/CACHE", root);            mkdir(buf, 0777);
  snprintf(buf, sizeof(buf), "%s/CACHE/%d_%d", root, rate, ch); mkdir(buf, 0777);
  snprintf(path, sizeof(path), "%s/CACHE/%d_%d/CURLY.PCM", root, rate, ch);

  memset(&h, 0, sizeof(h));
  memcpy(h.magic, "OPC1", 4);
  snprintf(h.build, sizeof(h.build), "TESTFIXTURE0");  /* not validated by SETUP */
  snprintf(h.song, sizeof(h.song), "CURLY");
  h.sample_rate     = rate;
  h.internal_rate   = rate;
  h.loop_start      = 0;
  h.loop_end        = nframes;
  h.loopstart_frame = 0;
  h.nframes         = nframes;
  h.channels        = ch;

  pcm = (int16_t *)malloc((size_t)nframes * sizeof(int16_t));
  if (!pcm) { fprintf(stderr, "oom\n"); return 1; }
  /* A simple two-note alternation so the snippet is clearly audible / non-silent. */
  for (i = 0; i < nframes; ++i)
  {
    double hz = ((i / (rate / 2)) & 1) ? 523.25 : 392.00; /* C5 / G4 */
    pcm[i] = (int16_t)(9000.0 * sin(2.0 * 3.14159265 * hz * (double)i / rate));
  }

  fp = fopen(path, "wb");
  if (!fp) { fprintf(stderr, "cannot write %s\n", path); free(pcm); return 1; }
  fwrite(&h, 1, sizeof(h), fp);
  fwrite(pcm, sizeof(int16_t), nframes, fp);
  fclose(fp);
  free(pcm);
  printf("wrote %s (%u frames, %d Hz mono, OPC1)\n", path, nframes, rate);
  return 0;
}
