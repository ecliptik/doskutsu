/*
 * imagetest.c — Phase B (path-B) SDL3_image DJGPP functional smoke test.
 *
 * Loads a 68-byte hand-built 1x1 RGBA transparent PNG via IMG_Load_IO and
 * verifies the returned SDL_Surface has the expected geometry. Confirms:
 *   - libSDL3_image.a links against libSDL3.a under DJGPP
 *   - PNG decoder (stb_image) initializes
 *   - Surface allocation works on DPMI
 *   - IMG_Load_IO returns a valid SDL_Surface with width=1, height=1
 *
 * The PNG byte array was generated via Python (zlib.crc32 + zlib.compress).
 * Smallest valid 1x1 PNG without palette: 68 bytes (sig + IHDR + IDAT + IEND).
 *
 * License: MIT (this file). doskutsu-authored. See LICENSE in repo root.
 */

#include <stdio.h>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

static const unsigned char tiny_png[68] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
    0x0B, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0x60, 0x00, 0x02, 0x00,
    0x00, 0x05, 0x00, 0x01, 0xE9, 0xFA, 0xDC, 0xD8, 0x00, 0x00, 0x00, 0x00,
    0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    int rc_init = 0, rc_iostream = 0, rc_load = 0, rc_dims = 0;

    printf("IMGTEST-BEGIN: doskutsu Path B SDL3_image smoke (SDL3_image %d.%d.%d)\n",
           SDL_IMAGE_MAJOR_VERSION, SDL_IMAGE_MINOR_VERSION, SDL_IMAGE_MICRO_VERSION);
    fflush(stdout);

    /* SDL3_image's IMG_Load_IO doesn't strictly require SDL_Init for the
     * decoder path (no audio/video subsystem). But initializing video
     * proves the SDL3 runtime + DPMI are healthy before we touch image. */
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("SDL-INIT: FAIL %s\n", SDL_GetError());
        rc_init = 1;
        goto end;
    }
    printf("SDL-INIT: OK driver=%s\n", SDL_GetCurrentVideoDriver());
    fflush(stdout);

    /* Load the embedded PNG via in-memory IOStream. */
    SDL_IOStream *iostream = SDL_IOFromMem((void *)tiny_png, sizeof(tiny_png));
    if (!iostream) {
        printf("IO-FROM-MEM: FAIL %s\n", SDL_GetError());
        rc_iostream = 1;
        goto end_sdl;
    }
    printf("IO-FROM-MEM: OK bytes=%zu\n", sizeof(tiny_png));
    fflush(stdout);

    SDL_Surface *surface = IMG_Load_IO(iostream, /*closeio=*/true);
    if (!surface) {
        printf("IMG-LOAD: FAIL %s\n", SDL_GetError());
        rc_load = 1;
        goto end_sdl;
    }
    printf("IMG-LOAD: OK\n");
    fflush(stdout);

    if (surface->w == 1 && surface->h == 1) {
        printf("IMG-DIMS: OK 1x1 fmt=%s\n", SDL_GetPixelFormatName(surface->format));
    } else {
        printf("IMG-DIMS: FAIL got %dx%d, expected 1x1\n", surface->w, surface->h);
        rc_dims = 1;
    }
    fflush(stdout);

    SDL_DestroySurface(surface);

end_sdl:
    SDL_Quit();
end:
    {
        int rc = (rc_init || rc_iostream || rc_load || rc_dims) ? 1 : 0;
        printf("IMGTEST-END: rc=%d init=%d io=%d load=%d dims=%d\n",
               rc, rc_init, rc_iostream, rc_load, rc_dims);
        fflush(stdout);
        return rc;
    }
}
