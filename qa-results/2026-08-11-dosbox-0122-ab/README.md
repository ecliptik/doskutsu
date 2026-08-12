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

## Caveats

- `P22A` carries only 34 flip-probe samples against `P22B`'s 236, because the
  run hit a 2-minute harness timeout partway through the second cell. The
  signature is unambiguous at that sample size, but a longer re-run is worth
  banking.
- The teardown line above is from the **killswitch** arm. `P22A` was killed
  before a clean shutdown, so the pump never stopped and the ENABLED arm has
  no teardown line yet. Same re-run closes it.
- DOSBox-X validates the mechanism, not real-hardware PIT behaviour. The
  MODE-2 latch read is the one part that could misbehave on a real board; the
  killswitch reverts instantly, and the designed fallback is an RTC/IRQ-8
  128 Hz pump.
