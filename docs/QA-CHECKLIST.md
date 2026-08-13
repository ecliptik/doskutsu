# QA checklist -- round 2

`QA n` after every boot: POD-83 = 1, Am5x86 = 2, DX2-66 = 3, DX2-50 = 4.
Every sweep waits for a **keypress** at its banner, then runs unattended.
One logback per CPU round, at the end. Never reuse a label.

---

## Round A -- 486DX2-50 (`QA 4`)

| Step | Hardware | Boot | Type after boot | Time |
|---|---|---|---|---|
| A0 | CF in *laptop* | -- | populate | 5 min |
| A1 | ViRGE + PicoGUS | 2 | `QA 4` -> `RB` `EAR` `PROVE` | 40 min |
| A2 | **Mach64** + PicoGUS | 5 | `QA 4` -> `VIDM 4 PG` `VIDMK 4 PG` | 20 min |
| A3 | ViRGE + **Vibra** | 1 | `QA 4` -> `GAP` | 12 min |
| A4 | CF in *laptop* | -- | **logback `r2f-dx250`** -- send | 2 min |

    scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh r2f-dx250

**Mach64 colour probe -- run at A2, BEFORE the normal `VIDM`.** By eye only;
it overwrites the same log tags, so the real cells must run after it.

    SET SDL_HINT_DOSKUTSU_PIXEL_FORMAT_8=0
    VIDM 4 PG                                 <- look at the colours, no logs kept
    SET SDL_HINT_DOSKUTSU_PIXEL_FORMAT_8=     <- REQUIRED, see below
    VIDM 4 PG                                 <- the real cells
    VIDMK 4 PG

Colours right at 16bpp -> fault is in the indexed/palette chain.
Still wrong -> fault is upstream, in the renderer path.
The empty `SET` is not optional: `CLRENV` does not clear this var, so it leaks
into every later sweep in the same boot and would put `GAP` at 16bpp silently.


## Round B -- 486DX2-66 (`QA 3`)

| Step | Hardware | Boot | Type after boot | Time |
|---|---|---|---|---|
| B1 | ViRGE + PicoGUS | 2 | `QA 3` -> `RB` `EAR` `PROVE` | 40 min |
| B2 | ViRGE + **Vibra** | 1 | `QA 3` -> `GAP` | 12 min |
| B3 | CF in *laptop* | -- | **logback `r2f-dx266`** -- send | 2 min |

    scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh r2f-dx266


## Round C -- Am5x86-133 (`QA 2`)

| Step | Hardware | Boot | Type after boot | Time |
|---|---|---|---|---|
| C1 | ViRGE + PicoGUS | 2 | `QA 2` -> `RB` `EAR` `PROVE` | 40 min |
| C2 | ViRGE + **Vibra** | 1 | `QA 2` -> `GAP` | 12 min |
| C3 | CF in *laptop* | -- | **logback `r2f-am5x86`** -- send | 2 min |

    scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh r2f-am5x86


## Round D -- Pentium OverDrive 83 (`QA 1`)

| Step | Hardware | Boot | Type after boot | Time |
|---|---|---|---|---|
| D1 | ViRGE + PicoGUS | 2 | `QA 1` -> `RB` `EAR` `PROVE` | 40 min |
| D2 | ViRGE + **Vibra** | 1 | `QA 1` -> `GAP` | 12 min |
| D3 | CF in *laptop* | -- | **logback `r2f-pod83`** -- send | 2 min |

    scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh r2f-pod83


Logs accumulate on the card across a round -- tags are unique per sweep and
prefixed per CPU, so one pull collects the lot. Do not re-populate mid-round;
that clears `LOGS\`.

---

## Populate -- *laptop*, once, at A0

    scp claude:/tmp/install-qa-v163.sh /tmp/ && bash /tmp/install-qa-v163.sh

## Round E -- Mach64 follow-up (PROVISIONAL -- do not run yet)

Runs last, on whatever the POD-83 round leaves in the box. Vibra preferred so
no card comes back out; PicoGUS is fine if it is still fitted.

| Step | Hardware | Boot | Type after boot | Time |
|---|---|---|---|---|
| E1 | **Mach64** + Vibra | 1 | `QA 1` -> colour probe, below | 10 min |
| E2 | -- | -- | TBD -- pending the Mach64 investigation | -- |
| E3 | CF in *laptop* | -- | **logback `r2f-mach64`** -- send | 2 min |

    scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh r2f-mach64

E1, the colour probe, never ran in Round A. By eye only; it overwrites the
real cells' log tags, so run it first if E2 turns out to need them.

    SET SDL_HINT_DOSKUTSU_PIXEL_FORMAT_8=0
    VIDM 1 PG                                 <- drop PG on a Vibra
    SET SDL_HINT_DOSKUTSU_PIXEL_FORMAT_8=     <- REQUIRED, CLRENV does not clear it

Colours right at 16bpp -> fault is in the indexed/palette chain.
Still wrong -> fault is upstream, in the renderer path.

**Known from Round A, so E2 does not need to re-establish it:** the card sets
`0x0101 640x480` (not 512x384), the letterbox works at offset 160,120, 1782 of
1782 drawcalls fall to the software slow path against 0 on the ViRGE, and the
result is 2.2 fps -- 38 minutes for one cell. Budget for that if E2 runs a full
cell. `VIDMK` produced no A/B: both cells died on SB detection, not video.

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
