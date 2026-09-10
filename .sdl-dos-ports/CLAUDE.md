# sdl-dos-ports

This is the **hub repository** for porting SDL-based games to MS-DOS using
the SDL3 DOS backend proven by doskutsu (Cave Story on DOS). It is not a
monolithic source tree — it tracks port status, hosts the reusable DOS
platform layer, and documents the shared porting strategy. Each port
(doskutsu, and future ports) lives in its **own separate git repository**
and consumes this repo's `shared/` directory via a git submodule.

Read `plans/SDL_DOS_PORTING_PROGRAM.md` for the full original porting
program rationale before making structural changes here. `plans/` is
gitignored — it holds planning inputs, not published documentation.

## What lives here vs. what doesn't

- Here: `shared/` (SDL3-DOS platform patches, MIDI/audio backends, build
  fragments, DOSBox-X/probe/smoke-test tooling, AI agent charter templates,
  Claude Code skill templates),
  `docs/` (architecture and process documentation), `templates/` (scaffolds
  for a new port repo), `gallery/` (screenshots/perf/features showcase per
  port), `ports.yaml` (the candidate backlog + status tracker).
- Not here: actual game source, vendored game engines, game assets, or a
  specific port's build output. Those belong in that port's own repo.

## Starting a new port

1. Check `ports.yaml` for an unclaimed (`BACKLOG`) candidate, or add a new
   one following the existing schema — never guess an upstream URL; locate
   and pin the canonical upstream repo and revision first.
2. Run `scripts/new-port.sh <name>` to scaffold the new port's own repo from
   `templates/`, wired to this repo as a `shared/` submodule.
3. Flip that candidate's `dos_status` in `ports.yaml` to `RESEARCH` and set
   `port_repo_url` once the new repo exists, in a commit here.
4. Follow the standard slice order for a new port: compile → video → input
   → filesystem → audio → gameplay → optimization. Do not attempt a giant
   first patch. Nearly all of this happens locally against DOSBox-X, no
   rig or real hardware required yet — see
   `shared/skills/dos-emulator-workflow/`.

## Rules for changes to `shared/`

`shared/` is consumed by every port repo via submodule pin, so changes here
ripple everywhere once a port bumps its pin. Because of that:

1. **Nothing game-specific.** If a change only makes sense for one game's
   engine, it belongs in that port's own repo, not here.
2. **DOS-specific behavior stays explicit.** Guard with `#ifdef __DJGPP__`
   or an equivalent project-wide DOS feature macro — never hide DOS
   assumptions in a generic code path.
3. **Preserve upstream source.** Vendor pinned by SHA + a local patch
   series (`shared/patches/<vendor>/NNNN-*.patch`, `git format-patch`
   origin, one concern per patch, "why not just what" commit messages) —
   never unexplained direct source divergence. See `docs/patch-conventions.md`.
4. **Hint/log naming is neutral, not doskutsu's.** Use a project-agnostic
   prefix (documented in `docs/patch-conventions.md`), not
   `SDL_HINT_DOSKUTSU_*` or similar single-port naming.
5. **Build after every meaningful change**, and validate under DOSBox-X
   before considering a change to `shared/` done. A host build is not a DOS
   build.
6. **No new dependencies casually.** Every library added to the shared
   layer becomes a portability project for every port that pulls it in.
7. **DJGPP hard constraints apply to everything here**: binary `fopen`
   mode, 32-bit `size_t` assumptions, `stubedit` stack sizing, no SIMD
   unless `NOSIMD_FLAGS`-gated, no shared libraries, ASCII-only source
   files (no smart quotes/em-dashes/non-ASCII).

## Never contribute upstream

We never send anything back to the third-party projects we patch or vendor
from — no pull requests, no issues, no bug reports, no mailing-list posts —
against SDL, SDL_mixer, SDL_image, any game engine, DOSBox-X, 86Box, or any
other external upstream, anywhere in this hub or in any port repo. All
patch and research work stays in this repository or the relevant port
repository. This applies to every patch in `shared/patches/`, every port's
own `patches/<engine>/`, and any testing/QA work that turns up a bug in an
emulator or other external tool used along the way.
`shared/patches/sdl3-dos/README.md` documents the rationale this policy
inherits from doskutsu: freedom to patch without waiting on upstream review
is worth more here than upstream alignment, and the trade is that every
upstream sync is on us.

(This does not apply to our own tools, like vcctrl — that's ecliptik's own
project and can be modified directly; "upstream" here means projects we
don't control.)

## Licensing boundaries

Never guess a license. `upstream_license` in `ports.yaml` stays `null` and
`upstream_license_verified: false` until someone has actually read that
project's own LICENSE/COPYING file in the pinned revision — a remembered or
commonly-believed license is not verification. See `docs/licensing.md`.

Never commit commercial/copyrighted game data, proprietary fonts, or music
requiring separate permission — this applies to `shared/`, `gallery/`
(screenshots of a port are fine; game data files are not), and any port
repo. Follow the open-engine + user-supplied-data model doskutsu
established. Every port repo needs its own `LICENSE-REVIEW.md` before
release packaging automation is built for it.

## Real hardware and QA

Real hardware (via `vcctrl`, see `docs/hardware-testing.md`) is the
authoritative result; DOSBox-X and 86Box are automation/regression gates,
not a substitute. Do not make performance claims without measurements —
add instrumentation before optimizing, and record results using
`templates/BENCHMARK.md`'s format so results are comparable across ports
and across the hardware matrix in `HARDWARE.md`.

## Agent rules

- Never guess about upstream code — inspect the pinned revision before
  proposing a patch.
- Keep changes in narrow, buildable slices; commit after each one.
- When working inside `shared/`, ask "would every port want this?" before
  adding it — if the answer is "just this one," it belongs in that port's
  own repo instead.
