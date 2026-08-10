# TAS record/replay: benchmarking and diagnosis

How doskutsu benchmarks real hardware with a recorded input reel, why the
engine behaves the way it does during replay, and how to diagnose a replay
that misbehaves. Written after the 2026-08-06 campaign, where a long-standing
replay bug cost several bench sessions to find. The point of this document is
that nobody has to work that out again.

Companion: `docs/QA-CELL-REFERENCE.md` lists the sweeps, every cell, and the
hardware lanes -- what to run. This file is why it works.

## Why TAS at all

Comparing framerates across CPUs, sound cards and video cards is only
meaningful if every machine runs the *same workload*. A human cannot replay a
route identically, and manual play contaminates the measurement. So the
operator records one input reel, and every benchmark cell replays that reel.
One reel, many machines, identical work.

## The format

`.TAS` is a small binary file: a 20-byte header (magic `DTASv1\n`, version,
PRNG seed, flags, event count) followed by 8-byte events, each a `(tick,
mask)` pair. Bit *i* of the mask is `inputs[i]` in `INPUTS` enum order --
bit 0 LEFT, 1 RIGHT, 2 UP, 3 DOWN, 4 JUMP, 5 FIRE, and so on.

**Events are COALESCED: one is written only when the mask CHANGES.** A key
held from tick 158 to tick 183 is two events, not twenty-five. This is the
single most important property of the format, and misunderstanding it is what
caused the 2026-08-06 bug: replay must HOLD the last mask on every tick, not
apply it only on ticks that carry an event.

`n_events = 0xFFFFFFFF` means stream mode -- read until EOF. That is the
normal recorded value, not corruption.

## The determinism contract

Game state is a pure function of `(tick index, input mask)`. It does not
depend on wall-clock or framerate. Two consequences worth internalising:

- A replay on a slow machine produces the *same run* as on a fast one; it just
  takes longer in wall-clock. Slower is not wrong.
- Therefore any divergence is either the input applied at the wrong tick, or
  state being driven by something outside the tick stream. Those are different
  bugs; see diagnosis below.

This is testable: two replays of one reel on one machine must produce
byte-identical traces.

## Ticks, and why loads corrupt them

Logic runs at a fixed 50 Hz via an accumulator: elapsed wall-clock is added to
`accum_ns`, and while it holds a tick's worth, `tick_state()` runs -- up to
`MAX_CATCHUP` (5) times per rendered frame, so the game keeps up on hardware
that cannot render at 50 fps. Backlog beyond that is discarded.

Outside `GM_NORMAL` (title, menus, inventory, cutscenes) the engine runs a
different path: one logic tick per rendered frame.

The hazard: **any main-loop blocking work lands in the next accumulator
delta**. A stage load or an instrument upload takes seconds; the catch-up loop
then runs extra ticks that represent *loading*, not gameplay, and the replay
index advances faster than the recording did. Enough of those and the reel
hits EOF early, truncating the run.

The fix is to exclude the load interval, not to cap the tick rate:

- `ft_accumulator_freeze()` / `ft_accumulator_thaw()` snapshot and restore the
  accumulator around a blocking section (patch 0162-v2).
- Patch **0280** applies it to `load_stage`.
- Patch **0285** applies it to `on_song_start()`, where the GUS backend
  uploads `.pat` instruments to GF1 DRAM.

Patch **0286**'s 150 ms stall threshold was calibrated partly on frame times
from pump-clocked cells, which are quantised rather than real -- re-derive it
from non-pump cells, or after the timebase fix lands.

If you add another slow main-loop load, wrap it the same way.

## Never clamp ticks to the frame rate

Patch 0281 capped the accumulator delta to one tick per frame during TAS. It
was reverted to default-off in 0284, and the reasoning matters:

Capping logic at one tick per rendered frame makes replay do **less logic work
per frame** than real gameplay. At ~40 fps render, normal play runs 50 logic
ticks/s (about 1.25 per frame); a clamped replay runs 40. Fewer ticks per
frame renders faster, so **measured framerates come out optimistically high**.
The operator can feel it -- replay looks like "playing through water".

Since these replays exist to benchmark hardware, playback must behave exactly
as normal play does. Exclude load time; never throttle gameplay ticks.

`SDL_HINT_DOSKUTSU_TAS_FT_GUARD=1` re-enables the clamp if some future reel
genuinely needs it. It should stay off for anything you intend to measure.

## Recording a campaign reel

    RECORD          (or RT282-style: record, then immediately replay it back)

- **Two to three minutes is plenty.** Short reels make short cells: a 102 s
  reel gives ~2 min per cell, so a 10-cell sweep is ~22 min. A five-minute
  reel triples that for no extra information.
- **Get past the title quickly.** Menus run on the frame-rate tick path, which
  is the least robust part of a reel.
- **Cover varied ground.** The 2026-08-06 campaign reel visits Save Point,
  Mimiga Village, Yamashita Farm, Reservoir, Assembly Hall and the Shack --
  ten stage transitions, so it exercises stage loading, music changes and a
  spread of render loads.
- **Quit via the in-game menu (ESC)** or the reel is not written. Ideally idle
  a few seconds first: the trailing menu inputs are the most fragile ticks in
  the file.
- **Verify immediately.** Replay it once before trusting it.

Record and replay must use the **same engine build**. A reel captured by a
binary with different tick behaviour will not replay faithfully on another.

## Running a benchmark

