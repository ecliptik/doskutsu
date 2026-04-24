# DOSKUTSU Implementation Plan

Phased roadmap for porting Cave Story (Doukutsu Monogatari) via NXEngine-evo to MS-DOS 6.22. For the project overview (what, why, how it all fits together), see `DOSKUTSU.md`. For toolchain and build system details, see `CLAUDE.md` and `BUILDING.md`.

---

## Locked Decisions

These are answered and **not** open for re-litigation mid-phase. If a phase reveals one needs to change, flag it explicitly as a plan amendment.

| # | Decision | Chosen |
|---|---|---|
| 1 | Repo hosting | Forgejo primary (`ssh://git@forgejo.ecliptik.com/ecliptik/doskutsu.git`). No GitHub / Codeberg mirrors for now. |
| 2 | Build system | Top-level `Makefile` orchestrating five stages (SDL3 → sdl2-compat → SDL2_mixer + SDL2_image → NXEngine-evo). Each stage is a CMake invocation. |
| 3 | Vendoring | Snapshots under `vendor/<name>/` with `vendor/sources.manifest` pinning SHAs + `patches/<name>/*.patch` applied at build time. Clones are gitignored; manifest + patches are tracked. |
| 4 | `[DOSKUTSU]` CONFIG.SYS profile | **No dedicated profile in this repo.** Run under existing `[VIBRA]`-style SB16+NOEMS boot; suggestions live in `docs/BOOT.md`. |
| 5 | Widescreen / Full HD in NXEngine-evo | **Lock at runtime to 320x240 fullscreen.** Code paths remain compiled in for future revisit. |
| 6 | Cave Story version | EN freeware 2004 (what NXEngine-evo expects by default). |
| 7 | Game data files | Gitignored. `docs/ASSETS.md` documents extraction from the 2004 freeware `Doukutsu.exe`. |
| 8 | DOSBox-X config | Two configs: `tools/dosbox-x.conf` (parity, `cycles=fixed 40000`, approximates PODP83) and `tools/dosbox-x-fast.conf` (`cycles=max`, iteration only). |
| 9 | Binary rename | Rename in CMake via `set_target_properties(nx PROPERTIES OUTPUT_NAME doskutsu)`. Applied by the NXEngine-evo port patch. |
| 10 | `emulators/` hub integration | Symlink `tools/djgpp` to `~/emulators/tools/djgpp` via `scripts/setup-symlinks.sh`. Documented in BUILDING.md for clone-to-build. |

---

## Licensing

DOSKUTSU's own source (the port glue in this repo — build system, patches, port-specific code, docs) is **MIT**. But the distributed `DOSKUTSU.EXE` binary statically links NXEngine-evo, which is **GPLv3**. Under GPLv3's linking clause, the combined binary work is **GPLv3**. MIT on our source is still valid and useful (re-use of our patches / build system / docs in other projects is unrestricted), but downstream redistribution of the binary must satisfy GPLv3.

### Component license matrix

