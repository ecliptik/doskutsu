# Architecture

The stack every port targets, top to bottom:

```
game data
      |
game engine (per-port, e.g. NXEngine-evo for doskutsu)
      |
SDL3 / SDL3_mixer / SDL3_image
      |
SDL3 DOS backend  <- shared/patches/sdl3-dos/
      |
DJGPP + CWSDPMI
      |
MS-DOS 6.22
```

`shared/` in this repo owns everything from the SDL3 DOS backend down, plus
the build/test/QA tooling that operates at that layer. A port repo owns
everything from the game engine up, plus the DOS-specific patches that are
genuinely specific to that engine (renderer flip paths, format decoders,
scripting VMs — not video-mode or audio-hardware plumbing).

## What we own vs. what we inherit

| Layer | Source | Patched here? |
|---|---|---|
| Game data | port repo, user-supplied, never committed | no |
| Game engine | port repo, vendored + pinned SHA | port repo's own `patches/<engine>/` |
| SDL3 / SDL3_mixer / SDL3_image | upstream `libsdl-org`, pinned SHA | `shared/patches/sdl3-dos/`, `shared/patches/sdl3-mixer/` |
| DJGPP / CWSDPMI | toolchain, not vendored | not patched |
| MS-DOS | target OS | not patched |

## Seams that matter

Each seam is a place ports have historically hit friction — see the
per-topic docs for detail:

- **Video** (`video.md`): resolution/color-depth assumptions, scaling,
  framebuffer presentation path.
- **Audio** (`audio.md`): PCM vs. MIDI, hardware backend selection, decode
  cost vs. render cost trade-offs.
- **Input** (`input.md`): keyboard/mouse/gameport joystick, no modern
  controller abstraction needed.
- **Filesystem** (`filesystem.md`): 8.3 names, drive letters, no `$HOME`/
  `%APPDATA%`/XDG.
- **Timing** (`timing.md`): decoupling simulation rate from render rate.
- **Optimization** (`optimization.md`): what's actually expensive on
  486/Pentium-class hardware, and in what order to chase it.
- **Testing** (`testing.md`): DOSBox-X/86Box automation vs. real hardware.

Do not design a new port's architecture from scratch — start from this
stack and this seam list, and only diverge where the specific game's engine
genuinely requires it.
