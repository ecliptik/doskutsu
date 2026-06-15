#ifndef SETUP_CS_ORGCACHE_H
#define SETUP_CS_ORGCACHE_H

/*
 * cs_orgcache.h -- reader for the engine's pre-rendered Organya PCM cache.
 *
 * In Organya mode the game can pre-render each song to a disk PCM cache
 * (engine patches 0208 / 0209 / 0215): CACHE/<rate>_<channels>/<NAME>.PCM, a
 * 64-byte OrgPcmHeader ("OPC1") followed by raw int16 PCM at the header's
 * sample_rate / channels. SETUP's music test, in Organya mode, plays a short
 * snippet of the real Title theme by reading that cache from the user's disk --
 * no Organya synth port, nothing Cave-Story-derived committed or shipped.
 *
 * SETUP is a DIFFERENT binary than DOSKUTSU.EXE, so it intentionally does NOT
 * validate the cache's build-sha field (that is the game's compile fingerprint,
 * used by the game to invalidate its own cache). SETUP only needs to PLAY the
 * audio, so it validates the magic + a sane rate/channels/length and reads the
 * raw payload; the mixer converts it to the device format on playback.
 *
 * DOS/DJGPP: fopen(path, "rb"); no threads; static; ASCII-only.
 */

#include <stdint.h>

/*
 * Load up to `max_ms` of the pre-rendered cache for `song` (the uppercase 8.3
 * org basename, e.g. "CURLY"). Tries the preferred tier subdir
 * (CACHE/<prefer_rate>_<prefer_channels>/) first, then the canonical tiers
 * (11025_1, 22050_2), so a cache built for either audio quality is found.
 *
 * On success returns a malloc'd int16 buffer (caller free()s) and writes:
 *   *out_samples  -- total int16 count in the buffer (capped to max_ms)
 *   *out_rate     -- sample rate from the cache header
 *   *out_channels -- 1 (mono) or 2 (stereo) from the cache header
 * Returns NULL on miss / bad format / OOM (outputs left at 0).
 */
int16_t *cs_orgcache_load(const char *song, int prefer_rate, int prefer_channels,
                          int max_ms, uint32_t *out_samples,
                          int *out_rate, int *out_channels);

#endif /* SETUP_CS_ORGCACHE_H */
