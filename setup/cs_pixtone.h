#ifndef SETUP_CS_PIXTONE_H
#define SETUP_CS_PIXTONE_H

/*
 * cs_pixtone.h -- standalone Cave Story Pixtone SFX synthesizer for SETUP.EXE.
 *
 * SETUP's audio test plays a representative effect so the operator can confirm
 * the SB16 PCM path on real hardware. The default is a generated sine blip;
 * this module instead synthesizes the REAL effect (e.g. the Polar Star shot)
 * from the user's own .pxt parameter file, exactly as the engine does.
 *
 * A .pxt file stores PARAMETERS, not PCM -- the game always synthesizes the
 * waveform at load. This is a plain-C port of the engine's Pixtone synth core
 * (vendor/nxengine-evo/src/sound/Pixtone.cpp: wave-table init, fgetv text
 * parser, stPXSound::load, stPXChannel::synth, stPXEnvelope::evaluate,
 * stPXSound::render). No engine link, no SDL dependency, no C++; it reads the
 * user's data at RUNTIME (same as the game) and emits raw PCM the caller hands
 * to SDL3_mixer.
 *
 * Nothing Cave-Story-derived is committed or shipped: this is our own code,
 * and it reads the player's installed data/pxt/<name>.pxt at run time.
 *
 * DOS/DJGPP constraints honored: fopen(path, "rb"); no threads; static; the
 * double/sin math runs on the SETUP main thread (no ISR / FPU constraint).
 * ASCII-only.
 */

#include <stdint.h>

/*
 * Synthesize the .pxt effect at `path` into a freshly malloc'd 8-bit mono
 * signed PCM buffer at the .pxt native storage rate (22050 Hz). On success
 * returns the buffer (caller owns -> free()) and writes the sample count to
 * *out_len. Returns NULL on file-not-found / parse failure / out-of-memory
 * (caller must fall back to a generated tone). *out_len is set to 0 on failure.
 *
 * The output spec the caller hands to the mixer is { SDL_AUDIO_S8, 1, 22050 };
 * the mixer resamples to the device format on playback.
 */
signed char *cs_pixtone_render(const char *path, uint32_t *out_len);

#endif /* SETUP_CS_PIXTONE_H */
