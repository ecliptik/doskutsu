# Phase 0: what a Mach64 frame is made of

Measured 2026-08-21 on the Gateway 2000, Pentium OverDrive 83, ATI Mach64,
640x480, binary `49344b16b9e9` (the Round R binary, unchanged). Three `diag`
cells, one instrument each, both halves of every guard asserted.

**These cells are `diag`. Their fps does not enter the perf matrix.** Round R's
27.6 fps is the perf figure.

---

## The headline

**The flip is 17.8% of the frame. The other 82% has never been decomposed.**

    whole frame          35.7 ms   (D1, 28.0 fps -- a diag cell)
      flip total          6.36 ms  17.8%
        VRAM write        4.77 ms  13.4%
        yield             1.41 ms
        everything else   0.15 ms
      NOT THE FLIP       29.3 ms   82.2%   <-- undecomposed

Even deleting the entire flip buys 6.36 ms against a 2.9 ms target. So the LFB
question is a fight over at most 4.77 ms while **29.3 ms sits unexamined.**

---

## D1 -- per-flip phase split (28 blocks of 100 flips)

    phase              med-of-med   mean-of-mean   worst max
    total                    6.36           7.71      158.36
      present                4.77           5.42      127.27
      housekeeping           0.05           0.06        4.09
      dirty                  0.04           0.04        4.98
      drain                  0.00           0.00
      program_palette        0.00           0.00
      get_generation         0.00           0.00

**Named components sum to 4.86 against a `total` of 6.36 — a residual of
1.50 ms, 23.6% of the flip.** Named rather than absorbed, per the
pre-registration. On means the residual is 2.19 ms, over half the 2.9 ms
target on its own.

## D2 -- inside-flip decomposition (28 blocks)

    fb_residual              4.75           5.24      244.84
    fb_yield                 1.41           1.21        8.83
    fb_diag_logs             0.01           0.64       11.62
    fb_audit_flushes         0.03           0.25       26.69
    fb_setup                 0.02           0.02
    fb_prev_frame_blit       0.02           0.02
    TOTAL                    6.24           7.38

## The cross-check, which is the strongest result here

    D1  present       4.77 ms med
    D2  fb_residual   4.75 ms med

**Two independent instruments, two separate cells, agreeing to 0.02 ms.**
D2's residual bucket IS D1's present phase: the VRAM write. Both
decompositions are therefore measuring what they claim.

It also **resolves most of D1's residual**: D2's `fb_yield` at 1.41 ms is
almost exactly D1's unexplained 1.50 ms, and D1 has no yield bucket. So the
gap is ~1.4 ms of yield plus ~0.1 ms genuinely unaccounted — not 1.5 ms of
mystery.

**A repeat was offered and declined**, correctly: a repeat gives one more
sample from one instrument, while cross-agreement between two instruments is a
stronger statement and much harder to produce by accident.

## D3 -- per-layer bytes (26 blocks, per scene draw)

    pass_clear_bytes             66,639 B   35.6%
    pass_backdrop_bytes          42,181 B   22.5%
    pass_bg_tile_scan_bytes      36,667 B   19.6%
    pass_fg_sprite_scan_bytes    41,738 B   22.3%
    TOTAL                       187,225 B
    avg passes per scene: 2.39

**The clear is the largest single layer.** A scene moves 187 KB against a
76,800-byte visible frame — **2.4x the payload before the flip moves it again**
through a 64 KB window via `dosmemput`, which copies through a DPMI transfer
buffer.

---

## What Phase 0 does NOT decide, and why

**The pre-registered band was in milliseconds and the decision is about an
LFB.** Whether an LFB helps depends on whether the write is *bandwidth-bound*
or *overhead-bound*, and a duration alone cannot separate those:

    single copy    76,800 B / 4.77 ms = 16.1 MB/s
    double copy   153,600 B / 4.77 ms = 32.2 MB/s

POD-83 system memcpy is ~17 MB/s. The single-copy reading lands almost exactly
on measured system bandwidth; the double-copy reading would need banked VRAM
writes at roughly twice system memcpy, implausible for this card. **That leans
toward "already bandwidth-bound, an LFB saves little" — and it is an argument
from a numerical coincidence, so it is not a call.**

**A pre-registered threshold on the wrong quantity is a well-formed instrument
pointed in the wrong direction**: three states, written down in advance,
procedurally correct, and unable to answer the question it gated.

**What decides it is a bandwidth probe, not a round** — write 76,800 bytes to
VRAM through the banked window and time it against a system-RAM memcpy of the
same size. Source-side work, not system work.

## Loose ends, real and out of scope

- **Long tails.** `present` max 127 ms, `fb_residual` max 244 ms. Something
  occasionally stalls a flip by two orders of magnitude. In the data, unchased.
- **`fb_diag_logs` mean 0.64 ms, p95 5.63** is the instrumentation billing
  itself, and it is not uniform across buckets — so proportions can skew even
  when the total is honest.
- **D1/D2 read 28.0/28.1 against Round R's 27.6.** Cross-session, band ~0.85,
  so 0.4-0.5 is not resolvable. Nothing to explain.

## Recommended next, on the evidence

**Phase 0b: decompose the other 82%** (`MODE_TICK_INSTR`, `engine-tick-stat`).
D3 already points into it — 187 KB per scene at 2.39 passes, clear alone at
35.6%. On this evidence it is worth more than Phase 1. **Operator's call.**

---

## The instrument that failed, and how

**D3 refused with `COULD NOT READ` on a cell that was fine.** The attestation
reader classified a single glyph into zero-like or one-like — correct for
`SET | FIND "NAME" | FIND /C "="`, which can only answer 0 or 1. Pointed at a
log file, where a marker appears any number of times, it met `count: 27` and
`2` was in neither class.

**A two-valued reader met a multi-valued question** — the same shape as an
outcome table that omits a third state, inside the instrument built to prevent
that shape. Fixed to parse the whole token with per-digit confusion classes,
still returning could-not-read for any unrecognised glyph rather than a
plausible wrong number (`de5dbfc`).

**It refused loudly rather than guessing, which is the design working even as
it failed.** A misread count would have attested a cell on a number nobody
checked.
