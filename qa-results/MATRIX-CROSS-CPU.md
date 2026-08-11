# Cross-CPU matrix -- POD-83 vs Am5x86-133 vs 486DX2-66

The point of the campaign. One g2k box, one motherboard, one CF, one 102 s
`QA.TAS` reel, binary `09e449c5a81d`. Only the CPU is swapped between lanes,
so bus speed, memory, video card and storage are held constant by construction.

| Lane | CPU | Sweep | Round |
|---|---|---|---|
| A / B | Pentium OverDrive 83 | `PG 1`, `VB 1` | `2026-08-10-POD83-picogus-v170-sfx/`, `2026-08-11-POD83-vibra-VB1/` |
| G | Am5x86-133 | `PG 2`, `VB 2` | `2026-08-11-Am5x86-full/` |
| C | 486DX2-66 | `PG 3`, `VB 3` | `2026-08-11-DX266-full/` |

`per-loop fps = flips / 102`, `overhead_s = render_s - 102`.

---

## The headline: a faster CPU stops buying render speed

The Am5x86-133 does **not** land between the POD-83 and the DX2-66. It lands
**on top of the POD-83**, across six card-matched cells, to within ~0.1 fps:

| Cell | Backend | POD-83 | Am5x86-133 | DX2-66 |
|---|---|---|---|---|
| `02` | AUTO | 30.51 / 23 | 30.51 / 28 | 22.40 / 44 |
| `16` | AUTO | 30.36 / 21 | 30.47 / 29 | 22.25 / 44 |
| `17` | WaveBlaster | 29.01 / 24 | 29.07 / 32 | 21.59 / 47 |
| `18` | OPL3 | 30.48 / 22 | 30.46 / 30 | 22.14 / 45 |
| `111` | Organya | 27.20 / 27 | 27.13 / 34 | 19.10 / 50 |
| `112` | Organya-HQ | 20.41 / 55 | 20.13 / 64 | 13.56 / 96 |

(All six are Vibra16 cells, so the sound card is matched -- see the correction
at the bottom for why that matters.)

The `C4` OPL3 anchor on the PicoGUS lane says the same thing: POD-83 **31.38**,
Am5x86-133 **31.32**, DX2-66 **23.22**.

Both machines run a 33 MHz bus on the same board. Going from an 83 MHz Pentium
to a 133 MHz 486 -- 60% more clock -- buys **zero** render fps. Within the 486
family alone, doubling the clock from 66 to 133 MHz buys only 1.35x, not 2x.

**The render loop is not CPU-bound on this platform above roughly 83 MHz
Pentium / 133 MHz 486. CPU is not the lever for the 50 fps KPI.**

### The overhead column is the discriminator

The two machines are *not* interchangeable, and that is what makes the reading
above more than a coincidence. Per-loop ties to within 0.1 fps; **overhead is
consistently 5-9 s worse on the Am5x86 in every single cell** (23->28, 21->29,
24->32, 22->30, 27->34, 55->64).

So the Pentium's advantage has not vanished -- it has moved. It shows up
entirely in the CPU-bound work (load stalls, audio-ring servicing) and not at
all in the render loop. If the tie on per-loop were merely a coincidence of
effective compute -- a Pentium at 83 being worth a 486 at 133 -- overhead
should tie as well. It does not, consistently, across six cells and both
sweeps.

That is the best-supported reading, not a proven one. What would settle it is a
machine on a **different bus speed**: lane H (486DX2-50) runs a 25 MHz bus
where all three lanes here run 33, so it is the one remaining cell that can
separate bus-bound from clock-bound. It was listed as optional; on this
evidence it is the most informative lane left.

---

## Full backend matrix (PicoGUS lane, ViRGE)

| Backend | POD-83 | Am5x86-133 | DX2-66 |
|---|---|---|---|
| GUS 14 voices | 32.0 / 47 | 31.90 / 45 | 24.19 / 53 |
| GUS 20 voices | 32.1 / 45 | 31.85 / 45 | 24.24 / 53 |
| GUS 32 voices | 32.3 / 48 | 31.84 / 46 | 24.21 / 55 |
| AdLib, SFX off | 32.2 / 18 | 31.75 / 18 | 24.19 / 25 |
| AdLib | 32.0 / 19 | 31.82 / 19 | 23.56 / 25 |
| music + SFX off | 31.8 / 22 | 31.56 / 28 | 23.78 / 42 |
| AUTO | 31.6 / 25 | 31.39 / 30 | 23.21 / 43 |
| OPL3, SFX off | 31.5 / 24 | 31.38 / 29 | 23.10 / 43 |
| **OPL3 (`C4` anchor)** | **31.38** / 22 | **31.32** / 27 | **23.22** / 43 |
| Organya | 28.5 / 27 | 28.23 / 32 | 20.04 / 48 |

