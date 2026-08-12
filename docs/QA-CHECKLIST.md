# QA checklist -- round 2

Tick as you go. Cell definitions are in `docs/QA-CELL-REFERENCE.md`; the round-1
sequence is in `docs/QA-RUN-SHEET.md`.

Commands are typed **on the DOS box** unless marked *laptop*.

---

## What is on the card right now

The installer populates the **round-1 binary `09e449c5a81d`**. That is
deliberate: it proves the round-2 harness works against a known-good build
before anything depends on it.

| BAT | Runs now? | Why |
|---|---|---|
| `PROVE` | partly | 6 cells. `P5`/`P5K` are the timebase A/B and are meaningless until the round-2 binary is on the card. `P4 P4B P3 P0` are useful now. |
| `GAP` | **yes** | 4 cells. Answers gaps round 1 left open. Nothing here needs round 2. |
| `EAR` | **yes** | 3 cells, needs a person listening. |
| `RB` | **no -- it refuses** | Re-baselining round 1 against round 1 agrees perfectly and proves nothing. It will print `RB REFUSES TO RUN` until a round-2 payload is installed. |

`BINARY.NFO` on the card records which binary and reel are installed. Read it
if you are ever unsure what produced a result.

---

## 1. Pre-flight gate -- *laptop*, before touching the box

Every silent-invalidation failure found so far would have been caught here.
Each check is seconds.

- [ ] **CF mounted** at `/media/micheal/DOS`
- [ ] **Reel is the benchmark reel.** `sha256sum /media/micheal/DOS/doskutsu/QA.TAS`
      must begin **`4118561edf26`**. A different reel produces complete,
      plausible, wrong numbers with no other symptom.
- [ ] **Binary matches the round you think you are running.**
      `cat /media/micheal/DOS/doskutsu/BINARY.NFO`
- [ ] **`LOGS/` is empty** (the installer clears it)
- [ ] **Logback label chosen** -- see step 4. Never reuse one.

Then physically:

- [ ] **Correct CPU** in the socket, and you know its number: POD-83 = 1,
      Am5x86-133 = 2, DX2-66 = 3, DX2-50 = 4
- [ ] **Correct sound card** seated for the sweep you intend
- [ ] **DreamBlaster on the right header** if the sweep uses WaveBlaster
- [ ] **S3 ViRGE** seated unless you are deliberately testing another card

---

## 2. Install -- *laptop*

```
scp claude:/tmp/install-qa-v163.sh /tmp/ && bash /tmp/install-qa-v163.sh
```

Watch for these lines. Any of them wrong means stop:

- `PASS: DOSKUTSU.EXE <sha>` -- the build you intended
- `installed QA.TAS (1956 B, sha 4118561edf26...) -- matches the round-1 benchmark reel`
- `binary is the ROUND-1 build ...` **or** `ROUND2.OK written, RB enabled`
- `PASS: all NN BATs CRLF + ASCII`
- `PASS: unmounted ...`

The script re-fetches the 188 MB payload only if it is not already staged, so
re-running it after a failure is cheap.

If it stops at `REPLACING a different reel`, that is the guard working -- it
found a non-benchmark reel and swapped it, keeping a backup. Read the sha it
printed before continuing.

Move the card to the box.

---

## 3. Run the cells -- *DOS box*

```
C:
CD \DOSKUTSU
QA n          <- n is the CPU number. Sets the log tag for everything after.
```

Each sweep prints its hardware before it starts:

```
SWEEP : ...
CPU   : Pentium OverDrive 83
SOUND : PicoGUS -- DreamBlaster on the PicoGUS header
VIDEO : S3 ViRGE
```

- [ ] **Read that banner and stop if it disagrees with the box.** It is the
      only check that catches the wrong CPU number, and the CPU is the one
      component no log records.

Then, in this order:

- [ ] `GAP` -- 4 cells, ~10 min, unattended. Add `GAP PG` instead if the
      PicoGUS is installed and needs initialising.
      Cells: `X8` Vibra forced to 8-bit DMA, `XH1`/`XH2` Organya-HQ re-measure,
      `X0` true audio floor.
- [ ] `EAR` -- 3 cells, ~8 min, **needs a person listening**.
      Cells: `E4` OPL3, `E3` Organya, `EA` AdLib.
- [ ] `PROVE` -- 6 cells. `P5`/`P5K` will run but mean nothing until round 2.

`RB` only after a round-2 payload is installed.

---

## 4. Retrieve the logs -- *laptop*

```
scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh <label>
```

- [ ] **Label every pull** -- `round2-gap-pod83`, `round2-ear-dx266`. The
      destination used to be fixed per machine, which silently merged a later
      round over an earlier one. It now refuses if the label already exists.
- [ ] **Pull between sweeps**, not only between rounds.
- [ ] Confirm the final line is `UPLOAD_OK:` with the file count you expect.
- [ ] Confirm the `.NFO` came back and its `cpu=` matches the CPU actually in
      the box.

---

## 5. Expected -- NOT broken

Stopping a good run costs a bench session. Letting a bad one continue costs the
numbers.

| Looks like | Actually |
|---|---|
| `X0` / `P0` play **no sound at all** | Correct. `AUDIO_OFF=1` is the true silence floor. |
| Keyboard does nothing during a cell | Correct. The reel drives input; the cell ends itself. |
| `RB` prints `RB REFUSES TO RUN` | Correct on a round-1 card. Not a failure. |
| "no PicoGUS detected" from `GAP PG` on a Vibra box | You passed `PG` on the wrong card. Re-run as plain `GAP`. |
| A cell takes minutes on the title screen | Organya-HQ cold-rendering because the 22050 cache is missing. Abort and check the card. |
| The box appears hung immediately | Check the reel sha. A stub reel ends early while replay keeps feeding input. |

---

## 6. Known-bad, do not bank

- Any cell run before commit `28218c0` -- those used the fallback reel.
- `RB` results from a round-1 card, if the guard is ever bypassed.
- `5112` / `6112` from round 1 -- self-contradictory, already excluded.

---

## Before round 2's payload exists

The round-2 binary is `812447456e9a`. Building its payload needs, in order:

1. New binary into the kit
2. `make convert-music` -- the MIDI sets are gitignored build products, so a
   rebuild that skips it ships old files with a new binary and nothing reveals
   the mismatch
3. Organya cache re-render at the new key -- the shipped caches are keyed
   `f8d446b4b0e0` and go stale, and every Organya cell cold-renders on the
   bench if this is missed
4. `tests/qa/embed-bats.sh` after any BAT edit, then re-stage the installer
5. Update `EXP_DOSKUTSU_SHA` and `TARBALL` in the installer

Then `RB` enables itself, and the three-cell re-baseline (`R4` control,
`R5` pump path, `R3` re-rendered cache) is the first thing to run.
