# Benchmark save state

`QA-ROUND1.TAS` does not start a new game. It opens Load Game and enters map 20
from a save, so these two files are as load-bearing as the reel itself: without
them the reel replays against a different starting state and every cell in the
round measures something other than what round 1 measured. Nothing about the
failure is visible while it happens -- the game boots, the reel runs, the log
looks ordinary.

Both slots hold the same state. The reel's inputs select a slot, and making 3
and 5 identical means the selection cannot change where the run begins.

| | |
|---|---|
| Map | 20, 'Save Point' |
| Position | px 64469, py 73728 |
| HP | 3 / 3, Lv 0, no weapon |
| Size | 1540 bytes each, format `Do041220` |
| sha256 | both begin `ccb9610bbd77` |

## Provenance

These are not the round-1 originals. Those were deleted from the CF by a
populate whose purge list had grown to include `profile*.dat`, and carving the
card recovered only three unrelated new-game saves at map 13. The installer no
longer purges profiles and backs up any it finds before the purge step runs.

These were rebuilt: a new game, the Mimiga Village door script into map 20, and
a save at that map's disk.

## Why the reel did not have to be re-recorded

A reel is a list of `(tick, input)`. It replays against whatever state the save
supplies, so restoring the state restores the run -- re-recording would instead
orphan all 89 round-1 cells, whose banked routes would no longer describe what
the reel does.

That reasoning is only as good as the state match, so it was tested rather than
assumed. Replaying `QA-ROUND1.TAS` against these saves on the **round-1 binary**
(`09e449c5a81d`) reproduces the round-1 stage sequence exactly:

    72  20  11  17  11  15  11  19  11  14  11

identical to `qa-results/2026-08-11-DX250-full/5C4.LOG`. The route is the
fingerprint: it is driven by the reel's inputs acting on the loaded state, so a
state that differs in any way the run touches diverges within a few transitions.

What the match does not establish is that the *player state* is byte-identical
to what round 1 held -- HP, weapons and flags are not recoverable, and the route
does not exercise them. `RB` on real hardware is where that gets settled: if
`R4` lands on the round-1 anchor (17.39 per-loop on DX2-50 + ViRGE + OPL3), the
comparison carries forward. A route match with an off-anchor frame rate means
the state differs in something that costs render work, and round 2 becomes a
fresh baseline rather than a continuation.

## Reproducing

Requires a console-enabled build -- the console is gated behind `#if
defined(DEBUG)` in `src/input.cpp`, so a release binary cannot open it. Build
`vendor/nxengine-evo` with `-DDEBUG` appended to `CMAKE_CXX_FLAGS`, keeping
`DOSKUTSU_BUILD_SHA12` unchanged so the Organya cache still keys.

In game, backtick opens the console:

    script 109      <- Mimiga Village's door into map 20; places the player
                       at tile (10,9), which `warp` does not (it uses 16,16,
                       outside this room, leaving the player out of bounds)

Then walk onto the save disk, press down, and save into slots 3 and 5.
