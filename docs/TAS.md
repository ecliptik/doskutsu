# DOSKUTSU TAS -- tool-assisted record/replay

DOSKUTSU has an engine-side, game-tick-keyed input record/replay system. It serves
deterministic input from a pre-recorded file, so a play session can be reproduced
exactly -- used for perf-iteration (removing operator-timing variance across runs) and
for the per-stage end-to-end (E2E) determinism regression suite.

It is OFF by default: with neither `DOSKUTSU_TAS_RECORD` nor `DOSKUTSU_TAS_REPLAY` set,
every hook is a no-op and the binary behaves identically to a normal run.

This document covers the feature. The research/feasibility analysis (why a foreign
online Cave Story TAS cannot be imported, etc.) lives in the internal notes, not here.

## How it works

### The DTASv1 file format

A recording is a single binary file:

    header:
      magic      "DTASv1\n"   (7 bytes)
      version    1 byte
      prng_seed  u32 little-endian   (the base RNG seed for the run)
      flags      u32 LE              (reserved, 0)
      n_events   u32 LE              (0xFFFFFFFF = stream mode; events read to EOF)
    events: a stream of
      tick  u32 LE   (the game-tick the input change occurred on)
      mask  u32 LE   (bitfield over the 28 engine inputs)
    ... repeated until end of file.

Events are delta-encoded: an event is written only when the input mask CHANGES, so a
file is compact. On replay, each event's mask fully overwrites the engine input array
for every tick at or after the event's tick.

### Record / replay

Both are selected by environment variable (mutually exclusive; replay wins if both are
set):

    DOSKUTSU_TAS_RECORD=PLAY.TAS    capture live input -> PLAY.TAS
    DOSKUTSU_TAS_REPLAY=PLAY.TAS    feed recorded input from PLAY.TAS

Supporting variables:

    DOSKUTSU_TAS_PRNG_SEED=<n>      force the base RNG seed (default 0)
    DOSKUTSU_TAS_AUTO_EXIT_TICK=<n> end the run cleanly at tick n (safety bound)
    DOSKUTSU_TAS_REPLAY_HOLD=1      after the replay file is exhausted, hold the last
                                    input instead of auto-exiting (default: auto-exit
                                    at end of replay)

