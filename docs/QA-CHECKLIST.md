# QA checklist -- round 2

`QA n` after every boot: POD-83 = 1, Am5x86 = 2, DX2-66 = 3, DX2-50 = 4.
Every sweep waits for a **keypress** at its banner, then runs unattended.
Pull logs after every sweep -- never reuse a label.

---

## Round A -- 486DX2-50 (`QA 4`)

| Step | Hardware | Boot | Type after boot | Then pull | Time |
|---|---|---|---|---|---|
| A0 | CF in *laptop* | -- | populate | -- | 5 min |
| A1 | ViRGE + PicoGUS | 2 | `QA 4` -> `RB` | `r2f-rb-dx250` **-- send** | 12 min |
| A2 | -- | -- | `EAR` | `r2f-ear-dx250` | 9 min |
| A3 | -- | -- | `PROVE` | `r2f-prove-dx250` | 18 min |
| A4 | **Mach64** + PicoGUS | 5 | `QA 4` -> `VIDM 4 PG` `VIDMK 4 PG` | `r2f-vidm-dx250` | 20 min |
| A5 | ViRGE + **Vibra** | 1 | `QA 4` -> `GAP` | `r2f-gap-dx250` | 12 min |

## Round B -- 486DX2-66 (`QA 3`)

| Step | Hardware | Boot | Type after boot | Then pull | Time |
|---|---|---|---|---|---|
| B1 | ViRGE + PicoGUS | 2 | `QA 3` -> `RB` | `r2f-rb-dx266` | 12 min |
| B2 | -- | -- | `EAR` | `r2f-ear-dx266` | 9 min |
| B3 | -- | -- | `PROVE` | `r2f-prove-dx266` | 18 min |
| B4 | ViRGE + **Vibra** | 1 | `QA 3` -> `GAP` | `r2f-gap-dx266` | 12 min |

## Round C -- Am5x86-133 (`QA 2`)

| Step | Hardware | Boot | Type after boot | Then pull | Time |
|---|---|---|---|---|---|
| C1 | ViRGE + PicoGUS | 2 | `QA 2` -> `RB` | `r2f-rb-am5x86` | 12 min |
| C2 | -- | -- | `EAR` | `r2f-ear-am5x86` | 9 min |
| C3 | -- | -- | `PROVE` | `r2f-prove-am5x86` | 18 min |
| C4 | ViRGE + **Vibra** | 1 | `QA 2` -> `GAP` | `r2f-gap-am5x86` | 12 min |

## Round D -- Pentium OverDrive 83 (`QA 1`)

| Step | Hardware | Boot | Type after boot | Then pull | Time |
|---|---|---|---|---|---|
| D1 | ViRGE + PicoGUS | 2 | `QA 1` -> `RB` | `r2f-rb-pod83` | 12 min |
| D2 | -- | -- | `EAR` | `r2f-ear-pod83` | 9 min |
| D3 | -- | -- | `PROVE` | `r2f-prove-pod83` | 18 min |
| D4 | ViRGE + **Vibra** | 1 | `QA 1` -> `GAP` | `r2f-gap-pod83` | 12 min |

`--` in Hardware/Boot means no swap and no reboot; keep going.

---

## Log commands -- *laptop*, CF mounted

Fetch the script once per session, then one line per pull:

    scp claude:/tmp/logback-qa.sh /tmp/

    bash /tmp/logback-qa.sh r2f-rb-dx250        <- A1  (send these)
    bash /tmp/logback-qa.sh r2f-ear-dx250       <- A2
    bash /tmp/logback-qa.sh r2f-prove-dx250     <- A3
    bash /tmp/logback-qa.sh r2f-vidm-dx250      <- A4
    bash /tmp/logback-qa.sh r2f-gap-dx250       <- A5

    bash /tmp/logback-qa.sh r2f-rb-dx266        <- B1
    bash /tmp/logback-qa.sh r2f-ear-dx266       <- B2
    bash /tmp/logback-qa.sh r2f-prove-dx266     <- B3
    bash /tmp/logback-qa.sh r2f-gap-dx266       <- B4

    bash /tmp/logback-qa.sh r2f-rb-am5x86       <- C1
    bash /tmp/logback-qa.sh r2f-ear-am5x86      <- C2
    bash /tmp/logback-qa.sh r2f-prove-am5x86    <- C3
    bash /tmp/logback-qa.sh r2f-gap-am5x86      <- C4

    bash /tmp/logback-qa.sh r2f-rb-pod83        <- D1
    bash /tmp/logback-qa.sh r2f-ear-pod83       <- D2
    bash /tmp/logback-qa.sh r2f-prove-pod83     <- D3
    bash /tmp/logback-qa.sh r2f-gap-pod83       <- D4

Populate (once, at A0):

    scp claude:/tmp/install-qa-v163.sh /tmp/ && bash /tmp/install-qa-v163.sh

---

## Sweeps

| Sweep | Cells | Time | Needs |
|---|---|---|---|
| `RB` | 4 | ~12 min | ViRGE + PicoGUS |
| `EAR` | 3 | ~9 min | ViRGE + PicoGUS + **ears** |
| `PROVE` | 6 | ~18 min | ViRGE + PicoGUS |
| `GAP` | 4 | ~12 min | ViRGE + Vibra |
| `VIDM 4 PG` | 2 | ~10 min | Mach64 + PicoGUS |
| `VIDMK 4 PG` | 2 | ~10 min | Mach64 + PicoGUS |

`GAP` is slow on purpose -- 2 of its 4 cells are Organya-HQ. Only `X0` is fast.
Mach64: UniVBE, never M64VBE.

## `EAR` -- the only sweep needing ears

Shack = last map change, ~87 s in, on screen 6-8 s. Already confirmed on
DX2-50; on the other CPUs just report same or different.

| Cell | Backend | Expected |
|---|---|---|
| 1 `E4` | OPL3 | a note or two |
| 2 `E3` | Organya | correct music |
| 3 `EA` | AdLib | silence |

---

## Pre-flight -- read off the populate output

- [ ] `PROFILE3.DAT: map 20 'Save Point', weapon 2` -- and `PROFILE5.DAT` same
- [ ] `PASS: QA.TAS = benchmark reel (1956 B)`
- [ ] `PASS: DOSKUTSU.EXE fe44805fb603`
- [ ] `already staged and current (sha d7d1d01dbbd9)` or a re-fetch

## Not broken

| | |
|---|---|
| nothing happens after typing the sweep | the `PAUSE` -- press a key |
| `X0` / `P0` silent | correct -- that is the measurement |
| keyboard dead in a cell | correct -- the reel drives it |
| `GAP` choppy | correct -- Organya-HQ cells |
| minutes on the title screen | HQ cache missing -- abort |
| route is not `72 20 11 17 11 15 11 19 11 14 11` | wrong save -- abort |

## Do not bank

- `RB` from a card whose `BINARY.NFO` is not `fe44805fb603`
- round-1 `5112` / `6112`
- the `r2-rb-dx250-noweapon` set (unarmed save)

## Payload

| | |
|---|---|
| Binary | `fe44805fb603` |
| Tarball | `d7d1d01dbbd9` |
| Reel | `4118561edf26`, 1956 B |
| Saves | `32529e291e0f`, map 20 + Polar Star |
| Sound witness | PicoGUS `irq=7`, Vibra `irq=5 dma=5` |
