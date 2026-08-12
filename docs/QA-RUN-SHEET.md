# QA run sheet -- tick as you go

Ordered to keep the CPU in the socket as long as possible: every card swap for
a given CPU happens before that CPU comes out. Cell definitions are in
`docs/QA-CELL-REFERENCE.md`; the harness itself is in
`docs/TAS-BENCHMARKING.md`.

Commands are typed **on the DOS box** unless marked *laptop*.

**Golden rule.** The number after `PG` / `VB` / `VID*` is the CPU in the box.
It is the only thing deciding which column a result lands in.

| CPU | number | tag |
|---|---|---|
| Pentium OverDrive 83 | 1 | `G..` |
| Am5x86-133 | 2 | `A..` |
| 486DX2-66 | 3 | `6..` |
| 486DX2-50 | 4 | `5..` |

**`PG` / `VB` / `VIDV` / `VIDC` / `VIDM` need nothing beforehand.** They take
the CPU number, set the log tag themselves and open their own environment.
Just `C:`, `CD \DOSKUTSU`, then the command.

**Individual cells DO need `QA n` first** (`G11`...`G117`, `C1`). They refuse
to run untagged rather than risk filing results under the wrong CPU -- you get
`ERROR: machine not set`. Run `QA n` once, then the cells, then `EXIT`.

**Do NOT run `RECORD`.** `QA.TAS` is the fixed workload for the whole
campaign. Re-recording makes every column non-comparable.

**Check the reel before every round.** `DOSKUTSU\QA.TAS` must be **1956 bytes,
sha starting `4118561edf26`**. The payload used to ship a 492-byte fallback
that no banked cell ever ran, so a blank-card populate silently swapped the
workload while the logs stayed perfectly normal -- valid framerates, complete
routes, nothing anywhere flagging it. A five-second check against a failure
that only surfaces when someone tries to explain why the numbers moved.

If something sounds or looks wrong, note the **cell number** on screen
(`[n/10]`) in Notes at the bottom. The logs cannot tell me what you heard.

---

# ROUND 2 -- gap-fill and prove-out

Round 1 (below) is complete: 89 cells, 4 CPUs. This is what to run now.
Ordered to keep the SOUND CARD in as long as possible -- the opposite of
round 1, because here the card is the thing being compared.

## Pre-flight -- tick ALL of these before the first cell

Every silent-invalidation failure found so far would have been caught by one
of these, and each takes seconds.

| | Check | How | Stop if |
|---|---|---|---|
| 0.1 | Card populated with current BATs | *laptop:* `scp claude:/tmp/install-qa-v163.sh /tmp/ && bash /tmp/install-qa-v163.sh` | it errors |
| 0.2 | **Reel is the benchmark reel** | *laptop:* `sha256sum /media/micheal/DOS/doskutsu/QA.TAS` | not `4118561edf26...` |
| 0.3 | Binary is the one you mean to test | round 1 = `09e449c5a81d` | testing round-2 changes on it |
| 0.4 | Sound card seated for the PART below | Vibra for Part 1, PicoGUS for Part 2 | mismatch |
| 0.5 | Video is the S3 ViRGE | | anything else |
| 0.6 | **Organya HQ cache present** | *laptop:* `ls /media/micheal/DOS/doskutsu/CACHE/22050_2/READY.OK` | absent -- the `XH` cells would cold-render for many minutes and look hung |
| 0.7 | Logback label chosen | e.g. `round2-gaps` | -- |

---

# ROUND 2 -- PART 1 -- Vibra16   *(sound card never moves)*

`X8` is the cell that earns this part: it is Vibra-only and separates the
card from the DMA path in the ~1 fps Vibra-vs-PicoGUS gap. Running it on all
four CPUs turns a yes/no into a slope, which is what is wanted -- ring
servicing is CPU-bound, so the recovered fraction should itself vary by CPU.