Cells set `DOSKUTSU_TAS_REPLAY`, a fixed `DOSKUTSU_TAS_PRNG_SEED`, and
`DOSKUTSU_TAS_AUTO_EXIT_TICK` as a backstop; replay auto-exits at EOF anyway.
Each cell copies its own `CFGS/*.CFG` over `DOSKUTSU.CFG`, clears inherited
hints, deletes the save, and tags its log.

Measured overhead of TAS itself is about **1 fps** (g2k: replay 32.3 fps
median vs the operator playing the same route at 33.3), inside the +/-2 fps
noise floor. Recording is the heavier side -- it flushes an event per input
change; replay only reads 8 bytes on event ticks.

## Reading the results

Each cell log is self-contained:

| Field | Use |
|---|---|
| `inter_flip_ms=` | per-frame timing -- median and p95 framerate |
| `>> Entering stage N` | the replayed route; compare against a known-good run |
| `audio backend: X` | what actually initialised, vs what the CFG asked for |
| `dur=Ns` | total run wall-clock |

### Framerates from pump-clocked cells are INVALID

**Never take an `inter_flip_ms` median from a cell whose log contains
`OPL timer pump STARTED` or `PIT/IRQ-0 pump`.** That covers every `gus` and
`adlib` cell.

The SDL/0110 music pump reprograms PIT channel 0, which is also the channel
DJGPP's `uclock()` reads -- and `uclock` is the only timebase behind
`SDL_GetTicks` / `GetTicksNS` / `GetPerformanceCounter` / `Delay` on DOS
(`vendor/SDL/src/timer/dos/SDL_systimer.c`). `uclock` assumes the full-65536
divisor it programs itself, so with the count confined to a smaller range the
clock freezes between 55 ms BIOS ticks and then jumps, with local jitter that
can run BACKWARDS. The benchmark's timer is corrupted by the thing being
benchmarked.

**The signature is a forbidden band.** Histogram `inter_flip_ms`: on this
hardware real frames live at 10-40 ms, and in a pump cell that band is
*exactly empty* while hundreds of samples pile up below 10 ms and at 40-70 ms.
No real slowdown produces that. A "median" of 18.5 fps is just 1000/54 -- the
BIOS tick period.

    G22  (GUS)    <10ms: 73   10-40ms: 0    40-70ms: 167
    G41  (AdLib)  <10ms: 60   10-40ms: 0    40-70ms: 169
    GC4  (OPL3)   <10ms:  2   10-40ms: 287  40-70ms:  27

**Read those cells as flips / wall-clock instead** -- valid because the pump
chains the BIOS INT8 correctly, so log timestamps stay honest. Corrected:
AdLib is within a couple of fps of OPL3, GUS about 4 fps lower (dominated by
GF1 DRAM `.pat` upload stalls at song boundaries, not by the pump).

This cost a wrong headline finding once: GUS and AdLib were reported as
costing 42% of the framerate. They do not.

A real defect does hide underneath it: the fixed-timestep accumulator runs off
the same staircase clock, so logic advances in bursts every 55 ms and a large
fraction of flips repeat an unchanged frame. Those backends genuinely *feel*
like 18 fps despite ~28 fps throughput. That is a smoothness bug, not a
throughput one. Fix plan lives in `docs/internal/GUS-ADLIB-FPS-FINDINGS.md`.

**Always check the route before trusting a framerate.** A truncated cell
measured less work than a complete one and is not comparable. Cells whose
route is a strict prefix of the reference were cut short, not misrouted.

Raw logs live in `qa-results/<date>-<cpu>-<card>/`, with the driving `.TAS`
beside them -- routes and durations are uninterpretable without it.

## Diagnosing a bad replay

**Do this first.** Two rounds of patching from log inference got the
2026-08-06 bug partly right and partly wrong; one trace diff found it.

    SET SDL_HINT_DOSKUTSU_TAS_TRACE=1

Emits `[tas-trace] t= mode= map= px= py= mask=` every 25 ticks and on every
map change, from both tick paths. Record with it on, replay with it on, and
diff the two traces:

| Diff shows | Meaning |
|---|---|
| same tick, different state | state driven by something outside the tick stream |
| same state, different tick | replay index misaligned |
| identical until tick N | divergence starts at N -- read the map and mask there |
| replay route is a prefix | truncation: ticks inflating, look for an unguarded load |

The bug that motivated this document looked like:

    record: t=175 map=20 px=71845 mask=0x2    <- RIGHT held, moving
    replay: t=175 map=20 px=66065 mask=0x0    <- input dropped, frozen

with the reel carrying `t=158 mask=0x2` and `t=183 mask=0x0`. Replay was
applying each input for one tick instead of holding it (fixed in 0283).

## Environment gotchas

- **DOSBox-X cannot reproduce real-hardware stalls.** A green emulator A/B
  proves little about the bench. Replay *determinism* is testable locally,
  though: two identical replays must give identical traces.
- **DOS reserved names silently swallow BATs and logs.** `VERIFY` is a
  COMMAND.COM internal, so `VERIFY.BAT` never runs; `LOGS\CON.LOG` cannot be
  created. Avoid `CON PRN AUX NUL COM1-9 LPT1-9 VERIFY` for BAT and tag names.
- Every kit BAT needs the `COMMAND /E:2048` self-relaunch or the environment
  block overflows and `SET`s are dropped silently.
- `make nxengine` keys off the patch files, not the vendored tree: editing
  `vendor/nxengine-evo/src/` alone rebuilds nothing and exits 0. Generate the
  patch first. Run `make` from the repo root.
- Any engine source change moves the Organya cache key, so both PCM caches
  must be re-rendered before the next deploy or the operator eats a 19-60 min
  cold render mid-run.
