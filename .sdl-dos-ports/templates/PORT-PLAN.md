<!--
Template: a new port repo's PLAN.md. Fill in each milestone as it's
reached; don't pre-fill dates/results you don't have yet. See
sdl-dos-ports' PORTING.md for the recommended slice order.
-->

# <PORT NAME> — DOS port plan

## Research (do this before writing code)

- [ ] Upstream repo located and a DOS-appropriate revision chosen and pinned
      in `vendor/sources.manifest`
- [ ] License reviewed (`LICENSE-REVIEW.md` started)
- [ ] SDL/engine API surface inventoried (which calls, which subsystems)
- [ ] External dependencies inventoried (codecs, archive formats, fonts,
      networking, threading)
- [ ] Filesystem/platform assumptions inventoried (see
      `.sdl-dos-ports/docs/filesystem.md`)
- [ ] Minimum patch sequence to reach a compiling, linking DJGPP build
      proposed

## Milestones

Use the `dos_status` ladder from `templates/PORT-STATUS.md`. For each
milestone reached, note the commit/tag and, where relevant, a DOSBox-X
smoke-test result.

- [ ] BOOTSTRAP — cross-build environment set up, `shared/build/sdl3-dos.mk`
      included, project's own `vendor/sources.manifest` entry added
- [ ] COMPILES — engine compiles and links under DJGPP (SDL/audio/etc. may
      still be stubbed)
- [ ] STARTS — executable runs under DOSBox-X, SDL3 initializes, exits
      cleanly
- [ ] TITLE_SCREEN — native-resolution framebuffer opens, title/menu
      renders
- [ ] PLAYABLE — a representative slice is playable start-to-finish
- [ ] FULL_GAME — the complete game is playable
- [ ] OPTIMIZING — profiling-driven optimization pass (see
      `.sdl-dos-ports/docs/optimization.md`); real-hardware benchmarks
      recorded per `templates/BENCHMARK.md`
- [ ] RELEASE_READY — packaged without redistributed copyrighted assets,
      `LICENSE-REVIEW.md` complete, `gallery/<name>/` entry written in the
      hub repo

## Open questions / risks

<!-- e.g. audio decode cost on 486, licensing terms, engine features with
     no DOS equivalent -->