| | Answers | Hardware change | Run | Time |
|---|---|---|---|---|
| 1.1 | X8 slow end, floor, ORGHQ | none (current setup) | `QA 4` then `GAP` then `EXIT` | 10 min |
| 1.2 | X8, floor, ORGHQ | **CPU -> DX2-66** | `QA 3` then `GAP` then `EXIT` | 10 min |
| 1.3 | X8, floor | **CPU -> Am5x86-133** | `QA 2` then `GAP` then `EXIT` | 10 min |
| 1.4 | X8 fast end, floor | **CPU -> POD-83** | `QA 1` then `GAP` then `EXIT` | 10 min |

Totals: 0 card swaps, **3 CPU swaps**. ~40 min.

Plain `GAP` here -- the Vibra already IS the Sound Blaster, so no `pgusinit`
runs and no PicoGUS is required.

---

# ROUND 2 -- PART 2 -- PicoGUS   *(one sound swap to get here)*

`RB` gives the noise floor from its adjacent `R4`/`R4B` pair. That is a
property of the machine rather than the binary, so it stays valid afterwards
and is what makes every later "inside the noise" claim mean anything.

| | Answers | Hardware change | Run | Time |
|---|---|---|---|---|
| 2.1 | noise floor, Shack music | **sound -> PicoGUS** (CPU stays POD-83) | `QA 1` then `RB` then `EAR` then `EXIT` | 18 min |
| 2.2 | noise floor | **CPU -> Am5x86-133** | `QA 2` then `RB` then `EXIT` | 10 min |
| 2.3 | noise floor, AdLib-as-486-default | **CPU -> DX2-66** | `QA 3` then `RB` then `EAR` then `EXIT` | 18 min |
| 2.4 | noise floor, AdLib on the slowest box | **CPU -> DX2-50** | `QA 4` then `RB` then `EAR` then `EXIT` | 18 min |
| 2.5 | -- | none | pull logs (*laptop*) | 2 min |

Totals: 1 card swap, **3 CPU swaps**. ~1 h 6 m.

`EAR` needs your ears for ~8 min and needs the PicoGUS: its AdLib cell
switches the card to adlib mode, and on a Vibra the Sound Blaster stays live,
the music pump never starts, and you would be judging audio quality against
silence.

On a PicoGUS box `GAP` takes an argument -- **`GAP PG`** -- which forces SB
mode first. Plain `GAP` there would run the SB cells against whatever mode the
previous sweep left behind.

---

# ROUND 2 -- send it back

*laptop*, CF re-mounted:

    scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh round2-gaps

| | Check | Why |
|---|---|---|
| 3.1 | Tag set printed matches what you ran | a missing cell is easier to re-run now than to explain later |
| 3.2 | Each `LOGS\<M><SWEEP>.NFO` names the CPU that was actually in the box | if it disagrees, that run is filed under the wrong machine and must be repeated |

---

# ROUND 2 -- expected, NOT broken

Three things look like failures and are not:

| Symptom | Why | Action |
|---|---|---|
| A cell plays **no sound** | `X0` is the `AUDIO_OFF=1` floor cell; silence is its purpose. It runs LAST in `GAP`. | none |
| **Keyboard does nothing** during a cell | TAS replay overwrites input every tick. The cell ends itself. | wait |
| `no PicoGUS detected!` | `GAP PG` was typed on a Vibra box | harmless; use plain `GAP` |

A cell that ends almost immediately, or sits far longer than ~2 min, is
usually the WRONG REEL -- re-check 0.2.

Totals, whole round: **1 card swap, 6 CPU swaps, ~1 h 50 m.**

---

# ROUND 1 -- PART 1 -- Pentium OverDrive 83   *(CPU never moves)*

