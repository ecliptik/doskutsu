# Timing

Never assume display FPS and simulation/game timing must be identical.
doskutsu demonstrated why this matters: on slow real hardware, render rate
varies a lot (see `HARDWARE.md`'s baseline table), but game logic must
continue at its intended rate regardless.

## Determine, per port

```
simulation tick
animation tick
audio tick
render tick
input polling rate
```

Where possible:

```
simulation = fixed timestep
rendering  = as fast as practical
```

This prevents slow rendering from slowing gameplay — a candidate does not
need to hit 50/60 FPS to be considered playable if its simulation timing is
cleanly separated from its render rate.

## Add debug output for

```
simulation Hz
render FPS
frame time
audio underruns
dropped renders
```

This instrumentation should exist before any optimization work starts —
see `docs/optimization.md`: never optimize without numbers.
