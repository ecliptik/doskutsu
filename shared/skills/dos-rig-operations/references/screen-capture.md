# Screen capture: what makes a capture actually evidence

For the literal capture mechanics (shot/burst/record/frame commands),
see vcctrl's own `vcctrl-rig-hazards` and the capture section of
`vcctrl-common-workflows`. What follows is why a capture can look fine
and still not prove what you think it proves.

## A black frame is not a black screen

The capture stick emits flat-black output while it's re-syncing (e.g.
after a mode change or a reboot). A frame full of black pixels is
ambiguous between "the screen is genuinely black" and "the capture
device hasn't locked onto signal yet" -- treat a black frame as
inconclusive, not as confirmation of a black-screen bug, until signal
lock is independently confirmed.

## Liveness needs a distinct-frame check, not a volume check

A frame counter or fps figure can keep climbing even after the actual
video *source* has died -- the capture pipeline can keep emitting frames
(possibly repeats of the last real one) independent of whether the guest
is still producing new video. Checking "frames are still arriving" tells
you the capture pipeline is alive, not that the guest is. Confirm frames
are actually *changing* (a distinct-frame check) before treating capture
volume as a liveness signal for the machine under test.

## A ring dump needs an explicit start-time boundary

A capture ring buffer that gets dumped without an explicit "since this
timestamp" boundary can silently include frames from a *previous* run --
producing a dump that looks like it's showing the current test but is
actually contaminated with stale footage from before the current cell
started. This is not hypothetical: it required an actual vcctrl fix
during a real validation session (`record` now refuses to run without an
explicit `--since` boundary). Always bound a ring-buffer dump to the
current run's actual start time; never dump "whatever's in the ring" and
assume it's all from now.

## What makes a capture useful evidence, not just a picture

A raw screenshot is weak evidence for a correctness claim on its own,
because inter-run animation/timing jitter means two runs of the identical
correct behavior can produce visually different raw frames -- a naive
pixel diff between two runs false-alarms on jitter that has nothing to do
with whether the thing under test is actually broken.

Prefer a **tick-locked or deterministic-replay-locked capture**: capture
at a specific, reproducible frame/tick count driven by a deterministic
replay (a TAS-style input reel, a fixed seed, a fixed frame-advance
count) rather than "whatever frame happened to be on screen when the
screenshot fired." A capture taken at the same deterministic tick across
runs is directly comparable; a freehand screenshot is not. If a port's
engine supports deterministic replay at all, wire capture timing to it
for anything meant to support a correctness claim -- reserve freehand
screenshots for casual liveness checks, not evidence.
