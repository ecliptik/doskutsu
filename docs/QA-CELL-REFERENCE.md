# QA cell reference -- what to run, and what each cell measures

Companion to `docs/TAS-BENCHMARKING.md`, which covers *how* the TAS harness
works and how to diagnose it. This file is the operator-facing index: the
sweeps, the individual cells, and the hardware lanes the campaign covers.

Every cell replays the same input reel (`QA.TAS`), so a cell measured on one
machine is directly comparable with the same cell on another. See
`docs/TAS-BENCHMARKING.md` for why that holds and what breaks it.

---

## The two sweeps

Both are unattended after one keypress, switch configuration themselves, and
end each cell when the reel does. `n` is the CPU (see below).

### `PG n` -- PicoGUS, 10 cells, ~22 min

Drives the card through `sb -> gus -> adlib` via `pgusinit`, no reboots.

| # | Cell | CFG | Measures |
|---|---|---|---|
| 1 | `C3` | ORGANYA | Organya music; cave-transition stutter, music continuity |
| 2 | **`C4`** | OPL3 | **the fps anchor row** -- the cross-CPU comparison |
| 3 | `G31` | PGSB | PicoGUS-SB 8-bit DMA path; mono SFX, cave screech |
| 4 | `G51` | AUTO | video-card render witness: boot, title, backdrop scroll |
| 5 | `G17` | WB | WaveBlaster MIDI, DreamBlaster on the **PicoGUS** header |
| 6 | `G22` | GUS | GF1 wavetable, 20 voices |
| 7 | `G23A` | GUS14 | GF1, 14 voices -- instruments dropping? |
| 8 | `G23B` | GUS32 | GF1, 32 voices -- better or worse than 14? |
| 9 | `G41` | ADLIB | OPL-only: FM music + PC-speaker SFX |
| 10 | `C5` | ADLIB | same CFG as `G41`; second sample for repeatability |

`C4` and `G31` load byte-identical configs (`OPL3.CFG` == `PGSB.CFG`), as do
`G41` and `C5`. Treat each pair as two samples of one row, not two rows. The
value of the duplicate is a by-ear check and a read on run-to-run noise.

### `VB n` -- Vibra16, 6 cells, ~13 min

One card, no mode switching.

| # | Cell | CFG | Measures |
|---|---|---|---|
| 1 | `G02` | AUTO | cold-boot baseline to title, daily-driver config |
| 2 | `G16` | AUTO | the engine's default backend selection (see the note below) |
| 3 | `G17` | WB | WaveBlaster MIDI via the **Vibra's** own header |
| 4 | `G18` | OPL3 | the SB16's own OPL3 -- the orgmid2 witness |
| 5 | `G111` | ORGANYA | Organya 11025 cache-hit |
| 6 | `G112` | ORGHQ | Organya-HQ 22050 stereo |

`G17` appears in both sweeps deliberately: the DreamBlaster sits on the
PicoGUS header in `PG` and the Vibra's in `VB`, and those are different code
paths in the SDL layer. **The DreamBlaster has to physically move with the
lane, or `G17` silently measures nothing.** Confirm from the log: it must say
`audio backend: wb`.

