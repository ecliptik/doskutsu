# Third-party licensing

`sdl-dos-ports` follows the model doskutsu established: **open engine +
locally-authored patches, no redistributed proprietary assets.**

## What this repository contains

`shared/` vendors nothing directly — it carries a **local patch series**
against upstream sources that individual port repos also pin (currently the
SDL3 DOS backend, `libsdl-org/SDL`, zlib-licensed). A port repo applies
these patches to its own pinned checkout of the vendor source; this repo
never ships a full copy of SDL, SDL_mixer, or SDL_image.

| Component | License | Notes |
|---|---|---|
| This repo's own code/docs/scripts/templates | MIT (see `LICENSE`) | |
| `shared/patches/sdl3-dos/*` | zlib (inherits SDL's license) | patches against `libsdl-org/SDL`'s DOS backend |
| `shared/patches/sdl3-mixer/*` | zlib (inherits SDL_mixer's license) | |
| `shared/audio/midi_sched.{c,h}` and hardware-MIDI backends | MIT (original work, this repo) | no upstream dependency |
| `shared/tests/probes/*` | MIT (original work, this repo) | standalone DJGPP diagnostics, no upstream dependency |

## What a port repo must track separately

Every port repo is responsible for its own `LICENSE-REVIEW.md` (see
`templates/LICENSE-REVIEW.md`) covering:

- the game engine's own source license (e.g. GPLv3 for NXEngine-evo),
- any DOS-specific patches authored against that engine,
- game asset licensing and redistribution rights (music, fonts, sprites),
- whether the finished port binary can be distributed at all, and under
  what conditions (open engine + user-supplied original data is the
  fallback model whenever asset licensing is uncertain).

This repository, its `shared/` layer, and `gallery/` must never contain
commercial or copyrighted game data, proprietary fonts, or music requiring
separate permission — screenshots of a port's UI in `gallery/` are fine;
game data files are not.
