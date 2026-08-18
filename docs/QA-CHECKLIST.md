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
- [ ] `already staged and current (sha bb7d4849bc39)` or a re-fetch
- [ ] `PASS: all N BATs CRLF + ASCII`

A cache-key `WARN` means every Organya cell cold-renders. Stop, re-populate.

---

## Round P -- the last unmeasured milliseconds, POD-83

One machine, ViRGE + PicoGUS, ~20 minutes. **No new patch and no new lever** --
this round measures something that has never been measured, using an ablation
already compiled into the binary.

| Step | Type after boot | Time |
|---|---|---|
| P0 | *laptop*, populate | 6 min |
| P1 | `QA 1` then `SET DOSKUTSU_NO_INPUT_POLL=1` then `RB` | 12 min |
| P2 | `SET DOSKUTSU_NO_INPUT_POLL=` -- **clear it, see below** | - |
| P3 | *laptop*, logback `r15-inputpoll`, send | 3 min |

Both *laptop* commands, CF mounted:

    scp claude:/tmp/install-qa-v163.sh /tmp/ && bash /tmp/install-qa-v163.sh
    scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh r15-inputpoll

### What this measures

The POD's frame is 32.8 ms in-loop. Round N accounted for all but **~3.9 ms**
of it -- tilemap 21.05, flip 4.55, sim 2.39, HUD and post 0.94. That remaining
12% has never been instrumented on any machine. Most of it is expected to be
`input_poll`, which runs ~1.67 times per frame and is unbracketed.

`DOSKUTSU_NO_INPUT_POLL=1` skips the event pump entirely. The fps gap against
the control is the total cost of that region, including the cooperative-
scheduler yield that happens inside it.

### The control is Round O, not a second run

Round O measured `R4` **30.3** and `R4B` **30.2** per-loop on this exact binary
(`e9e8ff80ae10`, unchanged in r15). Compare the ablated `R4`/`R4B` against
those. **Do not run `RB` twice in one round to get a local control** -- the
second run reuses the same log tags and overwrites the first.

### CLEARING THE VARIABLE IS NOT OPTIONAL

`DOSKUTSU_NO_INPUT_POLL` is **not** among the 204 variables `CLRENV` clears, so
once set it survives every cell and every later sweep in that boot. Leave it set
and everything you run afterwards is silently measuring an ablated game, with no
banner saying so. Clear it at P2, or reboot before running anything else.

### What to expect, and what is not a fault

- **Audio may glitch, stutter or drop out.** The event pump is where the
  SDL3-DOS cooperative scheduler yields, and skipping it starves the audio
  thread. Expected. This is an ablation to size a cost, never a shippable
  lever.
- The reel still drives input under TAS, so the run should complete normally
  despite the game being uncontrollable by hand.
- If a cell hangs or exits early instead of completing, that is itself the
  result -- report it rather than retrying.

**Do not bank these as fps rows.** They are an ablation arm, not a
configuration anyone would ship.

### Optional ride-along: confirm the capture flag (1 min)

Only if you care about the harness thread. At the prompt before P1:

    SET DKTCAP=1

then start any sweep and check the banner area prints a `[DKTCAP=1] capture
session:` line before the `PAUSE`. That line is the witness that r15 landed.
Then `SET DKTCAP=` and carry on -- with it set, console text goes slow and the
console switches to mode 12h between cells, which is correct behaviour but
unhelpful if nobody is capturing.

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

| | Round 5 | Round 8 | Round 13 |
|---|---|---|---|
| Binary | `c1729d2fe065` | `6971e9f73bc9` | `e9e8ff80ae10` |
| Tarball | `d49dccd49558` | `162fb8b55a4c` | `bb7d4849bc39` |
| Cache key | `482d88eba8b2` | `3ba36d8dd56d` | `66ff01f7f997` |

**Round 15 is a BAT-only repack of Round 13** (r14 was superseded before
it ever reached a card) -- same binary, same caches,
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