Both sweeps also write the tag `G17`/`17`. Pull and clear `LOGS\` between
them, or the second run overwrites the first. This has already destroyed the
PicoGUS-header cell on two machines.

`G16` does not test SETUP's hardware auto-detection. With no
SETUP-generated CFG present, the cell falls through to the engine default,
which has been OPL3 since wave 46, and never consults detection at all --
`G16` and its cross-CPU counterparts all initialise `opl3` while the
neighbouring `G17` proves the DreamBlaster is present and working. Reading it
as an auto-detect result is a mistake; testing detection requires a
SETUP-generated CFG.

Organya-HQ (22050 stereo) costs about a quarter of the frame rate, and it does
so on the reference machine, not only on the 486s: POD-83 measures 27.20
per-loop on `G111` against 20.41 on `G112`, a 25% drop, with load-stall
overhead rising 27 s to 55 s. The Am5x86 pair agrees (27.13 -> 20.13). The
486-class HQ figures are not quotable -- `5112` scored higher than `6112`,
which cannot be true of identical work, and both are excluded from round 1
pending a re-measure.

---

## The CPU argument

| `n` | CPU | Log tag |
|---|---|---|
| 1 | Pentium OverDrive 83 | `G..` |
| 2 | Am5x86-133 | `A..` |
| 3 | 486DX2-66 | `6..` |
| 4 | 486DX2-50 | `5..` |

The argument is the **only** thing that decides which column a result lands
in. `PG 1` writes `GC3 GC4 G31 G51 G17 G22 G23A G23B G41 GC5`; `PG 3` writes
the same cells as `6C3 6C4 631 ...`.

---

## Cells not in either sweep

Hands-on, run individually -- these need a human and cannot be automated.

| Cell | What |
|---|---|
| `G11` | SETUP Express: detect, profile, video bench, SAVE |
| `G12` | SETUP Sound menu UX: pickers always open, bad combos WARN not block |
| `G13` | SETUP audio tests + Organya preview (real title theme, no 5 s loop) |
| `G14` | SETUP save-and-exit: no auto-launch, no stale SETs afterwards |
| `G15` | SETUP input remap: rebind, verify in game, restore |
| `G115` | Joystick: calibrate, axis + buttons + invert-Y + keyboard coexist |
| `G116` | Save / Load / quit-to-DOS soak |
| `G117` | Free play stability soak |
| `C1` | Env probe -- prints the environment, no game launch |
| `C2`, `C7`, `C8` | SETUP Express / save-load / short play, `C`-numbered variants |
| `RECORD` | Re-record the campaign reel (see the warning below) |

Optional extras on the card but walked by nothing: `G19` (OPL3 + wiimidi A/B),
`G110` (AdLib on the SB16's OPL3), `G113` (no music), `G114` (no SFX), `G25`
(GUS with `SFX_DEVICE=none`), `C6`, `G52`.

**Do not re-record mid-campaign.** Every CPU must replay the same reel or the
columns are not comparable.

---

## Hardware lanes

A lane is one hardware combination. Cells are identical across lanes; only the
hardware differs, which is what makes the comparison meaningful.

| Lane | CPU | Sound | Video | Run | Scope |
|---|---|---|---|---|---|
| A | POD-83 | PicoGUS | S3 ViRGE | `PG 1` | full, 10 cells |
| B | POD-83 | Vibra16 + DreamBlaster | S3 ViRGE | `VB 1` + hands-on | full |
| C | 486DX2-66 | PicoGUS | S3 ViRGE | `PG 3` | full -- pairs with A |
| D | POD-83 | PicoGUS | S3 ViRGE | `VIDV 1` | video only, 2 cells |
| E | POD-83 | PicoGUS | **Cirrus CL-GD5430** | `VIDC 1` | video only, 2 cells |
| F | POD-83 | PicoGUS | **ATI Mach64** | `VIDM 1` | video only, 2 cells |
| G | Am5x86-133 | PicoGUS | S3 ViRGE | `PG 2` | optional, full |
| H | 486DX2-50 | PicoGUS | S3 ViRGE | `PG 4` | optional, full |

**A vs C is the point of the campaign**: same card, same modes, same reel, CPU
the only variable.

### Video lanes have their own BATs

Each video card gets its own launcher, so nothing collides and the log name
says which card produced it:

| Command | Video card | Logs |
|---|---|---|
| `VIDV n` | S3 ViRGE | `<M>C4V`, `<M>51V` |
| `VIDC n` | Cirrus CL-GD5430 | `<M>C4C`, `<M>51C` |
| `VIDM n` | ATI Mach64 | `<M>C4M`, `<M>51M` |

`n` is the CPU, same as everywhere else, and `<M>` is its tag -- so `VIDC 1`
on the POD-83 writes `GC4C.LOG` and `G51C.LOG`. Every combination of CPU and
video card lands on a distinct filename, well inside 8.3 (`GC4CSDL.LOG` is
seven characters).

Two cells each, ~10 min:

- **`C4`** -- the fps anchor, the same OPL3 config every other lane uses, so
  the number sits directly beside lanes A and C in the matrix.
- **`G51`** -- the render witness: boot, title, backdrop scroll, on AUTO.

A video card changes rendering, not audio, so re-running the whole audio
matrix per card would measure the same sound nine more times for nothing. Each
BAT puts the PicoGUS into sb mode itself, exactly as the full sweeps do.

**Keep everything except the video card identical**, or it is not a
video-card comparison.

**`VIDC` doubles as a bridge.** The project's historical figures (~33 fps
POD-83, ~19 fps DX2-66) were measured on the Cirrus CL-GD5430 while this
campaign runs the ViRGE. `VIDC 1` ties the new matrix to those older numbers
instead of comparing across different video hardware. Lane A measured 32.3 fps
median on the ViRGE, so they look close -- `VIDC` is what makes that a fact.

Running `VIDV` as well as the full `PG` sweep is not redundant: it gives the
ViRGE a `C4` measured under identical conditions to the Cirrus and Mach64
runs, which is a cleaner three-way comparison than reading the ViRGE number
out of a different launcher.

---

## Reading a result

Each cell log stands alone: `inter_flip_ms=` per frame (median and p95
framerate), `>> Entering stage N` (the replayed route), the audio backend that
actually initialised, `dur=`, and any warnings.

**Check the route before trusting a framerate.** A cell whose route is a
strict prefix of the reference was cut short and measured less work than a
complete one. Since patch 0286, logs also carry
`tas: stall frame N ms -> contributing 1 tick` for every load discounted from
the tick stream -- useful for correlating truncation against stalls.

Raw logs are kept in `qa-results/<date>-<cpu>-<card>/` with the reel that
drove them.
