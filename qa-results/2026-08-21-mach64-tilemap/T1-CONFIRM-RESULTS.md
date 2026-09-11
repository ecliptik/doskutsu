# T1 confirming round — results

**Twelve cells, one binary, one daemon, no restarts.** Run 2026-08-24, Mach64 at
640x480, binary byte-verified at 7,816,241 bytes before the first cell.

## What was asked

T1 measured two levers singly and their sum was to be tested for additivity:

    TILE_DWORD_COPY  +1.00      ASM_BLIT  +0.50      sum  +1.50

Three ABBA-counterbalanced blocks: `X` (both levers), `W'` (dword alone),
`S'` (asm alone). Bands fixed before any cell ran.

## Result, on the pre-registered metric

    X   both levers        A=27.60  B=28.75   +1.15
    W'  TILE_DWORD_COPY    A=27.60  B=28.65   +1.05   T1 said +1.00   CONFIRMED
    S'  ASM_BLIT           A=27.65  B=28.00   +0.35   T1 said +0.50   NOT CONFIRMED

    six stock control cells: 27.6 27.6 27.6 27.6 27.6 27.7
    A-to-A spread 0.10, gate 0.6 -- passes

**Additivity: SUB-ADDITIVE.** The gain from both is `+1.15`, not the `+1.50` the
singles sum to.

## The result that matters more: the band was never capable

**Stated in the raw counted unit, which is where it becomes legible:**

    T1  ASM_BLIT   +50.0 flips = +0.486 fps
    S'  ASM_BLIT   +40.0 flips = +0.389 fps      the two rounds differ by 10.0

    the pre-registered band   0.10 fps  =  10.3 flips
    same-config pair spread from the archive:  0 1 2 5 12 15 16 flips

**The acceptance band is narrower than the system's demonstrated repeatability.** A
test required to resolve 10.3 flips, on a machine whose identically-configured
pairs differ by up to 16, cannot reliably confirm anything.

**`ASM_BLIT` did not fail to reproduce. The test could not have confirmed it.**

The band traces to a measurement artifact: `per_loop_fps` is computed as
`_flips * 500 / _reel_ticks` in **integer** arithmetic (`main.cpp:1530`), so it
truncates to 0.1 fps before it is ever formatted. Four cells reading identically
became a "±0.1 repeatability band", and that became the acceptance criterion.
**The artifact was load-bearing in the round's design before anyone examined
it.** See `HARNESS-STANDARD.md` 10.0b and 10.0e, and `lab/FINDINGS.md` §40.

**This is not hindsight.** The repeatability characterisation was completed and
sent at 17:07, before `CS4A` had finished. The base rate predates the result it
condemns.

### A rescue was available and was refused

Exact values are recoverable from `fps_true_flips` and `reel_ticks`, which every
manifest already carries. T1's true figure was `+0.486`, not the rounded `+0.50`,
and this round's is `+0.389` — a gap of `0.096` against a band of `0.10`, landing
inside by four thousandths.

**That compares against a threshold the round never committed to.** The verdict
stands as pre-registered. *The test was not capable* and *the lever failed* are
different statements, and reporting only the second would have been the more
flattering half.

### `W'` passed for the wrong reason

`+1.05` against `+1.00` is a gap of 5 flips, inside a band that cannot
discriminate at 10. **It passed a test that could not have failed it.** A
CONFIRMED from an incapable test is worse than a NOT-CONFIRMED, because nobody
interrogates it.

## What ships, and why

    TILE_DWORD_COPY      +112 flips   7.0x the worst-case pair spread   SHIPS
    ASM_BLIT              +40 flips   2.5x                              real
    ASM_BLIT on top       +9 flips    0.56x                             within noise

**`TILE_DWORD_COPY` ships on the size of its effect, not on the stamp** — seven
times the noise floor is a property of the lever, and it is what the CONFIRMED
was standing in front of.

**`ASM_BLIT` is real** — both rounds put it well above zero — **but its magnitude
is unpinned between +40 and +50 flips**, and the two rounds' disagreement is
exactly the size of ordinary pair variation.

**Paired, it adds at most a fifth of what it does alone, and the data cannot
exclude complete redundancy.** Ship `TILE_DWORD_COPY`; `ASM_BLIT` adds little or
nothing on top of it.

## Three candidate findings raised and refused

All three were built on the same four unusually quiet stock cells, and each was
refuted by one pass over runs already on disk. See `HARNESS-STANDARD.md` 10.0d.

1. **"Levers add variance."** Stock arms spread 0-1 flips, lever arms 11-12.
   Archive: stock pairs spread 0 to 16, lever pairs 4 to 13 — **the treated arms
   sit inside the control distribution.**
2. **"The lever couples render cost to frame timing, so perturbations compound."**
   Per-stage cumulative divergence wanders `+3, -10, -5, -12` — a random walk,
   not a compounding one.
3. **"Stock lands near zero by cancellation."** Stock endpoints reach ±16. The
   two pairs it was built on were the tightest two of seven.

**The refusals and the band finding are worth more than the two confirmations.**

## Provenance caveat

**Do not use `started_rtc_local` for ordering or sitting membership** — it is
corrupt. See `lab/OPEN-FAULTS.md` §10. The in-log `[HH:MM:SS]` prefixes are the good
clock, and the archive comparison above was only buildable after abandoning the
manifest field.