**The backend ordering is identical on all three CPUs** -- GUS fastest loop,
then AdLib, then OPL3, then WaveBlaster, Organya last. A backend decision made
on one machine transfers to the others.

### AdLib's overhead advantage tracks CPU speed, not loop speed

Overhead penalty of OPL3 over AdLib: POD-83 **3 s**, Am5x86-133 **8 s**,
DX2-66 **18 s**.

Note the ordering: the Am5x86 ties the POD-83 on loop speed but pays nearly
three times the SB-ring overhead penalty. Backends holding an SB DMA ring pay
CPU-bound costs; AdLib omits `SDL_INIT_AUDIO` entirely and GUS's overhead is
bus-bound GF1 DRAM upload time, so neither scales the same way. **On anything
slower than the POD-83, AdLib's real advantage is in wall-clock, not fps.**

---

## Video card, all three CPUs

ViRGE minus Cirrus, per-loop:

| Cell | POD-83 | Am5x86-133 | DX2-66 |
|---|---|---|---|
| `C4` | -1.20 | -1.12 | -0.77 |
| `G51` | -1.44 | -1.49 | -0.73 |

The Cirrus banked-path penalty (`patches/SDL/0019`, sound -- see
`MATRIX-VIDEO-POD83.md`) is ~1.1-1.5 on both fast machines and roughly half
that on the DX2-66. Consistent with the DX2-66 being the only one of the three
still CPU-bound in the loop: a memory-side cost partly hides behind CPU time
there, and is fully exposed on the machines that are not CPU-bound. This is
independent support for the headline reading.

Launcher check repeats a third time: `AC4` 31.32 vs `AC4V` 31.27 = 0.05 fps on
the same card.

---

## Sound card costs ~1 fps -- now confirmed on three CPUs

Vibra16 against PicoGUS-SB, same CPU, OPL3:

| CPU | Vibra (`18`) | PicoGUS (`C4`) | Delta |
|---|---|---|---|
| POD-83 | 30.48 | 31.38 | -0.90 |
| Am5x86-133 | 30.46 | 31.32 | -0.86 |
| DX2-66 | 22.14 | 23.22 | -1.08 |

Three CPUs, consistent sign and magnitude, well outside the 0.22 fps noise
floor. Mechanism candidate remains SDL/0106's `16-bit-high-DMA` (Vibra) vs
`8-bit-low-DMA` (PicoGUS), still **confounded with the card itself**; one cell
with `force_8bit=1` on the Vibra separates them.

`dsp_ver` differs between lanes for the PicoGUS (4 on the Am5x86, 2 on the
other two) but the resolved DMA path is `8-bit-low-DMA` in all three, so it is
not a confound.

---

## Two corrections to this file's earlier version

**1. "Loop speed scales almost perfectly with the CPU" -- WITHDRAWN.** The
earlier version reported a per-loop ratio of ~0.74 across backends against a
66/83 = 0.795 clock ratio and read it as near-clock-linear scaling. That was a
**two-point fit**, and the third point refutes it: the Am5x86-133 has 60% more
clock than the POD-83 and identical per-loop fps. Scaling is strongly
sub-linear within the 486 family and flat above it. The ratio was real; the
mechanism read off it was not.

**2. The WaveBlaster row compared two different sound cards.** The earlier
cross-CPU table paired POD-83 `G17` (`is_sb16=0`, DreamBlaster on the
**PicoGUS** header) against DX2-66 `617` (`is_sb16=1`, **Vibra** header). The
provenance was noted in the DX2-66 round README and then not carried into the
comparison. Corrected here by using the Vibra-header `17` cell on all three
CPUs, which is card-matched.

Root cause is mechanical and is a live hazard, not a one-off: **`G17`/`17`
is written by both the `PG` and `VB` sweeps.** Running `VB n` after `PG n` on
the same CPU overwrites the PicoGUS-header WaveBlaster cell unless `LOGS\` is
pulled and cleared in between. That happened on the DX2-66 and again on the
Am5x86; the POD-83 kept both only because its `PG 1` round was logged back
before `VB 1` ran. **The PicoGUS-header WaveBlaster cell therefore exists for
POD-83 only**, and the campaign has no cross-CPU comparison for it.

Both corrections come from the same habit: reading a mechanism off a derived
number without checking what the cells actually were. The logs said so in both
cases.
