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

## Round O -- world cache, three arms on one binary, POD-83

One machine, ViRGE + PicoGUS, no swaps. First fps read on the world cache.

**All three arms are in ONE binary.** That is deliberate: this campaign has
found cross-binary comparison unreliable, so the control and both cache arms
run on the same reel, same boot, same silicon.

| Step | Type after boot | Time | Kind |
|---|---|---|---|
| O0 | *laptop*, populate | 6 min | new payload |
| O1 | `QA 1` then `WC 1` | 9 min | **3 arms, fps + hit rate** |
| O2 | `RB` | 12 min | KPI re-anchor |
| O3 | *laptop*, logback `r13-worldcache` | 3 min | |

Both *laptop* commands, CF mounted:

    scp claude:/tmp/install-qa-v163.sh /tmp/ && bash /tmp/install-qa-v163.sh
    scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh r13-worldcache

**O0 repopulates** -- new binary carrying `0313`, `0314`, `0315`. All three
ship default-OFF, so the control arm should reproduce Round M.

### O1 `WC 1` -- the three arms

| cell | tag | hints set | what it is |
|---|---|---|---|
| 1 | `GW0` | none | uncached control |
| 2 | `GWA` | `WORLD_CACHE=1` | stage 3a, composites every frame |
| 3 | `GWB` | `WORLD_CACHE=1` + `WORLD_CACHE_KEY=1` | stage 3b, keyed |

**Cell 2 is EXPECTED TO BE SLOWER than cell 1.** It rebuilds the composite
unconditionally with no key and no possible hit, so it pays the compositing
cost twice over. That is what it is for -- it is the correctness arm. A cell
2 that is slower than cell 1 is the expected result, not a failure. The
comparison that matters is **cell 3 against cell 1**.

**The number to pull is the hit rate, and it is in the log, not the fps.**

    [world-cache n=100 hits=.. composites=.. declined=.. hit_pct_x10=.. bytes=..]

Break-even is a 0.33 hit rate (`hit_pct_x10=330`). Under DOSBox the smoke
measured 990-1000, but that route barely moves the camera and is NOT a
gameplay figure -- Round N measured 71% static frames on this reel, so
expect something in the 700-900 band. If `hit_pct_x10` comes back near 1000
on a reel that scrolls, be suspicious rather than pleased: it would more
likely mean the key is not invalidating than that the cache is perfect.

`declined=` should be small. A large count means the cache is standing down
(negative scroll on MAP_BORDER paths) and cell 3 is measuring almost
nothing.

### Watch the screen on cell 3

The DOSBox screenshots are byte-identical to the control, but that route is
mostly stationary, so the **invalidation paths are barely exercised**. These
are the cases that would expose a stale-tile bug, and the reel passes through
all of them:

- [ ] scrolling edges -- tiles must not smear, tear, or lag the camera
- [ ] a destructible tile being shot -- the hole must appear immediately
- [ ] a sprite walking behind foreground geometry -- must still be occluded
- [ ] room transitions -- no leftover tiles from the previous room

Any of those failing is a real defect and worth aborting the cell to report.
Every prior cache in this codebase produced a black-region or stale artifact
at some point (`0183`, `0143`/`0157`, wave-54's black valley), so treat a
clean run as evidence rather than assuming it.

### O2 `RB`

The binary changed, so the KPI anchor is re-checked. Cells `R4`/`R4B` are the
KPI pair. Expect `per_loop_fps` ~30.2 as in Round M; a drop means one of the
three new patches costs something even switched off, which would matter more
than anything the cache does.

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
| N | POD-83, ViRGE | `MINE` `LEV` `DEEP` | `r12-pod-diag` |

All labels above are spent. Nothing in Rounds A-N is to be re-run.

Results and analysis live in `docs/internal/POST-BENCHMARK-PLAN.md` and
`docs/internal/HANDOFF-ENGINE-AUDIO-BUCKET.md`.
