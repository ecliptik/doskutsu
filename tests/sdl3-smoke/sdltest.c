/*
 * sdltest.c — Phase 2d SDL3 DOS-backend smoke test.
 *
 * Exercises the same SDL3 DOS-backend code paths as upstream's testaudioinfo
 * and testdisplayinfo (init the audio + video subsystems, enumerate drivers,
 * enumerate devices, enumerate displays + fullscreen modes), but writes its
 * output via printf() to stdout instead of SDL_Log() to stderr.
 *
 * Why we don't use upstream tests directly: SDL_Log writes to stderr (see
 * vendor/SDL/src/SDL_log.c:808 — fprintf(stderr,...)). Our headless DOSBox-X
 * harness only redirects stdout — neither MS-DOS COMMAND.COM nor DOSBox-X's
 * built-in shell support `2>&1` syntax, so stderr cannot be merged at the
 * shell level. Patching SDL test source to call SDL_SetLogOutputFunction
 * would conflict with vendor/SDL/CLAUDE.md (no AI-generated code into SDL).
 * So we author this small probe in our own tree (MIT, doskutsu-only) and
 * call the same public SDL3 APIs the upstream tests do.
 *
 * The output format starts every line with a fixed prefix ("AUDIO:" /
 * "VIDEO:" / "MODE:" / "DEVICE:") so tests/run-sdl3-smoke.sh can grep for
 * known-stable substrings instead of matching the whole capture.
 *
 * Phase 8 baseline: the VIDEO:/MODE: lines are captured to
 * tests/fixtures/sdl3-modes-dosbox.txt for later diffing against real
 * hardware (M64VBE on the g2k Mach64) — divergence flags VESA backend
 * quirks early.
 *
 * License: MIT (this file). See LICENSE in repo root.
 */

#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

static void list_audio_devices(bool recording)
{
    int n = 0;
    const char *kind = recording ? "recording" : "playback";
    SDL_AudioDeviceID *devices = recording
        ? SDL_GetAudioRecordingDevices(&n)
        : SDL_GetAudioPlaybackDevices(&n);

    if (!devices) {
        printf("AUDIO-DEVICES-%s: error: %s\n", kind, SDL_GetError());
        return;
    }
    printf("AUDIO-DEVICES-%s: count=%d\n", kind, n);
    for (int i = 0; i < n; ++i) {
        const char *name = SDL_GetAudioDeviceName(devices[i]);
        printf("DEVICE-%s: %d %s\n", kind, i, name ? name : "(null)");
    }
    SDL_free(devices);
}

static int run_audio(void)
{
    /* Driver enumeration does NOT require SDL_Init — these are the
     * compile-time bootstrap entries. Print them first so the capture
     * proves the DOS audio driver is in the binary even if init below
     * fails (it might: see comment in SDLTEST-END). */
    int ndrv = SDL_GetNumAudioDrivers();
    printf("AUDIO-DRIVERS: count=%d\n", ndrv);
    for (int i = 0; i < ndrv; ++i) {
        printf("AUDIO-DRIVER: %d %s\n", i, SDL_GetAudioDriver(i));
    }

    if (!SDL_Init(SDL_INIT_AUDIO)) {
        /* Note: PR #15377's SoundBlaster detection currently fails under
         * DOSBox-X's SB16 emulation — DSP reset returns "ready" but the
         * post-reset 0xAA byte read is wrong. The audio driver IS compiled
         * in (see AUDIO-DRIVER lines above); init just can't pick a
         * working device under emulation. We treat this as a soft failure
         * here (still report what we can about audio state, then proceed)
         * and let the gate decide whether it's blocking. */
        printf("AUDIO-INIT: FAIL %s\n", SDL_GetError());
        printf("AUDIO-CURRENT: (none)\n");
        printf("AUDIO-DEVICES-playback: count=0\n");
        printf("AUDIO-DEVICES-recording: count=0\n");
        return 1;  /* reported in END line; doesn't abort the probe */
    }

    printf("AUDIO-INIT: OK\n");
    const char *cur = SDL_GetCurrentAudioDriver();
    printf("AUDIO-CURRENT: %s\n", cur ? cur : "(none)");

    list_audio_devices(false);
    list_audio_devices(true);

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return 0;
}

static int run_video(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("VIDEO-INIT: FAIL %s\n", SDL_GetError());
        return 1;
    }

    const char *vd = SDL_GetCurrentVideoDriver();
    printf("VIDEO-DRIVER: %s\n", vd ? vd : "(none)");

    int ndisp = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&ndisp);
    printf("VIDEO-DISPLAYS: count=%d\n", ndisp);

    for (int i = 0; displays && i < ndisp; ++i) {
        SDL_DisplayID dpy = displays[i];
        const char *name = SDL_GetDisplayName(dpy);
        SDL_Rect bounds = {0, 0, 0, 0};
        SDL_GetDisplayBounds(dpy, &bounds);
        printf("DISPLAY: %u name=%s bounds=%dx%d@%d,%d\n",
               (unsigned)dpy, name ? name : "(null)",
               bounds.w, bounds.h, bounds.x, bounds.y);

        const SDL_DisplayMode *cur_mode = SDL_GetCurrentDisplayMode(dpy);
        if (cur_mode) {
            printf("MODE-CURRENT: %u %dx%d %.2fHz fmt=%s\n",
                   (unsigned)dpy, cur_mode->w, cur_mode->h,
                   cur_mode->refresh_rate,
                   SDL_GetPixelFormatName(cur_mode->format));
        } else {
            printf("MODE-CURRENT: %u UNKNOWN (%s)\n",
                   (unsigned)dpy, SDL_GetError());
        }

        int nmodes = 0;
        SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(dpy, &nmodes);
        printf("MODES: %u count=%d\n", (unsigned)dpy, nmodes);
        for (int m = 0; modes && m < nmodes; ++m) {
            printf("MODE: %u %d %dx%d %.2fHz fmt=%s\n",
                   (unsigned)dpy, m,
                   modes[m]->w, modes[m]->h,
                   modes[m]->refresh_rate,
                   SDL_GetPixelFormatName(modes[m]->format));
        }
        SDL_free(modes);
    }
    SDL_free(displays);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return 0;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    /* Banner first so an empty capture file is unambiguously a crash. */
    printf("SDLTEST-BEGIN: doskutsu Phase 2d smoke (SDL3 %d.%d.%d)\n",
           SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
    fflush(stdout);

    int rc_audio = run_audio();
    fflush(stdout);
    int rc_video = run_video();
    fflush(stdout);

    SDL_Quit();

    int rc = (rc_audio != 0 || rc_video != 0) ? 1 : 0;
    printf("SDLTEST-END: rc=%d audio=%d video=%d\n", rc, rc_audio, rc_video);
    fflush(stdout);
    return rc;
}
