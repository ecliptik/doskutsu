# Third-Party Components

Complete attribution and license matrix for everything DOSKUTSU touches, vendors, or ships. Kept in sync with `vendor/sources.manifest` and `PLAN.md § Licensing`.

---

## At-a-glance

| Component | Version / Ref | License | Shipped in dist? | Role |
|---|---|---|---|---|
| [NXEngine-evo](https://github.com/nxengine/nxengine-evo) | `master` @ `1f093d1` | **GPLv3** | Yes (statically linked) | Cave Story engine re-implementation |
| [SDL3](https://github.com/libsdl-org/SDL) | `main` @ `74a7462` (post-[PR #15377](https://github.com/libsdl-org/SDL/pull/15377)) | zlib | Yes (statically linked) | Platform abstraction + DOS backend |
| [sdl2-compat](https://github.com/libsdl-org/sdl2-compat) | `main` @ `91d36b8` | zlib | Yes (statically linked) | SDL2-API shim forwarding to SDL3 |
| [SDL_mixer](https://github.com/libsdl-org/SDL_mixer) | `release-2.8.x` @ `2b00802` | zlib | Yes (statically linked) | Audio mixing + Organya PCM path |
| [SDL_image](https://github.com/libsdl-org/SDL_image) | `release-2.8.x` @ `67c8f53` | zlib | Yes (statically linked) | PNG loading |
| stb_vorbis | bundled in SDL_mixer | **public domain / MIT** | Yes (via SDL_mixer) | OGG Vorbis decoder |
| stb_image | bundled in SDL_image | **public domain / MIT** | Yes (via SDL_image) | PNG decoder |
| [DJGPP libc](https://www.delorie.com/djgpp/) | 2.05+ (via GCC 12.2.0) | **GPL + runtime-library exception** | Yes (statically linked) | C runtime on DOS |
| [CWSDPMI](https://sandmann.dotster.com/cwsdpmi/) | r7 | **freeware, redistribution permitted** | Yes (separate .exe, not linked) | DPMI host |
| [Cave Story / Doukutsu Monogatari](https://www.cavestory.org/) | 2004 freeware EN | **freeware per Pixel's 2004 terms** | **No** — user-extracted | Game content (maps, sprites, music, text) |
| [DOSBox-X](https://dosbox-x.com/) | system package | GPLv2 | No (dev-only) | Pre-hardware testing emulator |
| [andrewwutw/build-djgpp](https://github.com/andrewwutw/build-djgpp) | upstream | MIT-like | No (dev-only) | DJGPP installer script |

---

## License compatibility analysis

### The binary is GPLv3

`DOSKUTSU.EXE` statically links NXEngine-evo, which is GPLv3. Under GPLv3's copyleft, the combined work is GPLv3. Everything else linked in must be GPLv3-compatible:

- **zlib** (SDL3, sdl2-compat, SDL_mixer, SDL_image) is GPLv3-compatible (FSF-listed as compatible).
- **stb_* public domain / MIT** are GPLv3-compatible.
- **DJGPP libc's runtime-library exception** explicitly permits distributing statically-linked binaries under terms of the program's own license — exactly how libstdc++'s exception works. Statically linking DJGPP libc into a GPLv3 binary is fine; the libc does not re-impose its own GPL on downstream.

All GPLv3-compatible. No conflicts.

### MIT source in a GPLv3 binary world

The source code in this repository is licensed MIT:

- Makefile, build scripts, tools/, docs — **MIT**.
- `tests/smoketest/hello.c` — **MIT** (no upstream code in it).
- `patches/SDL/*.patch`, `patches/sdl2-compat/*.patch`, `patches/SDL_mixer/*.patch`, `patches/SDL_image/*.patch` — **derivatives of zlib-licensed upstreams**, therefore zlib. More permissive than MIT; no conflict.
- `patches/nxengine-evo/*.patch` — **derivatives of a GPLv3 upstream**, therefore GPLv3. This does not conflict with the repo's `LICENSE` — our MIT license correctly describes our original, non-derivative code, and the GPLv3 on patches is inherited by operation of copyright law, not by our choice.

No conflicts. The MIT license on the repo is accurate.

### CWSDPMI is separate

CWSDPMI is a DPMI host — a separate `.exe` that `DOSKUTSU.EXE` invokes at runtime via the DPMI API. It is not statically linked, not dynamically linked, not an ABI-level dependency in the C-library sense. Same legal posture as shipping `glibc.so` alongside a GPL binary, or a GPL program invoking `/bin/sh`: aggregation, not combination.

CWSDPMI's license requires:
- Redistribution is permitted
- The accompanying `CWSDPMI.DOC` must be included
- No fee beyond media / transfer costs (the dist zip is free)

We satisfy all three by shipping `CWSDPMI.EXE` + `CWSDPMI.DOC` in `dist/doskutsu-cf.zip`.

### Cave Story data is user-supplied

Pixel released the 2004 `Doukutsu.exe` as freeware with redistribution permitted under his terms. We do not redistribute it — users extract their own assets per `docs/ASSETS.md`. This keeps us cleanly out of:

- Pixel's 2004 terms (we aren't the redistributor)
- NXEngine-evo's GPLv3 attempting to re-license game data by inclusion (nothing is included)
- Any ambiguity about whether `data/base/*.pxm` etc. are "part of the work"

---

## Full per-component detail

### NXEngine-evo

- **License:** GPL-3.0-only (see `vendor/nxengine-evo/LICENSE` once cloned)
- **Source:** https://github.com/nxengine/nxengine-evo
- **Pinned ref:** `master` @ `1f093d1423cc395eb199230cd609b806ef1daa36` (per `vendor/sources.manifest`)
- **Role:** Cave Story engine re-implementation in C++11. Statically linked into `DOSKUTSU.EXE`.
- **Modifications:** DOS-port patches in `patches/nxengine-evo/*.patch` (see `TODO.md` Phase 5 for the full list)
- **Redistribution:** GPLv3 terms — we include `vendor/nxengine-evo/LICENSE` as `GPLV3.TXT` in `dist/doskutsu-cf.zip` and point to this repo for corresponding source

### SDL3

- **License:** zlib (see `vendor/SDL/LICENSE.txt` once cloned)
- **Source:** https://github.com/libsdl-org/SDL
- **Pinned ref:** `main` @ `74a746281f2208e07a7680560fcb7ec57565228e` (post-[PR #15377](https://github.com/libsdl-org/SDL/pull/15377), the DOS-backend merge)
- **Role:** Platform abstraction. The DOS backend (VESA video + SoundBlaster audio drivers from PR #15377) is what makes this entire port possible.
- **Modifications:** none yet. Any DJGPP fixes land in `patches/SDL/*.patch`. Phase 2d uncovered an SB16-detection bug under DOSBox-X — see CHANGELOG `### Known issues` and TODO #16 / #17.
- **Redistribution:** zlib permits redistribution under the same terms

### sdl2-compat

- **License:** zlib
- **Source:** https://github.com/libsdl-org/sdl2-compat
- **Pinned ref:** `main` @ `91d36b8d9d06958e2663623d100d12b596675120`
- **Role:** Pure-C shim exposing the SDL2 API on top of SDL3. Lets NXEngine-evo (SDL2-written) link against SDL3 (the only SDL with a DOS backend) without the SDL2→SDL3 migration.
- **Modifications:** expected — DJGPP port patches land in `patches/sdl2-compat/`
- **Redistribution:** zlib

### SDL_mixer (release-2.8.x)

- **License:** zlib
- **Source:** https://github.com/libsdl-org/SDL_mixer
- **Pinned ref:** `release-2.8.x` @ `2b00802865e614f994b85264069c7933cdf538e2`
- **Role:** Audio decoder + mixer. We enable WAV and OGG (via stb_vorbis) only; MP3, MOD, MIDI, FLAC, Opus are all disabled in our CMake options. Keeps the dependency graph zlib + public-domain.
- **Included vendored code:** stb_vorbis (public domain / MIT dual)
- **Modifications:** none expected; any DJGPP fixes land in `patches/SDL_mixer/`

### SDL_image (release-2.8.x)

- **License:** zlib
- **Source:** https://github.com/libsdl-org/SDL_image
- **Pinned ref:** `release-2.8.x` @ `67c8f531ad09ddc0e6d4c7b1468c863235711ed4`
- **Role:** Image loader. We enable PNG only (via stb_image); libpng, libjpeg, WebP, AVIF, TIFF are all disabled.
- **Included vendored code:** stb_image (public domain / MIT dual)

### DJGPP libc

- **License:** GPL with the same runtime-library exception used by libstdc++ ([details](https://www.delorie.com/djgpp/doc/libc/)). The exception explicitly permits distributing statically-linked binaries under whatever license you want, without imposing GPL on downstream.
- **Source:** https://www.delorie.com/djgpp/
- **Role:** C runtime for DJGPP-compiled binaries. Statically linked into everything.
- **Redistribution:** covered by the runtime-library exception; no specific action needed in dist zip beyond attribution here

### CWSDPMI

- **License:** freeware with specific redistribution terms; see `vendor/cwsdpmi/cwsdpmi.doc`
- **Source:** https://sandmann.dotster.com/cwsdpmi/
- **Role:** DPMI host — provides 32-bit protected mode services to DJGPP binaries on DOS
- **Shipped:** `CWSDPMI.EXE` + `CWSDPMI.DOC` in `dist/doskutsu-cf.zip`. **Not statically linked**; it's a separate `.exe` invoked at runtime.
- **Redistribution:** permitted with `CWSDPMI.DOC` bundled

### Cave Story / Doukutsu Monogatari (game data)

- **License:** freeware, redistribution permitted per Daisuke "Pixel" Amaya's 2004 terms
- **Source:** https://www.cavestory.org/ (2004 EN freeware)
- **Role:** the actual game — maps, sprites, music, dialogue, NPCs
- **Shipped:** **No.** Users extract their own per `docs/ASSETS.md`.

### DOSBox-X

- **License:** GPLv2
- **Role:** Pre-hardware testing emulator. Dev tool only — not shipped, not linked.
- **Our use:** `tools/dosbox-x.conf`, `tools/dosbox-x-fast.conf`, `tools/dosbox-launch.sh`, `tools/dosbox-run.sh`

### andrewwutw/build-djgpp

- **License:** MIT-like (see upstream repo)
- **Role:** Wrapper script that installs DJGPP. Wrapped by `~/emulators/scripts/update-djgpp.sh`.

### Sibling projects (documentation inspiration)

- **vellm** (DOS port pattern): https://forgejo.ecliptik.com/ecliptik/vellm — MIT
- **Geomys** (doc style, team structure): https://codeberg.org/ecliptik/geomys — ISC
- **Flynn** (doc style, team structure): https://codeberg.org/ecliptik/flynn — ISC

No code from these projects is linked into DOSKUTSU; they are stylistic and structural references only.

---

## Verification

Before cutting a release, verify `dist/doskutsu-cf.zip` contains:

- [ ] `DOSKUTSU.EXE`
- [ ] `CWSDPMI.EXE`
- [ ] `CWSDPMI.DOC` (CWSDPMI license, required by its terms)
- [ ] `LICENSE.TXT` (this repo's MIT)
- [ ] `GPLV3.TXT` (NXEngine-evo's GPLv3)
- [ ] `THIRD-PARTY.TXT` (CRLF-normalized version of this file)
- [ ] `README.TXT` (user-facing DOS-readable quick-start)

The `make dist` target in the root Makefile is the source of truth for what ends up in the zip. If it diverges from this list, fix the Makefile and this document together.
