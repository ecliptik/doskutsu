# Cross-CPU matrix -- POD-83 vs 486DX2-66

The point of the campaign. Lane A (`PG 1`, POD-83) and lane C (`PG 3`,
486DX2-66) ran the same 102 s `QA.TAS` reel on the same binary
`09e449c5a81d`, the same PicoGUS, and the same S3 ViRGE. **CPU is the only
variable.**

Sources: `2026-08-10-POD83-picogus-v170-sfx/` and `2026-08-11-DX266-full/`.
Lane C banked 22 cells, **22/22 full route** -- the cleanest round of the
campaign.

Decomposed per `MATRIX-POD83.md`: `per-loop fps = flips / 102`,
`overhead_s = render_s - 102`.

## The anchor rows

| Backend | POD-83 per-loop | DX2-66 per-loop | Ratio | POD-83 overhead_s | DX2-66 overhead_s |
|---|---|---|---|---|---|
| GUS 32 voices | 32.3 | 24.2 | 0.75 | 48 | 55 |
| GUS 20 voices | 32.1 | 24.2 | 0.75 | 45 | 53 |
| GUS 14 voices | 32.0 | 24.2 | 0.76 | 47 | 53 |
| AdLib, SFX off | 32.2 | 24.2 | 0.75 | 18 | 25 |
| AdLib | 32.0 | 23.6 | 0.74 | 19 | 25 |
| music + SFX off | 31.8 | 23.8 | 0.75 | 22 | 42 |
| AUTO | 31.6 | 23.2 | 0.73 | 25 | 43 |
| OPL3, SFX off | 31.5 | 23.1 | 0.73 | 24 | 43 |
| **OPL3 (`C4` anchor)** | **31.4** | **23.2** | **0.74** | 22 | 43 |
| WaveBlaster | 30.1 | 21.6 | 0.72 | 24 | 47 |
| Organya | 28.5 | 20.0 | 0.70 | 27 | 48 |

## Two findings, and they are different in kind

**1. Loop speed scales almost perfectly with the CPU, and the backend ranking
is CPU-invariant.** Every ratio lands in 0.70-0.76 against a 66/83 = 0.795
clock ratio, so per-loop fps is close to clock-linear with a small extra
penalty for the slower memory system. More useful: the *ordering* of backends
is identical on both machines -- GUS fastest loop, then AdLib, then OPL3, then
WaveBlaster, with Organya last and worst by a clear margin. An audio-backend
decision made on one CPU transfers to the other.

**2. Overhead does not scale uniformly, and the split is mechanistically
clean.** Comparing overhead growth POD-83 -> DX2-66:

| Backend group | Overhead growth | Reading |
|---|---|---|
| AdLib (no `SDL_INIT_AUDIO` at all) | 19 -> 25 (+6) | barely moves |
| GUS (GF1 `.pat` DRAM uploads) | 45-48 -> 53-55 (+8) | barely moves |
| OPL3 / WB / Organya / AUTO (SB DMA ring live) | 22-27 -> 43-48 (**+21**) | roughly doubles |

The backends that keep an SB DMA ring running pay an overhead cost that nearly
doubles on the slower CPU, while the two that do not -- AdLib, which omits
`SDL_INIT_AUDIO` entirely, and GUS, whose overhead is GF1 DRAM upload time --
are almost CPU-insensitive. That is the expected signature if SB-ring
maintenance is CPU-bound work and GF1 uploads are bus-bound: the bus does not
get faster when the CPU does.

Consequence: **AdLib's advantage grows on slower silicon.** On the POD-83 it
leads OPL3 by 0.6 per-loop fps and 3 s of overhead; on the DX2-66 by 0.4
per-loop and **18 s** of overhead. On 486-class hardware the backend choice is
worth far more in wall-clock than the per-loop numbers alone suggest.

## Video card, cross-CPU

