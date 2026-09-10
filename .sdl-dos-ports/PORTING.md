# Starting a new port

This describes the practical steps to take a candidate from `ports.yaml`
through to a working DOS port, reusing everything already proven by
doskutsu and staged in `shared/`.

## 1. Pick a candidate

Look at [`ports.yaml`](ports.yaml) for a `BACKLOG` entry. Lower `priority`
numbers are recommended next steps, but difficulty and your own interest
matter too — `difficulty: 1-2` candidates (Passage, Meritous, POWDER) exist
specifically to prove the pattern works with minimal engineering before
attempting something like Adventure Game Studio.

If the entry's `upstream_url` is `null`, your first task is locating the
canonical upstream repository and picking a DOS-appropriate revision —
never guess. Record the URL and pinned SHA in `ports.yaml` before writing
any code.

## 2. Scaffold the port repo

```sh
scripts/new-port.sh <name>
```

This creates a new sibling repository (or a local directory ready to push,
if you haven't set up a remote for it yet), pre-populated from
[`templates/`](templates/), with `sdl-dos-ports` added as a git submodule at
`.sdl-dos-ports/` so the new repo can immediately reference
`.sdl-dos-ports/shared/...` from its build.

Update this repo's `ports.yaml`: set `dos_status: RESEARCH` and
`port_repo_url` for the candidate you claimed.

## 3. Research before touching code

Follow the model in `plans/SDL_DOS_PORTING_PROGRAM.md` (local, gitignored —
the original program specification this repo implements): inventory the
SDL/engine surface, dependencies, filesystem assumptions, and license
terms before writing a line of DOS-specific code. For an engine as large as
AGS this is its own milestone (RESEARCH.md, SDL-SURFACE.md,
DEPENDENCIES.md, PLATFORM-SURFACE.md, LICENSE-REVIEW.md); for something as
small as Passage it's a page.

## 4. Port in narrow, buildable slices

```
compile -> video -> input -> filesystem -> audio -> gameplay -> optimization
```

Build and validate under DOSBox-X after every meaningful slice — a host
build is not a DOS build. Don't accumulate speculative compatibility edits
across multiple subsystems before compiling.

Reuse before rewriting:
- SDL3-DOS platform behavior (VESA/Cirrus/S3, SB16/OPL2/OPL3/GUS/WaveBlaster
  audio, gameport joystick, DPMI timing): `.sdl-dos-ports/shared/patches/sdl3-dos/`.
- MIDI playback: `.sdl-dos-ports/shared/audio/midi_sched.{c,h}` plus the
  hardware backend of your choice.
- Cross-build stages for SDL3/SDL3_mixer/SDL3_image:
  `.sdl-dos-ports/shared/build/sdl3-dos.mk` (your Makefile defines its own
  final `game` stage and includes this fragment for the rest).
- DOSBox-X automation and bring-up smoke tests:
  `.sdl-dos-ports/shared/tools/` and `.sdl-dos-ports/shared/tests/`.
- Hardware diagnostics (VESA/DAC/sound-card probes) if you hit unexplained
  real-hardware behavior: `.sdl-dos-ports/shared/tests/probes/`.
- AI agent team charters: `.sdl-dos-ports/shared/agents/` — copy
  `templates/PORT-CLAUDE.md` as your repo's `CLAUDE.md`, and adapt
  `shared/agents/ENGINE-SPECIALIST.md.template` into your own engine
  specialist charter (this is what `nx-engine.md` is for doskutsu).

## 5. Real-hardware QA

Once you reach `PLAYABLE`, follow [`docs/hardware-testing.md`](docs/hardware-testing.md)
to validate on real hardware via vcctrl, using
[`templates/vcctrl-profile.yaml.template`](templates/vcctrl-profile.yaml.template)
as your project's profile. Record results with
[`templates/BENCHMARK.md`](templates/BENCHMARK.md).

## 6. Showcase it

Once the port is playable, add `gallery/<name>/` (screenshots, a short
README covering features/performance/hardware notes) so the next porter can
see what's possible.
