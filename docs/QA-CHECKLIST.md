# QA checklist -- round 2

Detail lives in `docs/QA-RUN-SHEET.md` and `tests/qa/README.md`.
*laptop* = run on the laptop. Everything else is typed on the DOS box.

CPU number for `QA n`: POD-83 = 1, Am5x86 = 2, DX2-66 = 3, DX2-50 = 4.

**Sound card rule.** Sweeps use whatever SB is in the box. Add `PG` only
when a PicoGUS is fitted -- `GAP PG`, `VIDM 4 PG`. On a Vibra use plain
`GAP` / `VIDM 4`. Passing `PG` without a PicoGUS overwrites BLASTER with
jumper values the Vibra cannot use.

---

## Order

| Part | Hardware | Time |
|---|---|---|
| 1 | DX2-50, ViRGE, Vibra -> PicoGUS | 45 min |
| 2 | Mach64 verification | 20 min |
| 3 | DX2-66, Vibra | 12 min |
| 4 | blocked -- needs round-2 payload | -- |

---

## Part 1 -- DX2-50 (in the box now)

| | Swap | Run |
|---|---|---|
| 1.0 | CF to laptop | populate |
| 1.1 | CF to box | `QA 4` then **`RB`** |
| 1.2 | -- | pull logs *laptop* -- **stop and check** |
| 1.3 | -- | `GAP` |
| 1.4 | sound -> PicoGUS | `EAR` (listen) |
| 1.5 | -- | `PROVE` |
| 1.6 | -- | pull logs *laptop* |

**`RB` runs first and 1.2 is a real stop.** It decides whether the ~90 banked
round-1 cells still compare against this binary. Four cells:

| Cell | Catches |
|---|---|
| `R4` OPL3 | control -- no music timer, isolates observer effect |
| `R4B` | repeat of `R4` -- the noise floor, measured not inferred |
| `R5` AdLib | the music-timer path, which `R4` cannot see |
| `R3` Organya | the re-rendered PCM cache, which nothing else exercises |

Send me those logs before anything else. If `R4` agrees with round-1's `5C4`
(17.39 per-loop) within the noise floor, everything carries forward. If
it does not, the rest of the round is measuring something new and we need to
know that first.

`GAP` not `GAP PG` -- no PicoGUS in the box until 1.4.

---

## Part 2 -- Mach64 verification

**Use UniVBE, not M64VBE.** The card cannot do 320x240 -- the Mach64-CT has no
double scanning -- so the engine now centres the 320x240 picture inside the
512x384 surface (`patches/nxengine-evo/0296`). M64VBE additionally hangs
SDL_Init. Boot any normal menu entry; entry 6 has no use.

Two independent questions. A correct picture does **not** mean a usable card.

| | Swap | Run |
|---|---|---|
| 2.1 | video -> Mach64, ViRGE out | -- |
| 2.2 | -- | `QA 4` then `VIDM 4` |
| 2.3 | -- | pull logs *laptop* |
| 2.4 | only if stalls persist | `VIDMK 4` -- same cells, `VBLANK_BOUND=0` |
| 2.5 | -- | pull logs *laptop* |
| 2.6 | video -> ViRGE | -- |

**a) Does the picture land right?** Look at the screen. Expect the image
centred with a black border, not in the corner. The log proves it engaged:

    center-oversized: ENGAGED surface=...
    center-oversized: flush rect=320x240@96,72 bytes=76800

Killswitch for an A/B: `SET SDL_HINT_DOSKUTSU_CENTER_OVERSIZED=0`.

**b) Do the stalls survive?** Round 1 had **40% of frames over 300 ms** while
the unstalled ones hit 20 ms -- 50 fps at 512x384. The letterbox does nothing
for that; it is what decides whether the card is usable. Check the
`inter_flip_ms` spread, then run 2.4 if they are still there.

`VIDMK` tags `5C4MK` / `551MK` so the A/B cannot overwrite its own baseline.

Expect the flush to be **logical-sized (76800 B), not surface-sized** -- the
patch scopes it to the centred rect. Fewer bytes is certain; the exact
millisecond gain is a bench question, because a sub-rect flush goes per-row
rather than as one contiguous blast.

---

## Part 3 -- DX2-66

| | Swap | Run |
|---|---|---|
| 3.1 | CPU -> DX2-66, sound -> Vibra | `QA 3` then `GAP` |
| 3.2 | -- | pull logs *laptop* |

---

## Pre-flight -- *laptop*, every time

- [ ] `sha256sum /media/micheal/DOS/doskutsu/QA.TAS` starts `4118561edf26`
- [ ] `cat /media/micheal/DOS/doskutsu/BINARY.NFO` -- expected binary
- [ ] `ls /media/micheal/DOS/doskutsu/CACHE/22050_2` exists
- [ ] `LOGS/` empty
- [ ] new logback label picked
- [ ] right CPU, sound card, video card in the box

Wrong reel:
```
scp claude:/tmp/QA-ROUND1.TAS claude:/tmp/fix-reel.sh /tmp/
bash /tmp/fix-reel.sh
```

---

## Commands

Populate -- *laptop*:
`scp claude:/tmp/install-qa-v163.sh /tmp/ && bash /tmp/install-qa-v163.sh`

Mach64 boot profile -- *laptop*, once:
`scp -r claude:/tmp/mach64kit /tmp/ && bash /tmp/mach64kit/install-mach64.sh`

Sweep -- DOS: `C:` / `CD \DOSKUTSU` / `QA n` / sweep name

Logs -- *laptop*:
`scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh <label>`

Labels: `r2-gap-dx250`, `r2-ear-dx250`, `r2-prove-dx250`, `r2-vidm-dx250`,
`r2-gap-dx266`. Never reuse one. Pull between sweeps.

---

## Not broken

| | |
|---|---|
| `X0` / `P0` silent | correct -- that is the measurement |
| keyboard dead in a cell | correct -- the reel drives it |
| "no PicoGUS detected" | wrong sweep for the card in the box |
| minutes on the title screen | HQ cache missing -- abort |
| hangs at once | wrong reel -- check the sha |

---

## Do not bank

- anything run before commit `28218c0`
- `RB` output from a card whose `BINARY.NFO` is not the round-2 build
- round-1 `5112` / `6112`

---

## Payload -- NOT BUILT YET

The populate command above still fetches the **round-1** payload. Do not run
Part 1 until this is done; `RB` would compare round 1 against itself.

| | Step | State |
|---|---|---|
| 1 | binary with `0296` letterbox | built, **stamp wrong** -- see below |
| 2 | `make convert-music` | done (rebuilt at the converter fix) |
| 3 | Organya cache, both tiers | rendered, `READY.OK` not yet written |
| 4 | repack tarball | not started |
| 5 | `EXP_DOSKUTSU_SHA` + `TARBALL` | not updated |

**Open issue.** The binary contains the letterbox code but is stamped
`ff96af07db07`, the fingerprint of the *pre-0296* patch set; a fresh `make`
computes `1f79ce20e4ee`. A stale `-D` in the CMake cache. It works only because
the Organya cache carries the same wrong stamp, so the two agree. A clean
rebuild fixes the stamp and invalidates the cache, costing a rebuild plus both
tier renders, about 25 minutes unattended.

Shipping as-is means every round-2 log reports a build sha belonging to a
different patch set -- in the round where `BINARY.NFO` and `ROUND2.OK` were
added specifically to make provenance checkable.
