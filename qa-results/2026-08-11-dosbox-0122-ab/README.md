# SDL/0122 pump-aware timebase -- DOSBox-X A/B (2026-08-11)

**Result: PASS.** The acceptance signature reproduced and then disappeared.

Two adlib cells, same binary, same reel, `SDL_HINT_DOSKUTSU_PUMP_TIMEBASE`
the only variable. DOSBox-X config `sbtype=none` + `oplmode=opl2`, so no SB
device exists and the PIT/IRQ-0 music pump actually starts (it refuses to
start when an SB is hot).

| cell | timebase | <10 ms | **10-40 ms** | 40-70 ms | >=70 ms |
|---|---|---|---|---|---|
| `P22B` | `=0`, pre-0122 behaviour | 52 | **0** | 183 | 1 |
| `P22A` | default ON (0122 active) | 0 | **33** | 1 | 0 |
| `P22C` | default ON, longer clean run | 0 | **261** | 2 | 1 |

The 10-40 ms band is where this machine's real frame times live. With the
killswitch engaged it is **exactly empty** -- the same signature that
originally proved the clock was quantized to the 55 ms BIOS tick, and the
source of the phantom "42% audio deficit" (18.5 fps = 1000/54). With 0122
active the band repopulates and the bimodal split vanishes.

Both arms are witnessed rather than assumed:

    P22B  SDL/0122 pump-aware timebase DISABLED(killswitch=0)
    P22A  SDL/0122 pump-aware timebase ENABLED

## The lost-BIOS-tick question, now a number

An open question since the pump shipped -- does it drop BIOS ticks while
interrupts are masked? -- is answered by the 0122 teardown ride-along:

    SDL/0122 pump teardown -- isr_count=12219 div=9943
      expected_bios_chains=1853 actual_bios_chains=1853 delta=0

**Zero ticks lost.** Earlier reasoning had bounded the skew at under 1% from
flip counts; it is now measured at exactly 0. This is the DOSBox arm -- the
real-hardware arm needs the same line off g2k, since DOSBox-X does not model
IRQ-0/PIT timing faithfully ([[dosbox_not_proxy]]). Every pump run now prints
this line, so the real-HW answer arrives with the next g2k cell for free.

## SDL/0123 DAC width

    DOSVESA-DACWIDTH: 4F08.get ax=0x004F supported=1 width_bits=6

DOSBox-X reports a 6-bit DAC, matching what `ProgramVGADAC` writes, so this
environment cannot show the suspected fault. The deliverable is the per-card
real-hardware pair: one line from a Cirrus log and one from an S3 log. If the
S3 reports 8, it explains the operator's "Cirrus colours look richer" report
and retroactively qualifies every cross-card visual comparison this project
has made.

## Both original caveats are now CLOSED by `P22C`

`P22C` is the ENABLED arm re-run alone, to a clean end-of-replay exit:

- **Sample size.** 264 samples against `P22B`'s 236, so the two arms are now
  comparable in weight. **261 of 264 land in the 10-40 ms band that is
  EXACTLY EMPTY (0 of 236) with the killswitch engaged.** The first run's
  34-sample arm was directionally right and is now superseded.
- **The ENABLED-arm teardown line exists**, and it is the one that matters,
  because the earlier line came from the MODE-3 killswitch path:

      SDL/0122 pump teardown -- isr_count=7500 div=9943
        expected_bios_chains=1137 actual_bios_chains=1137 delta=0
        (timebase=MODE2/ON)

  **`delta=0` on the MODE-2 path.** The new timebase loses no BIOS ticks
  either, which is the specific thing that could have gone wrong when the
  pump switched counter modes -- the DOS time-of-day chain is intact under
  the mode the fix actually ships.

## Remaining caveat

- DOSBox-X validates the mechanism, not real-hardware PIT behaviour. The
  MODE-2 latch read is the one part that could misbehave on a real board; the
  killswitch reverts instantly, and the designed fallback is an RTC/IRQ-8
  128 Hz pump.
