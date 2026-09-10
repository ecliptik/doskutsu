# Cell protocol: set / forbid / expect_log

A "cell" is one arm of a comparison: one binary, one set of lever values,
one run, one collection. This is the discipline for making a single cell's
result trustworthy before it ever gets compared to another cell.

## Why three witnesses, not one

Each witness catches a different failure mode; none of them catches all
three:

- **`set_vars` alone** proves what you *asked* the run to do, not what it
  *did*. A typo'd var name, a var the engine doesn't actually read, or a
  var read but silently clamped all pass this check while producing a
  wrong result.
- **`forbid` alone** proves nothing was asked to leak, but says nothing
  about whether the lever under test actually took effect.
- **`expect_log` alone** (checking only that *something* looks different)
  can pass on a coincidence -- a run that looks different for an unrelated
  reason (e.g. a stale save file from a previous session) reads as
  "the lever worked" if you're not also confirming *nothing else* moved.

Together: `set_vars` says what you asked for, `forbid` says nothing else
was asked for (accidentally or via leftovers), `expect_log` says the
engine confirms it actually happened.

## `set_vars`

Set explicitly in **every** arm, including the "default" arm. Rationale:
a default is a value the code falls back to when the var is absent --
which is not necessarily the same as "the var, explicitly set to what
today's default happens to be." Setting it explicitly in both arms removes
one variable (whether the fallback path and the explicit-set path are
truly equivalent) from what you're trying to measure.

## `forbid`

For every var that is *not* the lever under test but that the engine or
harness reads as a diagnostic/behavior toggle:

1. Confirm it is absent from this run's environment -- a positive check
   that reads the value and asserts it is unset, not an assumption based
   on "I didn't set it."
2. If it's present, that's contamination from a prior cell (or a prior
   session) and this cell is invalid -- clear it and re-run, don't try to
   reason about whether it "probably didn't matter."

Build the forbid list from the full set of vars the engine/harness
reads (grep the engine and SDL patch series for env-var reads), not just
the ones you remember setting recently -- a var you set three cells ago
and forgot about is exactly the kind of thing this check exists to catch.

## `expect_log`

Define, before running, what the engine's own output must show for this
arm to count as "the lever took effect" -- a boot-banner field or a
specific init-time log line. Check this *after* every cell, not just
spot-checked occasionally. A cell whose `expect_log` check fails is
invalid regardless of what the rest of the run looked like -- don't fold
its numbers into the comparison "just this once."

**Verify the literal expected string against a real captured log before
trusting the check at all, not just the field/timing it targets.** A
correctly-designed check (right timing, right field, not gated on a
late-emitting block) can still be wrong for a much simpler reason: the
exact string hand-written into the check spec doesn't match what the
engine actually emits. This has caused a real false refusal in practice
-- a cell that ran completely correctly was rejected because the
`expect_log` string in the design doc didn't match the real log line,
and the mismatch wasn't caught until someone diffed the check against an
actual captured log. Before a campaign's first cell, confirm every
`expect_log`/`set_vars` string against a real log from a real run of the
engine -- don't trust that a string looks plausible just because it was
written thoughtfully.

**Never gate `expect_log` on a RUNMANIFEST field (or any other
exit-path-only summary block).** RUNMANIFEST is for post-collect metrics
extraction (see `runmanifest-log.md`), not live cell validity. The
concrete failure mode: an engine that emits its manifest late (e.g. after
`SDL_Quit()`) can suppress the block on a run that behaved completely
correctly, if the engine has any known or unknown quit-path timing issue
-- that's exactly what happened validating doskutsu (a documented
intermittent quit-hang suppressed the manifest on an otherwise-good run).
Gating `expect_log` on the manifest would have discarded a good cell as
invalid for a reason unrelated to the lever under test. The general rule:
anything used for live pass/fail gating must be provably unaffected by
exit-path timing, which a summary block written at the very end usually
is not.

If a manifest is missing (or a field in it doesn't match) after collect,
treat that as **inconclusive, not automatically invalid** -- re-run the
cell once before deciding whether it's a real problem or a benign
suppression. This is a judgment call informed by whether the engine has a
known emit-suppression bug, which is exactly the kind of thing to ask the
port's own maintainer/agent about before designing the campaign (see the
main skill's "design first, verify against source" step).

## Populate mechanics (staging a binary before a cell)

1. **Stage** the local binary (identify it unambiguously -- a build
   directory can contain more than one candidate binary if a previous
   build wasn't cleaned).
2. **Send** with an explicit destination directory -- do not rely on a
   transfer capability's default inbox being the game's live install
   directory; look up or ask for that path explicitly per port (see
   `templates/vcctrl-profile.yaml.template`'s DOS-working-directory field
   in the sdl-dos-ports hub).
3. **Verify** with a sha256 round trip: hash the local file, hash what
   landed on the rig, compare. A size match alone is not sufficient --
   two different builds can coincidentally match in size.

## Collect mechanics (after a cell)

1. Fetch the log(s)/result file(s) off the rig.
2. Size-verify the fetched file against the rig's own directory listing
   of that file -- not "the fetch command returned success," which proves
   the transfer mechanism worked, not that the bytes are complete.
3. Record identity for this specific cell (see the main SKILL.md's
   "Collect" section) -- every cell, not once per session. Hardware state
   can change between cells (a card reporting stale identity after a
   previous cell's power cycle, for instance), so identity from cell 1
   does not vouch for cell 8.
