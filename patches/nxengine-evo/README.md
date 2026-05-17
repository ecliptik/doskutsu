# patches/nxengine-evo/

DOS-port patches against `vendor/nxengine-evo/`, applied by `scripts/apply-patches.sh` in lexical order. The general patch convention (one concern per patch, `[DOSKUTSU]` subject prefix, `git format-patch` output, license inheritance) lives in `../README.md`. This README documents only the **layout policy** -- how the SDL2->SDL3 migration plus DOS-build patches are split across `NNNN-*.patch` files.

## Numeric ranges

Patches cluster by phase and concern. Numeric gaps reserve insertion points so a late-discovered DJGPP issue doesn't force renumbering downstream.

### `0001`-`0009`: DJGPP / DOS build adaptations

Static, mechanical adjustments to the upstream build system and minor source guards. Independent of the SDL3 migration -- would apply identically against a hypothetical Path A. **All nine slots filled** as of Phase 5 close (`0001`-`0005` from the original plan; `0006`-`0009` from issues uncovered during integration, exactly the use the reservation gap was designed for).

| File | Concern |
|---|---|
| `0001-cmake-drop-jpeg-find-package.patch` | Cave Story ships no `.jpg` assets; remove `find_package(JPEG REQUIRED)`. |
| `0002-cmake-djgpp-target-flags.patch` | `-march=i486 -mtune=pentium -O2`, `NXE_DOS` define, `-fno-rtti` only. **Not** `-fno-exceptions` -- `src/map.cpp:448` has a real try/catch around `nlohmann::json::parse`. |
| `0003-cmake-binary-rename-doskutsu.patch` | `set_target_properties(nx PROPERTIES OUTPUT_NAME doskutsu)`. |
| `0004-renderer-force-software-renderer.patch` | `Renderer.cpp:119` `SDL_RENDERER_ACCELERATED` -> `SDL_RENDERER_SOFTWARE`. SDL3-DOS has no accelerated renderer. |
| `0005-renderer-lock-320x240-fullscreen.patch` | Runtime lock to 320x240 fullscreen. Widescreen / HD code paths in `getResolutions()` remain compiled in. |
| `0006-djgpp-spdlog-replacement.patch` | Replace upstream `spdlog` with an `fmt`-backed Logger shim. spdlog's `fputws` / `wostream` use is incompatible with DJGPP's libc. |
| `0007-cmake-drop-png-on-dos.patch` | Drop `libpng` dep + the screenshot-via-libpng path on DOS. SDL3_image's stb_image backend handles PNG load; libpng was unnecessary linker bulk and didn't build cleanly under DJGPP. |
| `0008-cmake-exclude-extract-target-on-djgpp.patch` | Exclude NXEngine-evo's host-side `extract` data-extraction utility from the DJGPP build. It depends on host libs and isn't part of the runtime; users extract Cave Story data on Linux per `../../docs/ASSETS.md`. |
| `0009-djgpp-posix-headers.patch` | POSIX header visibility for the `nx` target on DJGPP -- DJGPP's POSIX headers need explicit feature-test macros that upstream's CMakeLists doesn't surface. |

The haptic-disable patch originally planned at slot `0006` is **dropped** -- the migration audit found zero `SDL_Haptic` references in NXEngine-evo (also zero gamepad/sensor/camera/touch/pen). No prophylactic subsystem-gating patches were needed; the slot freed up for the spdlog replacement instead.

### `0010`-`0019`: SDL2 -> SDL3 API migration (Path B)

The migration patches. Clustered together so the API-migration commit range is reviewable as a unit and bisectable independently of the DOS-build patches.

