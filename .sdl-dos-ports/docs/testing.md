# Testing

Three environments, in increasing order of authority:

```
DOSBox-X  ->  86Box  ->  real hardware (vcctrl)
```

## DOSBox-X: the default automation gate

Minimum pipeline any port should have, adapted from doskutsu:

```
fetch -> patch -> compile -> link -> package -> DOSBox-X launch ->
verify expected log / screenshot / exit
```

`shared/tools/{dosbox-launch.sh,dosbox-run.sh,dosbox-teardown.sh}` and
`shared/tests/{smoketest,sdl3-smoke,sdl3-mixer-smoke,sdl3-image-smoke,
dpmi-lfn-smoke}/` give a new port a generic DJGPP/CWSDPMI/SDL3 bring-up
smoke test for free — run these before writing any game-specific test.

Recommended smoke checks for a port's own suite: executable starts, SDL
initializes, video mode opens, an input event reaches the engine, the audio
device initializes, an asset loads, the game reaches its title/first room,
a save file writes and reloads, clean exit. If the game has deterministic
input, a replay-based smoke test is worth the investment.

For the day-to-day mechanics and judgment calls of working in this tier
-- which of the three DOSBox-X tools to reach for, the emulator-only
escape hatches already baked into the shipped confs and why they must
never leak to a real-hardware run, what a clean DOSBox-X pass does and
doesn't prove, and what local timing observation can (and can't) be used
for -- see `shared/skills/dos-emulator-workflow/`. This is the default
mode for most of a port's life, not just a fallback when a rig isn't
available.

## 86Box: a second correctness opinion

doskutsu also validates against 86Box as an independently-implemented
emulator, catching bugs that happen to be masked by one specific emulator's
quirks. Not required for every commit, but worth running before declaring a
milestone (title screen, playable, etc.) reached.

## Real hardware: authoritative

DOSBox-X and 86Box are automation, not proof. Real hardware determines
actual performance and device compatibility — see `docs/hardware-testing.md`
for how that's currently done via vcctrl, and `HARDWARE.md` for the
reference machine matrix.

Passing DOSBox-X (or even 86Box) is not proof a build is actually correct
on real hardware — a clean smoke can pass while never exercising the
hardware I/O path a fix depends on, and a clean `strings` grep proves a
symbol was compiled in, not that it ran. See
`shared/skills/dos-realhw-verification/` for the failure shapes this has
actually taken (stale build caches, emulator-vs-hardware I/O divergence,
build-host tooling traps) and the discipline for debugging a bug that's
specific to real hardware.

## Never contribute upstream

If a probe or smoke test surfaces a bug in DOSBox-X, 86Box, or any other
external tool this harness depends on, that stays a workaround/note in our
own docs — never a PR, issue, or bug report against that project. See
`CLAUDE.md`'s "Never contribute upstream" section.

## Probe library

`shared/tests/probes/` holds standalone, DJGPP-only, no-SDL, no-engine
hardware-characterization tools (memory bandwidth, palette DAC, VESA/
Cirrus/S3 chip identification, WaveBlaster/MPU-401/GUS detection, PC
speaker). Reach for these when real-hardware behavior is unexplained by the
game/engine code — they isolate whether a bug is in your port or in how a
specific card responds.
