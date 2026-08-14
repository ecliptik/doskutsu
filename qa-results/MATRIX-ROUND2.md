# Round 2 -- cross-CPU matrix and re-baseline verdict

Binary `fe44805fb603`, build fingerprint `1f79ce20e4ee`, reel `4118561edf26`,
saves `32529e291e0f` (map 20, Polar Star). Four CPUs, 68 gameplay cells, 68/68
complete route. S3 ViRGE and UniVBE 6.70 throughout; PicoGUS in SB mode for
`RB`/`EAR`/`PROVE`, Vibra16 for `GAP`.

Figures are `per_loop_fps` = `flips / 102`, computed identically to round 1.
Regenerate from [`ROUND2-CELLS.csv`](ROUND2-CELLS.csv) via `extract-cells-r2.sh`.

---

## The re-baseline holds, with one qualification

Round 1 remains comparable to round 2 for anything above ~2%. Below that it
does not, because the OPL3 control moved.

### The OPL3 control is systematically ~1% low

Each round-2 figure is the mean of five measurements of the same cell taken
across three different sweeps (`RB`, `PROVE`, `EAR`).

| CPU | Round 2 | Round 1 | Delta | | In-round spread |
|---|---|---|---|---|---|
| 486DX2-50 | 17.28 | 17.39 (`5C4`) | -0.11 | -0.6% | 0.21 |
| 486DX2-66 | 22.89 | 23.22 (`6C4`) | -0.33 | -1.4% | 0.08 |
| Am5x86-133 | 30.95 | 31.32 (`AC4`) | -0.37 | -1.2% | 0.15 |
| POD-83 | 30.84 | 31.38 (`GC4`) | -0.54 | -1.7% | 0.18 |

**Twenty measurements, every one below its anchor.** On three of four CPUs the
deficit is 2-4x the measurement spread, so it is not scatter. The round-2
binary is about 1% slower on the OPL3 path than the round-1 binary, and the
cause is not identified.

Read on the DX2-50 alone this looks like noise, which is what the first
analysis concluded. It took four CPUs to separate a systematic 1% from the
0.20 run-to-run floor.

**Consequence for banked data.** The 89 round-1 cells stay usable. Any
comparison at or below ~2% between a round-1 and a round-2 figure has to
account for this, and a round-2 figure that beats round 1 by less than 1%
is really beating it by more.

---

## The pump-aware timebase costs nothing and gains with clock speed

`SDL/0122`, measured directly by `PROVE`'s `P5` (fix ON) against `P5K`
(`PUMP_TIMEBASE=0`, fix OFF). Both arms in the same sweep, same boot.

| CPU | `P5` fix ON | `P5K` fix OFF | Gain |
|---|---|---|---|
| 486DX2-50 | 18.58 | 18.45 | +0.13 |
| 486DX2-66 | 24.81 | 24.30 | +0.51 |
| Am5x86-133 | 32.78 | 31.90 | +0.88 |
| POD-83 | 32.93 | 32.08 | +0.85 |

Monotonic with clock speed, then flat between Am5x86 and POD-83 -- which is
also where those two CPUs' OPL3 figures converge (30.95 and 30.84), so they
behave as one speed class on this workload, as round 1 also found.

The fix was written for measurement correctness, not speed. It turns out to be
worth 0.7-2.8% as well, most where the machine is fastest. The DX2-50 figure
alone sits inside that CPU's noise floor, which is why a single-CPU reading
called it fps-neutral.

---

## AdLib gained; Organya did not move

| CPU | AdLib vs round 1 | Organya vs round 1 |
|---|---|---|
| 486DX2-50 | +0.70 | +0.23 |
| 486DX2-66 | +1.31 | +0.61 |
| Am5x86-133 | +1.00 | -0.01 |
| POD-83 | +0.82 | -0.12 |

AdLib is up on every CPU, by more than the timebase A/B accounts for, so part
of that movement is still unexplained. Organya straddles zero with no pattern
and should be read as unchanged.

---

## Organya-HQ: the round-1 exclusions were wrong

