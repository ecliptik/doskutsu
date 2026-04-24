# TODO

Current work is organized by phase (see `PLAN.md`). Mark items complete as they land.

---

## Phase 0 — Prerequisites

- [ ] `scripts/setup-symlinks.sh` creates `tools/djgpp` → `~/emulators/tools/djgpp`
- [ ] `make djgpp-check` passes
- [ ] `vendor/cwsdpmi/cwsdpmi.exe` + `cwsdpmi.doc` present (copied from vellm or downloaded)
- [ ] DOSBox-X installed (`dosbox-x -version` works)

## Phase 1 — Toolchain smoke test

- [ ] `tests/smoketest/hello.c` compiles via `make hello`
- [ ] `make smoke-fast` passes (hello.exe runs under `dosbox-x-fast.conf`)
- [ ] `make smoke` passes (hello.exe runs under `dosbox-x.conf` parity)

## Phase 2 — SDL3 for DOS

- [ ] `vendor/sources.manifest` pins a concrete SDL SHA post-PR-#15377 (currently `main` / `PIN_ME`)
- [ ] `./scripts/fetch-sources.sh` clones `vendor/SDL` at pinned SHA
- [ ] `make sdl3` produces `build/sysroot/lib/libSDL3.a`
- [ ] At least one SDL test program (`testdraw2.c` / `testaudioinfo.c`) runs in DOSBox-X
- [ ] Any DJGPP-specific fixes captured as `patches/SDL/*.patch` (upstream them after)

## Phase 3 — sdl2-compat for DOS (highest risk)

- [ ] `./scripts/fetch-sources.sh` clones `vendor/sdl2-compat`
- [ ] `make sdl2-compat` produces `build/sysroot/lib/libSDL2.a`
- [ ] A trivial SDL2-API test program links against `-lSDL2` and runs in DOSBox-X
- [ ] `dlopen` / `dlsym` code paths cleanly disabled via patch (not by lying about the symbols)
- [ ] Fallback path documented in `PLAN.md` but not entered

## Phase 4 — SDL2_mixer + SDL2_image

- [ ] `make sdl2-mixer` produces `build/sysroot/lib/libSDL2_mixer.a` with WAV + OGG (stb_vorbis)
- [ ] `make sdl2-image` produces `build/sysroot/lib/libSDL2_image.a` with PNG (stb_image)
- [ ] Test harness: `Mix_OpenAudio` + `IMG_Load` both succeed under DOSBox-X

## Phase 5 — NXEngine-evo → doskutsu.exe

- [ ] `patches/nxengine-evo/0001-drop-jpeg-dep.patch`
- [ ] `patches/nxengine-evo/0002-dos-target-flags.patch`
- [ ] `patches/nxengine-evo/0003-software-renderer.patch`
- [ ] `patches/nxengine-evo/0004-lock-320x240-fullscreen.patch` (widescreen code retained)
- [ ] `patches/nxengine-evo/0005-audio-init.patch` (22050 stereo default)
- [ ] `patches/nxengine-evo/0006-disable-haptic.patch`
- [ ] `patches/nxengine-evo/0007-binary-rename-doskutsu.patch`
- [ ] Grep `throw|try|dynamic_cast|typeid` across the codebase; drop `-fno-exceptions -fno-rtti` if any hit
- [ ] `make nxengine` produces `build/doskutsu.exe` (stubedit'd to 2048K min stack)
- [ ] Title screen reachable in `tools/dosbox-launch.sh --exe build/doskutsu.exe` (requires Phase 6 assets)

## Phase 6 — Cave Story assets

- [ ] `docs/ASSETS.md` extraction procedure verified against current cavestory.org
- [ ] `data/base/Stage/`, `Npc/`, `org/`, `wav/` populated on the dev host
- [ ] Title screen → First Cave → Quote visible, moves, jumps

## Phase 7 — DOSBox-X playtest

- [ ] 30-min continuous session under `tools/dosbox-x.conf` (parity cycles)
- [ ] Mimiga Village: dialogue + Organya stable
- [ ] First Cave / Hermit Gunsmith: combat clean, no audio dropout
- [ ] Egg Corridor entry: scrolling smooth, enemies rendered
- [ ] Save/load cycle: `Profile.dat` written, reloaded correctly

## Phase 8 — Real hardware (g2k)

- [ ] `make dist` produces `dist/doskutsu-cf.zip` with LICENSE.TXT, GPLV3.TXT, CWSDPMI.DOC, THIRD-PARTY.TXT
- [ ] g2k `sync-manifest.txt` updated with `C:\DOSKUTSU\*` paths
- [ ] `scripts/push-to-card.sh --go` deploys
- [ ] Boot under `[VIBRA]` profile, title screen reached
- [ ] Phase 7 playtest checklist passes on real HW
- [ ] `bench/results.md` populated with real-HW frame-time + audio-buffer numbers

## Phase 9 — Performance tuning (apply in order if needed)

Levers 1-5 correspond to the descent from Tier 1 (PODP83 / 48 MB, reference) through Tier 2 (486DX2-66 / 16 MB) to Tier 3 (486DX2-50 / 8 MB, absolute minimum stretch target). See `docs/HARDWARE.md § Hardware tiers`.

- [ ] Audio 22050 stereo → 11025 mono (Tier 2+ requires this)
- [ ] Renderer texture path → direct surface path (`SDL_BlitSurface`)
- [ ] 16bpp → 8bpp indexed (Tier 3 requires this; needs Cave Story sprite palette mgmt)
- [ ] Disable per-sprite alpha blending (Tier 3; Cave Story uses colorkey mostly)
- [ ] Working-set reduction: lazy sprite loading, stage streaming, Organya voice cache (Tier 3, 8 MB RAM)
- [ ] Fallback: switch to original NXEngine (C, SDL1.2-era) if evo is architecturally too heavy
- [ ] Real-hardware validation on a 486DX2-50 with 8 MB (no hardware currently available; research target)

---

## Cross-cutting

- [ ] Patches all carry `Subject:` line explaining *why DOS needs this*, not *what the patch does*
- [ ] `THIRD-PARTY.md` stays synchronized with `vendor/sources.manifest`
- [ ] `CHANGELOG.md` updated per phase gate pass
- [ ] `docs/PERFORMANCE.md` captures every Phase 9 experiment with measured before/after

## Future / nice-to-have (post-1.0)

- [ ] PicoGUS native backend (bypasses SB16 emulation on g2k's PicoGUS hardware)
- [ ] DreamBlaster S2 (WaveBlaster header) music output option
- [ ] Widescreen mode re-enabled as a runtime option for users on better-than-g2k hardware
- [ ] JP original Cave Story support (font + text encoding different from EN freeware)
- [ ] Cave Story Remix soundtrack (OGG) preset
- [ ] Configurable keybindings (save to `DOSKUTSU.CFG`)
- [ ] Mod support / Cave Story Tweaked integration

## Known bugs

(None yet — will accumulate once Phase 5+ is running.)
