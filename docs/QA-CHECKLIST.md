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
| 2 | Mach64 | 28 min |
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

## Part 2 -- Mach64

| | Swap | Run |
|---|---|---|
| 2.1 | video -> Mach64 | -- |
| 2.2 | CF to laptop | `bash /tmp/mach64kit/install-mach64.sh` |
| 2.3 | CF to box | reboot, pick menu entry **6** |
| 2.4 | -- | `M64VBE` then `VESATEST` |
| 2.5 | -- | `QA 4` then `VIDM 4` (Vibra) |
| 2.6 | -- | pull logs *laptop* |
| 2.7 | video -> ViRGE | reboot, pick any other entry |

Load it bare -- **`M64VBE`**, no arguments. There is no `I` switch; an invalid
one just prints help and installs nothing.

Entry 6 sets up the **Vibra**, so `VIDM 4` -- no `PG`.

`M64VBE` says:

| | |
|---|---|
| `M64VBE (V2.21) is installed` | good |
| `M64VBE is already installed` | fine, carry on |
| `Can not load ... adapter is not detected` | stop, report it |

**`VESATEST` cannot answer this -- do not read it as a verdict.** It is a
1994 tool that only knows standard VESA mode numbers, and M64VBE puts
320x240 at ATI's `0x0212`. Confirmed 2026-08-12: TSR resident, `320 modes
enabled`, VESATEST still listed only 640x400 and up.

| `VESATEST` shows | |
|---|---|
| 320x240 | confirmed present, go to 2.5 |
| 640x400 and up only | inconclusive -- go to 2.5 anyway |
| 512x384 | UniVBE is loaded, not M64VBE -- wrong boot entry |

**`DOSVESA-CTRL` in the log is the real answer.** It walks the card's own
mode list and does pick up OEM numbers -- round 1 logged SciTech's `0x01F3`.

Match on **resolution**, not mode number.

Picture wrong but 320x240 present: `M64VBE U` then `M64VBE VW VGA`.

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
