# patches/SDL/

Local-only patches against `vendor/SDL/` (the libsdl-org/SDL snapshot pinned in `vendor/sources.manifest`).

**These patches are never upstreamed.** Per project policy, doskutsu does not submit patches or open issues against any vendored upstream. By committing to never upstream, we sidestep `vendor/SDL/CLAUDE.md`'s "no AI-generated code for contributions" rule entirely — that rule scopes to *contributions* (PRs / issues), and we do not make any. The trade is real: every upstream sync (rebasing this patch series against a new pinned SHA) is on us. Freedom-to-patch beats upstream alignment for this project's purposes.

## Patches

- `0001-sb16-dsp-detection-fix.patch` — DOSBox-X SB16 emulation returns garbage for the DSP-reset detection sequence; this patch makes the audio backend tolerant of that path. Required for SB16 audio to init under DOSBox-X.
- `0002-debug-dosvesa-framebuffer-trace.patch` — diagnostic instrumentation in `DOSVESA_UpdateWindowFramebuffer`. Helped localize the Phase 7 framebuffer-paint bug; kept through Phase 8 so real-HW divergences from DOSBox-X behaviour can be diagnosed without a re-patch cycle. Strip if/when the SDL3 DOS backend stabilizes.
