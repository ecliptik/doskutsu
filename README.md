# DOSKUTSU

DOSKUTSU is a faithful port of Cave Story (Doukutsu Monogatari) to MS-DOS 6.22 on vintage Pentium-class hardware. It plays Daisuke "Pixel" Amaya's 2004 freeware classic on real 1990s-era PCs via [SDL3](https://www.libsdl.org/)'s [DOS backend](https://github.com/libsdl-org/SDL/pull/15377), [DJGPP](https://www.delorie.com/djgpp/), and [CWSDPMI](https://en.wikipedia.org/wiki/DOS_Protected_Mode_Interface).

The name is a portmanteau of **DOS** and **Doukutsu Monogatari** (Cave Story's original Japanese title); it also fits the DOS 8.3 filename convention as `DOSKUTSU.EXE`.

This project exists for preservation, for the historical-computing community, and as an engineering artifact: running Cave Story on hardware that became obsolete eight years before Pixel released it. The reference PC is a 1995-era desktop with an Intel Pentium OverDrive 83 MHz CPU upgrade.

<p align="center">
<a href="#status">Status</a> | <a href="#game-assets">Game Assets</a> | <a href="#requirements">Requirements</a> | <a href="#usage">Usage</a> | <a href="#building">Building</a> | <a href="#boot-profile">Boot Profile</a> | <a href="#how-this-project-is-developed">How It's Developed</a> | <a href="#acknowledgments">Acknowledgments</a> | <a href="#license">License</a>
</p>

### DOSBox-X

| | |
|:---:|:---:|
| <img src="docs/screenshots/doskutsu-title.png" alt="DOSKUTSU title screen running in DOSBox-X" width="100%"> | <img src="docs/screenshots/doskutsu-intro-dialogue.png" alt="Opening dialogue: From somewhere, a transmission..." width="100%"> |
| **Title Screen** | **Opening Transmission** |
| <img src="docs/screenshots/doskutsu-first-room.png" alt="First lab room with Quote and the broken teleporter" width="100%"> | <img src="docs/screenshots/doskutsu-first-cave.png" alt="First Cave with Quote, HUD, and a Heart pickup" width="100%"> |
| **First Lab Room** | **First Cave** |

<p align="center">captures from DOSBox-X running <code>DOSKUTSU.EXE</code></p>

---

## Status

DOSKUTSU runs at **~30 fps median** on the reference PC (Intel Pentium OverDrive 83 MHz / Cirrus Logic CL-GD5434 / Creative SB16 PnP) with music on the default OPL3 FM-synth backend. The original organya software synthesizer is selectable, but runs lower -- its per-tick software mixing is the single heaviest cost on a Pentium-class CPU.

Music, parallax backgrounds, menus, combat, save/load, and the full gameplay path render correctly. Cave Story is authored for 50 fps; the reference PC's memory bus caps the faithful render near 30 fps, and because NXEngine couples game logic 1:1 to render the game then plays slightly slow. The opt-in **Fixed-Timestep mode** (`SDL_HINT_DOSKUTSU_FIXED_TIMESTEP=1`) decouples game logic onto a fixed 50 Hz accumulator so the game plays at near-correct speed at full fidelity with no visual cost; it is default-OFF pending a full-play validation pass. Per-wave performance history is in [CHANGELOG.md](./CHANGELOG.md).

### Audio backends

`DOSKUTSU.EXE` ships three music backends, selected via the `SDL_HINT_DOSKUTSU_AUDIO_BACKEND` environment variable:

- **opl3** (default): dispatches converted MIDI to the SB16 / Sound Blaster Pro 2 OPL3 FM synthesizer. Moving music off the CPU's software mixer is worth **~+8.77 fps** over organya at the canonical scene on the reference PC -- which is why it is the default. The bundled WiiWare-arrangement MIDIs track the original soundtrack closely.
- **organya**: the original Cave Story `.org` synthesizer, software-mixed inside the engine -- the faithful 2004 timbre. Opt in with `SDL_HINT_DOSKUTSU_AUDIO_BACKEND=organya`; its per-tick mixing is the heaviest single cost on the audio path on Pentium-class CPUs.
- **wb** (WaveBlaster): dispatches converted MIDI to a WaveBlaster header daughterboard (e.g. Serdaco DreamBlaster S2) over the SB16 DSP-mediated path. Same CPU-offload class as OPL3; wavetable timbre depends on the daughterboard firmware.

MIDI source-file selection is independent of backend choice and is set by `SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE` (`wiimidi` for the WiiWare arrangement, `orgmid` for the Hart legacy `.mid` set; optional `DOSKUTSU_GM_VARIANT=v1` or `v2` picks an `org2mid`-converted variant).

---

## Game Assets

**DOSKUTSU does not include any Cave Story game data.** The binary you build from this repository plays nothing on its own. Users supply their own copy of Pixel's 2004 EN freeware assets, extracted from the canonical `Doukutsu.exe`.

[docs/ASSETS.md](./docs/ASSETS.md) is the canonical, complete asset procedure -- follow it start to finish; it covers fetching the freeware bundle and extracting the full data tree (maps, sprites, music, SFX) plus the expected directory layout. The two scripts below automate only the Pixtone-SFX slice of that workflow; running them alone does not produce a playable `DATA\` tree:

- `scripts/fetch-cs-pxt.py` is the one-shot orchestrator. It fetches the 2004 EN freeware bundle from [cavestory.one](https://www.cavestory.one/downloads/cavestoryen.zip) (SHA-256-pinned), extracts `Doukutsu.exe` to a tempdir, runs the Pixtone parameter extractor, and cleans up. Pixel's freeware archive does not persist on the user's machine after the script completes.
- `scripts/extract-pxt.py` is the canonical extractor, transcribed from NXEngine-evo's own `extract/extractpxt.cpp`. It operates on file offsets in `Doukutsu.exe` and emits ASCII Pixtone parameter files.

The same posture applies as the broader Cave Story port community ([NXEngine-evo](https://github.com/nxengine/nxengine-evo), [doukutsu-rs](https://github.com/doukutsu-rs/doukutsu-rs)): the engine code is open source; the game data is user-supplied freeware.

### Cave Story+ commercial release: incompatible

NICALIS published Cave Story+ as a commercial product across multiple platforms. It contains additional SFX as proprietary WAV samples. Those samples are NICALIS-owned and not GPLv3-compatible; DOSKUTSU cannot use them and the build system has no integration path for them. Use the 2004 freeware exclusively.

---

## Requirements

DOSKUTSU targets two hardware tiers.

**Tier 1: Reference (tested)** -- the PC every real-hardware measurement is taken on:

- CPU: Pentium 75 MHz or faster (reference PC uses a Pentium OverDrive 83)
- RAM: 16 MB or more
- Video: VESA 1.2+ with chip-level 320x240 support (Cirrus CL-GD5434, Tseng, Trident, S3, etc.; [UniVBE](https://en.wikipedia.org/wiki/UniVBE) 6.70 works as a VESA fallback driver)
- Sound: Sound Blaster 16 or compatible
- OS: MS-DOS 6.22 or compatible
- Disk: ~10 MB free

**Tier 2: Minimum (designed for, not yet tested on hardware)**:

- CPU: 486DX2-66 with FPU
- RAM: 8 MB or more
- Video: VESA 1.2+ (UniVBE loadable if firmware lacks it)
- Sound: any SB16-compatible

Hard floors are non-negotiable: no 486SX without a 487 coprocessor (DJGPP emits x87 instructions); no pre-VESA video (the SDL3 DOS backend requires VESA 1.2+ with a linear framebuffer); no DOS variant that cannot host CWSDPMI's DPMI 0.9 service.

---

## Usage

```
C:\DOSKUTSU>DOSKUTSU
```

`DOSKUTSU.EXE` expects its DPMI host and game data co-located in one directory:

```
C:\DOSKUTSU\
  DOSKUTSU.EXE      the game binary
  CWSDPMI.EXE       the DPMI host -- must sit beside DOSKUTSU.EXE
  DATA\             user-extracted Cave Story assets (see Game Assets)
```

`make install CF=...` and `make dist` lay the directory out this way for you. Title screen should appear within a few seconds. Controls follow NXEngine-evo's defaults:

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

Save files live in `DATA\Profile.dat` alongside the binary.

---

## Building

Full build documentation in [docs/BUILDING.md](./docs/BUILDING.md): prerequisites, DJGPP cross-compiler install, the four-stage build (SDL3, SDL3_mixer, SDL3_image, NXEngine-evo), DOSBox-X testing, common errors.

Short version, once DJGPP is installed:

```bash
git clone ssh://git@forgejo.ecliptik.com/ecliptik/doskutsu.git
cd doskutsu
./scripts/setup-symlinks.sh     # one-time: link tools/djgpp to the emulators hub
./scripts/fetch-sources.sh      # clone the upstream repos at pinned SHAs
./scripts/apply-patches.sh      # apply DOS-port patches
make                            # orchestrate all four build stages
make smoke-fast                 # headless DOSBox-X smoke (fast config)
```

---

## Boot Profile

DOSKUTSU runs under any DJGPP-compatible DOS boot profile with:

- `HIMEM.SYS` loaded
- `NOEMS` (DJGPP uses DPMI, not EMS; an EMS page frame just wastes UMB space)
- SB16-compatible `BLASTER` environment variable set
- VESA 1.2+ video BIOS (UNIVBE works as a fallback)
- CTMOUSE or equivalent INT 33h mouse driver (optional; keyboard-only play is fully supported)

---

## How This Project Is Developed

DOSKUTSU is developed agentically through [Claude Code](https://claude.com/code).

- **Claude authors patches across the full source stack.** Patches touch the SDL3 DOS backend (in `vendor/SDL/`), the NXEngine-evo engine (in `vendor/nxengine-evo/`), the build system, scripts, documentation, and test harnesses. Patches land as `patches/<vendor>/NNNN-*.patch` files in this repository.
- **Humans review every patch before commit.** Commit messages cite reasoning and measurement evidence; speculative perf claims are refuted or confirmed against real-hardware iter results on the reference PC before patches are promoted from instrumentation to optimization.
- **Workspace-local patches only.** This project does not contribute patches upstream to [libsdl-org/SDL](https://github.com/libsdl-org/SDL), [libsdl-org/SDL_mixer](https://github.com/libsdl-org/SDL_mixer), [libsdl-org/SDL_image](https://github.com/libsdl-org/SDL_image), or [nxengine/nxengine-evo](https://github.com/nxengine/nxengine-evo).
- **Agent-team coordination.** Specialist roles (engine, SDL backend, build orchestration, real-hardware iter, perf diagnostics) coordinate via shared task lists and peer messaging during development sessions.

---

## Acknowledgments

- **[Cave Story / Doukutsu Monogatari](https://www.cavestory.org/)** by Daisuke "Pixel" Amaya (2004), available under Pixel's original freeware terms; game data is not redistributed here
- **[NXEngine-evo](https://github.com/nxengine/nxengine-evo)**, the open-source C++11 re-implementation of the Cave Story engine; GPLv3 plus third-party licenses
- **[SDL3](https://www.libsdl.org/)** by Sam Lantinga and the SDL team
- **[SDL3 DOS backend](https://github.com/libsdl-org/SDL/pull/15377)** by the PR #15377 author, the piece that makes this port possible
- **[DJGPP](https://www.delorie.com/djgpp/)** by DJ Delorie, the 32-bit DOS GCC port
- **[CWSDPMI](https://www.delorie.com/pub/djgpp/current/v2misc/)** by Charles W. Sandmann, DOS DPMI host
- **[DOSBox-X](https://dosbox-x.com/)**, DOS emulator for pre-hardware testing
- **[build-djgpp](https://github.com/andrewwutw/build-djgpp)** by Andrew Wu, installer wrapper
- **[Geomys](https://codeberg.org/ecliptik/geomys)**, sibling retro-port project, documentation and team-structure reference
- **[Claude Code](https://claude.com/code)** by [Anthropic](https://www.anthropic.com/)

Full attribution matrix: [THIRD-PARTY.md](./THIRD-PARTY.md).

---

## License

The repository-original source -- the build system, scripts, agent-team orchestration, and documentation -- is licensed under the **MIT License**. See [LICENSE](./LICENSE).

The DOS-port patches under `patches/` are derivative works of their upstreams and inherit those upstreams' licenses, not MIT: patches against NXEngine-evo are **GPLv3**, and patches against SDL3 / SDL3_mixer / SDL3_image are **zlib**.

The `DOSKUTSU.EXE` binary is GPLv3 as a combined work because it statically links [NXEngine-evo](https://github.com/nxengine/nxengine-evo), which is GPLv3. Redistributed binary bundles include a copy of the GPLv3 license text and a pointer back to this repository's source.

| Component | License | Linked into `DOSKUTSU.EXE`? |
|---|---|---|
| DOSKUTSU port source (this repo) | [MIT](./LICENSE) | n/a (source, not binary) |
| **NXEngine-evo** | **[GPLv3](https://github.com/nxengine/nxengine-evo/blob/master/LICENSE)** | **Yes; dominant license of the binary** |
| SDL3 | [zlib](https://github.com/libsdl-org/SDL/blob/main/LICENSE.txt) | Yes (zlib is GPLv3-compatible) |
| SDL3_mixer | [zlib](https://github.com/libsdl-org/SDL_mixer/blob/main/LICENSE.txt) | Yes |
| SDL3_image | [zlib](https://github.com/libsdl-org/SDL_image/blob/main/LICENSE.txt) | Yes |
| DJGPP libc | [GPL with runtime-library exception](https://www.delorie.com/djgpp/v2faq/faq11_2.html) | Yes (the exception explicitly permits static linking) |
| CWSDPMI | [freeware, redistribution permitted](./vendor/cwsdpmi/cwsdpmi.doc) | No; separate executable shipped alongside |
| LFNDOS | [GPLv2](./vendor/lfndos/COPYING) | No; separate TSR shipped alongside |
| DOSLFN | [Freeware with sources](./vendor/doslfn/doslfn.txt) | No; separate TSR shipped alongside |
| Cave Story game data | [freeware per Pixel's 2004 terms](https://www.cavestory.org/) | No; user-extracted, not redistributed in this repo |
| Cave Story+ assets | NICALIS commercial / proprietary | **No; not GPLv3-compatible; cannot bundle** |
