# Local "benchmarking": what it can and can't mean

## The hard constraint

**Never quote a DOSBox-X (or 86Box) frame timing as a performance
result.** This isn't specific to this skill -- it's already a hard
constraint in `shared/agents/build-qa.md` ("Never quote a DOSBox-X frame
timing as a performance signal") and `docs/testing.md`'s tiering. It's
restated here because local iteration is exactly where it's easiest to
slip: you're staring at an fps counter in the emulator window all day
while developing, it moves when you change code, and it's tempting to
narrate that movement as if it meant something about real-world
performance. It doesn't, even under `dosbox-x.conf`'s fixed-cycles
"parity" config -- a fixed cycle count approximates *relative* CPU
throughput for one reference machine's integer performance under one
person's calibration; it does not reproduce real memory bus timing, real
video card fill rates, real DMA/IRQ latency, or any of the other things
that actually determine a DOS port's real-hardware frame rate.

## What local timing observation is actually useful for

- **A coarse, same-machine-config regression smell test.** If a change
  makes the emulator's own fps counter drop sharply under a *fixed*
  cycles config with nothing else changed, that's worth a raised eyebrow
  and a closer look at what the change actually did -- not because the
  number itself means anything, but because a large, sudden shift within
  the same measurement method across otherwise-identical conditions is
  more likely to reflect something real (an accidentally-quadratic loop,
  a busy-wait that used to be an interrupt handler) than emulator noise
  is likely to produce on its own.
- **Cycle-count-based reasoning about algorithmic cost**, if the DOSBox-X
  debugger/profiling facilities are used deliberately for that (counting
  instructions/cycles for a specific code path), which is a different and
  more rigorous thing than eyeballing the on-screen fps counter.

## How to phrase a local finding so it doesn't get mistaken for a result

If a local DOSBox-X observation prompts a real investigation or a
patch, say so honestly in whatever you report: "this looked
meaningfully slower in DOSBox-X under a fixed-cycles config after this
change, worth a real-hardware check" is a legitimate, useful sentence. "This
patch improves fps by N%" based on a DOSBox-X number is not -- that claim
requires `dos-hardware-validation`'s actual campaign discipline (ABBA,
pre-registered thresholds, real-hardware cells) before it's a claim
anyone should act on. If a milestone or PR description is about to state
a performance number, check where that number came from before it ships
-- a DOSBox-X-derived number dressed up as a result is exactly the kind
of thing `dos-realhw-verification`'s "state what a check's failure would
look like" test exists to catch: a DOSBox-X fps figure's "failure" (being
wrong about real-hardware performance) looks identical to its "success"
from inside the emulator, so it isn't actually evidence either way.

## When a real number is actually needed

Graduate to `dos-hardware-validation` once an actual performance claim
needs to be made to someone who's going to act on it -- a teammate
deciding whether to ship a change, a milestone report, a comparison
that's going in a commit message. That skill's ABBA methodology and
pre-registered-threshold discipline exist specifically to turn "some
numbers" into something defensible; a DOSBox-X observation is, at best,
the reason to go spend that rig time, never a substitute for it.
