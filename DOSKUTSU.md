# DOSKUTSU — Project Overview

Companion reference to `README.md` (user-facing) and `PLAN.md` (phased roadmap). This document describes the project as a whole: what we're porting, how the pieces stack, which seams we own vs inherit, and where to look when something is wrong.

---

## Project identity

**Name:** DOSKUTSU — portmanteau of **DOS** + **Doukutsu Monogatari** (Cave Story's original Japanese title, 洞窟物語). Fits the DOS 8.3 filename convention exactly: `DOSKUTSU.EXE`.

**Goal:** Play Cave Story on a 1995 business PC — the Gateway 2000 Pentium OverDrive 83 MHz (Socket 3, P54C core, no MMX) — using 2026 SDL tooling and the NXEngine-evo engine re-implementation. The deliverable is a single `DOSKUTSU.EXE` plus `CWSDPMI.EXE` plus user-extracted Cave Story data, bootable from a CF card on real DOS 6.22 hardware.

**Why:** "Ars gratia artis." Cave Story was released in December 2004 by Daisuke "Pixel" Amaya, eight years after the g2k machine would have been obsolete. Running it on that hardware is an artifact that could not have existed when either the hardware or the game were current. The port has no practical utility. That is the point.

---

## The stack, top to bottom

```
                     ┌──────────────────────────┐
   Game content      │  Cave Story 2004 data    │  user-extracted, freeware, not in repo
                     │  (maps, sprites, music)  │
                     └──────────────────────────┘
                                  │
                     ┌──────────────────────────┐
                     │  NXEngine-evo            │  C++11, GPLv3
   Engine            │  (Cave Story reimpl)     │  locked 320x240, widescreen retained
                     │  migrated SDL2 → SDL3    │  per Path B (PLAN § 2026-04-24)
                     └──────────────────────────┘
                                  │  SDL3 API calls
                     ┌──────────────────────────┐
   Audio + image     │  SDL3_mixer (WAV, OGG)   │  zlib
   helpers           │  SDL3_image (PNG)        │  zlib
                     └──────────────────────────┘
                                  │  SDL3 API calls
                     ┌──────────────────────────┐
   Platform abstr.   │  SDL3 + DOS backend      │  zlib — PR #15377
                     │  (VGA/VESA, SB16, INT 33)│
                     └──────────────────────────┘
                                  │
                     ┌──────────────────────────┐
   C runtime         │  DJGPP libc              │  GPL+RLE — static link OK
                     └──────────────────────────┘
                                  │
                     ┌──────────────────────────┐
   Protected-mode    │  CWSDPMI (separate .exe) │  freeware, bundled alongside
   host              │                          │
                     └──────────────────────────┘
                                  │  DPMI 0.9
                     ┌──────────────────────────┐
   OS                │  MS-DOS 6.22             │
                     │  + HIMEM.SYS + BLASTER   │
                     └──────────────────────────┘
                                  │  BIOS / VESA
                     ┌──────────────────────────┐
   Hardware          │  Pentium OverDrive 83,   │  the g2k machine
                     │  48 MB, Vibra16S, Mach64 │
                     └──────────────────────────┘
```

Each layer is vendored as a pinned snapshot under `vendor/<name>/` and built by a stage of the top-level `Makefile`. No layer is an ABI dependency on the host system (except DOSBox-X for dev testing and DOS itself at runtime).

---

## What we own vs inherit

| Owned (in this repo) | Inherited (vendored upstream) |
|---|---|
| `Makefile` — the four-stage build orchestration | NXEngine-evo source tree + its CMakeLists |
| `scripts/fetch-sources.sh`, `apply-patches.sh`, `setup-symlinks.sh` | SDL3 source + the DOS backend from PR #15377 |
| `tools/dosbox-x.conf`, `dosbox-x-fast.conf`, `dosbox-launch.sh`, `dosbox-run.sh` | SDL3_mixer + stb_vorbis |
| `patches/<name>/*.patch` (DOS-port patches) | SDL3_image + stb_image |
| `tests/smoketest/*` + `tests/run-smoke.sh` | DJGPP toolchain (from `~/emulators/tools/djgpp/`) |
| All docs under `docs/` + top-level `.md` files | CWSDPMI binary (sourced, but redistributed unmodified) |
| `vendor/cwsdpmi/` (binary + docs) | sdl2-compat (cloned but not built post-Path-B; preserved per `PLAN § Plan Amendments § 2026-04-24`) |
| `vendor/sources.manifest` (what we pin) | Everything under `vendor/<name>/` after `fetch-sources.sh` runs |

The explicit goal is that **our diff against upstream stays reviewable.** All DOS-specific changes to upstream code live in `patches/<name>/*.patch` — never as ad-hoc edits in the cloned vendor trees. All DOS-specific choices in our own code are annotated with `// DOS-PORT:` so `grep 'DOS-PORT:' -r <file>` enumerates the full delta.

---

## Seams that matter

These are the interfaces where a failure at one layer shows up as a symptom at another. When something breaks, identify the seam first.

### SDL3 DOS backend → DJGPP runtime

The DOS backend uses DJGPP's `uclock()` for timing, DJGPP's `setjmp`/`longjmp` for the cooperative scheduler, and real-mode INT calls via DJGPP's `__dpmi_int` for VESA + SB16 programming. When VESA mode switch fails or SB16 init hangs, the root cause is almost always in this seam — not in SDL3 API usage.

**Symptoms of problems here:** black screen after init, audio silence with no error, cooperative scheduler wedged (game window unresponsive but DOS not frozen).

### NXEngine-evo → SDL3 (post-Path-B; sdl2-compat removed from the link line)

Per `PLAN.md § Plan Amendments § 2026-04-24`, NXEngine-evo's source was migrated SDL2 → SDL3 directly (patches `0010`–`0019`, with `0020`–`0024` Phase 5 follow-ups, plus the audio cluster `0013`–`0017` for `SDL_AudioCVT` → `SDL_AudioStream` and `Mix_*` → `MIX_*`). There is no shim layer. Symptoms that previously belonged to "sdl2-compat → SDL3" now manifest at the engine ↔ SDL3 boundary directly.

**Symptoms of problems here:** audio with wrong pitch / speed / stereo / mono (suspect the Pixtone or Organya migration patches `0013`–`0017`); SDL3-era `SDL_GetError()` strings appearing in `debug.log` (suspect a missed mechanical-rename site — `0010`–`0012` was the bulk pass, `0021`/`0022` the follow-ups); link errors against `libSDL3.a` (suspect `find_package(SDL3)` wireup in `0019`).

### NXEngine-evo's renderer → SDL3's software renderer

NXEngine-evo calls `SDL_CreateRenderer(_window, -1, SDL_RENDERER_ACCELERATED)` by default. `patches/nxengine-evo/0004-renderer-force-software-renderer.patch` forces `SDL_RENDERER_SOFTWARE` because SDL3-DOS has no accelerated renderer. Once software, the path is `SDL_Surface` → `SDL_CreateTextureFromSurface` → `SDL_RenderTexture` (post-Path-B; was `SDL_RenderCopy` under SDL2), which is a per-frame texture upload we probably don't need.

**Symptoms of problems here:** correct output but severe FPS drop (upload-bound); garbled output (texture format mismatch with the software renderer's internal surface format).

**Mitigation (Phase 9):** bypass the texture path entirely and go surface-to-surface via `SDL_BlitSurface`.

### DOSBox-X parity config → real hardware

`tools/dosbox-x.conf` targets `cycles=fixed 40000` to approximate Pentium-40-class integer throughput. That's a rough calibration of what a PODP83 behaves like, but:

- Memory bandwidth is not calibrated (DOSBox-X uses host RAM speed; real HW is 80 ns EDO)
- VESA BIOS behavior is not emulated (DOSBox-X provides a synthetic VBE)
- SB16 DMA IRQ timing is emulated, not measured
- BIOS INT timings are approximated

**Implication:** DOSBox-X is necessary but not sufficient. Real-hardware testing on g2k is the only authoritative gate. When DOSBox-X and g2k disagree, trust g2k.

### Our binary + CWSDPMI → DOS 6.22

CWSDPMI loads from CWD or PATH on first invocation of `DOSKUTSU.EXE` and then self-installs into XMS. Subsequent runs don't need `CWSDPMI.EXE` on disk (until reboot). This means:

**Symptoms of problems here:** "No DPMI host available" at startup — `CWSDPMI.EXE` not found. "Not enough memory" — XMS is exhausted (SMARTDRV cache too large, or prior CWSDPMI session didn't release).

### DOS + BLASTER env var → SB16 programming

SDL3's DOS backend reads the `BLASTER` environment variable at init time (`SET BLASTER=A220 I5 D1 H5 T6`). If the variable is missing, malformed, or doesn't match the actual hardware, audio init silently falls back or fails.

**Symptoms:** audio silence with init appearing successful; audio on wrong IRQ (stutter + dropouts).

---

## Project relationships

DOSKUTSU is part of a cluster of retro-port projects sharing the `~/emulators/` toolchain hub:

- **[vellm](https://forgejo.ecliptik.com/ecliptik/vellm)** — llama2.c on DOS. Same DJGPP toolchain, same DOSBox-X testing pattern, different workload (ML inference, not a game). DOSKUTSU's Makefile orchestration, `tools/dosbox-*.sh`, and the `vendor/cwsdpmi/` convention all derive from vellm.
- **[Geomys](https://codeberg.org/ecliptik/geomys)** — Gopher browser for 68K Mac. Same doc conventions (README structure, CLAUDE.md format, team-role definitions).
- **[Flynn](https://codeberg.org/ecliptik/flynn)** — Telnet/Finger client for 68K Mac. Same doc conventions as Geomys.

All four share `~/emulators/` as the toolchain hub:
- `~/emulators/tools/djgpp/` — DJGPP for DOS projects (vellm, doskutsu)
- `~/emulators/tools/retro68-build/` — Retro68 for 68K projects (geomys, flynn)
- `~/emulators/snow/`, `~/emulators/basilisk/` — Mac emulators
- `~/emulators/scripts/` — shared installers + automation
- `~/emulators/docs/` — shared platform docs (DJGPP.md, DOSBOX.md, SNOW.md, etc.)

The hub itself lives at `/home/claude/emulators/` and has its own `CLAUDE.md` with cross-project rules.

---

## What "done" looks like

A successful v1.0 of DOSKUTSU ships:

1. `dist/doskutsu-cf.zip` — CF-ready bundle: `DOSKUTSU.EXE`, `CWSDPMI.EXE`, `CWSDPMI.DOC`, `LICENSE.TXT`, `GPLV3.TXT`, `THIRD-PARTY.TXT`, `README.TXT`
2. `README.md` populated with real-HW benchmarks on the g2k machine (frame times + audio buffer health at 22050 stereo and 11025 mono)
3. `bench/results.md` with per-stage timings (Mimiga Village scroll, First Cave combat, Egg Corridor entry)
4. One full-game playthrough video from g2k, demonstrating save/load and ending reach
5. Git tag `v1.0.0` pushed to Forgejo; `dist/doskutsu-cf.zip` attached to the release

The project is self-contained after that. No recurring maintenance is expected — Cave Story isn't going to get new levels.

Phases 2+ (re-enable widescreen at runtime, PicoGUS native backend, DreamBlaster S2 music, JP original support, modding) are v2.0+ territory, see `TODO.md § Future`.
