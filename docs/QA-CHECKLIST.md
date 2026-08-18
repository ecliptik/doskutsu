# QA checklist

`QA n` after every boot: POD-83 = 1, Am5x86 = 2, DX2-66 = 3, DX2-50 = 4.
Boot 1 = Vibra16. Boot 2 = PicoGUS.
Every sweep waits for a keypress at its banner, then runs unattended.
Never reuse a logback label.
Re-run UniVBE after every video card swap and check its banner names the card.

## The KPI

The ship target is **30 fps on the POD-83** and **25 fps on the DX2-66**,
measured with **OPL3 FM music on an S3 ViRGE** -- the configuration most
machines will run, since a Sound Blaster supplies OPL3 and a WaveBlaster is
uncommon. The figure is **per-loop fps** (`per_loop_fps`), the same metric
`docs/BENCHMARKS.md` publishes. Do not judge the KPI on `fps_mean`
(wall-clock) -- it is a different number, ~5 fps lower on the POD, and the
targets were not set against it.

The KPI cells are `RB`'s `R4` and `R4B` (both OPL3). `R3` is Organya and `R5`
is AdLib -- neither is the KPI, whatever they measure.

Round M: POD-83 30.2 (met, no margin), DX2-66 22.2 (2.8 short).

50 fps stays the moonshot, not the bar.

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
- [ ] `already staged and current (sha 5874f9008c50)` or a re-fetch
- [ ] `PASS: all N BATs CRLF + ASCII`

A cache-key `WARN` means every Organya cell cold-renders. Stop, re-populate.

---

## Round N -- POD-83 diagnostics, ViRGE + PicoGUS

One machine, no card swaps, no CPU swaps. Everything runs on the POD-83.

**The binary has not changed since Round M.** Round 12 is a BAT-only repack,
so Round M's KPI anchor (30.2 per-loop) and its noise band still apply and
`RB` does not need re-running. That band is what makes `LEV` judgeable: the
POD's identical pair differed by 0.0-0.1 fps, so anything above ~0.5 fps in
this round is a real effect.

**Do not pass `PG` to anything below.** These sweeps now switch the PicoGUS
to SB mode themselves. That was the Round M defect: `MINE` ran straight after
`RB`, which hands back an AdLib-mode card, and both cells died at `sdl_init`
one second in without drawing a frame.

| Step | Type after boot | Time | Kind |
|---|---|---|---|
| N0 | *laptop*, populate | 6 min | payload r12 |
| N1 | `QA 1` then `MINE 1` | 12 min | diagnostic |
| N2 | `LEV 1` | 9 min | **fps rows** |
| N3 | `DEEP 1` | 8 min | diagnostic |
| N4 | *laptop*, logback `r12-pod-diag` | 3 min | |

Both *laptop* commands, CF mounted:

    scp claude:/tmp/install-qa-v163.sh /tmp/ && bash /tmp/install-qa-v163.sh
    scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh r12-pod-diag

**N0 repopulates.** Same binary and same cache key, so nothing cold-renders --
but the fixed BATs only reach the CF this way. Without it N1 fails exactly as
it did in Round M.

**N1 `MINE 1`** is the Round M re-run and the point of the round: the mode
layer and the flip body, neither ever measured on the KPI machine. NOT fps
rows -- the instrumentation costs a few percent. Report whether
`[mode-tick-stat]` shows `n` in the DOZENS and appears past block 1. If `n` is
2, or every block is title-screen, patch 0312 did not take on the fixed path
and the cell answered nothing.

**N2 `LEV 1`** is three fps rows: control, `ASM_BLIT`, `PERF_MODE`. Both
levers ship default-OFF and have never been A/B'd on this machine against a
known noise band. Cell 3 drops decorative foreground tiles and keeps collision
tiles -- **watch the screen and say whether anything looks wrong or missing**.
An fps gain that makes the game look worse is a product call, and it is yours.

**N3 `DEEP 1`** is the seven-layer decomposition, run on the DX2-66 in Round J
and never on the POD. Diagnostic, not an fps row.

Not in this round: the Mach64, which rejoins for a full matrix once the
backdrop defect is fixed, and the ~5.9 ms inter-flip remainder, which has no
instrumentation on any machine and needs a patch before any cell can see it.

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

Add `PG` to a **Mach64** sweep only if the PicoGUS is in. `ADIAG` `DEEP`
`FINE` `LEV` `TAB` `MINE` no longer take `PG` -- they switch it themselves.

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

| | Round 5 | Round 8 | Round 12 |
|---|---|---|---|
| Binary | `c1729d2fe065` | `6971e9f73bc9` | `02d91d3be589` |
| Tarball | `d49dccd49558` | `162fb8b55a4c` | `5874f9008c50` |
| Cache key | `482d88eba8b2` | `3ba36d8dd56d` | `a483f4b30d24` |

**Round 12 is a BAT-only repack of Round 11** -- same binary, same caches,
same cache key, so nothing cold-renders. It fixes the `PG` argument defect:
`ADIAG` `DEEP` `FINE` `LEV` `TAB` `MINE` now switch the PicoGUS to SB mode
unconditionally, the way `RB` always has. The old opt-in never fired -- every
manifest this campaign has written says "SB already in the box" -- so those
sweeps were working only by inheriting SB mode from an earlier sweep. Round M
scheduled `MINE` directly after `RB`, whose last act is `pgusinit /mode
adlib`, and both cells died at `sdl_init` one second in. Do NOT pass `PG` to
those six any more; it is ignored. The Mach64 sweeps keep the opt-in, because
a Mach64 round can legitimately run on a Vibra16, but now print a loud warning
naming this exact failure when `PG` is absent.

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
| M | all four CPUs, ViRGE | `QA` `RB` x2 per CPU; `MINE` FAILED | `r11-crosscpu` |

All labels above are spent. Nothing in Rounds A-M is to be re-run, except
`MINE`, which never ran: both its cells aborted at `sdl_init`.

Results and analysis live in `docs/internal/POST-BENCHMARK-PLAN.md` and
`docs/internal/HANDOFF-ENGINE-AUDIO-BUCKET.md`.
