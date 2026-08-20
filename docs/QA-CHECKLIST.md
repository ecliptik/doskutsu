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

- [ ] `PASS: DOSKUTSU.EXE e9e8ff80ae10`
- [ ] `PASS: Organya 11025 cache keyed 66ff01f7f997`
- [ ] `PASS: Organya-HQ 22050 cache extracted` -- not `SKIPPING` / `no HQ cache`
- [ ] `PASS: QA.TAS = benchmark reel (1956 B)`
- [ ] `PROFILE3.DAT: map 20 'Save Point', weapon 2` -- `PROFILE5.DAT` same
- [ ] `already staged and current (sha c3daea95f466)` or a re-fetch
- [ ] `PASS: all N BATs CRLF + ASCII`

A cache-key `WARN` means every Organya cell cold-renders. Stop, re-populate.

---

## Round P -- event-pump ablation, POD-83

ViRGE + PicoGUS. No new patch. ~23 min.
Rationale and expected reading: `docs/internal/ROUND-P-PUMP.md`.

| Step | Hardware | Type after boot | Time |
|---|---|---|---|
| P0 | laptop | populate | 6 min |
| P1 | ViRGE + PicoGUS | `QA 1 VIRGE PICOGUS` then `PUMP 1` | 14 min |
| P2 | laptop | logback `r16-pump`, send | 3 min |

    scp claude:/tmp/install-qa-v163.sh /tmp/ && bash /tmp/install-qa-v163.sh
    scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh r16-pump

### Before P1 -- check UniVBE is actually resident

    MEM /C | FIND "UNIVBE"

No line means it is not loaded, and the round is already dead: the ViRGE
ROM offers no 320x240 and no LFB, so the engine falls to 640x480 banked and
crawls. `UNIVBE.EXE` prints NOTHING when it declines -- silence is the
failure, not the success. Re-run `UVCONFIG.EXE` at the machine after any
card swap, then reboot.

### Cells

| cell | tag | arm |
|---|---|---|
| 1 | `GPU0` | control |
| 2 | `GPUA` | pump OFF |
| 3 | `GPU0B` | control, repeat |
| 4 | `GPUAB` | pump OFF, repeat |

### Scoreable only if

- [ ] `LOGS\GPUMP.NFO` carries `schema=harness-1`, `cells=4`, four `cell=` lines
- [ ] `video_declared=VIRGE` and `sound_declared=PICOGUS`, neither `UNDECLARED`
- [ ] `config=` names the booted profile
- [ ] `pgusmode_readback=begin` block present
- [ ] eight logs back: `<tag>.LOG` + `<tag>SDL.LOG` for all four tags
- [ ] every SDL log: `oem_string='Universal VESA VBE 6.70'`
- [ ] every SDL log: `has_lfb=1 use_lfb=1 banked=0`
- [ ] `PUMP DONE` banner captured

Any box unticked -- provisional, does not enter the matrix.

### Not broken

| | |
|---|---|
| audio wrong in cells 2 and 4 | correct -- the ablation starves the audio yield |
| a cell hangs or exits early | that IS the result -- report it, never retry |
| `NO_INPUT_POLL` still set after | `PUMP` clears it; if it was set by hand, clear it |

### vcctrl

    QA 1 VIRGE PICOGUS -> SET DKTCAP=1 -> PUMP 1 -> one key at the PAUSE

No prompt between cells; nothing may be typed mid-sweep. ~14 min, timeout 2x.
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
| `PUMP` | 4 | ~14 min | any card + PicoGUS, tags `PU0`/`PUA`/`PU0B`/`PUAB` |
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

| | Round 5 | Round 8 | Round 13 | Round 17 |
|---|---|---|---|---|
| Binary | `c1729d2fe065` | `6971e9f73bc9` | `e9e8ff80ae10` | `e9e8ff80ae10` |
| Tarball | `d49dccd49558` | `162fb8b55a4c` | `c3daea95f466` | `03885e45e662` |
| Cache key | `482d88eba8b2` | `3ba36d8dd56d` | `66ff01f7f997` | `66ff01f7f997` |

**Round 17 is a BAT-only repack of Round 16.** Same binary, same caches, same
cache key, so nothing cold-renders. It brings 22 BATs up to the harness
standard -- `schema=harness-1`, declared hardware, per-cell `class=`, PicoGUS
readback instead of the requested mode -- and repairs two defects still live
in the r16 kit: `REM  Usage: QA <n>` redirected inside a comment, and
`ECHO ... -^> SB MODE` used a caret that COMMAND.COM does not have, so it
printed half the line and created a file called `SB`. Both were in
`DEEP` `FINE` `TAB`. `QA` now takes the declared hardware: `QA 1 VIRGE PICOGUS`.

**Round 16 is a BAT-only repack of Round 13** (r14 and r15 were superseded
before reaching a card). It adds the `PUMP` sweep and records the CONFIG.SYS
boot profile (`config=%config%`) in every sweep manifest, so a log proves
which profile produced it rather than asserting it -- same binary, same caches,
same cache key `66ff01f7f997`, so nothing cold-renders. It adds the `DKTCAP`
capture flag to the eight non-listening sweeps (`RB` `WC` `MINE` `LEV` `DEEP`
`ADIAG` `TAB` `FINE`).

**`DKTCAP` is unset by default and unset behaves byte-identically to every
banked round** -- the 10 s banner delay still fires and no mode switch
happens. Only an explicit `SET DKTCAP=1` changes anything, and then the sweep
collapses the banner delay and returns the console to mode 12h between cells
so a VGA capture device does not black out. The switch also fires ONCE at
the top of the sweep, before the banner prints, so the `[DKTCAP=1]` line is
itself capturable -- printed into text mode 03h it would be invisible to the
only thing that needs to read it. Console text is SLOW in mode 12h
(planar, read-modify-write per glyph), which is the cost of the flag. Turn it
off with `SET DKTCAP=` or restore text mode with `VGACAP\MODE03`.

Do NOT set `DKTCAP` on the listening sweeps -- `VB` and the individual ear
cells are excluded on purpose, because the banner delay is how a human knows
which audio cell is playing, and audio cannot be captured at all.

`RB.BAT` is now shipped in the payload for the first time; previously it was
carried on the CF from an older payload and never replaced.

**Round 13 carries `0313` (bg-skip flicker fix), `0314` (world cache stage
3a) and `0315` (stage 3b tile-aligned key), all default-OFF, plus the new
`WC` sweep.** Adding those three patches moved the build sha from
`a483f4b30d24` to `66ff01f7f997`, and that key is baked into every cached
Organya PCM -- so BOTH cache tiers were re-rendered against the new binary.
A stale key would make every Organya cell cold-render, which is an abort.
Check the populate line says `66ff01f7f997`, not the old key.

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
| O | POD-83, ViRGE | `WC` (3 arms) `RB` | `r13-worldcache` |

All labels above are spent. Nothing in Rounds A-O is to be re-run.

Results and analysis live in `docs/internal/POST-BENCHMARK-PLAN.md` and
`docs/internal/HANDOFF-ENGINE-AUDIO-BUCKET.md`.
