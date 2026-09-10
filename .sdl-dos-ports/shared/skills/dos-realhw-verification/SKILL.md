---
name: dos-realhw-verification
description: Verifying that a DOS port build, fix, or diagnosis is actually correct on real hardware, not just apparently correct -- two-witness build verification, DOSBox-X/86Box tiering discipline, build-host tooling traps, and how to debug a bug that reproduces on real hardware but not (or differently) in an emulator. Use this whenever claiming a build is "ready," a fix is "confirmed," a bug is "emulator-only," or before shipping any change whose correctness claim rests on source review or emulator behavior rather than a real-hardware measurement.
---

# DOS real-hardware verification

The methodology for knowing a build or a fix is actually correct, as
opposed to merely appearing correct because the check that would have
caught the difference wasn't actually capable of catching it. Distilled
from a real multi-round doskutsu debugging campaign where a fix shipped
twice on hypotheses that later real-hardware evidence refuted outright --
see `shared/skills/README.md` for provenance.

## The one question that generalizes across this whole skill

**For any check you're about to trust: state what its failure would look
like. If that's the same observation as success, it isn't actually a
check.**

This single test catches most of the specific failure modes documented in
this skill's reference files, and it's worth applying explicitly, out
loud, before trusting any of the following as confirmation of anything:
a clean compile, a clean DOSBox-X smoke, a source trace that looks
compelling, a `strings` grep that finds the expected symbol embedded in
the binary. Each of these can be true on a run that is *wrong* in exactly
the way you're trying to rule out -- see the reference files for concrete
cases where each one was.

## Two-witness build verification

"The expected string is embedded in the binary" proves the code was
*compiled in*, not that it *ran*. Verify both: the binary's sha256
actually changed vs. the pre-build baseline, **and** a runtime log/banner
line that only appears when the new code path actually executes shows up
in an actual run's output. A stale cached build artifact surviving a
supposedly-clean rebuild is a real, recurring failure mode, not a
hypothetical -- see `references/stale-cache-and-smoke-gate.md` for the
documented failure shapes and the concrete case where a DOSBox-X-clean
probe froze real hardware on first run because the emulator's own
behavior silently routed around the one hardware I/O sequence under test.

## DOSBox-X / 86Box tiering, and build-host traps

86Box is a conditional second opinion, not a default gate -- reach for it
specifically when a chip-specific behavior DOSBox-X's generic VBE can't
exercise is in question, and treat a DOSBox-X-only "this is emulator-only,
won't reproduce on real hardware" diagnosis as needing an empirical
second-emulator co-witness before it's trusted for a ship decision. Two
build-host tooling traps (a dead X display reading exactly like a boot
wedge, a screenshot tool's auto-suffix defeating a naive "latest file"
glob) round this out. See
`references/emulator-tiering-and-build-host-traps.md`.

## Debugging a real-hardware-vs-emulator divergence

The hardest and highest-value case: a bug that only shows up on real
hardware (or shows up differently there than in any emulator). The core
discipline, and a worked example of a specific, transferable trap for any
hardware-conditional fast path, are in
`references/hw-emulator-divergence-debugging.md`.

## How this fits with the rest of this port's process

- `docs/testing.md` and `docs/hardware-testing.md` (this hub) already
  establish the DOSBox-X -> 86Box -> real-hardware authority ordering;
  this skill is the discipline for not fooling yourself at any of those
  three tiers, not a replacement for that ordering.
- `shared/agents/build-qa.md`'s charter already requires sha + strings +
  banner-emit verification before calling a build ready -- this skill is
  where the "why" and the failure-shape detail that charter's checklist
  compresses into one line actually lives.
- `dos-hardware-validation` and `dos-rig-operations` cover the real-
  hardware *campaign and rig-operation* mechanics once you're confident
  the build itself is correct; this skill is what earns that confidence
  in the first place.
