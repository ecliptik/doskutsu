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

**Do NOT run `RECORD`.** `QA.TAS` is the fixed workload for the whole
campaign. Re-recording makes every column non-comparable.

If something sounds or looks wrong, note the **cell number** on screen
(`[n/10]`) in Notes at the bottom. The logs cannot tell me what you heard.

---

# PART 1 -- Pentium OverDrive 83   *(CPU never moves)*

| | Lane | Hardware change | Run | Time |
|---|---|---|---|---|
| 1.1 | A | none (current setup) | `PG 1` | 22 min |
| 1.2 | D | none | `VIDV 1` | 10 min |
| 1.3 | E | video -> Cirrus | `VIDC 1` | 10 min |
| 1.4 | F | video -> Mach64 | `VIDM 1` | 10 min |
| 1.5 | B | video -> ViRGE, sound -> Vibra | `VB 1` | 13 min |
| 1.6 | B | none | hands-on cells | ~1.5 h |
| 1.7 | -- | none | pull logs (*laptop*) | 2 min |

Totals: 3 video swaps, 1 sound swap, **0 CPU swaps**. ~2 h 40 m with the
hands-on block, ~1 h 10 m without.

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

No hands-on cells here -- the SETUP walk, joystick and soaks are behaviour
checks, not benchmarks, so they only need doing once (Part 1).

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
| **Minimum** | 1.1-1.7, 2.1 | ~3 h 10 m | The A/C anchor pair, all audio backends, the full SETUP + hardware behaviour walk. This is the campaign as designed. |
| **+ video** | add 1.2-1.4, 2.2-2.4 | +~1 h | Three video cards on two CPUs, plus the Cirrus bridge to the historical figures. |
| **+ CPUs** | add Parts 3-4 | +~1 h (rows only) | Four-CPU framerate matrix. |
| **Complete** | everything | ~6 h | Every lane on every CPU. |

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

# Swap count, whole campaign

| Part | Video | Sound | CPU |
|---|---|---|---|
| 1  POD-83 (5 lanes + hands-on) | 3 | 1 | 0 |
| 2  DX2-66 | 3 | 2 | 1 |
| 3  Am5x86 *(optional)* | 3 | 2 | 1 |
| 4  DX2-50 *(optional)* | 3 | 2 | 1 |

Running only the required lanes (1.1-1.7 and 2.1) costs **0 video swaps,
2 sound swaps and 1 CPU swap** for the entire campaign.