| | Lane | Hardware change | Run | Time |
|---|---|---|---|---|
| 1.1 | A | none (current setup) | `PG 1` | 22 min |
| 1.2 | D | none | `VIDV 1` | 10 min |
| 1.3 | E | video -> Cirrus | `VIDC 1` | 10 min |
| 1.4 | F | video -> Mach64 | `VIDM 1` | 10 min |
| 1.5 | B | video -> ViRGE, sound -> Vibra | `VB 1` | 13 min |
| 1.6 | -- | none | pull logs (*laptop*) | 2 min |

Totals: 3 video swaps, 1 sound swap, **0 CPU swaps**. ~1 h 10 m.

The hands-on SETUP walk is **deferred** -- see the end of this sheet.

---

# PART 2 -- 486DX2-66   *(one CPU swap to get here)*

Lane C pairs with lane A -- same card, same modes, same reel, CPU the only
variable. That pair is the point of the campaign; everything else in this part
is a bonus.

| | Lane | Hardware change | Run | Time |
|---|---|---|---|---|
| 2.1 | C | sound -> PicoGUS, **CPU -> DX2-66** | `PG 3` | ~28 min |
| 2.2 | G | none | `VIDV 3` | 12 min |
| 2.3 | H | video -> Cirrus | `VIDC 3` | 12 min |
| 2.4 | I | video -> Mach64 | `VIDM 3` | 12 min |
| 2.5 | J | video -> ViRGE, sound -> Vibra | `VB 3` | 16 min |
| 2.6 | -- | none | pull logs (*laptop*) | 2 min |

Totals: 3 video swaps, 2 sound swaps, 1 CPU swap. ~1 h 25 m.
**2.1 alone is the required part** (~30 min); 2.2-2.5 are optional depth.

No hands-on cells in any part -- the SETUP walk, joystick and soaks are
behaviour checks rather than benchmarks. Deferred; see the end of this sheet.

---

# PART 3 -- Am5x86-133   *(optional)*

| | Lane | Hardware change | Run | Time |
|---|---|---|---|---|
| 3.1 | K | sound -> PicoGUS, **CPU -> Am5x86** | `PG 2` | ~26 min |
| 3.2 | L | none | `VIDV 2` | 11 min |
| 3.3 | M | video -> Cirrus | `VIDC 2` | 11 min |
| 3.4 | N | video -> Mach64 | `VIDM 2` | 11 min |
| 3.5 | O | video -> ViRGE, sound -> Vibra | `VB 2` | 15 min |
| 3.6 | -- | none | pull logs (*laptop*) | 2 min |

Totals: 3 video swaps, 2 sound swaps, 1 CPU swap. ~1 h 15 m.
**3.1 alone gives the CPU row** (~26 min).

---

# PART 4 -- 486DX2-50   *(optional)*

| | Lane | Hardware change | Run | Time |
|---|---|---|---|---|
| 4.1 | P | sound -> PicoGUS, **CPU -> DX2-50** | `PG 4` | ~32 min |
| 4.2 | Q | none | `VIDV 4` | 13 min |
| 4.3 | R | video -> Cirrus | `VIDC 4` | 13 min |
| 4.4 | S | video -> Mach64 | `VIDM 4` | 13 min |
| 4.5 | T | video -> ViRGE, sound -> Vibra | `VB 4` | 18 min |
| 4.6 | -- | none | pull logs (*laptop*) | 2 min |

Totals: 3 video swaps, 2 sound swaps, 1 CPU swap. ~1 h 30 m.
**4.1 alone gives the CPU row** (~32 min).

The DX2-50 is the slowest chip in the set, so its cells take the longest: the
reel is a fixed 102 s of *game time*, and a machine that cannot render at
50 fps replays it over a longer wall-clock. Longer is expected, not a fault.

---

# How much of this to actually run