| File | Concern |
|---|---|
| `0010-sdl3-mechanical-renames.patch` | Bulk renames: `SDL_RenderCopy` -> `SDL_RenderTexture` (10 sites), `SDL_FreeSurface` -> `SDL_DestroySurface` (9 sites), `SDL_CreateRGBSurface` -> `SDL_CreateSurface`, `SDL_FillRect` -> `SDL_FillSurfaceRect`, `SDL_BlitSurface` signature change, `SDL_ShowCursor` signature change, `SDL_WarpMouse` -> `SDL_WarpMouseInWindow`, joystick API renames. One patch because the changes are uniform sed-bait. |
| `0011-sdl3-event-enum-renames.patch` | Event-type constants: `SDL_KEYDOWN` -> `SDL_EVENT_KEY_DOWN`, etc. (~14 constants). Separated from `0010` because event-enum renames are easy to miss in review when buried in mechanical-rename noise. |
| `0012-sdl3-renderer-properties.patch` | `SDL_GetRendererInfo` / `SDL_RendererInfo` -> `SDL_GetRendererProperties` + `SDL_GetStringProperty`. Genuinely different API surface, kept distinct from the rename pass. |
| `0013-sdl3-audio-pixtone-audiostream.patch` | `Pixtone.cpp` lines 372, 445, 508: `SDL_BuildAudioCVT` + `SDL_ConvertAudio` -> `SDL_AudioStream` lifecycle (Create/Put/Flush/Get/Destroy). |
| `0014-sdl3-mixer-pixtone-decoder.patch` | `Pixtone.cpp` lines 398, 473, 529: `Mix_QuickLoad_RAW` -> `MIX_CreateAudioDecoder` over `SDL_IOFromMem` wrapping the synthesized PCM. |
| `0015-sdl3-mixer-organya-track-callback.patch` | `Organya.cpp` lines 378, 400: `Mix_HookMusic` -> `MIX_Track` pull-callback. The on-demand sample-fill contract maps cleanly; API ceremony differs. |
| `0016-sdl3-mixer-ogg-finished-callback.patch` | `Ogg.cpp` lines 123, 140: `Mix_HookMusicFinished` migration to MIX_Track equivalent. |
| `0017-sdl3-mixer-soundmanager-init.patch` | `SoundManager.cpp` lines 36, 43, 49, 55: `Mix_Init` + `Mix_OpenAudioDevice` / `Mix_OpenAudio` + `Mix_AllocateChannels` -> `MIX_CreateMixer` on an `SDL_AudioDeviceID`. The `SDL_MIXER_PATCHLEVEL >= 2` conditional collapses to a single SDL3_mixer init path. |
| `0018-sdl3-image-load.patch` | `IMG_Init` + `IMG_Load` migration. Small -- SDL3_image kept the legacy `IMG_*` prefix; mostly signature-drift. |
| `0019-cmake-find-package-sdl3.patch` | Switch `find_package(SDL2)` -> `find_package(SDL3)` on the PC build. The reservation slot got used as designed once the upstream CMakeLists.txt's SDL2 lookup needed updating to match the migrated source. |

`0013`-`0017` is the audio refactor cluster. Originally reviewable as a self-contained unit; **see also the `0024-audio-cluster-followups.patch` Phase 5 follow-up** in the overflow cluster below -- the audio refactor's full bisection range is now `[0013, 0017]  U  {0020, 0024}` because some adjustments surfaced post-integration.

### `0020`-`0024`: Phase 5 follow-up overflow cluster

When the `0001`-`0009` and `0010`-`0019` reservation gaps were full, follow-up patches discovered during Phase 5 integration landed in the next free slots -- see `## Slot numbering convention` below for the policy.

| File | Concern |
|---|---|
| `0020-sfx-volume-wireup.patch` | Wire `SoundManager` SFX volume through to the new `MIX_*` track API. SDL3_mixer's volume model is float 0.0-1.0 vs SDL2_mixer's int 0-128; the audio-cluster (`0013`-`0017`) migrated the API surface but a corner of the volume-scale wiring needed a dedicated follow-up. |
| `0021-sdl3-additional-include-paths.patch` | Convert remaining SDL2-style include paths (`<SDL2/SDL.h>`) to SDL3 (`<SDL3/SDL.h>`) in spots `0010-sdl3-mechanical-renames.patch` missed. |
| `0022-sdl3-keysym-letter-rename.patch` | SDL3 renamed `SDLK_a`-`SDLK_z` (lowercase) to `SDLK_A`-`SDLK_Z` (uppercase). Separate patch because the renames are easy to miss in `0010`'s noise and the change is uniform mechanical sed-bait. |
| `0023-djgpp-fmt-disable-locale.patch` | Disable `fmt` library's locale code path on DJGPP. DJGPP libc's `std::locale` use crashes at static-init under specific conditions; `fmt` lets you compile out its locale paths via `FMT_USE_LOCALE=0`. DJGPP-cluster overflow (the `0006`-`0009` gap was already full when this was found). |
| `0024-audio-cluster-followups.patch` | Post-attempt-8 fixes to the `0013`-`0017` audio cluster: sample-clamp, callback linkage, and a handful of small adjustments surfaced during the audio-capture gate. |

