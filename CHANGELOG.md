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

### Known
- Phases 0 and 1 are green; Phase 2 (build SDL3 with the DOS backend) is next
- `vendor/SDL` requires PR #15377 merged into SDL main; pinned SHA in `vendor/sources.manifest` is still `PIN_ME` and must be resolved before `make sdl3` can run
- sdl2-compat on DJGPP is untested upstream; Phase 3 remains the highest-risk phase