| Component | License | How it reaches the user | Binary-linked? | Notes |
|---|---|---|---|---|
| DOSKUTSU source in this repo | **MIT** | source (git) | n/a | `LICENSE` in repo root. Applies to port glue, build scripts, patches, docs. |
| NXEngine-evo | **GPLv3** | statically linked into `DOSKUTSU.EXE` | Yes | The dominant license of the combined binary. Makes `DOSKUTSU.EXE` GPLv3-as-a-whole. |
| SDL3 (with DOS backend) | **zlib** | statically linked | Yes | zlib is GPLv3-compatible. |
| sdl2-compat | **zlib** | statically linked | Yes | zlib. |
| SDL2_mixer (release-2.8.x) | **zlib** | statically linked | Yes | zlib. Vendored stb_vorbis inside is BSD/public-domain. We exclude MP3 / MOD / MIDI / FLAC, so no LGPL / libsmpeg concerns. |
| SDL2_image (release-2.8.x) | **zlib** | statically linked | Yes | zlib. Uses stb_image (public domain / MIT). libpng / libjpeg are OFF in our CMake options. |
| DJGPP libc (`libc.a`) | **modified GPL + historic permissive** | statically linked into `DOSKUTSU.EXE` | Yes | DJGPP libc has a special "GPL with exception for the runtime library" (similar to libstdc++'s runtime-library exception) that explicitly permits distributing statically-linked binaries without imposing GPL on downstream user programs. This is what lets commercial DJGPP programs exist. GPLv3-compatible. |
| CWSDPMI (`cwsdpmi.exe`) | **freeware with redistribution permitted** | **separate executable** shipped alongside | No | **Not linked** into `DOSKUTSU.EXE` — it's a separate DPMI host invoked at runtime. Same legal posture as shipping glibc alongside a GPL binary. Redistribution terms are documented in `vendor/cwsdpmi/cwsdpmi.doc` (must be bundled with the binary). |
| Cave Story game data (`.pxm`, `.pxe`, `.pxa`, `.org`, sprites) | **freeware per Pixel's 2004 terms** | **user-supplied at runtime** | No | Not in this repo. Not in the dist bundle. Users extract from the 2004 `Doukutsu.exe` themselves. This keeps us clear of both Pixel's terms and the NXEngine-evo GPLv3 attempting to swallow game data. |
| NXEngine-evo's bundled engine data (fonts, PBM, JSON) | **GPLv3 (inherited from NXEngine-evo)** | shipped in `DATA\` | No | Cloned from `vendor/nxengine-evo/data/`. Not linked — data files, shipped as-is. GPLv3 terms apply to redistribution. |

### What this means in practice

1. **`LICENSE` in this repo is MIT.** Anyone can copy the Makefile, the DOSBox-X tooling, the port patches, the docs, without GPL obligations.
2. **`DOSKUTSU.EXE` when distributed is effectively GPLv3** (because it links GPLv3 NXEngine-evo). The dist zip (`dist/doskutsu-cf.zip`) must include:
   - A copy of `vendor/nxengine-evo/LICENSE` as `GPLv3.TXT` or similar.
   - A pointer to the full corresponding source (this repo's remote URL + the pinned NXEngine-evo SHA from `vendor/sources.manifest`).
   - The MIT `LICENSE` file (for the port glue that's ours).
   - `CWSDPMI.DOC` (CWSDPMI's own redistribution terms).
3. **CWSDPMI ships alongside, not linked in.** It's a separate `.exe` that `DOSKUTSU.EXE` invokes. Its redistribution terms (bundled `CWSDPMI.DOC` required) are orthogonal to the GPLv3 on the main binary.
4. **Cave Story game data stays out of the repo and out of the dist.** `docs/ASSETS.md` tells users how to obtain and extract it themselves. This avoids:
   - The NXEngine-evo GPLv3 attempting to re-license Pixel's freeware data via inclusion.
   - Redistribution questions around Pixel's 2004 terms.
5. **Our port patches in `patches/nxengine-evo/*.patch` are derivative of NXEngine-evo and are therefore GPLv3**, regardless of our repo's MIT `LICENSE`. This is fine — we license the *patches* (and anything that reads the NXEngine-evo source) implicitly under GPLv3 by virtue of being a derivative work. The MIT `LICENSE` covers our original, non-derivative code: Makefile, scripts, port-glue files that don't include NXEngine-evo headers, docs.
6. **Patches for zlib-licensed upstreams** (SDL, sdl2-compat, SDL_mixer, SDL_image) stay zlib-licensed as derivatives of those projects. That's more permissive than MIT and fine under either.

### Downstream redistribution checklist

When producing `dist/doskutsu-cf.zip` (Phase 8 / release), the Makefile's `dist` target must include:

- `DOSKUTSU.EXE` — the binary
- `CWSDPMI.EXE` — DPMI host
- `CWSDPMI.DOC` — CWSDPMI redistribution terms (required by its license)
- `LICENSE.TXT` — MIT `LICENSE` from this repo (for the port source)
- `GPLV3.TXT` — NXEngine-evo's `LICENSE` file (the dominant license of the binary)
- `README.TXT` — basic run instructions + pointer to source repo + note on how to obtain Cave Story data
- `THIRD-PARTY.TXT` — the attribution matrix from `THIRD-PARTY.md` in DOS-CRLF form

We do **not** include Cave Story game data in the dist. `README.TXT` tells users how to obtain it.

### Tasks added to the plan from this licensing review

- [ ] Phase 5: make sure the `Makefile`'s `dist` target includes `GPLV3.TXT`, `LICENSE.TXT`, `THIRD-PARTY.TXT`, `CWSDPMI.DOC` alongside the binary
- [ ] Phase 5: when cloning NXEngine-evo, copy its `LICENSE` to `vendor/nxengine-evo/LICENSE` (done by `fetch-sources.sh` implicitly — clone pulls the whole tree) and reference it from our dist packaging
- [ ] THIRD-PARTY.md must carry the license matrix from this section, verbatim, and be kept in sync
- [ ] Phase 6: `docs/ASSETS.md` must explicitly note that Cave Story data is freeware-from-Pixel, not redistributed by this project, user-obtained
- [ ] No git hook / CI step yet, but: before cutting a release, manually verify the dist zip contains all five license files (`LICENSE.TXT`, `GPLV3.TXT`, `CWSDPMI.DOC`, `THIRD-PARTY.TXT`, plus any zlib notices referenced in THIRD-PARTY.md)

## Architecture Recap

```
NXEngine-evo (SDL2 C++11 source)
    ↓ links against
libSDL2.a        (libsdl-org/sdl2-compat — source-compatible SDL2 API)
libSDL2_mixer.a  (SDL_mixer release-2.8.x, built against sdl2-compat)
libSDL2_image.a  (SDL_image release-2.8.x, built against sdl2-compat)
    ↓ all forward to
libSDL3.a        (libsdl-org/SDL main, with DOS backend from PR #15377)
    ↓ runs on
DJGPP 12.2.0 + CWSDPMI r7 + DOS 6.22
```

**Why sdl2-compat over direct SDL3 migration:** SDL2→SDL3 audio is real work (`SDL_AudioCVT` family is gone, replaced by `SDL_AudioStream`; touches Pixtone and Organya synth code). Renderer API renamed (`SDL_RenderCopy`→`SDL_RenderTexture`, `SDL_FreeSurface`→`SDL_DestroySurface`, etc., ~30 call sites). Surface format enum reshuffled. sdl2-compat absorbs all of this; it's maintained by libsdl-org and is stable on desktop. Risk: nobody has built sdl2-compat for DJGPP yet. It's pure-C forwarding layer — should port cleanly. If it doesn't, direct SDL3 migration is the documented fallback (~1-2 day detour).

---

## Phase 0 — Prerequisites

Verify the dev host has what's needed. These should already exist on this machine:

- [ ] Linux (or WSL) host for cross-compilation
- [ ] DJGPP cross-toolchain at `~/emulators/tools/djgpp/bin/i586-pc-msdosdjgpp-gcc` (install via `~/emulators/scripts/update-djgpp.sh`)
- [ ] `vendor/cwsdpmi/cwsdpmi.exe` (copy from `~/git/vellm/vendor/cwsdpmi/` or download per `vendor/cwsdpmi/README.md`)
- [ ] DOSBox-X with SB16 + VESA SVGA support (`sudo apt install dosbox-x`)
- [ ] CMake >= 3.16, git, standard build tools
- [ ] `scrot`, `xdotool` (for visible DOSBox-X automation)

Run:
```bash
./scripts/setup-symlinks.sh     # creates tools/djgpp -> ~/emulators/tools/djgpp
make djgpp-check                # verifies DJGPP is callable
```

**Gate:** `tools/djgpp/bin/i586-pc-msdosdjgpp-gcc --version` prints cleanly.

## Phase 1 — Toolchain smoke test

Confirm DJGPP + CWSDPMI + DOSBox-X work end-to-end before we pull in any SDL.

- [ ] `tests/smoketest/hello.c` — minimal hello-world (already scaffolded)
- [ ] `make smoke-fast` — builds `hello.exe`, runs it under DOSBox-X with `dosbox-x-fast.conf`, checks expected stdout
- [ ] `make smoke` — same against parity config

**Gate:** both `make smoke-fast` and `make smoke` print expected output and exit 0.

## Phase 2 — Build SDL3 for DOS

```bash
# In sources.manifest: SDL pinned to a post-PR-#15377 main commit
./scripts/fetch-sources.sh      # clones vendor/SDL if not present, checks out pinned SHA
./scripts/apply-patches.sh      # applies patches/SDL/*.patch (initially empty)
make sdl3                       # cmake + make, installs into build/sysroot/
```

The Makefile target invokes CMake with:
```
-DCMAKE_TOOLCHAIN_FILE=vendor/SDL/build-scripts/i586-pc-msdosdjgpp.cmake
-DSDL_SHARED=OFF -DSDL_STATIC=ON
-DCMAKE_INSTALL_PREFIX=<repo>/build/sysroot
```

Smoke test one of upstream's test programs (`testdraw2.c`, `testaudioinfo.c`) — wrap in `make sdl3-smoke`.

**Gate:** SDL test programs draw, play audio, and read keyboard in DOSBox-X via `tools/dosbox-run.sh`.

**Risks to track:** PR #15377 has no real-HW testing upstream. Budget time here for patches — anything DJGPP-specific that broke gets a `patches/SDL/*.patch`.

## Phase 3 — Build sdl2-compat for DOS

**This is the highest-risk phase.** sdl2-compat has no prior DJGPP build.

```bash
make sdl2-compat                # cmake + make, installs into build/sysroot/
```

CMake invocation:
```
-DCMAKE_TOOLCHAIN_FILE=vendor/SDL/build-scripts/i586-pc-msdosdjgpp.cmake
-DSDL2COMPAT_STATIC=ON -DSDL2COMPAT_TESTS=OFF
-DCMAKE_PREFIX_PATH=<repo>/build/sysroot
-DCMAKE_INSTALL_PREFIX=<repo>/build/sysroot
```

If the build fails, triage errors:
- Missing POSIX functions — sdl2-compat assumes a fuller libc than DJGPP may provide. Patch incrementally into `patches/sdl2-compat/`.
- Linker errors around `dlopen` / `dlsym` — sdl2-compat has a dynamic-loader path. Force-disable; SDL3-DOS is static-only anyway.
- CMake toolchain file handling — if `sdl2-compat` doesn't know about DJGPP, reference SDL3's `i586-pc-msdosdjgpp.cmake` via `CMAKE_TOOLCHAIN_FILE`.

**Gate:** a trivial SDL2-API test program (`SDL_Init(SDL_INIT_VIDEO); SDL_CreateWindow(...); SDL_Delay(1000); SDL_Quit();`) links against `-lSDL2` and runs in DOSBox-X.

**Fallback if unfixable in ~1 day:** skip Phases 3-4, go direct SDL3 port in Phase 5 (see "Fallback path" below).

## Phase 4 — Build SDL2_mixer and SDL2_image

**SDL2_mixer (release-2.8.x).** CMake options constrained to shrink footprint:
```
-DBUILD_SHARED_LIBS=OFF
-DSDL2MIXER_VENDORED=ON
-DSDL2MIXER_OPUS=OFF -DSDL2MIXER_MOD=OFF -DSDL2MIXER_MP3=OFF
-DSDL2MIXER_FLAC=OFF -DSDL2MIXER_MIDI=OFF
-DSDL2MIXER_VORBIS=STB -DSDL2MIXER_WAVE=ON
```

Rationale:
- **WAV / RAW required** — Organya synth output goes through `Mix_QuickLoad_RAW`.
- **OGG via stb_vorbis required** — NXEngine-evo supports custom OGG soundtracks (Remix, etc.).
- Everything else off.

**SDL2_image (release-2.8.x).** PNG only via stb_image; drop libpng, libjpeg, everything else:
```
-DBUILD_SHARED_LIBS=OFF
-DSDL2IMAGE_VENDORED=ON -DSDL2IMAGE_BACKEND_STB=ON
-DSDL2IMAGE_PNG=ON
-DSDL2IMAGE_JPG=OFF -DSDL2IMAGE_TIF=OFF -DSDL2IMAGE_WEBP=OFF -DSDL2IMAGE_AVIF=OFF
```

NXEngine-evo only uses `IMG_Init`, `IMG_INIT_PNG`, `IMG_Load`, `IMG_GetError` — PNG is all we need.

**Gate:** `Mix_OpenAudio` + `IMG_Load` both succeed in a DJGPP test harness under DOSBox-X.

## Phase 5 — Build NXEngine-evo for DOS

```bash
make nxengine      # consumes build/sysroot/, produces build/doskutsu.exe
```

Required patches (landed as `patches/nxengine-evo/*.patch`):

1. **Drop JPEG dep.** Remove `find_package(JPEG REQUIRED)` from `CMakeLists.txt` (confirmed: NXEngine-evo repo + Cave Story assets have no `.jpg`).
2. **DOS target flags.**
   ```cmake
   if(CMAKE_SYSTEM_NAME STREQUAL "MSDOS" OR CMAKE_CXX_COMPILER MATCHES "msdosdjgpp")
       add_compile_options(-march=i486 -mtune=pentium -O2)
       add_compile_definitions(NXE_DOS)
   endif()
   ```
   Grep for `throw` / `try` / `dynamic_cast` / `typeid` before adding `-fno-exceptions -fno-rtti`; drop those flags if any hits.
3. **Force software renderer** at `src/graphics/Renderer.cpp:119`: `SDL_RENDERER_ACCELERATED` → `SDL_RENDERER_SOFTWARE`.
4. **Lock window to 320x240 fullscreen at runtime.** Keep widescreen / HD code paths compiled in — the lock sits in `Renderer::initVideo` or equivalent. `NXE_DOS` ifdef.
5. **Audio init.** Initial: `Mix_OpenAudio(22050, AUDIO_S16SYS, 2, 2048)`. Fallback (if CPU-starved): `Mix_OpenAudio(11025, AUDIO_S16SYS, 1, 2048)` — matches Cave Story 2004 spec.
6. **Disable `SDL_Haptic` code paths.** DOS backend has no haptic subsystem. `#ifdef NXE_DOS` out the haptic init + calls.
7. **Binary rename.** `set_target_properties(nx PROPERTIES OUTPUT_NAME doskutsu)` → `doskutsu.exe`, which DOS uppercases at runtime to `DOSKUTSU.EXE`.
8. **Post-link stubedit.** Makefile handles this: `stubedit build/doskutsu.exe minstack=2048k`.

**Gate:** `build/doskutsu.exe` links. Running it under `tools/dosbox-launch.sh --exe build/doskutsu.exe` reaches the title screen. (Assumes Phase 6 extracted assets are present.)

## Phase 6 — Cave Story data files

NXEngine-evo ships engine support data (fonts, PBM backgrounds, JSON metadata) but not Cave Story game assets.

1. Obtain the 2004 freeware `Doukutsu.exe` (English translation). Canonical source: https://www.cavestory.org — verify URL before scripting, the site's layout drifts.
2. Extract assets. Options:
   - `doukutsu-rs` extractor (Rust, modern, maintained): https://github.com/doukutsu-rs/doukutsu-rs
   - NXExtract (older, harder to find)
   - Some NXEngine-evo forks ship pre-extracted `data/base/` — only if source is trusted.
3. Place under `data/base/` in this repo (gitignored). Expected subdirs:
   ```
   data/base/Stage/   (map .pxm, .pxe, .pxa)
   data/base/Npc/     (NPC sprite sheets)
   data/base/org/     (Organya .org music)
   data/base/wav/     (sound effect .wav or .pxt for Pixtone)
   ```
4. Smoke-test under DOSBox-X: title screen → first stage → Quote visible, moves, jumps.

Full procedure lives in `docs/ASSETS.md`.

**Gate:** Title screen loads, first stage reachable, player sprite moves and jumps.

## Phase 7 — DOSBox-X playthrough

Using `tools/dosbox-x.conf` (parity config, `cycles=fixed 40000`), not the fast config — we want real-HW-like timing here.

Configure DOSBox-X to approximate g2k:
```ini
[cpu]
core      = normal
cputype   = pentium_slow
cycles    = fixed 40000    # approximates PODP83 Pentium-40-class integer throughput

[sblaster]
sbtype    = sb16
sbbase    = 220
irq       = 5
dma       = 1
hdma      = 5

[vesa]
modelist  = compatible

[dosbox]
memsize   = 48
```

(The committed `tools/dosbox-x.conf` sets all of this.)

**Play-test targets:**
- Mimiga Village (dialogue + Organya music stability)
- First Cave / Hermit Gunsmith (combat, sprite-heavy)
- Egg Corridor entry (scrolling + enemies)
- Save/load cycle (disk I/O under DOS)
- ≥30 min continuous session (memory leak / heap-fragmentation shake-out)

**Gate:** zero crashes across the checklist; audio stable; save/load works; no visible corruption.

## Phase 8 — Deploy to g2k (real hardware)

Deployment target: CF card mounted on Linux. The g2k repo has `scripts/push-to-card.sh` as the rsync driver and `sync-manifest.txt` as the file-path list.

1. Stage deployment directory `C:\DOSKUTSU\`:
   ```
   C:\DOSKUTSU\DOSKUTSU.EXE
   C:\DOSKUTSU\CWSDPMI.EXE
   C:\DOSKUTSU\DATA\          (NXEngine-evo engine data)
   C:\DOSKUTSU\DATA\BASE\     (extracted Cave Story assets)
   ```
2. Add paths to g2k's `sync-manifest.txt`.
3. Commit in g2k, push, `scripts/push-to-card.sh --go`.
4. Boot g2k under the `[VIBRA]` profile (SB16 IRQ 5 / DMA 1/5 / base 220; NOEMS; VESA via `M64VBE`; CTMOUSE optional).
5. `C:`, `CD DOSKUTSU`, `DOSKUTSU.EXE`. Watch for:
   - DPMI load error → CWSDPMI not in PATH or CWD
   - VESA mode failure → `M64VBE` not loaded (check AUTOEXEC.BAT)
   - Audio silence → `BLASTER` env var mismatch (`A220 I5 D1 H5 T6` expected)
   - Mouse not detected → CTMOUSE not loaded (optional)

**Do not modify CONFIG.SYS / AUTOEXEC.BAT recklessly on g2k.** The canonical memory map lives in g2k's `README.TXT`; mirror existing profile blocks, don't improvise.

**Gate:** title screen on real hardware; first stage playable; audio stable.

## Phase 9 — Performance tuning

Apply in order if framerate or audio drop on real hardware. Levers 1-4 correspond to the Tier 2 → Tier 3 descent in `docs/HARDWARE.md` (Reference PODP83 → 486DX2-50 / 8 MB as the absolute-minimum stretch target):

1. **Audio 22050 stereo → 11025 mono.** Halves Organya CPU cost. Matches Cave Story's 2004 spec. Mandatory for Tier 2 (486DX2-66) and below.
2. **Renderer: texture path → direct surface path.** Currently: `SDL_Surface` → `SDL_CreateTextureFromSurface` → `SDL_RenderCopy`. Rewrite hot blits to go surface→surface via `SDL_BlitSurface`. Saves the per-frame texture upload.
3. **16bpp → 8bpp indexed.** VESA linear 8bpp is SDL3-DOS's best path (per PR description: "8-bit indexed color with VGA DAC palette programming"). Halves per-pixel bandwidth. Requires palette management for Cave Story sprites. Needed for Tier 3.
4. **Disable per-sprite alpha blending.** Cave Story uses colorkey masking mostly; alpha paths in evo may be over-generalized. Needed for Tier 3.
5. **Working-set reduction for Tier 3 (8 MB RAM).** Lazy-load sprite sheets (don't load all Npc/*.pbm at startup), stream `.pxm` / `.pxe` on stage entry rather than caching globally, drop Organya voice-cache size. Potentially requires port-side patches beyond simple flag flips.
6. **Last resort: switch to original NXEngine C source** (https://github.com/nxengine/nxengine). C, SDL1.2-era, ~1/4 the call-site count. Would need SDL2/SDL3 port but simpler codebase, no Organya re-architecture. Would also revisit Tier 3 assumptions from scratch.

Each lever gets a `docs/PERFORMANCE.md` entry with measured before/after numbers. Tier 3 validation requires real 486DX2-50 / 8 MB hardware which we don't currently have — treat the tier as a research target until somebody runs it.

---

## Fallback path: direct SDL3 migration

If Phase 3 (sdl2-compat on DJGPP) is unfixable in reasonable time, migrate NXEngine-evo's SDL2 code to SDL3 directly.

Size estimate from the survey in the original plan:

| Change | Call sites | Difficulty |
|---|---|---|
| `SDL_RenderCopy` → `SDL_RenderTexture` | ~9 | Mechanical rename |
| `SDL_FreeSurface` → `SDL_DestroySurface` | ~9 | Mechanical rename |
| `SDL_CreateRGBSurface` → `SDL_CreateSurface` | ~5 | Arg reshuffle (masks → format enum) |
| `SDL_BuildAudioCVT` + `SDL_ConvertAudio` → `SDL_AudioStream` | ~12 | **Real refactor** — touches Pixtone + Organya |
| Event struct field renames | varies | Mechanical |
| `SDL_Init` flag changes | 1 | Trivial |
| SDL2_mixer → SDL3_mixer | all `Mix_*` | Library swap, mostly source-compatible |
| SDL2_image → SDL3_image | 4 | Trivial |

**Estimate:** 1-2 days. The audio CVT → AudioStream migration is the only part requiring real thought.

Upstream migration guide: https://wiki.libsdl.org/SDL3/README/migration

---

## Artifacts produced by successful completion

- `vendor/SDL/` etc. — cloned upstream snapshots at pinned SHAs (gitignored)
- `build/sysroot/` — SDL3 + sdl2-compat + SDL2_mixer + SDL2_image static libs, installed per-project
- `build/doskutsu.exe` — DJGPP-built game binary, stubedit'd to 2048K min stack
- `dist/doskutsu-cf.zip` — CF-ready deploy bundle (`DOSKUTSU.EXE` + `CWSDPMI.EXE` + `DATA\` placeholder + README.TXT + LICENSE.TXT)
- CF card layout: `C:\DOSKUTSU\DOSKUTSU.EXE` + `CWSDPMI.EXE` + `DATA\BASE\`
- Boot on g2k: `[VIBRA]` profile → `C:` → `CD DOSKUTSU` → `DOSKUTSU.EXE` → title screen

---

## Open questions (to resolve as phases complete)

These aren't blocking; flag if you hit one.

- **SDL3 DOS backend real-HW behavior.** PR #15377 was DOSBox-only tested upstream. Expect VESA quirks, audio DMA edge cases. First real-HW bug reports should probably go back upstream as well as into our patches.
- **sdl2-compat static-only build.** We need the static build only; the compat shim's dynamic-loader code path must be disabled cleanly (not just by lying about `dlopen`). Phase 3 work.
- **Organya CPU cost at 22050 stereo on PODP83.** Likely tight. The 11025 mono fallback is prepared; when to trigger is a Phase 7 / 8 judgment call.
- **Heap fragmentation under DPMI over ~30-60 min.** NXEngine-evo's C++11 allocation patterns haven't been profiled under DJGPP. Watch CWSDPMI.SWP growth during Phase 7 sessions.
