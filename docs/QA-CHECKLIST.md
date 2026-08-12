# QA checklist -- round 2

Tick as you go. Ordered to keep the CPU in the socket as long as possible and
to start from the hardware already in the box. Cell definitions are in
`docs/QA-CELL-REFERENCE.md`; round 1's sequence is in `docs/QA-RUN-SHEET.md`.

Commands are typed **on the DOS box** unless marked *laptop*.

**Golden rule.** `QA n` sets which CPU the logs are filed under, and it is the
only thing deciding that. The CPU is the one component no log records.

| CPU | `n` | tag |
|---|---|---|
| Pentium OverDrive 83 | 1 | `G..` |
| Am5x86-133 | 2 | `A..` |
| 486DX2-66 | 3 | `6..` |
| 486DX2-50 | 4 | `5..` |

---

## What runs on what

The card carries the **round-1 binary `09e449c5a81d`** -- deliberately, so the
round-2 harness is proven against a known-good build first.

| Sweep | Sound card | Needs ears | Useful now? |
|---|---|---|---|
| `GAP` | **Vibra** (`X8` is meaningless elsewhere) | no | yes, fully |
| `EAR` | **PicoGUS required** (switches sb -> adlib) | **yes** | yes, fully |
| `PROVE` | **PicoGUS required** | no | 4 of 6 cells |
| `RB` | PicoGUS required | no | **no -- it refuses** |

`RB` prints `RB REFUSES TO RUN` on a round-1 card. That is correct: re-baselining
round 1 against round 1 agrees perfectly and tests nothing.

---

## PART 1 -- 486DX2-50 (already in the box; CPU never moves)

Starting hardware: **DX2-50 + S3 ViRGE + Vibra16.** `GAP` goes first because
`X8` is the one cell that *requires* the Vibra, and `XH1`/`XH2` re-measure the
Organya-HQ cell that contradicted itself in round 1 -- on this exact CPU.

**Populate the card first.** Everything below assumes the current installer has
been run. Five things landed after the last populate and none are on the card
yet: the `RB` round-2 guard, `BINARY.NFO`, `ROUND2.OK`, the `CLRENV`
`PUMP_TIMEBASE` top-up, and the byte-level CRLF gate. Without them `RB` will
not refuse, nothing records which binary produced a result, and a killswitch
set by one cell survives into the next.

| | Hardware change | Run | Time |
|---|---|---|---|
| 1.0 | CF to the *laptop* | populate (see **Install** below) | ~5 min |
| 1.1 | CF back in the box | `QA 4` then `GAP` | ~12 min |
| 1.2 | sound -> PicoGUS | `EAR` | ~9 min + listening |
| 1.3 | none | `PROVE` | ~18 min |
| 1.4 | none | pull logs (*laptop*) | 2 min |

Totals: 1 sound swap, 0 video swaps, 0 CPU swaps. **~45 min including the populate.**

`GAP` runs as plain `GAP` here -- **not** `GAP PG`. The `PG` argument forces a
PicoGUS into sb mode and there is no PicoGUS in the box at step 1.1.

---

## PART 2 -- 486DX2-66 (one CPU swap to get here)

Only worth doing for `GAP`: `XH1`/`XH2` on this CPU are the other half of the
round-1 Organya-HQ contradiction, where the DX2-50 scored *higher* than the
DX2-66 on the same cell. Both halves are needed to settle it.

| | Hardware change | Run | Time |
|---|---|---|---|
| 2.1 | CPU -> DX2-66, sound -> Vibra | `QA 3` then `GAP` | ~10 min |
| 2.2 | none | pull logs (*laptop*) | 2 min |

Totals: 1 CPU swap, 1 sound swap. **~12 min.**

`EAR` and `PROVE` do not need repeating here -- one CPU answers both.

---

## PART 3 -- deferred until the round-2 payload exists

`RB` is the re-baseline and refuses to run until a non-round-1 binary is
installed. When that payload lands, `RB` on each CPU is the first thing to run,
and its three anchor cells each catch a different class of change:

| Cell | Catches |
|---|---|
| `R4` OPL3 | the control -- no music timer, isolates observer effect |
| `R5` AdLib | the music-timer path, which a timebase fix changes and `R4` cannot see |
| `R3` Organya | the re-rendered PCM cache, which nothing else exercises |

`R4B` is a straight repeat of `R4` and measures the run-to-run noise floor
directly, rather than inferring it from configurations that happened to repeat.

---

## Pre-flight gate -- *laptop*, before touching the box

Every silent-invalidation failure found so far would have been caught here.
Seconds each.

