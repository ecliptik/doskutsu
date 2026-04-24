# Changelog

All notable changes to DOSKUTSU are documented here. Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), versioning follows [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Initial repository scaffold per `DOSKUTSU-PLAN.md` (now `PLAN.md`)
- Top-level `Makefile` orchestrating the five-stage build (SDL3 → sdl2-compat → SDL2_mixer + SDL2_image → NXEngine-evo)
- `scripts/setup-symlinks.sh`, `scripts/fetch-sources.sh`, `scripts/apply-patches.sh`
- `tools/dosbox-x.conf` (parity, `cycles=fixed 40000`) and `tools/dosbox-x-fast.conf` (`cycles=max`)
- `tools/dosbox-launch.sh` (visible DOSBox-X launcher) and `tools/dosbox-run.sh` (headless runner)
- `vendor/sources.manifest` pinning the five upstream repos
- `tests/smoketest/hello.c` + `tests/run-smoke.sh` (Phase 0 toolchain gate)
- Documentation: `README.md`, `CLAUDE.md`, `PLAN.md`, `BUILDING.md`, `DOSKUTSU.md`, `TODO.md`, `THIRD-PARTY.md`, `docs/ASSETS.md`, `docs/HARDWARE.md`, `docs/BOOT.md`

### Licensing
- Project source licensed MIT (this repo)
- Analysis of GPLv3 implications of linking NXEngine-evo added to `PLAN.md § Licensing` and `THIRD-PARTY.md`

### Phase 0 — Prerequisites (gate passed: 22da27e)
- `tools/djgpp` symlink to `~/emulators/tools/djgpp` via `scripts/setup-symlinks.sh`
- `vendor/cwsdpmi/cwsdpmi.exe` + `cwsdpmi.doc` vendored (CWSDPMI r7, copied from sibling vellm)
- DJGPP 12.2.0 + binutils confirmed callable via `make djgpp-check`
- DOSBox-X 2025.02.01 confirmed installed and runnable headless

### Phase 1 — Toolchain smoke test (gate passed)
- `make hello` compiles `tests/smoketest/hello.c` with `i586-pc-msdosdjgpp-gcc -march=i486 -mtune=pentium -O2 -Wall`, producing `build/hello.exe` (150,962 bytes), stubedit'd to `minstack=256k` with autoload of `CWSDPMI.EXE`
- `make smoke-fast` — `[smoke] PASS` under `tools/dosbox-x-fast.conf` (`cycles=max`)
- `make smoke` — `[smoke] PASS` under `tools/dosbox-x.conf` parity config (`cycles=fixed 40000`)
- Headless DOSBox-X test harness via `tools/dosbox-run.sh` (silent + exit, captures `STDOUT.TXT` from the ephemeral C: mount)
- First automated correctness gate (per `CLAUDE.md § Correctness Gate` item 1) — passing

### Phase 2 — SDL3 for DOS (gate passed)
- `vendor/sources.manifest` SHAs resolved out of `PIN_ME` for all five upstreams (SDL `74a7462`, sdl2-compat `91d36b8`, SDL_mixer `2b00802`, SDL_image `67c8f53`, nxengine-evo `1f093d1`)
- `make sdl3` cross-builds SDL3 via DJGPP into `build/sysroot/lib/libSDL3.a` (~2 MB, 8 DOS-backend translation units present, `SDL_AUDIO_DRIVER_DOS_SOUNDBLASTER=1` and `SDL_VIDEO_DRIVER_DOSVESA=1` confirmed in the generated `SDL_build_config.h`)
- `make sdl3-smoke` — new DOS-backend probe and DOSBox-X harness (`tests/sdl3-smoke/sdltest.c`, `tests/run-sdl3-smoke.sh`). Verifies video subsystem init, mode enumeration (34 VESA modes including all four 320x240 variants — XRGB8888 / RGB565 / XRGB1555 / **INDEX8**, the last is the Phase 9 lever 3 target), and audio driver bootstrap
- `tests/run-smoke.sh --contains STRING` — additive substring stdout match alongside the existing exact-match `--expected`
- `tools/dosbox-run.sh --merge-stderr` — additive optional stderr capture
- `tests/fixtures/sdl3-modes-dosbox.txt` — VESA mode-list baseline captured under DOSBox-X for Phase 8 real-hardware diff against `M64VBE` on g2k

### Known issues
- **PR #15377 SoundBlaster detection fails under DOSBox-X SB16 emulation.** `SDL_Init(SDL_INIT_AUDIO)` errors out: DSP reset's "data ready" flag goes true but the byte read is not `0xAA`. Tracked as task #16 (downstream investigation, will produce `patches/SDL/*.patch` if local) and #17 (upstream bug report at libsdl-org/SDL, draft pending a human-with-`gh`-creds filing — URL TBD). Does not block Phases 3–6; will be resolved before the Phase 7 playtest gate where audio is required.
- sdl2-compat on DJGPP is untested upstream; Phase 3 remains the highest-risk phase.