This cluster mixes concerns deliberately -- the overflow rule is "next free slot wins over cluster purity once the reservation gap is full." Bisection within `[0020, 0024]` doesn't tell you category at a glance; read the patch filename. The trade-off was accepted because renumbering downstream patches every time a follow-up landed would have churned the patch series and broken `git blame` continuity.

### `0025`-`0032`: Phase 7 -- pre-title-screen fatal fixes + framebuffer-wall investigation + closure

Phase 7 produced two adjacent runs of patches. **`0025`-`0026`** are pre-title-screen fatal fixes (DJGPP data-path baking + the SDL3 INDEX8 palette regression in `zoom.cpp`); they belong to the original Phase 7 prep work (CHANGELOG `### Phase 7 prep` covers their history). **`0027`-`0032`** are the framebuffer-wall investigation + closure cluster -- what made the title screen render to the DOSBox-X framebuffer.

| File | Concern | Role |
|---|---|---|
| `0025-cmake-djgpp-data-path.patch` | Gate `IF(UNIX_LIKE)` on `AND NOT DJGPP` so `ResourceManager::getPath()` falls through to `SDL_GetBasePath() + "data/"` instead of stat-ing a Linux-host absolute path baked in at CMake time. Phase 7 prep fatal-fix. | Required |
| `0026-sdl3-zoom-index8-palette.patch` | Explicitly create + attach a palette to INDEX8 dst surfaces in `zoom.cpp`. SDL3's `SDL_CreateSurface(...,INDEX8)` no longer attaches a default palette (SDL2 did); downstream `SDL_CreateTextureFromSurface` failed with `"src does not have a palette set"` for every paletted `.pbm`. SDL2->SDL3 migration regression. Phase 7 prep fatal-fix. | Required |
| `0027-sdl3-renderer-logical-presentation.patch` | `SDL_SetRenderLogicalPresentation(_renderer, 320, 240, LETTERBOX)` after `SDL_CreateRenderer`. SDL2's implicit `SDL_RenderSetLogicalSize` letterboxing is gone in SDL3; without the explicit opt-in the renderer paints into a 320x240 corner of the framebuffer with no scaling. Structurally correct; not required for the wall (the wall sat further down the pipeline). | Defensive |
| `0028-log-null-texture-from-silent-create-sites.patch` | NULL check + `LOG_ERROR` at the two previously-silent `SDL_CreateTextureFromSurface` sites in `Renderer::initVideo` (`_spot_light`) and `Font::load` (atlas pages). Bisect proved both creates succeed; the NULL source is elsewhere. | Diagnostic |
| `0029-sdl3-set-invalid-param-checks-hint-dos.patch` | Programmatic `SDL_SetHintWithPriority(SDL_HINT_INVALID_PARAM_CHECKS, "0", SDL_HINT_OVERRIDE)` before `SDL_Init`. Tested whether the env-var path was being missed by SDL3's hint-callback init order -- env arrives intact; the "Parameter 'texture' is invalid" flood was NULL-pointer attribution, not validation-hash misses. | Diagnostic |
| `0030-log-null-texture-in-drawsurface.patch` | Explicit NULL checks + early-return + throttled per-Surface logging at `Renderer::drawSurface` / `drawSurfaceMirrored` / `blitPatternAcross`. Replaces the Release-build no-op `assert(src->texture())` that let NULL textures fall through to `SDL_RenderTexture`. Throttles the diagnostic flood **and** confirmed the second cause: even with the NULL flood throttled, the framebuffer stayed pure black, proving an independent present-pipeline bug. | Required |
| `0031-log-renderpresent-and-per-flip-drawcalls.patch` | `SDL_RenderPresent` return-value check + per-flip drawcall counter in `Renderer::flip()`. Confirmed 1000/1000 present calls return success and the drawcall counter is non-zero -- so the engine is rendering and the present is succeeding; the bug lives below `SDL_RenderPresent` in SDL3-DOS's normal-path framebuffer flush. | Diagnostic |
| `0032-sdl3-dos-fast-framebuffer-hint.patch` | Programmatic `SDL_SetHintWithPriority(SDL_HINT_DOS_ALLOW_DIRECT_FRAMEBUFFER, "1", SDL_HINT_OVERRIDE)` before `SDL_Init`. Empirically what unblocks the framebuffer; the actual mechanism is a side-effect during `fb_state` init rather than fast-path engagement. Latent upstream init-state-leak bug worked around by this patch -- no-action by policy. | Required (workaround) |