The log (in `<CWD>\DOSKUTSU\LOGS\`, or `<TAG>.LOG` when `DOSKUTSU_LOG_TAG` is set)
records the open + the end: `tas: record opened ...` / `tas: replay opened ...` and
`tas: end-of-replay auto-exit at tick N`.

### Determinism model

Replay reproduces a run exactly because three things are pinned:

1. FIXED LOGIC RATE. The game logic runs at `GAME_FPS = 50` Hz. The fixed-timestep
   accumulator advances logic ticks at exactly 50 Hz DECOUPLED from render speed
   (catch-up ticks when render lags), so the tick stream -- and therefore replay
   timing -- is independent of how fast the machine renders. A recording made on a fast
   PC replays the same on a slow 486.

2. SEEDED RNG. `getrand()` is the classic MSVC linear-congruential generator
   (`seed = seed*214013 + 2531011; return (seed >> 16) & 0x7fff`). On replay the base
   seed is taken from the file header (or `DOSKUTSU_TAS_PRNG_SEED` if set), so the RNG
   stream is reproducible.

3. PER-STAGE RE-SEED. When TAS record or replay is active, the engine re-seeds the RNG
   at each stage entry to a deterministic per-stage value (`base_seed + stage*K`),
   before any stage-spawn or entry-event RNG is consumed. This makes each stage's RNG
   reproducible from its own entry -- which is what lets a per-stage segment (below) be
   replayed independently. (When TAS is not active, no re-seed happens and the RNG
   stream is byte-identical to a normal run.)

### Warp integration + auto-start

The dev warp (`DOSKUTSU_WARP_STAGE`, see docs/internal BOOT notes) starts a new game
directly in a chosen stage on the real stage-entry path (correct fade-in, camera, and
spawn). It is the fixed per-segment START for the E2E suite.

When BOTH a warp AND a TAS record/replay are active
(`DOSKUTSU_WARP_STAGE>0` and a record/replay file is set), the engine AUTO-STARTS a new
game -- skipping the intro/title -- so record and replay are hands-free and
deterministic (no title-menu navigation to drive or record). With neither, or only one,
of those set, boot is normal.

### Limitation: a recording is build-specific

A `.TAS` is only valid on the EXACT engine build it was recorded on. Determinism depends
on the precise SEQUENCE of `getrand()` calls per tick, which is a property of the
compiled logic. Any change that alters how often / in what order RNG is consumed (an AI
tweak, a new code path, a different engine version) will desync an old recording. In
particular, TAS files made for the original Cave Story executable or other NXEngine
builds will NOT replay correctly here -- do not try to import them. Re-record on any
intentional logic change.

## How to use

### Record a run or a segment

    SET DOSKUTSU_TAS_RECORD=PLAY.TAS
    DOSKUTSU.EXE
    ... play ...  (quit normally to flush + close the file)

For a per-stage segment, add the warp so it starts in the target stage (auto-start makes
it hands-free). The committed example fixture warps into First Cave (stage 12) at the
door tile (37,11) and walks through it to Start Point (stage 13):

    SET DOSKUTSU_WARP_STAGE=12
    SET DOSKUTSU_WARP_X=37
    SET DOSKUTSU_WARP_Y=11
    SET DOSKUTSU_WARP_LOADOUT=1
    SET DOSKUTSU_TAS_PRNG_SEED=0
    SET DOSKUTSU_TAS_RECORD=SEG12.TAS
    DOSKUTSU.EXE
    ... play the stage to its exit, then quit ...

(Six-plus `SET`s overflow the default DOS environment -- use a `COMMAND /E:2048 /C %0 GO`
self-respawn BAT.)

### Recording notes

Two things make a segment replay robustly (learned during suite bring-up):

1. HOLD inputs through a transition, do not tap. A single-tick door-tap replays
   fragile and can desync; hold the input across the whole transition (keep Down
   pressed for the ~75 ticks it takes to walk into and enter a door). A held input is
   robust to small timing differences and replays deterministically.

2. A headless harness BAT must SET the BLASTER env (e.g. `SET BLASTER=A220 I5 D1 H5 T6`)
   before launching DOSKUTSU.EXE; without it SDL audio init fails under headless
   DOSBox-X ("No BLASTER env") and the run never reaches the stage. (On real hardware
   the BLASTER env is already set by the sound-card boot driver, so this is a
   headless-only requirement.)

### Replay a recording

    SET DOSKUTSU_TAS_REPLAY=PLAY.TAS
    DOSKUTSU.EXE

For a segment, set the same warp variables that were used to record it, plus
`DOSKUTSU_TAS_REPLAY=SEG12.TAS`. The replay auto-exits at end of file.

### The per-stage E2E determinism suite

The suite is a set of INDEPENDENT per-stage segments. Each segment warps into a stage
from a fixed start, replays a short recorded input, and is expected to deterministically
reach the next stage. A desync in one segment does not cascade into the others.

    make tas-e2e                            # build the stage + run the full suite
    tests/run-tas-e2e.sh                    # run directly (default paths)
    tests/run-tas-e2e.sh --self-test        # validate the oracle + parser only (no DOSBox)
    tests/run-tas-e2e.sh --manifest PATH --segments-dir DIR --timeout SECS

The harness iterates the manifest, runs each segment headless under DOSBox-X (hands-free
via the auto-start), and checks the log per segment:

    expected ">> Entering stage <next>:" before "tas: end-of-replay auto-exit"  = PASS
    auto-exit reached without it                                                = FAIL (desync)
    replay file bad magic / replay not opened                                   = FAIL (error)
    neither seen within --timeout                                               = FAIL (incomplete)

Exit code: 0 = all pass (or graceful skip when no manifest exists yet), 1 = any fail,
2 = setup error. The suite is standalone -- it is NOT part of `make smoke`.

### The segments.txt manifest

Default location `tests/tas-segments/segments.txt`; the `.TAS` segment files live in
`tests/tas-segments/` (committed golden-master fixtures). One row per segment
(whitespace-separated; `#` comments):

    # seg_file  start_stage  start_x  start_y  warp_loadout  base_seed  expected_next_stage
    SEG12.TAS   12           37       11       1             0          13

### Adding or re-recording a segment

1. Record the segment with the warp + record recipe above.
2. Read the `>> Entering stage N` line the stage exit produces -- that N is the row's
   `expected_next_stage`.
3. Add (or update) the row in `segments.txt` and commit the `.TAS` file.

The committed `.TAS` segments are GOLDEN-MASTER fixtures. On an intentional logic change
that legitimately alters behavior, the affected segments must be re-recorded (a desync
after such a change is expected, not a regression).
