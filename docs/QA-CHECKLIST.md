# QA checklist

`QA n` after every boot: POD-83 = 1, Am5x86 = 2, DX2-66 = 3, DX2-50 = 4.
Boot 1 = Vibra16. Boot 2 = PicoGUS.
Every sweep waits for a keypress at its banner, then runs unattended.
Never reuse a logback label.
Re-run UniVBE after every video card swap and check its banner names the card.

## The loop

    populate once per binary -> run the round -> logback -> feedback -> next round

Logs accumulate across a round; one pull collects the lot.
Do not re-populate mid-round -- it clears `LOGS\`.

---

## Populate -- *laptop*, CF mounted at `/media/micheal/DOS`

    scp claude:/tmp/install-qa-v163.sh /tmp/ && bash /tmp/install-qa-v163.sh

Unmounts the CF when done. Logback does not -- unmount that by hand.

Check in the output:

- [ ] `PASS: DOSKUTSU.EXE 02d91d3be589`
- [ ] `PASS: Organya 11025 cache keyed a483f4b30d24`
- [ ] `PASS: Organya-HQ 22050 cache extracted` -- not `SKIPPING` / `no HQ cache`
- [ ] `PASS: QA.TAS = benchmark reel (1956 B)`
- [ ] `PROFILE3.DAT: map 20 'Save Point', weapon 2` -- `PROFILE5.DAT` same
- [ ] `already staged and current (sha 2b1eb1289a35)` or a re-fetch
- [ ] `PASS: all N BATs CRLF + ASCII`

A cache-key `WARN` means every Organya cell cold-renders. Stop, re-populate.

---

## Round M -- cross-CPU baseline, ViRGE + PicoGUS throughout

**The KPI machine is the POD-83.** Rounds G-L were run on the DX2-66 and
every recent lever was judged there. The README matrix shows POD-83 and
Am5x86-133 both pinned at ~33 fps, so the POD is already not CPU-bound and
486 gains do not automatically transfer up. This round re-anchors the
baseline on the machine the target is written against.

Video and sound stay fixed all round -- **ViRGE + PicoGUS, boot 2**. Only
the CPU changes. One card swap at the start, then four CPU swaps.

| Step | CPU | Type after boot | Time |
|---|---|---|---|
| M0 | laptop | populate | 6 min |
| M1 | POD-83 | `QA 1` then `RB` | 12 min |
| M2 | POD-83 | `RB` again -- same cells, second run | 12 min |
| M3 | POD-83 | `MINE 1 PG` | 12 min |
| M4 | Am5x86-133 | `QA 2` then `RB` | 12 min |
| M5 | 486DX2-66 | `QA 3` then `RB` | 12 min |
| M6 | 486DX2-50 | `QA 4` then `RB` | 12 min |
| M7 | laptop | logback `r11-crosscpu`, send | 3 min |

Both *laptop* commands, CF mounted:

    scp claude:/tmp/install-qa-v163.sh /tmp/ && bash /tmp/install-qa-v163.sh
    scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh r11-crosscpu

**M0 repopulates.** New binary, new cache key. Check the pre-flight lines.

**M2 is the noise band and is not optional.** Round K measured 0.6 fps
between two configuration-identical runs -- larger than most levers this
campaign has banked. Without a same-round repeat on the KPI machine, no
future fps claim can be judged.

**M3 `MINE 1 PG`** -- the two never-measured regions, on the KPI machine.
NOT fps rows. Report whether `[mode-tick-stat]` shows `n` in the DOZENS; if
`n` is 2 or the lines are title-only, patch 0312 did not take.

**`QA n` must match the installed CPU every time** -- 1 POD-83, 2 Am5x86,
3 DX2-66, 4 DX2-50. A wrong digit mislabels every row in that lane and the
cross-CPU comparison is the entire point of the round.

Not in this round: the Mach64. It rejoins for a full hardware matrix once
the backdrop defect is fixed.

---

## Reference

### Sweeps

| Sweep | Cells | Time | Needs |
|---|---|---|---|
| `RB` | 4 | ~12 min | ViRGE + PicoGUS |
| `EAR` | 3 | ~9 min | ViRGE + PicoGUS + **ears** |
| `PROVE` | 6 | ~18 min | ViRGE + PicoGUS |
| `GAP` | 4 | ~12 min | ViRGE + Vibra |
| `MIDIAB` | 2 | ~7 min | ViRGE + PicoGUS |
| `VB` | 6 | ~35 min | ViRGE + Vibra (WB cell needs the header) |
| `VIDM` | 2 | ~6 min | Mach64 |
| `VIDMI` | 2 | ~6 min | Mach64, own tags `C4I`/`51I` |
| `VIDKS` | 1 | ~5 min | Mach64, own tag `KS`, watch-then-reset |
| `FINE` | 2 | ~6 min | any card + **PicoGUS needs `PG`**, tags `C4F`/`51F` |
| `DEEP` | 2 | ~8 min | any card + `PG`, 7 instr layers, tags `C4D`/`51D` |
| `TAB` | 3 | ~9 min | any card + `PG`, tags `T0`/`TB`/`TC`, all fps rows |
| `MEMBW` | 1 | ~2 min | standalone probe, writes `MEMBW.OUT` (logback does NOT collect it -- copy it off by hand) |
| `BDIAG` | 1 | ~5 min | **Mach64 only**, tag `BD`, diagnostic not fps |
| `MINE` | 2 | ~12 min | any card + `PG`, tags `MN`/`MF`, diagnostic not fps |
| `RB` x2 | 8 | ~24 min | same sweep twice -- gives the round its noise band |
| `ADIAG` | 4 | ~12 min | any card + `PG`, tags `A0`/`AM`/`AS`/`AX`, all fps rows |
| `LEV` | 3 | ~9 min | any card + `PG`, tags `L0`/`LA`/`LP`, all fps rows |

Add `PG` to a Mach64 sweep only if the PicoGUS is in.

### Not broken

| | |
|---|---|
| nothing happens after typing the sweep | the `PAUSE` -- press a key |
| `X0` / `P0` silent | correct |
| keyboard dead in a cell | correct, the reel drives it |
| `GAP` choppy | correct, Organya-HQ cells |
| minutes on the title screen | HQ cache missing -- abort |
| route is not `72 20 11 17 11 15 11 19 11 14 11` | wrong save -- abort |

### Do not bank

- `RB` from a card whose `BINARY.NFO` does not match the round
- round-1 `5112` / `6112`
- the `r2-rb-dx250-noweapon` set
- `VIDMI` cells as fps rows -- instrumentation costs ~0.6 fps
- any `VB` WB cell whose log does not say `audio_backend=wb`

### Payload

| | Round 5 | Round 8 | Round 11 |
|---|---|---|---|
| Binary | `c1729d2fe065` | `6971e9f73bc9` | `02d91d3be589` |
| Tarball | `d49dccd49558` | `162fb8b55a4c` | `2b1eb1289a35` |
| Cache key | `482d88eba8b2` | `3ba36d8dd56d` | `a483f4b30d24` |

The Round-11 binary carries `0310` (tile-slot skip, default ON), `0311`
(surface-extent probe, default OFF) and `0312` (mode-layer instrumentation
on the fixed path, default OFF). `0309` is retracted -- out of the series
AND out of the binary -- and the installer deletes the stale `VIDMR.BAT`
from the CF, since tar extraction never removes files and a cell for a
vanished lever would measure two identical arms.

| | |
|---|---|
| Reel | `4118561edf26`, 1956 B |
| Saves | `32529e291e0f`, map 20 + Polar Star |
| Sound witness | PicoGUS `irq=7`, Vibra `irq=5 dma=5` |
| Video witness | `oem_string='Universal VESA VBE 6.70'` |

### Done

| Round | Machine | Sweeps | Logback |
|---|---|---|---|
| A | DX2-50, ViRGE | `RB` `EAR` `PROVE` `GAP` | `r2f-dx250` |
| B | DX2-66, ViRGE | `RB` `EAR` `PROVE` `GAP` | `r2f-dx266` |
| C | Am5x86-133, ViRGE | `RB` `EAR` `PROVE` `GAP` | `r2f-am5x86` |
| D | POD-83, ViRGE | `RB` `EAR` `PROVE` `GAP` | `r2f-pod83` |
| E | POD-83, Mach64 | `VIDM` `VIDMC` | `r2f-mach64`, `vidmc-pod-` |
| F | DX2-66, ViRGE | `RB` `EAR` `MIDIAB` `GAP` | `r3-rb-dx266`, `r3-rb-dx266-b`, `r3-dx266` |
| G | DX2-66, ViRGE + Mach64 | `GAP` `MIDIAB` `VIDM` `VIDMI` | `r3-fredo-dx266`, `r3-fredo-dx266-2`, `r3-mach64`, `r3-mach64-b` |
| H | DX2-66, ViRGE + Mach64 | `RB` `EAR` `VB` `VIDKS` `VIDM` `VIDMI` | `r4-rb-dx266`, `r4-rb-dx266-vibra`, `r4-mach64`, `r4-mach64-b` |
| I | DX2-66, ViRGE + Mach64 | `RB` `FINE` `VIDMR` | `r5-virge-dx266`, `r5-virge-dx266-b`, `r5-mach64` |
| J | DX2-66, ViRGE | `DEEP` `TAB` `MEMBW` | `r6-deep-dx266` |
| K | DX2-66, ViRGE + Mach64 | `RB` `ADIAG` `LEV` `BDIAG` | `r10-rb-dx266`, `r10-mach64` |

All labels above are spent. Nothing in Rounds A-K is to be re-run.

Results and analysis live in `docs/internal/POST-BENCHMARK-PLAN.md` and
`docs/internal/HANDOFF-ENGINE-AUDIO-BUCKET.md`.
