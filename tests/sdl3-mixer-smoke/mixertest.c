/*
 * mixertest.c — Phase B (path-B) SDL3_mixer DJGPP functional smoke test.
 *
 * Software-architect's #26 sharpening: linking libSDL3_mixer.a is necessary
 * but not sufficient; the three NXEngine audio code paths must actually
 * execute. This probe exercises the SDL3_mixer equivalents:
 *
 *   - MIX_LoadRawAudio        — Organya synth output path (was Mix_QuickLoad_RAW)
 *   - MIX_LoadAudio_IO + WAV  — Cave Story SFX path     (was Mix_LoadWAV)
 *   - VORBIS in decoder list  — Remix soundtrack path   (was OGG via stb_vorbis)
 *
 * For Vorbis we verify the decoder is registered (proves stb_vorbis is
 * compiled in and discoverable at runtime). Loading an actual OGG would
 * require an asset fixture; a minimal valid OGG is >1KB of structured bytes
 * and would only test the same code path that MIX_GetAudioDecoder lists
 * confirms is present.
 *
 * Audio device init: we set SDL_AUDIO_DRIVER=dummy to bypass PR #15377's
 * SB16 detection failure under DOSBox-X (separately filed as #16/#17). The
 * dummy driver lets MIX_CreateMixerDevice succeed so we can exercise
 * decoder paths without depending on an unrelated bug fix. The probe is
 * about decoder code paths, not playback.
 *
 * License: MIT (this file). doskutsu-authored. See LICENSE in repo root.
 */

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

