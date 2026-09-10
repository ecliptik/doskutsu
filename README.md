# DOSKUTSU

<p align="center">
<a href="#quickstart">Quickstart</a> | <a href="#status">Status</a> | <a href="#requirements">Requirements</a> | <a href="#download">Download</a> | <a href="#usage">Usage</a> | <a href="#building">Building</a> | <a href="#how-this-project-is-developed">How It's Developed</a> | <a href="#components-and-license">Components and License</a>
</p>

DOSKUTSU is a faithful port of [Cave Story](https://www.cavestory.org/) (Doukutsu Monogatari) to MS-DOS 6.22 on retro Pentium-class hardware. It plays Daisuke "Pixel" Amaya's 2004 freeware classic on real 1990s-era PCs via [SDL3](https://www.libsdl.org/)'s [DOS backend](https://github.com/libsdl-org/SDL/pull/15377), [DJGPP](https://www.delorie.com/djgpp/), and [CWSDPMI](https://en.wikipedia.org/wiki/DOS_Protected_Mode_Interface).

The name is a portmanteau of **DOS** and **Doukutsu Monogatari** (Cave Story's original Japanese title).

DOSKUTSU exists for preservation and the engineering challenge of running Cave Story on a 1990s MS-DOS PC.

This project was 100% built agentically using [Claude Code](https://claude.com/claude-code).

### Screenshots

| | |
|:---:|:---:|
| <img src="docs/screenshots/doskutsu-title.png" alt="DOSKUTSU title screen running in DOSBox-X" width="100%"> | <img src="docs/screenshots/doskutsu-first-room.png" alt="First lab room with Quote and the broken teleporter" width="100%"> |
| **Title Screen** | **First Lab Room** |
| <img src="docs/screenshots/doskutsu-first-cave.png" alt="First Cave with Quote, HUD, and a Heart pickup" width="100%"> | <img src="docs/screenshots/setup-main-menu.png" alt="SETUP.EXE main menu with detected system profile" width="100%"> |
| **First Cave** | **SETUP.EXE** |

<p align="center">captures from DOSBox-X running <code>DOSKUTSU.EXE</code> and <code>SETUP.EXE</code></p>

### Demo

<p align="center">
<a href="https://www.youtube.com/watch?v=YLFztifZKQ8"><img src="https://img.youtube.com/vi/YLFztifZKQ8/maxresdefault.jpg" alt="Cave Story running on DOS (DOSKUTSU) -- watch on YouTube" width="75%"></a>
</p>

<p align="center"><a href="https://www.youtube.com/watch?v=YLFztifZKQ8">DOSKUTSU running on Gateway 2000 reference hardware</a></p>

---

## Quickstart

1. **Get the binaries.** [Build from source](#building) or download the latest `doskutsu-<version>.zip` [release](#download) and unzip it.
2. **Add game data.** DOSKUTSU ships no Cave Story content. See [ASSETS.md](./docs/ASSETS.md) for details.
3. **Copy the folder** containing binaries and assets to a DOS system, or mount it in [DOSBox-X](https://dosbox-x.com/).
4. **Configure sound.** Run `SETUP.EXE` to auto-detect sound cards or manually configure.
5. **Play.** Run `DOSKUTSU.EXE` from the game folder.

See [Download](#download), [Game Assets](#game-assets), [Building](#building), [Usage](#usage), [Configuration](#configuration) for details.

---

## Status

**Features**

- DOSKUTSU plays the full game, start to finish
- Sound Blaster, AdLib, OPL3 FM, WaveBlaster, General MIDI, Gravis UltraSound, and PicoGUS sound support
- Original Organya soundtrack or MIDI music with selectable arrangements (see [docs/SOUND.md](./docs/SOUND.md))
- Gameport joystick/gamepad support and input remapping
- DOS-era like `SETUP.EXE` configuration utility
- Up to ~33fps (depending on CPU and bus bandwidth)
- TAS support (see [docs/TAS.md](./docs/TAS.md))

Frame rate depends on CPU, video card, and audio backend. Figures below are real-hardware measurements from an identical 102-second replay:

| CPU | Frame rate | Best configuration |
|---|---|---|
| Pentium OverDrive 83 | ~33 fps | AdLib + S3 ViRGE |
| Am5x86-133 | ~33 fps | AdLib + S3 ViRGE |
| 486DX2-66 | ~25 fps | AdLib + S3 ViRGE |
| 486DX2-50 | ~19 fps | AdLib + S3 ViRGE, choppy |

[docs/BENCHMARKS.md](./docs/BENCHMARKS.md) is the full report: 157 cells across two measurement rounds, four CPUs, three video cards, and three sound cards, with charts, method, and raw logs.

Cave Story runs at 50 fps natively; on this hardware it renders at up to ~33 fps, but [Fixed-Timestep mode](#fixed-timestep-mode) keeps game logic advancing at the correct 50 Hz regardless of render rate.

See the [changelog](CHANGELOG.md) for development and progress details.

---

## Requirements

**Recommended**

- CPU: Pentium 75 MHz or faster
- RAM: 32 MB
- Video: VESA 1.2+ with 320x240 support
- Sound: Sound Blaster 16 or compatible; AdLib/OPL2-only and Gravis UltraSound (or PicoGUS) cards also supported
- OS: MS-DOS 6.22 or compatible
- Disk: 10 MB free

**Minimum**

- CPU: 486DX2-66 with FPU
- RAM: 16 MB
- Video: VESA 1.2+
- Sound: Sound Blaster 16 or compatible; AdLib/OPL2-only and Gravis UltraSound (or PicoGUS) cards also supported
- OS: MS-DOS 6.22 or compatible
- Disk: 10 MB free

---

## Fixed-Timestep mode

Cave Story's engine ties game logic to the render rate, so at 30 fps it also runs at ~60% speed. Fixed-Timestep mode decouples the two: logic advances on a fixed 50 Hz clock regardless of frame rate, so the game plays at its intended speed even with fewer frames drawn. Motion is less smooth; speed is correct.

On by default; set `SDL_HINT_DOS_FIXED_TIMESTEP=0` for the legacy frame-coupled loop.

## Audio backends

Music plays through either Cave Story's original Organya synthesizer or MIDI. Organya is more faithful but costs significant CPU; MIDI runs on a hardware synthesizer, off the CPU, and is the recommended default. Supported hardware:

- **Sound Blaster OPL3 FM** -- the default; works on any Sound Blaster; sound effects on the SB DAC
- **WaveBlaster / DreamBlaster** -- wavetable daughterboard on the SB16 WaveBlaster header
- **AdLib / OPL2** -- music on a card with no Sound Blaster (music only; no sound effects)
- **Gravis UltraSound** (or PicoGUS) -- GF1 wavetable music *and* sound effects; no Sound Blaster needed
- **Organya** -- Pixel's original tracker synth, in software (higher CPU cost)

MIDI backends offer a choice of music sets: an `org2mid` conversion of the original score (default), the WiiWare arrangement, or a custom drop-in set. Configure everything in `SETUP.EXE`; see [docs/SOUND.md](./docs/SOUND.md) for sound configuration and [docs/CONFIG.md](./docs/CONFIG.md) for every setting and environment variable.

---

## Download

See **[Releases](https://github.com/ecliptik/doskutsu/releases)** for pre-built binaries, or build from source (see [Building](#building)).

<!-- LATEST-RELEASE:START -->
**Latest release:** [`doskutsu-1.7.0.zip`](https://github.com/ecliptik/doskutsu/releases/download/v1.7.0/doskutsu-1.7.0.zip) (v1.7.0)
<!-- LATEST-RELEASE:END -->

Each bundle (`doskutsu-<version>.zip`) contains `DOSKUTSU.EXE`, `SETUP.EXE`, the `CWSDPMI.EXE` DPMI host, license texts, and NXEngine-evo's GPLv3 engine data. The engine is the program; game data is user-supplied, like a Doom source port shipping without an IWAD.

### Game Assets

**DOSKUTSU ships no Cave Story game data** and plays nothing on its own. Users supply their own copy of the 2004 EN freeware assets, extracted from the canonical `Doukutsu.exe`.

[docs/ASSETS.md](./docs/ASSETS.md) is the canonical, complete procedure -- follow it start to finish; it covers fetching the freeware bundle, extracting the full data tree (maps, sprites, music, SFX), and the expected directory layout. The two scripts below automate only the Pixtone-SFX slice of that workflow; running them alone does not produce a playable `DATA\` tree:

- `scripts/fetch-cs-pxt.py` is the one-shot orchestrator: fetches the 2004 EN freeware bundle from [cavestory.one](https://www.cavestory.one/downloads/cavestoryen.zip) (SHA-256-pinned), extracts `Doukutsu.exe` to a tempdir, runs the Pixtone extractor, and cleans up. The archive doesn't persist after it runs.
- `scripts/extract-pxt.py` is the canonical extractor, transcribed from NXEngine-evo's `extract/extractpxt.cpp`. It reads file offsets in `Doukutsu.exe` and emits ASCII Pixtone parameter files.

Same posture as the broader Cave Story port community ([NXEngine-evo](https://github.com/nxengine/nxengine-evo), [doukutsu-rs](https://github.com/doukutsu-rs/doukutsu-rs)): engine code is open source, game data is user-supplied freeware.

---

## Usage

`DOSKUTSU.EXE`, the CWSDPMI host, and the Cave Story data all live together in one directory:

```
C:\DOSKUTSU\
  DOSKUTSU.EXE     the game
  SETUP.EXE        hardware / sound configurator (run once before first play)
  SETUP.BAT        launcher for SETUP.EXE (clears stale audio settings first)
  CWSDPMI.EXE      the DPMI host - must sit beside DOSKUTSU.EXE
  DOSKUTSU.CFG     written by SETUP.EXE (optional; the game runs without it)
  DATA\            Cave Story assets, user-extracted (see Game Assets)
```

See [Quickstart](#quickstart) to get binaries/assets and set up the game directory.

The DOS machine needs a standard DJGPP boot environment: `HIMEM.SYS` loaded, `NOEMS`, a SB16-compatible `BLASTER` variable, and a VESA 1.2+ video BIOS (a software VESA driver works as a fallback).

Run `SETUP.EXE` once to configure sound ([Configuration](#configuration)), then
run the game:

```
C:\DOSKUTSU> SETUP          (once, to configure)
C:\DOSKUTSU> DOSKUTSU       (play)
```

The title screen appears within a few seconds. Controls follow NXEngine-evo's defaults:

| Key | Action |
|---|---|
| Arrow keys | Move / navigate menus |
| Z | Jump / confirm |
| X | Fire / cancel |
| A / S | Cycle weapons |
| Q | Inventory |
| W | Map |
| Escape | Pause menu |
| F11 | Toggle fullscreen (no-op on DOS; always fullscreen) |

Use `SETUP.EXE` to map keys and configure joystick support.

### Configuration

Use `SETUP.EXE` to configure DOSKUTSU -- sound, input and other settings:

```
C:\DOSKUTSU> SETUP
```

SETUP detects hardware, recommends settings, and configures sound,
performance, and input. It can play a sound effect and the Title theme to
confirm audio works, then writes `DOSKUTSU.CFG`, which the game reads
at startup. See [docs/SETUP.md](./docs/SETUP.md) for the full reference and
[docs/SOUND.md](./docs/SOUND.md) for sound configuration.

Or skip SETUP and use DOS environment variables (`SET` in
`AUTOEXEC.BAT` or at the prompt). Precedence: **environment variable >
`DOSKUTSU.CFG` > built-in default**. See [docs/CONFIG.md](./docs/CONFIG.md) for
every option.

---

## Building

Building needs a Linux (or WSL) host with:

- the [DJGPP](https://github.com/andrewwutw/build-djgpp) cross-compiler -- the one prerequisite that isn't a package install (~30 min one-time build)
- `cmake`, `git`, `make`, `gcc`, `python3`, `unzip`, `zip`
- `dosbox-x` -- runs the automated build-verification smoke tests

[docs/BUILDING.md](./docs/BUILDING.md) covers distro install commands, the DJGPP install, each build stage, DOSBox-X testing, and common errors.

Once DJGPP is installed -- the one-command path:

```bash
git clone https://github.com/ecliptik/doskutsu.git   # or ssh: git@github.com:ecliptik/doskutsu.git
cd doskutsu
./scripts/bootstrap.sh          # verify prereqs, fetch upstreams, apply patches, build
```

Or run stages individually:

```bash
./scripts/setup-symlinks.sh     # one-time: link tools/djgpp (only if using the ~/emulators hub)
./scripts/fetch-sources.sh      # clone the upstream repos at pinned SHAs
./scripts/apply-patches.sh      # apply DOS-port patches
make                            # orchestrate all four build stages
make smoke-fast                 # headless DOSBox-X smoke (fast config)
make setup                      # build SETUP.EXE (the configurator)
make setup-test                 # host-side SETUP unit tests
```

`make dist` bundles the game, `CWSDPMI.EXE`, and live-audio `SETUP.EXE` into a ready-to-deploy archive.

---

## How This Project Is Developed

DOSKUTSU is developed agentically with [Claude Code](https://claude.com/code).

- **Claude Code authors the patches** across the SDL3 DOS backend, NXEngine-evo, the build system, scripts, and docs, landing as `patches/<vendor>/NNNN-*.patch` files in this repository.
- **Human developers drive testing and iteration**: TAS replays, real-hardware playthroughs, bug reports, and deciding what to fix next.
- **Workspace-local patches only.** Nothing is contributed upstream to [libsdl-org/SDL](https://github.com/libsdl-org/SDL), [libsdl-org/SDL_mixer](https://github.com/libsdl-org/SDL_mixer), [libsdl-org/SDL_image](https://github.com/libsdl-org/SDL_image), or [nxengine/nxengine-evo](https://github.com/nxengine/nxengine-evo).

---

## Components and License

DOSKUTSU's own source -- the build system, scripts, and documentation -- is **MIT-licensed** ([LICENSE](./LICENSE)). The shipped `DOSKUTSU.EXE` is **GPLv3**: it statically links NXEngine-evo (GPLv3), which governs the combined binary. Patches under `patches/` are derivative works of their upstreams and carry those licenses: GPLv3 for the NXEngine-evo patches, zlib for the SDL3 patches. Redistributed bundles include the GPLv3 license text and a pointer back to this repository.

Each component below is listed with its purpose, license, and whether it links into `DOSKUTSU.EXE`:

| Component | Purpose | License | In `DOSKUTSU.EXE` |
|---|---|---|---|
| [DOSKUTSU port source](./LICENSE) (this repo) | Build system, patches, scripts, docs | MIT | n/a - source, not the binary |
| [NXEngine-evo](https://github.com/nxengine/nxengine-evo) | The C++11 re-implementation of the Cave Story engine | [GPLv3](https://github.com/nxengine/nxengine-evo/blob/master/LICENSE) | **Yes - governs the binary** |
| [SDL3](https://www.libsdl.org/) | Platform layer; its [DOS backend](https://github.com/libsdl-org/SDL/pull/15377) is what makes the port possible | [zlib](https://github.com/libsdl-org/SDL/blob/main/LICENSE.txt) | Yes |
| [SDL3_mixer](https://github.com/libsdl-org/SDL_mixer) | Audio mixing | [zlib](https://github.com/libsdl-org/SDL_mixer/blob/main/LICENSE.txt) | Yes |
| [SDL3_image](https://github.com/libsdl-org/SDL_image) | Image loading | [zlib](https://github.com/libsdl-org/SDL_image/blob/main/LICENSE.txt) | Yes |
| [DJGPP](https://www.delorie.com/djgpp/) libc | 32-bit DOS C runtime, by DJ Delorie | [free to use unmodified](https://www.delorie.com/djgpp/v2faq/faq19_1.html) + [GCC Runtime Library Exception](https://www.gnu.org/licenses/gcc-exception-3.1.html) for linked `libgcc` code | Yes - neither imposes GPL on the result |
| [CWSDPMI](https://www.delorie.com/pub/djgpp/current/v2misc/) | DPMI host, by Charles W. Sandmann | [freeware, redistributable](./vendor/cwsdpmi/cwsdpmi.doc) | No - ships alongside as a separate program |
| [Cave Story](https://www.cavestory.org/) game data | Maps, sprites, music, and SFX, by Daisuke Amaya (2004) | [freeware, 2004 terms](https://www.cavestory.org/) | User extracted, not redistributed |

Built with the [DJGPP](https://www.delorie.com/djgpp/) toolchain (installed via [build-djgpp](https://github.com/andrewwutw/build-djgpp) by Andrew Wu), tested with [DOSBox-X](https://dosbox-x.com/) and real hardware.

Full attribution detail: [THIRD-PARTY.md](./THIRD-PARTY.md).
