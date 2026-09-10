# Optimization

Always optimize based on profiling — instrument first (see `timing.md`),
optimize second. Do not prematurely rewrite major systems in assembly;
determine which subsystem is actually expensive before touching it.

## Likely DOS bottlenecks, roughly in order

1. Full-screen pixel copies.
2. Pixel-format conversion.
3. Alpha blending.
4. Software scaling.
5. Compressed audio decode.
6. Audio mixing.
7. Excessive heap allocations.
8. Floating point.
9. C++ container churn.
10. Filesystem calls.
11. Sprite/collision loops.
12. Cache-unfriendly object layouts.

## High-value strategies

- Native framebuffer resolution (see `video.md`).
- 8/16-bit rendering instead of 32-bit when practical.
- Dirty rectangles, especially for mostly-static-screen games.
- Preconverted graphics and preconverted audio.
- Hardware MIDI instead of software synthesis or decode (see `audio.md`).
- Static lookup tables, fixed-point math.
- Object pooling; avoid per-frame allocation.
- Reduce intermediate SDL surfaces; cache decoded assets.
- Compile-time removal of unused subsystems.

## Rule

Do not make a performance claim without a measurement, and prefer a
real-hardware measurement over an emulator one for anything you intend to
publish in `gallery/` or `COMPATIBILITY.md` — see `docs/hardware-testing.md`.
