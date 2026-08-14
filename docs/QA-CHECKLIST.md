# QA checklist -- round 2

`QA n` after every boot: POD-83 = 1, Am5x86 = 2, DX2-66 = 3, DX2-50 = 4.
Every sweep waits for a **keypress** at its banner, then runs unattended.
One logback per CPU round, at the end. Never reuse a label.

**After every video card swap, re-set-up UniVBE and watch its boot line** --
it names the chipset it took over. A card on its own BIOS is not comparable to
round 1 and may not boot.

---

## Round A -- 486DX2-50 (`QA 4`) -- DONE

| Step | Hardware | Boot | Type after boot | Time |
|---|---|---|---|---|
| A0 | CF in *laptop* | -- | populate | 5 min |
| A1 | ViRGE + PicoGUS | 2 | `QA 4` -> `RB` `EAR` `PROVE` | 40 min |
| A2 | ViRGE + **Vibra** | 1 | `QA 4` -> `GAP` | 12 min |
| A3 | CF in *laptop* | -- | logback `r2f-dx250` | 2 min |

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

Logs accumulate on the card across a round; one pull collects the lot.
Do not re-populate mid-round -- that clears `LOGS\`.

---

## Populate -- *laptop*, once, at A0

    scp claude:/tmp/install-qa-v163.sh /tmp/ && bash /tmp/install-qa-v163.sh

## Sweeps

| Sweep | Cells | Time | Needs |
|---|---|---|---|
| `RB` | 4 | ~12 min | ViRGE + PicoGUS |
| `EAR` | 3 | ~9 min | ViRGE + PicoGUS + **ears** |
| `PROVE` | 6 | ~18 min | ViRGE + PicoGUS |
| `GAP` | 4 | ~12 min | ViRGE + Vibra |

`GAP` is slow on purpose -- 2 of its 4 cells are Organya-HQ. Only `X0` is fast.

## `EAR` -- the only sweep needing ears

Shack = last map change, ~87 s in, on screen 6-8 s. Confirmed on DX2-50; on the
other CPUs just report same or different.

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
| Video witness | `oem_string='Universal VESA VBE 6.70'` |

---

# Mach64 -- LAST, after every round is done

Mach64 in, ViRGE out. Keep whatever sound the POD-83 round left in the box.

| Step | Do | Time |
|---|---|---|
| M1 | Fit Mach64, set up UniVBE for it, boot | -- |
| M2 | **Does UniVBE take the Mach64?** If it declines -- STOP, report, done | -- |
| M3 | `QA 1` then `VIDM 1` (add `PG` only if the PicoGUS is in) | 40 min |
| M4 | Colours still wrong by eye? `VIDMC 1` -- watch, then reset | ~5 min |
| M5 | logback `r2f-mach64` -- send | 2 min |

    scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh r2f-mach64

Colour probe (`VIDMC`) runs the VIDM cells at 16bpp -- no palette, no index
remap, no DAC in the path. It sets and clears the hint itself, so there is
nothing to type by hand.

Get it onto the card first -- *laptop*, CF mounted:

    scp claude:/tmp/VIDMC.BAT /media/micheal/DOS/doskutsu/

Then:

    QA 1
    VIDMC 1        (add PG only if the PicoGUS is in)

**Do not let it finish.** It runs both VIDM cells and 16bpp pushes double the
bytes, so it is slower than the 38 min VIDM took. The answer arrives in the
first few minutes:

**Watch the map-name banner** -- route is `72` -> `20 Save Point` ->
`11 Mimiga Village`, and the name appears as each loads.

| | |
|---|---|
| text **white** | indexed + colour-mod path is at fault -- patch `0297` fixes it |
| text **pink** | fault is upstream of the palette -- `0297` is not the whole story |

Once seen, **reset the machine.** Its logs are disposable by design and the
hint dies with the DOS session, so an abort leaks nothing even though the
BAT's own cleanup line never runs.

Optional: the diversion is already confirmed from the round-2 logs (13173
fallback draws on the Mach64 against 0 on the ViRGE). This is independent
confirmation by a different route, not the only evidence.

- Budget 40 min per cell -- it ran at 2.2 fps last time.
- Its fps is not comparable to Round A's Mach64 number (different CPU + sound).
- UniVBE, never M64VBE.
