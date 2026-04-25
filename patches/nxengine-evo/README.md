# patches/nxengine-evo/

> **Why this layout exists:** see `PLAN.md § Plan Amendments / 2026-04-24 Path B` for the rationale and decision record. This README is the operational truth (which patches, in which order); PLAN.md is the why.

DOS-port patches against `vendor/nxengine-evo/`, applied by `scripts/apply-patches.sh` in lexical order. The general patch convention (one concern per patch, `[DOSKUTSU]` subject prefix, `git format-patch` output, license inheritance) lives in `../README.md`. This README documents only the **layout policy** — how the SDL2→SDL3 migration plus DOS-build patches are split across `NNNN-*.patch` files.

Companion: `docs/SDL3-MIGRATION.md` documents the architectural decisions these patches encode.

## Numeric ranges

Patches cluster by phase and concern. Numeric gaps reserve insertion points so a late-discovered DJGPP issue doesn't force renumbering downstream.

### `0001`–`0009`: DJGPP / DOS build adaptations

Static, mechanical adjustments to the upstream build system and minor source guards. Independent of the SDL3 migration — would apply identically against a hypothetical Path A.

| File | Concern |
|---|---|
| `0001-cmake-drop-jpeg-find-package.patch` | Cave Story ships no `.jpg` assets; remove `find_package(JPEG REQUIRED)`. |
| `0002-cmake-djgpp-target-flags.patch` | `-march=i486 -mtune=pentium -O2`, `NXE_DOS` define, `-fno-rtti` only. **Not** `-fno-exceptions` — `src/map.cpp:448` has a real try/catch around `nlohmann::json::parse` (per #27 audit). |
| `0003-cmake-binary-rename-doskutsu.patch` | `set_target_properties(nx PROPERTIES OUTPUT_NAME doskutsu)`. |
| `0004-renderer-force-software-renderer.patch` | `Renderer.cpp:119` `SDL_RENDERER_ACCELERATED` → `SDL_RENDERER_SOFTWARE`. SDL3-DOS has no accelerated renderer. |
| `0005-renderer-lock-320x240-fullscreen.patch` | Runtime lock to 320x240 fullscreen. Widescreen / HD code paths in `getResolutions()` remain compiled in per CLAUDE.md. |

`0006`–`0009` reserved for DJGPP-specific issues uncovered during integration: `size_t` / `off_t` corrections, `fopen(..., "rb")` enforcement, stack-budget tuning. The haptic-disable patch originally planned at slot `0006` is **dropped** — #27 audit found zero `SDL_Haptic` references in NXEngine-evo (also zero gamepad/sensor/camera/touch/pen). No prophylactic subsystem-gating patches needed.

### `0010`–`0019`: SDL2 → SDL3 API migration (Path B)

The migration patches. Clustered together so the API-migration commit range is reviewable as a unit and bisectable independently of the DOS-build patches.

| File | Concern |
|---|---|
| `0010-sdl3-mechanical-renames.patch` | Bulk renames: `SDL_RenderCopy` → `SDL_RenderTexture` (10 sites), `SDL_FreeSurface` → `SDL_DestroySurface` (9 sites), `SDL_CreateRGBSurface` → `SDL_CreateSurface`, `SDL_FillRect` → `SDL_FillSurfaceRect`, `SDL_BlitSurface` signature change, `SDL_ShowCursor` signature change, `SDL_WarpMouse` → `SDL_WarpMouseInWindow`, joystick API renames. One patch because the changes are uniform sed-bait. |
| `0011-sdl3-event-enum-renames.patch` | Event-type constants: `SDL_KEYDOWN` → `SDL_EVENT_KEY_DOWN`, etc. (~14 constants). Separated from `0010` because event-enum renames are easy to miss in review when buried in mechanical-rename noise. |
| `0012-sdl3-renderer-properties.patch` | `SDL_GetRendererInfo` / `SDL_RendererInfo` → `SDL_GetRendererProperties` + `SDL_GetStringProperty`. Genuinely different API surface, kept distinct from the rename pass. |
| `0013-sdl3-audio-pixtone-audiostream.patch` | `Pixtone.cpp` lines 372, 445, 508: `SDL_BuildAudioCVT` + `SDL_ConvertAudio` → `SDL_AudioStream` lifecycle (Create/Put/Flush/Get/Destroy). |
| `0014-sdl3-mixer-pixtone-decoder.patch` | `Pixtone.cpp` lines 398, 473, 529: `Mix_QuickLoad_RAW` → `MIX_CreateAudioDecoder` over `SDL_IOFromMem` wrapping the synthesized PCM. |
| `0015-sdl3-mixer-organya-track-callback.patch` | `Organya.cpp` lines 378, 400: `Mix_HookMusic` → `MIX_Track` pull-callback. The on-demand sample-fill contract maps cleanly; API ceremony differs. |
| `0016-sdl3-mixer-ogg-finished-callback.patch` | `Ogg.cpp` lines 123, 140: `Mix_HookMusicFinished` migration to MIX_Track equivalent. |
| `0017-sdl3-mixer-soundmanager-init.patch` | `SoundManager.cpp` lines 36, 43, 49, 55: `Mix_Init` + `Mix_OpenAudioDevice` / `Mix_OpenAudio` + `Mix_AllocateChannels` → `MIX_CreateMixer` on an `SDL_AudioDeviceID`. The `SDL_MIXER_PATCHLEVEL >= 2` conditional collapses to a single SDL3_mixer init path. |
| `0018-sdl3-image-load.patch` | `IMG_Init` + `IMG_Load` migration. Small — SDL3_image kept the legacy `IMG_*` prefix; mostly signature-drift. |

`0013`–`0017` is the audio refactor cluster (task #30). It's reviewable as a unit because all five patches touch the same subsystem and pass through one before/after audio-capture gate. Per `docs/SDL3-MIGRATION.md § 3`, this work is co-owned by sdl-engine (SDL3 mechanics) and nxengine (synthesis correctness).

`0019` reserved for any unforeseen SDL3 migration patches.

### Why this layout, not the alternatives

- **One concern per patch** (per `../README.md`). The migration is many concerns; one mega-patch fails review and bisection. We had this debate; the answer is "split."
- **Per-file split is too fine-grained.** The mechanical-rename pass touches `Renderer.cpp` + `Sprites.cpp` + `Font.cpp` + `Surface.cpp` for the same concern (the SDL2→SDL3 renderer/surface API delta). Splitting per-file produces five patches that all need to land together to compile — that's a fake split. Group by concern instead.
- **Numeric gaps reserve insertion points.** A future DJGPP issue slots into `0006`–`0009`. A future SDL3 follow-up slots into `0019`. No renumbering churn.
- **The 0010–0017 block is bisectable as the SDL3 work.** If a regression is bisected to `[0010, 0017]`, the failure is in the migration, not the DOS-build adaptations. Conversely if it bisects to `[0001, 0005]`, it's a DJGPP issue, independent of API.

## Authoring order

`0001`–`0005` are written first against the un-migrated SDL2 source. The `0010+` migration patches are written against a tree with `0001`–`0005` already applied. This avoids merge conflicts when migration touches code already adjusted by the DOS-build patches (notably `Renderer.cpp:119` — patched in `0004`, then renamed in `0010`).

When rerolling against a new upstream NXEngine-evo SHA: rebase `0001`–`0005` first; let `0010+` rebase on top. If `0010+` conflicts on lines `0001`–`0005` already touched, fix the build patch first — it's the foundation.

## Cross-references

- `../README.md` — general patch convention
- `docs/SDL3-MIGRATION.md` — architectural decisions encoded by these patches
- `PLAN.md § Phase 5` — the originally-planned patch list this layout refines
- Task #27 — the audit that sized the migration
- Task #30 — the audio refactor governing the `0013`–`0017` cluster
