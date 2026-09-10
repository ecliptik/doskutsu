# Debugging a real-hardware-vs-emulator divergence

## The core rule: a compelling story is not confirmation

For a bug that's specific to real hardware (doesn't reproduce in
DOSBox-X, or reproduces differently there), a strong source-code trace
combined with "and it looks right in the emulator" is **not** mechanism
confirmation. Only a real-hardware marker that actually fires *at the
moment of the wedge/bug* confirms the mechanism. This sounds obvious
stated plainly; in practice it's easy to skip because a good source trace
plus a clean emulator run genuinely feels like enough evidence to act on.

The concrete cost of skipping this: a fix was shipped on a static
(source-trace-only) hypothesis -- a clean DOSBox-X smoke *and* a strong
source trace both behind it -- and turned out to be a complete no-op on
real hardware once real-hardware markers were actually collected: the
diagnostic marker instrumented to prove the suspected path was entered
read the same value (indicating "path not entered") on every single
sample collected, across the full run. The suspected mechanism was never
even reached; the fix addressed a plausible-sounding but wrong theory. A
second static hypothesis in the same campaign was caught and corrected
before it ever shipped, precisely because it got checked against this
same discipline first -- worth noting as the positive case: the discipline
isn't just damage control after the fact, it also works as a pre-ship
catch when applied before committing to a fix.

## The generalized check: what would this check's failure look like?

A companion discipline, generalized from nine separate incidents across
this port's campaign history: before trusting any check as confirmation
of anything, state explicitly what that check's *failure* would look
like. If the failure case and the success case would produce the *same
observation*, the check isn't actually discriminating between "the
mechanism is real" and "the mechanism isn't real" -- it's a check in name
only. This is the same underlying test as `SKILL.md`'s top-level rule;
it's restated here because divergence debugging is where it matters most,
since a compelling source trace is exactly the kind of thing that *feels*
discriminating without actually being it.

## Worked example: a fast path that was correct when written

A five-round debugging effort on a hardware-specific rendering bug
initially blamed the graphics card itself, before real-hardware surface
dumps proved the actual cause was in the port's own code: a "route
directly to the window surface" fast path was skipping a centering-offset
step that a sibling (slower) code path applied. The fast path's own code
comment justified skipping that step -- "the tile loop covers the full
screen, so the per-pixel result is identical" -- and that comment was
**true when it was written**. It became false much later, when an
unrelated change elsewhere gave a nonzero value to something the
comment's reasoning had implicitly assumed would always be zero, and
nothing flagged the fast path's now-stale justification because the
comment itself wasn't re-checked against the changed precondition.

The transferable lesson: any hardware-conditional (or otherwise
special-cased) fast path whose correctness argument depends on a
precondition holding elsewhere in the codebase is a standing risk, not a
one-time-verified fact -- a comment that was true when written carries no
guarantee about remaining true after later changes touch the precondition
it depends on. When a hardware-specific bug surfaces near a fast path
like this, re-verify the fast path's stated justification against the
*current* code before assuming the fast path is innocent because "it was
checked before."

## Practical sequence for a divergence bug

1. State the hypothesis and, explicitly, what a real-hardware marker
   firing *at the moment of the bug* would look like if the hypothesis is
   correct -- and what it would look like if the hypothesis is wrong.
   If you can't describe a marker that would distinguish the two, the
   hypothesis isn't testable yet; find one that is before writing a fix.
2. Instrument for that marker and collect it on real hardware, not just
   in an emulator (see `dos-hardware-validation` for cell/RUNMANIFEST
   discipline for collecting this cleanly).
3. Only treat the mechanism as confirmed once the marker actually fires
   consistent with the hypothesis on real hardware -- a clean emulator
   run plus a compelling source trace is not a substitute for this step,
   no matter how strong the trace looks.
4. If a fast path or special-cased branch is anywhere near the bug,
   re-verify its own correctness justification against the current code
   before assuming it's innocent, per the worked example above.
