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
| 2 | Mach64 -- **held for letterbox** | 20 min |
| 3 | DX2-66, Vibra | 12 min |
| 4 | blocked -- needs round-2 payload | -- |

---

## Part 1 -- DX2-50 (in the box now)

| | Swap | Run |
|---|---|---|
| 1.0 | CF to laptop | populate |
| 1.1 | CF to box | `QA 4` then `GAP` |
| 1.2 | sound -> PicoGUS | `EAR` (listen) |
| 1.3 | -- | `PROVE` |
| 1.4 | -- | pull logs *laptop* |

`GAP` not `GAP PG`. No PicoGUS in the box at 1.1.

---

## Part 2 -- Mach64 -- HELD until the letterbox patch lands

**The card cannot do 320x240.** The Mach64-CT has no double scanning, and
320x200/320x240 are double-scanned modes, so no VBE driver can provide them.
UniVBE says so when it loads; ATI's M64VBE cannot either, and additionally
hangs SDL_Init in every switch combination tried. Use UniVBE, not M64VBE.

Evidence: `qa-results/2026-08-12-mach64-pathA/`.

The engine letterbox fix is being written. When it lands this becomes a
**verification lane**, answering two independent questions:

| | Question | Read |
|---|---|---|
| a | does the image land correctly? | look at the screen |
| b | do the stalls survive? | `inter_flip_ms` spread in the log |

Round 1 had **40% of frames stalled over 300 ms** while the unstalled ones hit
20 ms -- 50 fps at 512x384. The letterbox does not address that; it is a
separate, unexplained problem and it is what decides whether the card is
usable.

| | Swap | Run |
|---|---|---|
| 2.1 | video -> Mach64, ViRGE out | -- |
| 2.2 | -- | `QA 4` then `VIDM 4` |
| 2.3 | -- | pull logs *laptop* |
| 2.4 | only if stalls persist | `VIDMK 4` -- same cells, `VBLANK_BOUND=0` |
| 2.5 | -- | pull logs *laptop* |
| 2.6 | video -> ViRGE | -- |

`VIDMK` exists so the A/B cannot overwrite its own baseline -- it tags
`5C4MK` / `551MK` against `VIDM`'s `5C4M` / `551M`.

Boot any normal menu entry. Entry 6 is M64VBE-only and has no use now.

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
| `RB REFUSES TO RUN` | correct on this card |
| "no PicoGUS detected" | wrong sweep for the card in the box |
| minutes on the title screen | HQ cache missing -- abort |
| hangs at once | wrong reel -- check the sha |

---

## Do not bank

- anything run before commit `28218c0`
- `RB` output from this card
- round-1 `5112` / `6112`
