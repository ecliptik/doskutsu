<!--
Template: a new port repo's README.md. Replace <NAME>/<GAME> placeholders.
-->

# <NAME>

<GAME> ported to MS-DOS via SDL3, DJGPP, and CWSDPMI, using the shared
platform layer from [sdl-dos-ports](https://github.com/ecliptik/sdl-dos-ports)
(hub repo — status tracker, `shared/` platform layer, porting docs).

- Upstream: <UPSTREAM URL>
- Status: see `STATUS.md` (and this port's entry in the hub's `ports.yaml`)
- Plan: see `PLAN.md`

## Building

This repo consumes `sdl-dos-ports`'s `shared/` layer as a git submodule at
`.sdl-dos-ports/`:

```sh
git submodule update --init --recursive
./scripts/bootstrap.sh   # fetch pinned sources, apply patches, build
```

See `.sdl-dos-ports/docs/` for the shared architecture/video/audio/input/
filesystem/timing/optimization/testing docs this port follows.

## Game data

This repository does not contain <GAME>'s game data. See `LICENSE-REVIEW.md`
for how to supply your own legally obtained copy.

## License

This repo's own code follows <LICENSE>. See `LICENSE-REVIEW.md` for the
full per-component breakdown (engine, DOS patches, SDL, assets).
