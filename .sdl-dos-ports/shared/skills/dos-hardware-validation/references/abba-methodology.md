# ABBA methodology with pre-registered thresholds

A method for turning a real-hardware A/B performance claim into something
someone can act on, instead of "some numbers that happened."

## Why ABBA, not A-then-B

A straight "run A, then run B" comparison confounds the lever under test
with anything that drifts over time -- thermal state, a card warming up,
a background process, session-to-session rig variance. ABBA (A/B/B/A,
repeated as a block) cancels a monotonic drift across the block: if
performance is trending up or down for reasons unrelated to the lever,
that trend shows up symmetrically in both A's and both B's rather than
biasing the comparison toward whichever arm happened to run first.

## Steps, in order

1. **Pick the lever(s) and write the pre-registered thresholds** -- what
   delta magnitude counts as a real win, what counts as noise, what counts
   as a regression -- *before* running the first cell. If the thresholds
   get adjusted after seeing the data, the result is no longer a test, it
   is a story fit to the data.
2. **Write the invalidity conditions** -- see below. Also before running.
3. **Run the block**: A, B, B, A (one full cell each, full
   set/forbid/expect_log discipline per `cell-protocol.md`). Repeat the
   block (x2 or more) if the campaign's budget allows -- a single block
   is the minimum, not the target.
4. **Check invalidity conditions before looking at the delta.** If the
   block is invalid, don't compute a verdict from it -- re-run instead.
5. **Compute the verdict** from the pre-registered threshold table, not
   from eyeballing the raw numbers.

## Invalidity conditions (check these before trusting any delta)

- **A-to-A spread too wide.** The two A cells in a block should agree with
  each other within a stated band (a same-config, same-binary re-run
  should reproduce closely). If they don't, the rig was not sitting
  stable during this block -- something changed between the two A runs
  that had nothing to do with the lever, and the B cells sandwiched
  between them are not trustworthy either. Discard the block, don't
  average through the instability.
- **Baseline does not reproduce a known prior figure within a stated
  band.** If this port/config has a previously-measured baseline number,
  today's A cells should land near it. If they don't, something changed
  outside the lever under test (a different binary than intended, a rig
  config drift, a different card/CPU than expected) -- resolve that before
  trusting any comparison run on top of it.

Both checks exist because a comparison run on an unstable baseline can
produce a confident-looking delta that is actually measuring instability,
not the lever.

## Verdict table shape

Key the verdict to the delta magnitude against the pre-registered
thresholds, not to statistical significance alone (a real hardware sample
size is usually too small for significance testing to be the deciding
factor) -- e.g.:

| Delta vs. baseline | Verdict |
|---|---|
| Within the noise band (± threshold) | No measurable effect -- do not claim a win or a loss |
| Beyond the noise band, below the "large" threshold | Small effect -- note it, don't lead with it |
| Beyond the "large" threshold | Large effect -- actionable conclusion |
| Negative beyond the noise band | Regression -- do not ship without understanding why |

Set the actual threshold numbers per port/metric based on that port's own
measured noise band (see `docs/optimization.md` and `docs/timing.md` in
the sdl-dos-ports hub for why real-hardware FPS noise has been measured at
several times the within-session band -- a threshold copied from a
different port's noise characteristics is not safe to reuse blind).

## What a finished campaign write-up needs

- The full verdict table and which row the result landed in.
- The actual per-cell numbers (raw data, not just the computed delta --
  see the main skill's "hand off raw data" rule).
- Which invalidity checks were run and that they passed.
- Identity fields for every cell (see `cell-protocol.md`'s Collect
  section) -- a campaign result is only as trustworthy as the weakest
  cell's identity confirmation.
