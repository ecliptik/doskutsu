# Contributing

## Adding or claiming a port candidate

1. Check [`ports.yaml`](ports.yaml) for an unclaimed (`BACKLOG`) entry, or
   propose a new one via PR following the existing schema. Never guess an
   upstream URL — locate and pin the canonical upstream repository and
   revision first; if you can't yet, leave `upstream_url: null` and say so
   in `notes`.
2. Run `scripts/new-port.sh <name>` to scaffold that port's own repository,
   wired to this repo's `shared/` via a git submodule, using
   [`templates/`](templates/).
3. Open a PR here updating that candidate's `ports.yaml` entry
   (`dos_status: RESEARCH`, `port_repo_url` set).
4. Do the actual porting work in the new port's own repository, following
   [`PORTING.md`](PORTING.md) and the slice order it describes (compile →
   video → input → filesystem → audio → gameplay → optimization).

## Changing `shared/`

`shared/` is consumed by every port repo via a pinned git submodule commit,
so a change here doesn't take effect anywhere until a port bumps its pin.
Read the "Rules for changes to `shared/`" section in [`CLAUDE.md`](CLAUDE.md)
before proposing one — in short: nothing game-specific, DOS-specific
behavior stays explicit, patches (not direct source edits) against pinned
upstream, build and validate under DOSBox-X before calling it done.

## Commit and patch conventions

Small, narrow, buildable slices. See [`docs/patch-conventions.md`](docs/patch-conventions.md)
for the patch-series format used in `shared/patches/`.

## Never contribute upstream

No PRs, issues, or bug reports against any project we vendor from or patch
(SDL/SDL_mixer/SDL_image, any game engine, vcctrl) — see `CLAUDE.md`'s
"Never contribute upstream" section. All work stays here or in the
relevant port repository.

## Real-hardware results

If you're submitting a benchmark or QA result, use
[`templates/BENCHMARK.md`](templates/BENCHMARK.md)'s format so results stay
comparable across ports and across the hardware matrix in
[`HARDWARE.md`](HARDWARE.md). Real hardware (via
[vcctrl](https://github.com/ecliptik/vcctrl), see
[`docs/hardware-testing.md`](docs/hardware-testing.md)) is authoritative;
DOSBox-X/86Box results are automation gates, not a substitute — don't claim
a number you only measured in an emulator.