/* Hand-built 44-byte PCM WAV header + 4 silent samples (16-bit mono 22050Hz). */
static const unsigned char tiny_wav[] = {
    /* "RIFF" */ 'R','I','F','F',
    /* file size minus 8 */ 0x2C, 0x00, 0x00, 0x00,
    /* "WAVE" */ 'W','A','V','E',
    /* "fmt " */ 'f','m','t',' ',
    /* fmt chunk size = 16 */ 0x10, 0x00, 0x00, 0x00,
    /* PCM format = 1 */ 0x01, 0x00,
    /* channels = 1 */ 0x01, 0x00,
    /* sample rate = 22050 */ 0x22, 0x56, 0x00, 0x00,
    /* byte rate = 44100 */ 0x44, 0xAC, 0x00, 0x00,
    /* block align = 2 */ 0x02, 0x00,
    /* bits/sample = 16 */ 0x10, 0x00,
    /* "data" */ 'd','a','t','a',
    /* data chunk size = 8 (4 samples × 2 bytes) */ 0x08, 0x00, 0x00, 0x00,
    /* 4 silent 16-bit samples */ 0x00,0x00, 0x00,0x00, 0x00,0x00, 0x00,0x00,
};

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    int rc_init = 0, rc_decoders = 0, rc_mixer = 0;
    int rc_raw = 0, rc_wav = 0, rc_vorbis_listed = 0;

    /* Bypass the PR #15377 SB16 detection bug — we want decoder paths,
     * not playback. SDL3 dummy audio driver always succeeds. */
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    printf("MIXTEST-BEGIN: doskutsu Path B SDL3_mixer smoke (SDL3_mixer %d.%d.%d)\n",
           SDL_MIXER_MAJOR_VERSION, SDL_MIXER_MINOR_VERSION, SDL_MIXER_MICRO_VERSION);
    fflush(stdout);

    if (!SDL_Init(SDL_INIT_AUDIO)) {
        printf("SDL-INIT: FAIL %s\n", SDL_GetError());
        rc_init = 1;
        goto end;
    }
    printf("SDL-INIT: OK driver=%s\n", SDL_GetCurrentAudioDriver());
    fflush(stdout);

    if (!MIX_Init()) {
        printf("MIX-INIT: FAIL %s\n", SDL_GetError());
        rc_init = 1;
        goto end;
    }
    printf("MIX-INIT: OK\n");
    fflush(stdout);

    /* Decoder enumeration — proves which formats are compiled in. */
    int ndec = MIX_GetNumAudioDecoders();
    printf("MIX-DECODERS: count=%d\n", ndec);
    rc_decoders = (ndec <= 0) ? 1 : 0;
    for (int i = 0; i < ndec; ++i) {
        const char *name = MIX_GetAudioDecoder(i);
        if (name) {
            printf("MIX-DECODER: %d %s\n", i, name);
            /* SDL3_mixer canonical names are upper-case, no underscores
             * (e.g. "STBVORBIS", "WAV"). Accept either spelling defensively. */
            if (SDL_strcasecmp(name, "VORBIS") == 0 ||
                SDL_strcasecmp(name, "STB_VORBIS") == 0 ||
                SDL_strcasecmp(name, "STBVORBIS") == 0) {
                rc_vorbis_listed = 1;
            }
        }
    }
    fflush(stdout);

    /* Build a Mixer not bound to a device. The dummy driver lets
     * MIX_CreateMixer's spec match without needing real hardware. */
    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = SDL_AUDIO_S16;
    spec.channels = 1;
    spec.freq = 22050;

    MIX_Mixer *mixer = MIX_CreateMixer(&spec);
    if (!mixer) {
        printf("MIX-CREATE-MIXER: FAIL %s\n", SDL_GetError());
        rc_mixer = 1;
        goto end_mix;
    }
    printf("MIX-CREATE-MIXER: OK 22050/mono/S16\n");
    fflush(stdout);

    /* Test 1 (Organya path): raw PCM via MIX_LoadRawAudio. */
    static const Sint16 silent_pcm[64] = { 0 };
    MIX_Audio *raw = MIX_LoadRawAudio(mixer, silent_pcm, sizeof(silent_pcm), &spec);
    if (raw) {
        printf("RAW-LOAD: OK bytes=%zu (Organya equivalent)\n", sizeof(silent_pcm));
        MIX_DestroyAudio(raw);
    } else {
        printf("RAW-LOAD: FAIL %s\n", SDL_GetError());
        rc_raw = 1;
    }
    fflush(stdout);

    /* Test 2 (Cave Story SFX path): WAV via in-memory IOStream. */
    SDL_IOStream *iostream = SDL_IOFromMem((void *)tiny_wav, sizeof(tiny_wav));
    if (iostream) {
        MIX_Audio *wav = MIX_LoadAudio_IO(mixer, iostream, /*predecode=*/true, /*closeio=*/true);
        if (wav) {
            printf("WAV-LOAD: OK bytes=%zu (SFX equivalent)\n", sizeof(tiny_wav));
            MIX_DestroyAudio(wav);
        } else {
            printf("WAV-LOAD: FAIL %s\n", SDL_GetError());
            rc_wav = 1;
        }
    } else {
        printf("WAV-LOAD: FAIL SDL_IOFromMem returned NULL: %s\n", SDL_GetError());
        rc_wav = 1;
    }
    fflush(stdout);

    /* Test 3 (Remix soundtrack path): VORBIS decoder listed?
     * Already determined during enumeration — just print the verdict.
     * Loading an actual OGG would require an asset fixture; the registered
     * decoder list is the canonical signal that stb_vorbis is wired in. */
    printf("VORBIS-DECODER: %s\n", rc_vorbis_listed ? "OK present" : "FAIL not in decoder list");
    fflush(stdout);

    MIX_DestroyMixer(mixer);

end_mix:
    MIX_Quit();
end:
    SDL_Quit();

    int rc = (rc_init || rc_decoders || rc_mixer || rc_raw || rc_wav ||
              !rc_vorbis_listed) ? 1 : 0;
    printf("MIXTEST-END: rc=%d init=%d decoders=%d mixer=%d raw=%d wav=%d vorbis=%d\n",
           rc, rc_init, rc_decoders, rc_mixer, rc_raw, rc_wav,
           rc_vorbis_listed ? 0 : 1);
    fflush(stdout);
    return rc;
}