- [ ] **CF mounted** at `/media/micheal/DOS`
- [ ] **Reel is the benchmark reel:**
      `sha256sum /media/micheal/DOS/doskutsu/QA.TAS` begins **`4118561edf26`**
      (1956 bytes). A different reel gives complete, plausible, wrong numbers
      and no other symptom. **If it does not match:**

      ```
      scp claude:/tmp/QA-ROUND1.TAS claude:/tmp/fix-reel.sh /tmp/ && bash /tmp/fix-reel.sh
      ```

      A full populate also fixes it -- the installer writes this reel at step
      5d regardless of what the payload tarball contains, which is still the
      old fallback.
- [ ] **Binary is what you think:** `cat /media/micheal/DOS/doskutsu/BINARY.NFO`
- [ ] **HQ cache present** if running `GAP`:
      `ls /media/micheal/DOS/doskutsu/CACHE/22050_2` -- without it `XH1`/`XH2`
      cold-render for minutes and look exactly like a hang
- [ ] **`LOGS/` empty** (the installer clears it)
- [ ] **Logback label chosen**, never reused

Then physically:

- [ ] Correct **CPU** in the socket, and you know its number
- [ ] Correct **sound card** for the sweep -- see the table above
- [ ] **S3 ViRGE** seated

---

## Install -- *laptop*

```
scp claude:/tmp/install-qa-v163.sh /tmp/ && bash /tmp/install-qa-v163.sh
```

Stop if any of these is wrong:

- `PASS: DOSKUTSU.EXE <sha>` -- the build you intended
- `installed QA.TAS (1956 B, sha 4118561edf26...) -- matches the round-1 benchmark reel`
- `binary is the ROUND-1 build ...` **or** `ROUND2.OK written, RB enabled`
- `PASS: all NN BATs CRLF + ASCII`
- `PASS: unmounted ...`

Re-running is cheap: the 188 MB payload is only fetched if not already staged.
`REPLACING a different reel` is the guard working -- it found a non-benchmark
reel, swapped it, and kept a backup.

---

## Running a sweep -- *DOS box*

```
C:
CD \DOSKUTSU
QA n
```

Each sweep prints its hardware before the prompt:

```
SWEEP : ...
CPU   : 486DX2-50
SOUND : MIXED -- X8 needs the VIBRA; the rest run on any card
VIDEO : S3 ViRGE
```

- [ ] **Read it and stop if it disagrees with the box.** This is the only check
      that catches a wrong CPU number.

---

## Retrieve the logs -- *laptop*

```
scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh <label>
```

Suggested labels: `r2-gap-dx250`, `r2-ear-dx250`, `r2-prove-dx250`, `r2-gap-dx266`.

- [ ] **Label every pull, never reuse one.** The destination used to be fixed
      per machine, which silently merged a later round over an earlier one. It
      now refuses if the label exists.
- [ ] **Pull between sweeps**, not only between parts.
- [ ] Final line reads `UPLOAD_OK:` with the file count you expect.
- [ ] The `.NFO` came back and its `cpu=` matches the CPU actually in the box.

---

## Expected -- NOT broken

Stopping a good run costs a bench session; letting a bad one continue costs the
numbers.

| Looks like | Actually |
|---|---|
| `X0` / `P0` play **no sound at all** | Correct. `AUDIO_OFF=1` is the true silence floor -- that is the measurement. |
| Keyboard does nothing during a cell | Correct. The reel drives input and the cell ends itself. |
| `RB` prints `RB REFUSES TO RUN` | Correct on a round-1 card. Not a failure. |
| "no PicoGUS detected" | You ran `GAP PG`, or `EAR`/`PROVE`, without a PicoGUS in the box. |
| `X8` results look odd on a PicoGUS | `X8` is meaningless off the Vibra. Ignore it there. |
| A cell sits minutes on the title screen | Organya-HQ cold-rendering -- the 22050 cache is missing. Abort, check the card. |
| The box appears hung straight away | Check the reel sha. A stub reel ends early while replay keeps feeding input. |

---

## Known-bad, do not bank

- Anything run before commit `28218c0` -- fallback reel, different workload.
- `RB` output from a round-1 card, if the guard is ever bypassed.
- Round-1 `5112` / `6112` -- self-contradictory, already excluded. `XH1`/`XH2`
  in part 1 and part 2 exist to replace them.

---

## Before the round-2 payload can be built

New binary is `812447456e9a`. In order:

1. New binary into the kit
2. `make convert-music` -- the MIDI sets are gitignored build products, so a
   rebuild that skips it ships old files with a new binary and nothing reveals it
3. Organya cache re-render at the new key -- shipped caches are keyed
   `f8d446b4b0e0` and go stale; miss this and every Organya cell cold-renders
4. `tests/qa/embed-bats.sh` after any BAT edit, then re-stage the installer
5. Update `EXP_DOSKUTSU_SHA` and `TARBALL` in the installer

`ROUND2.OK` then writes itself and `RB` enables.
