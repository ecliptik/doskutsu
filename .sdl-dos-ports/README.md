# sdl-dos-ports

A hub for porting SDL-based games to MS-DOS, built on the SDL3 DOS backend
proven by [doskutsu](https://github.com/ecliptik/doskutsu) (Cave Story on
real 486/Pentium-class DOS hardware via SDL3, DJGPP, and CWSDPMI).

This repository does not contain the games themselves. Each port lives in
its own git repository and pulls in the reusable platform layer from
[`shared/`](shared/) via a git submodule. This repo tracks status, hosts
that shared layer, documents the porting strategy and hardware/QA process,
and showcases finished ports.

See [`CLAUDE.md`](CLAUDE.md) for the rules an AI agent (or contributor)
should follow when working in this repo, and
[`PORTING.md`](PORTING.md) for how to start a new port.

## Ports

| Priority | Project | Upstream | License | Port repo | Status | Difficulty |
|---:|---|---|---|---|---|---:|
| 0 | doskutsu (Cave Story) | [nxengine/nxengine-evo](https://github.com/nxengine/nxengine-evo) | GPL-3.0 (verified) | [ecliptik/doskutsu](https://github.com/ecliptik/doskutsu) | RELEASE_READY | reference |
| 1 | Adventure Game Studio | [adventuregamestudio/ags](https://github.com/adventuregamestudio/ags) | unverified | unclaimed | BACKLOG | 5 |
| 2 | VVVVVV | [TerryCavanagh/VVVVVV](https://github.com/TerryCavanagh/VVVVVV) | unverified — see caution below | unclaimed | BACKLOG | 3 |
| 3 | Meritous | TBD | unverified | unclaimed | BACKLOG | 2 |
| 4 | OpenJazz | [OSSGames/GAME-SDL-openjazz](https://github.com/OSSGames/GAME-SDL-openjazz) | unverified | unclaimed | BACKLOG | 2-3 |
| 5 | POWDER | TBD | unverified — may not be open source, see `ports.yaml` | unclaimed | BACKLOG | 2 |
| 6 | Kobo Deluxe | TBD | unverified | unclaimed | BACKLOG | 3-4 |
| 7 | Blob Wars: Metal Blob Solid | TBD | unverified | unclaimed | BACKLOG | 3-4 |
| 8 | Passage | TBD | unverified | unclaimed | BACKLOG | 1 |
| 9 | SuperTux 0.1.x | TBD | unverified | unclaimed | BACKLOG | 4 |

This table is generated from [`ports.yaml`](ports.yaml) — the machine-
readable source of truth, including `upstream_license_verified` per entry.
"Unverified" means exactly that: nobody has yet read the actual upstream
LICENSE/COPYING file for that project in this repo. **Never treat an
unverified license as known** — see [`docs/licensing.md`](docs/licensing.md)
and `ports.yaml`'s header comment. VVVVVV in particular has historically
been distributed under source-available terms that are not automatically
redistribution rights; see its `ports.yaml` entry before assuming anything.

See [`COMPATIBILITY.md`](COMPATIBILITY.md) for a richer boots/playable/
hardware-target matrix as ports progress, and [`gallery/`](gallery/) for
screenshots, performance notes, and features per finished port.

## Repository layout

```
sdl-dos-ports/
├── ports.yaml         # candidate backlog + status tracker (source of truth)
├── docs/              # architecture, video/audio/input/filesystem/timing, testing, licensing
├── shared/            # the reusable SDL3-DOS platform layer -- port repos consume this via submodule
├── templates/         # scaffolding for a new port repo (STATUS/PLAN/CLAUDE/benchmark/license-review templates)
├── gallery/            # per-port showcase: screenshots, performance, features
└── scripts/           # scripts/new-port.sh and friends
```

## Reference hardware

Primary targets, shared across all ports (see [`HARDWARE.md`](HARDWARE.md)
for the full matrix and real-hardware testing process via
[vcctrl](https://github.com/ecliptik/vcctrl)):

| ID | CPU | Role |
|---|---|---|
| HW-486-50 | 486DX2-50 | low-end torture test |
| HW-486-66 | 486DX2-66 | primary minimum target |
| HW-POD83 | Pentium OverDrive 83 | upgrade-path target |
| HW-5X86 | AMD Am5x86-133 | fast 486 platform |
| HW-P75 | Pentium 75 | recommended target |

## License

See [`LICENSE`](LICENSE) for this repository's own code and docs, and
[`THIRD-PARTY.md`](THIRD-PARTY.md) for the licensing model applied to
vendored/patched third-party sources in `shared/`. Every port repo tracks
its own game engine and asset licensing separately in its own
`LICENSE-REVIEW.md`.