| Scope | Parts | Time | What you get |
|---|---|---|---|
| **Minimum** | 1.1-1.6, 2.1 | ~1 h 40 m | The A/C anchor pair and every audio backend on two CPUs -- the benchmark matrix. |
| **+ video** | add 1.2-1.4, 2.2-2.4 | +~1 h | Three video cards on two CPUs, plus the Cirrus bridge to the historical figures. |
| **+ CPUs** | add Parts 3-4 | +~1 h (rows only) | Four-CPU framerate matrix. |
| **Complete** | everything | ~4 h 30 m | Every lane on every CPU. Add ~1.5 h if the deferred SETUP walk is picked up. |

Split across sessions freely -- each part ends with a log pull, and nothing in
a later part depends on an earlier one beyond the reel staying untouched.

---

# Send it back

*laptop:*

    scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh

or

    tar czf /tmp/qa-final.tar.gz -C /media/micheal/DOS/doskutsu LOGS QA.TAS \
      && scp /tmp/qa-final.tar.gz claude:/tmp/

- [ ] Logs sent
- [ ] Notes below sent

---

# Notes

Cell number and what you saw or heard. Terse is fine.

    lane A  cell __   ...
    lane B  cell __   ...
    lane C  cell __   ...

---

# If something goes wrong

| Symptom | Do this |
|---|---|
| Replay ends in seconds / character stuck | Stop, send the log, don't run the rest |
| "Bad command or file name" | Not in `C:\DOSKUTSU`, or the CPU number was missing |
| Unsure which CPU a cell filed under | The cell banner prints its tag: `G..` POD-83, `6..` DX2-66, `A..` Am5x86, `5..` DX2-50 |
| Sweep stalls on one cell | Ctrl-C aborts; earlier cells' logs are already written |
| `G17` sounds thin / synthetic | The DreamBlaster is not on the header the current sound card presents -- check the log for `audio backend: wb` |

---

---

# DEFERRED -- hands-on SETUP walk *(not in the current run)*

Cut from the flow. Nothing in Parts 1-4 depends on these, and they can be
picked up in any later session: they need only the Vibra16 + DreamBlaster in
the box, on any CPU.

The consequence, stated plainly: these cells are the real-hardware
confirmation of the v1.6.3 SETUP work -- the `#19` Organya preview, `#20`
save-and-exit, `#21` sound-menu pickers -- which is what this QA campaign was
originally created to verify. The benchmark matrix does not cover them, so
until they run, those v1.6.3 changes stay DOSBox-validated only.

Run `QA 1` once, then the cells inside that shell, then `EXIT`:

    QA 1

- [ ] `G11`  SETUP Express: detect, accept profile, video bench, SAVE.
      Should name the Vibra + DreamBlaster + ViRGE.
- [ ] `G12`  SETUP Sound menu: under **every** Music Type both pickers open
      and list all devices; bad combos WARN, never block.
- [ ] `G13`  SETUP audio tests. Organya preview = **real title theme**, full
      window, no 5-second loop, no arpeggio.
- [ ] `G14`  SETUP save-and-exit: toast says "Run DOSKUTSU.EXE to play", no
      auto-launch, clean exit, no stale SETs at the prompt after.
- [ ] `G15`  SETUP input remap: rebind one key, verify in game, restore.
- [ ] `G115` Joystick: calibrate, then axis + 4 buttons + invert-Y + keyboard
      still works alongside. Gamepad? test the D-pad too.
- [ ] `G116` Save / Load / quit-to-DOS x5. Watch for the quit hang.
- [ ] `G117` Free play 20-30 min (Mimiga -> First Cave -> Egg Corridor).

# Swap count, whole campaign

| Part | Video | Sound | CPU |
|---|---|---|---|
| 1  POD-83 (5 lanes) | 3 | 1 | 0 |
| 2  DX2-66 | 3 | 2 | 1 |
| 3  Am5x86 *(optional)* | 3 | 2 | 1 |
| 4  DX2-50 *(optional)* | 3 | 2 | 1 |

Running only the required lanes (1.1-1.6 and 2.1) costs **0 video swaps,
2 sound swaps and 1 CPU swap** for the entire campaign.