Companion diagnostic in the SDL tree: `patches/SDL/0002-debug-dosvesa-framebuffer-trace.patch` instruments `DOSVESA_UpdateWindowFramebuffer` and writes to `sdldbg.log` (SDL_Log's stderr-redirect doesn't survive DOSBox-X's shell). Workspace-local per the project policy of not upstreaming patches; pinned the bug to the normal-path branch.

### `0036`-`0037`: Phase 8 -- real-HW diagnostics (branch `phase8/real-hw-diagnostics`)

Added 2026-04-25 after the first g2k boot attempt: engine reached `Renderer::showLoadingScreen()` (LOADING.. visible on the CRT, with a separate one-row VESA stride bug at the bottom of the framebuffer), then wedged with a solid-blue framebuffer for several minutes until hard reset. DEBUG.LOG was empty post-reset.

| File | Concern | Role |
|---|---|---|
| `0036-djgpp-fsync-debug-log-per-line.patch` | Per-line `fsync(fileno(djgpp_log_fp))` after the existing `fflush` in `Logger_djgpp.h`'s `DJGPP_LOG_` macro. `fflush` only commits libc -> OS; DOS BUFFERS plus the CF card's write cache hold both data and the directory entry recording new file size. A hard reset on a wedged engine drops both. fsync routes to DOS INT 21h fn 68h "Commit File," propagating data + metadata to media. Slow on CF (~tens of ms per call) but during diagnostic phases crash-survivability beats throughput. | Required for diagnosis (no log = no diagnosis) |
| `0037-diag-env-gated-no-audio-bypass.patch` | Env-var-gated bypass of `SoundManager::getInstance()->init()` in `main.cpp` between `showLoadingScreen()` and `trig_init()`. `SET DOSKUTSU_NO_AUDIO=1` skips the call so the engine progresses past the suspected SDL3-DOS SB16 DSP probe wall. If the engine reaches the title screen with audio bypassed, audio init is confirmed as the wall. Default behavior unchanged when env var unset. | Diagnostic |

These two stay in the diagnostic branch only until the real-HW audio path is fixed. `0036` is a candidate for permanent merge into `main` regardless of where the audio investigation lands -- the lack of crash-survivable logging cost us debugging time twice now (Phase 7 framebuffer wall plus this one). `0037` is intentionally diagnostic-only: leaving it in production means a typo'd env var disables audio silently.

This cluster mixes required fixes with diagnostic instrumentation deliberately -- the diagnostic patches stay applied (not reverted post-investigation) because their throttled-logging contracts remain useful for ongoing real-HW debugging on g2k. If a future regression bisects to `[0027, 0032]`, read the filename + the role column to know whether you're looking at a fix or a probe.

### Why this layout, not the alternatives

- **One concern per patch** (per `../README.md`). The migration is many concerns; one mega-patch fails review and bisection. We had this debate; the answer is "split."
- **Per-file split is too fine-grained.** The mechanical-rename pass touches `Renderer.cpp` + `Sprites.cpp` + `Font.cpp` + `Surface.cpp` for the same concern (the SDL2->SDL3 renderer/surface API delta). Splitting per-file produces five patches that all need to land together to compile -- that's a fake split. Group by concern instead.
- **Numeric gaps reserved insertion points; gaps now full.** `0006`-`0009` absorbed DJGPP-specific issues exactly as designed (the reservation worked). `0019` absorbed the SDL3 CMake follow-up. Once both gaps filled, Phase 5 follow-ups went into `0020`-`0024` per the slot-numbering convention below -- content-determined slotting beat cluster purity once the gaps were exhausted.
- **The 0010-0019 block + `{0020, 0021, 0022, 0024}` is the SDL3 migration work; `[0001, 0009] + {0023}` is the DJGPP build work; `[0025, 0032]` is the Phase 7 work (pre-title-screen fatal fixes + framebuffer-wall investigation + closure).** Bisection isn't as cleanly partitioned as it was at the start of Phase 5 (the overflow + Phase 7 clusters mix concerns), but inside any single patch the concern is still scoped -- the filename tells you the category. If a regression is bisected to `[0010, 0019]`, look at SDL3 mechanics first; to `[0001, 0009]`, DJGPP first; to `[0020, 0024]`, read the filename; to `[0025, 0032]`, read the filename + the role column in the Phase 7 cluster table (required vs. diagnostic).

## Authoring order

`0001`-`0009` are written first against the un-migrated SDL2 source. The `0010`-`0019` migration patches are written against a tree with `0001`-`0009` already applied. This avoids merge conflicts when migration touches code already adjusted by the DOS-build patches (notably `Renderer.cpp:119` -- patched in `0004`, then renamed in `0010`).

`0020`-`0024` are Phase 5 follow-up fixes against a tree with both prior clusters applied. They're conceptually fix-ups to the relevant earlier cluster (`0020` and `0024` to `0013`-`0017`; `0021` and `0022` to `0010`; `0023` to `0001`-`0009`); slot order reflects discovery order during Phase 5 integration, not concern category.

`0025`-`0032` are Phase 7 patches authored against a tree with `0001`-`0024` applied. `0025`-`0026` (pre-title-screen fatal fixes) were written first against the build that booted but couldn't reach graphics init; `0027`-`0032` (framebuffer-wall investigation + closure) were authored serially against the build that reached the title screen but rendered black. The diagnostic patches in `0028`/`0029`/`0031` stay applied alongside the required `0030`/`0032` -- see the cluster table for role attribution.

When rerolling against a new upstream NXEngine-evo SHA: rebase `0001`-`0009` first, then `0010`-`0019`, then `0020`-`0024`, then `0025`-`0032`. If a later cluster conflicts on lines an earlier cluster already touched, fix the earlier one first -- it's the foundation.

## Slot numbering convention

**Pure numeric slots only -- no alpha suffixes** (e.g., do not name a patch `0014a-foo.patch`). Sort behavior of `find ... | sort` varies across locales (C vs UTF-8); `scripts/apply-patches.sh` forces `LC_ALL=C` for consistency, which puts alpha-suffix slots **after** the same-prefix numeric slot -- so `0014a` lands after `0015`, not between `0014` and `0015` as a casual reader would expect. This bit us during Phase 5 closure; the locale-stable sort is now enforced in code, but the cleanest rule is to avoid alpha suffixes entirely.

**Use the next free numeric slot** when adding a patch, even if it's outside the original cluster range. Reserved gaps (`0006-0009` for DJGPP follow-ups, `0019` for SDL3 follow-ups) remain the preferred home, but content-determined slotting wins when those gaps are full or when keeping a related patch adjacent reads better than scattering it. The `0010-0019` cluster has overflowed into `0020-0024` for Phase 5 follow-up patches; that pattern is acceptable when content-determined slotting requires it.

## See also

- `../README.md` -- general patch convention
- `../../CHANGELOG.md sec. Phase 7 prep` -- narrative history of the `0025`-`0026` fatal-fix work and the Phase 7 framebuffer-wall investigation that produced `0027`-`0032`