`6C4V`/`6C4C` and `651V`/`651C` repeat lanes D/E on the DX2-66.

| Cell pair | POD-83 ViRGE - Cirrus | DX2-66 ViRGE - Cirrus |
|---|---|---|
| `C4` | -1.20 | -0.77 |
| `G51` | -1.44 | -0.73 |

The Cirrus banked-path penalty (`patches/SDL/0019`, see
`MATRIX-VIDEO-POD83.md`) is **smaller on the slower CPU**, which is the right
sign: when the loop is more CPU-bound, a memory-side cost is a smaller share of
the frame. It is a real bandwidth-side cost, not an artifact.

The launcher check repeats too: `6C4` 23.22 vs `6C4V` 23.19 = 0.03 fps on the
same card. The `VID*` BATs are not a variable on either machine.

## Where this does NOT reconcile with the historical figures

`CURRENT-STATE.md` carried ~33 fps POD-83 and ~19 fps DX2-66 from the v1.0.1
cross-CPU round, a ratio of 0.58 against the 0.74 measured here. It is
tempting to read that as "the DX2-66 got 24% faster". **Do not.** These are
not the same measurement:

- Historical is a scene-specific p50 at Mimiga `BK_PARALLAX`, a heavy parallax
  backdrop. These are whole-reel figures across the full route, which includes
  much lighter scenes.
- A whole-reel median flatters a slow CPU more than a fast one, because the
  slow CPU suffers disproportionately in the heavy scenes the reel averages
  away. So the ratio is expected to look better here even with no real change.

The DX2-66's own numbers bracket it: reel median **23.8 fps**, reel p95
**15.2 fps**. The historical 19 sits between them, exactly where a heavy-scene
figure should sit. Nothing here contradicts the old number and nothing here
confirms a gain.

**Use the internal comparison, not the cross-round one.** Same reel, same
binary, same metric, one variable -- that is what the 0.74 ratio rests on.
Any claim that 486-class performance improved since v1.0.1 needs the v1.0.1
binary replayed against this reel, which no one has done.

## Secondary: `AUTO` does not select the WaveBlaster

`616` (DX2-66) and `G16` (POD-83) both initialise **`opl3`**, on machines where
the DreamBlaster is demonstrably present and working -- `617` and `G17` both
report `audio backend: wb`, MPU-401 found at 0x0330, cold-init succeeded, and
dispatch bytes climbing.

`QA-CELL-REFERENCE.md` poses this cell as "auto-detect -- **should** pick the
WaveBlaster. Does it?" The answer as configured is no, but the cell cannot
settle the question it asks: `616` logs `config: loaded DOSKUTSU.CFG (1 keys)`,
so the run is falling through to the *engine's* built-in default, which has
been OPL3 by design since wave 46 patch 0139. It never consults SETUP's
detection.

So this measures the engine default, not auto-detect. To answer the intended
question the cell needs a SETUP-generated CFG, which makes it dependent on the
deferred hands-on SETUP walk. Re-scope the cell or drop the claim from the
reference.

## Secondary: Organya-HQ is not viable on a 486

`6112` (ORGHQ, 22050 stereo): **13.6 per-loop fps, 96 s overhead** -- by far
the worst cell in the campaign on either machine, against 19.1 / 50 for the
same content at 11025 mono (`6111`). Consistent with the ~89 ms/flip figure
recorded when the HQ tier shipped in v1.3.0. Correctly default-off; nothing to
fix, but worth a line in the docs if the tier is ever surfaced in SETUP on
486-class hardware.

## Metric hygiene for this round

`622`, `623A`, `623B`, `641`, `6C5`, `662` run the PIT/IRQ-0 music pump and
their `inter_flip` medians are corrupt. Only `[fps-true]` is quoted for those
cells. Every other cell is pump-free and its median is reported in
`2026-08-11-DX266-full/` as an independent cross-check; where both metrics
exist they agree on the ratio to within 0.02.