`5112` and `6112` were excluded from round 1 as untrustworthy, on the grounds
that the DX2-50 appeared to beat the DX2-66 on identical work. Round 2 ran the
cell twice per CPU (`XH1`, `XH2`).

| CPU | Round 2 (two runs) | Round 1 | Round-1 status |
|---|---|---|---|
| 486DX2-50 | 14.13, 14.14 | 14.08 (`5112`) | **excluded** |
| 486DX2-66 | 13.29, 13.26 | 13.56 (`6112`) | **excluded** |
| Am5x86-133 | 20.35, 20.25 | 20.13 (`A112`) | kept |
| POD-83 | 20.45, 20.42 | 20.41 (`G112`) | kept |

**Every figure reproduces within noise, twice each, on a different binary.**
The measurements were never bad. What is misleading is the metric: on this cell
the HQ cost lands almost entirely in load-stall overhead rather than in loop
rate, and it does so more as the CPU slows.

| CPU | Organya overhead | Organya-HQ overhead | Per-loop change |
|---|---|---|---|
| POD-83 | 27 s | 60 s | -8.0 |
| Am5x86-133 | 34 s | 68 s | -7.9 |
| 486DX2-66 | 50 s | 101 s | -7.4 |
| 486DX2-50 | 70 s | 187 s | **-0.8** |

On the DX2-50 the loop barely slows while wall-clock overhead nearly triples.
Comparing per-loop across CPUs for this cell therefore compares two different
things, which is exactly what produced the impossible-looking ratio. The
exclusion should be reversed and both figures restored, with the caveat that
per-loop understates the HQ cost on slow CPUs.

---

## The audio floor

`X0` (`AUDIO_OFF=1`, from `GAP`) and `P0` (audio off, from `PROVE`) are the
first true floors measured in this campaign -- round 1 had none, because
`MUSIC_OFF`/`SFX_DEVICE=none` still open the device and feed it silence.

| CPU | `X0` | `P0` | Best audio-on cell | Cost of audio |
|---|---|---|---|---|
| 486DX2-50 | 18.09 | 18.34 | 18.59 (AdLib) | none measurable |
| 486DX2-66 | 24.17 | 24.49 | 24.88 (AdLib) | none measurable |
| Am5x86-133 | 32.60 | 32.52 | 32.54 (AdLib) | none measurable |
| POD-83 | 32.84 | 32.92 | 32.86 (AdLib) | none measurable |

**AdLib matches or beats the audio-off floor on every CPU.** That is not a
measurement error -- it reproduces across two independent cells per CPU -- but
it does mean the floor is not a floor in the expected sense, and the AdLib
path's cost is at or below the noise of the measurement. Worth understanding
before quoting either number as an audio budget.

---

## Cross-CPU scaling is unchanged

OPL3 control, round 2 against round 1:

| Pair | Round 2 | Round 1 |
|---|---|---|
| DX2-50 / DX2-66 | 0.755 | 0.749 |
| DX2-66 / POD-83 | 0.742 | 0.740 |
| Am5x86 / POD-83 | 1.004 | 0.998 |

Whatever moved the individual cells did not distort the relationships between
CPUs. The Am5x86 and POD-83 remain indistinguishable on this workload.

---

## The KPI

The target is 50 fps median on heavy-music gameplay. The best round-2 figure on
the reference machine is **32.93** per-loop (POD-83, AdLib, timebase fix on),
against a round-1 best of 31.54. The gap to the KPI is unchanged in character:
roughly 17 fps, and no CPU in the matrix closes it -- the Am5x86-133 does not
beat the POD-83 despite a 60% clock advantage.

---

## What this round does not answer

- **Why the OPL3 path lost ~1%.** Four CPUs agree it did. Nothing in the round
  identifies the cause.
- **Why AdLib gained more than the timebase A/B explains.**
- **Why AdLib meets or beats the audio-off floor.**
- **The Mach64.** Its one cell ran on the card's own VBE 2.0 BIOS rather than
  UniVBE, so nothing measured there is comparable to anything else. See
  `2026-08-13-r2-mach64/README.md`.
