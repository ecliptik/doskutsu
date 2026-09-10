---
name: dos-emulator-workflow
description: >
  Developing, testing, and coarsely benchmarking a DOS port locally with
  DOSBox-X (and, secondarily, 86Box) before real hardware or a vcctrl rig
  is involved at all -- this is the default mode for most of a port's
  life, not a fallback. Use this whenever compiling, patching, smoke-
  testing, debugging, or iterating on a DOS port on a local dev machine;
  whenever launching or scripting DOSBox-X; whenever deciding if a bug or
  a performance question can be answered locally or needs real hardware;
  or whenever tempted to quote a DOSBox-X number as a performance result.
  Covers shared/tools/dosbox-launch.sh, dosbox-run.sh, and
  dosbox-teardown.sh mechanics, and the emulator/hardware boundary.
---

# DOS emulator workflow (local development, DOSBox-X-first)

Most of a port's actual work -- compile, patch, boot, video mode, input,
filesystem, most of gameplay logic -- happens entirely on a local dev
machine against DOSBox-X, with no rig, no vcctrl, and no real hardware
involved. `dos-hardware-validation`, `dos-rig-operations`, and
`dos-realhw-verification` all assume you've reached the point where a
real-hardware measurement or confirmation is actually required. This
skill is what comes *before* that point, and for a fresh port it's most
of the work -- treat it as the default, not a fallback used only when the
rig is unavailable.

This hub's own porting order (`CLAUDE.md`) is compile -> video -> input ->
filesystem -> audio -> gameplay -> optimization. Every stage up through
gameplay correctness can be driven almost entirely by this skill; audio
device-specific behavior and anything in the optimization stage is where
the emulator/hardware boundary below starts to matter.

## The three tools

- **`shared/tools/dosbox-launch.sh`** -- visible, long-running DOSBox-X
  for manual play-testing, screenshot capture, and `xdotool` automation.
- **`shared/tools/dosbox-run.sh`** -- headless, stages one exe, captures
  stdout, exits. The workhorse for scripted smoke checks.
- **`shared/tools/dosbox-teardown.sh`** -- conf-scoped kill helper.
  **Never `pkill -x dosbox-x` globally** -- see `references/tooling.md`
  for why that's a real, repeatedly-hit hazard, not a theoretical one.

Full mechanics, flags, and env-var hooks (`LAUNCH_LOG_VERBOSE_VAR`,
`LAUNCH_EXTRA_SET`, `DOSBOX_CONF`, the three `.conf` variants) are in
`references/tooling.md`, which also covers driving DOSBox-X with
`xdotool`/`scrot` (focus-once-no-`--window`, window-ID caching across a
video mode switch, `scrot` overwrite/fallback traps) and a `cycles=max`
gotcha where audio pre-buffering can make a working runtime change look
unresponsive.

## The boundary: what DOSBox-X can and cannot prove

A DOSBox-X pass is real signal for anything the emulator actually
implements faithfully -- but it is not a stand-in for real hardware on
device-specific timing, exact chip behavior, or DPMI/LFN passthrough the
emulator can't represent. Getting this boundary wrong in either direction
costs real time: treating an emulator-only escape hatch as if it were
real behavior, or re-deriving real-hardware confirmation for something
DOSBox-X already answers definitively. See
`references/emulator-vs-hardware-boundary.md` for the concrete escape
hatches this hub's own tooling already carries (and why), the
"necessary but not sufficient" pattern for probes that can only partially
answer a hardware question locally, and the explicit list of what should
send you to `dos-hardware-validation`/`dos-realhw-verification` instead
of staying here.

## Never quote a DOSBox-X number as a performance result

This is a hard constraint already stated in `shared/agents/build-qa.md`
and `docs/testing.md`, restated here because it's exactly the mistake
this skill's own workflow makes easy to slip into: DOSBox-X's frame
timing is not real-hardware performance, even under a fixed-cycles
config. What local emulator timing *is* useful for, and how to phrase a
local finding so it doesn't get mistaken for a shipped benchmark claim,
is in `references/local-benchmarking.md`.

## How this fits with the rest of the hub

- `docs/testing.md` sets the DOSBox-X -> 86Box -> real hardware authority
  ordering; this skill is the day-to-day mechanics and judgment calls
  for staying productively inside the first tier as long as that's the
  right call.
- `shared/agents/build-qa.md`'s charter already runs a DOSBox-X smoke as
  part of its checklist -- this skill is where the deeper mechanics and
  gotchas that checklist compresses into a couple of lines actually live.
- `dos-realhw-verification` covers the epistemics of *knowing* a build is
  correct, including why a clean DOSBox-X smoke isn't proof of hardware
  I/O correctness -- read that skill once a question in
  `emulator-vs-hardware-boundary.md`'s graduation list comes up.
