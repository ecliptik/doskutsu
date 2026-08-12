# QA round 2 -- automation BATs, hardware combos, and what each answers

Companion to `docs/QA-CELL-REFERENCE.md` (the cell index) and
`docs/TAS-BENCHMARKING.md` (how the reel harness works and how to read it).

Every cell here replays the same input reel (`QA.TAS`), so a cell measured on
one machine is directly comparable with the same cell on another. Nothing in
this directory needs a human to play the game; the reel drives it. Two of the
three BATs are fully unattended -- only `EAR.BAT` needs a person listening.

Copy these three BATs and this README into the kit alongside the existing
`PG.BAT` / `VB.BAT` / `C*.BAT` cells. They follow the same conventions:
CRLF line endings, `COMMAND /E:2048` self-relaunch, `CALL CLRENV` before
every cell, a `CFGS\*.CFG` copy per cell, and an 8.3-safe `DOSKUTSU_LOG_TAG`.

**Run `QA.BAT` first** to set `%QAM%` (which CPU is in the box). Every BAT
here refuses to run without it and tells you so.

---

## The reel -- use THIS one or nothing compares

`QA-ROUND1.TAS` in this directory is the exact input recording that produced
all 89 round-1 cells. **sha256 begins `4118561edf26b93a`, 1956 bytes.**

Every cell in every round must replay the same reel or the columns are not
comparable -- that is the whole basis of the campaign. Before a round, confirm
the CF's `DOSKUTSU\QA.TAS` matches that sha.

**The kit payload does NOT ship this reel.** Its `DOSKUTSU/QA.TAS` is a
fallback (`5c661a8723e5`) that no round-1 cell ever used; the round-1 reel is
an operator recording made with `RECORD.BAT` on the bench.
`install-qa-v163.sh` stashes a non-fallback `QA.TAS` off the CF and restores it
after extracting, so populating a card that already holds the take is safe.

**Populating a blank or replaced card is not.** It installs the fallback, and
the round then replays a different reel while every log looks entirely normal.
Nothing downstream would catch it. Either populate onto the card that already
carries the take, copy `QA-ROUND1.TAS` to `DOSKUTSU\QA.TAS` afterwards, or
bake it into the payload as the shipped reel.

**Do not re-record mid-campaign.** The superseded 2026-08-06 and v167 rounds
used a third reel (`30dd4b7050468eeb`, 2044 bytes), which is part of why their
numbers cannot be compared with round 1's.

---

## Lane instrumentation and log retrieval

Three scripts, tracked here as the source of truth. The copies under `/tmp/`
and `/home/claude/qa-v163-durable/` are deployment copies -- if they disagree
with these, these win.

| Script | What it does |
|---|---|
| `qa-instrument.py` | Adds the hardware banner, the `.NFO` lane manifest and unique WaveBlaster tags to `PG` / `VB` / `VID*` BATs in a given directory. Idempotent, preserves CRLF, backs up to `.BAK`. |
| `instrument-cf.sh` | Wrapper that applies the above to a mounted CF, then syncs and unmounts. Only needed to instrument a card in place; a kit re-populate already carries it. |
| `logback-qa.sh` | Pulls every cell log off the CF and ships it to `claude:/tmp/qa-v163/<label>/<MACH>/`. |

### What the instrumentation adds

Each sweep prints its hardware **before** the PAUSE, so the box can be checked
against the sweep before committing to a run:

    SWEEP : PicoGUS sweep (sb + gus + adlib)
    CELLS : 13 cells, ~50 min
    CPU   : Pentium OverDrive 83
    SOUND : PicoGUS -- DreamBlaster on the PicoGUS header
    VIDEO : S3 ViRGE

and writes `LOGS\<M><SWEEP>.NFO` recording `cpu`, `cpu_arg`,
`log_tag_prefix`, `sound` and `video`. **This is the only CPU witness the log
set has** -- no engine log records the processor, so without it a lane's CPU
rests entirely on which digit was typed. It is operator-declared rather than
probed, so treat it as making the declaration explicit rather than as
verification.

The WaveBlaster cell is `17P` in `PG` and `17V` in `VB`. They both wrote
`<M>17` before, so running `VB n` after `PG n` destroyed the PicoGUS-header
cell -- it happened on two CPUs before anyone noticed.

Validated under DOSBox-X with `ver=6.22`, `lfn=false`: banner renders,
`%QACPU%` expands, the `MKDIR LOGS` guard works, the `.NFO` is written with
CRLF, `PG 3` tags `6PG.NFO`, and `PG` with no argument creates nothing.

**Do not add `DATE /T` or `TIME /T` to a sweep BAT.** MS-DOS 6.22 does not
reliably support those switches, and an unrecognised switch can prompt --
hanging an unattended sweep at cell 1. Run order is recoverable from the
`[HH:MM:SS]` prefixes in the cell logs.

### Retrieving logs

    scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh <label>

**Label every pull.** The destination used to be a fixed
`claude:/tmp/qa-v163/<MACH>/`, and the script ships with `scp -r`, so a later
round silently overwrote an earlier one -- same cell names, same machine code,
merge, older file gone. It now ships to `claude:/tmp/qa-v163/<label>/<MACH>/`,
defaults to `run-<timestamp>`, and refuses to run if the destination exists.

Pull between sweeps as well as between rounds: two sweeps on one CPU share
cell names, and the second overwrites the first on the CF before any logback
runs.

---

## The prove-out run (do this FIRST after a new binary)

`PROVE.BAT` + `prove-verdict.sh` are the automated pair that answers "is this
binary good on real hardware" before anyone spends a full round on it.
6 cells, ~15 min, fully unattended, one run per hardware config.

**Requires the PicoGUS.** The sweep switches it `sb` -> `adlib` itself, because
the AdLib cells cannot run with a Sound Blaster hot: the PIT music pump is the
single owner of PIT ch0 and REFUSES to start when an SB device is live. On a
Vibra the pump never starts, both A/B arms come back identical, and the key
measurement silently measures nothing. It leaves the card in `adlib` mode.

    PROVE.BAT                       (on the box, after QA.BAT sets %QAM%)
    bash /tmp/logback-qa.sh prove-G (pull, labelled)
    tests/qa/prove-verdict.sh /tmp/qa-v163/prove-G/G/

The verdict script is the point. Collecting logs proves nothing; it checks
each CHANGE against its own witness and prints PASS / FAIL / SKIP per claim,
exiting non-zero on any failure. **SKIP is never a pass** -- an absent
witness is an absence.

| cell | proves |
|---|---|
| `P4` | control: OPL3, non-pump. Re-baseline anchor against the banked `C4`. |
| `P4B` | the noise floor, measured rather than inherited from an accidental duplicate. Every later "inside the noise" claim cites this number. |
| **`P5`** | **the timebase fix ON** -- 10-40 ms band populated, `pump_clock_state=pump-timebase-ok`, teardown `delta=0` on MODE 2. |
| **`P5K`** | **the same cell with the killswitch** -- that band must be EXACTLY EMPTY. |
| `P3` | Organya: catches a silently different PCM cache re-render. |
| `P0` | the true `AUDIO_OFF=1` floor (`SILENT.CFG` is not one). |

The Vibra card-vs-DMA-path check is `GAP.BAT` cell `X8`, not this sweep -- it
needs a different card, so it cannot share a run.

**`P5` vs `P5K` is the highest-information measurement in the set**, because
it is the only test DOSBox structurally cannot perform: DOSBox does not model
IRQ-0/PIT timing, and the MODE-2 latch read is the one part of the fix that
could behave differently on real silicon. Everything else in this sweep
confirms; that pair decides.

Run the whole sweep **once per video card** -- the DAC-width question needs
the Cirrus and the S3 readings as a pair, and one alone settles nothing.

---

## The three BATs

| BAT | Cells | Time | Needs ears | Answers |
|---|---|---|---|---|
| `RB.BAT` | 4 | ~10 min | no | Is the new binary still comparable to the ~90 banked cells? |
| `GAP.BAT` | 4 | ~10 min | no | Four measurement gaps the main matrix left open |
| `EAR.BAT` | 3 | ~8 min | **yes** | Two questions no log can answer |

### `RB.BAT` -- re-baseline after ANY new binary

Run this on each CPU after a new binary is installed, before trusting any
comparison against banked numbers. Four cells, and none is redundant --
each covers a different thing the new binary changed:

| cell | CFG | what it is there to catch |
|---|---|---|
| `R4` | OPL3 | **control.** Non-pump, so it isolates pure observer effect from the added telemetry. Compare directly against the banked `C4` anchor for this machine. |
| `R5` | AdLib | **the pump.** The timebase fix touches only `gus`/`adlib`. A set without this reports all-clear about cells it never exercised. |
| `R3` | Organya | **the re-rendered cache.** A new binary re-renders the Organya PCM rather than carrying it over; nothing else in the set touches it, so a silently different re-render would pass unnoticed and contaminate every Organya cell. |
| `R4B` | OPL3 | **noise floor.** Back-to-back repeat of `R4`. The spread between them IS the noise floor, and every "inside the noise" claim depends on having it. |

Reading it: `R4` matching its banked `C4` within the `R4`-vs-`R4B` spread
carries the whole existing matrix forward. If `R5` moves, that is a **finding
about the timebase fix** -- record it as such, do not fold it into a
re-baseline delta.

**Run with the PicoGUS installed**, since the banked `C3`/`C4`/`C5` anchors
come from the PicoGUS sweep. Comparing against them on different hardware
compares two things at once.

### `GAP.BAT` -- unattended gap cells

| cell | Runs on | Gap it closes |
|---|---|---|
| `X0` | every CPU | **The true audio floor.** `AUDIO_OFF=1` is the device-level off. The existing `SILENT.CFG` is NOT a floor -- `MUSIC_OFF`/`SFX_DEVICE=none` are dispatch gates, so the SB device still opens with its DMA ring live and fed silence. That is why AdLib measures *above* it. |
| `X8` | **Vibra only** | **Card vs DMA path.** The Vibra trails the PicoGUS by ~0.9-1.1 fps on all four CPUs, but it also runs 16-bit-high DMA where the PicoGUS runs 8-bit-low (twice the bytes per sample). Forcing 8-bit separates the two. Meaningless on other cards -- skip or ignore it there. |
| `XH1`, `XH2` | DX2-50 and DX2-66 | **Organya-HQ re-measure.** `5112` produced a DX2-50/DX2-66 ratio of 1.038 -- the slower CPU beating the faster one on identical work, which cannot be true -- and `6112` already held the lowest per-loop in the campaign. Two runs because the suspicion is non-reproducibility, not a wrong constant. This matters because ORGHQ is exactly the tier we intend to tell 486-class users to avoid. |

### `EAR.BAT` -- the two questions logs cannot answer

Run the three cells back to back so the comparison stays fresh, and write
the answers down immediately.

**Q1, the Shack.** Near the end of the reel there is a short visit to the
Shack (stage 14), about 6-11 seconds. Its music is song 8, `vivi`. Every cell
loads it successfully, so this is not a missing-file problem. What is not
known is whether Organya's music plays for the whole visit or arrives late.
On Organya the PCM cache read costs 2 s at 11025 mono and 5 s at 22050 HQ,
against that 6-11 s visit -- so "late" is expected and benign, while "never"
is a dispatch bug. Answer for `E4` (OPL3) and `E3` (Organya) separately.

**Q2, AdLib as the 486 default.** AdLib's overhead advantage grows as
machines get slower -- it leads OPL3 by 3 s of overhead on the POD-83 but
18 s on the DX2-66, and lane H showed it degrades *less* than
clock-proportionally. The fps case is strong. The open question is whether it
sounds acceptable, because AdLib is **music-only**: PC-speaker SFX, no DAC
SFX. Compare `EA` against `E4` by ear.

---

## Hardware combos -- what to install for which run

The campaign established that the card is worth about 1 fps and the video
card about 1.2, so **hardware must be stated with every number**. These are
the combos worth running, in value order:

| # | CPU | Sound | Video | Run | Why this combo |
|---|---|---|---|---|---|
| 1 | each of the four | PicoGUS | S3 ViRGE | `RB.BAT` | Re-baseline. ViRGE + PicoGUS is what the banked anchors used. |
| 2 | each of the four | PicoGUS | S3 ViRGE | `GAP.BAT` (`X0` only) | True audio floor per CPU. Item 3.2 predicts the floor moves a lot between CPUs. |
| 3 | POD-83 | **Vibra16** | S3 ViRGE | `GAP.BAT` (`X8`) | Card-vs-DMA-path. One cell decides a finding that currently spans four CPUs. |
| 4 | DX2-66 | Vibra16 | S3 ViRGE | `EAR.BAT` | Where the Shack observation was made, and the machine the 486-default decision is about. |
| 5 | DX2-50, DX2-66 | PicoGUS | S3 ViRGE | `GAP.BAT` (`XH1`/`XH2`) | Organya-HQ re-measure on the two suspect lanes. |
| 6 | POD-83 | PicoGUS | **Cirrus** | `RB.BAT` (`R4` only) | Reads the new DAC-width line on a second card -- see telemetry below. |
| 7 | POD-83 | PicoGUS | **Mach64** | `RB.BAT` (`R4` only) | Only after loading `M64VBE.COM`; see the Mach64 note below. |

Combos 1-3 are the ones that unblock plan items. Combos 4-7 are each a single
question.

### Mach64, if it is being retried

The Mach64 lane failed because the card's VBE offers **no mode smaller than
512x384**, so the engine drew its 320x240 screen into the top-left of a
larger surface. Before re-running anything, load ATI's `M64VBE.COM` instead
of UNIVBE and check `DOSVESA-CTRL` in the SDL log for a mode list containing
`0x01F8` (320x240).

**If 320x240 appears but is BANKED-ONLY**, stop and capture `win_gran` and
`win_size` from the `DOSVESA-MODESET` line before drawing any conclusion.
That configuration would be the first real-hardware exercise of SDL/0115's
`gran < size` bank walk, which shipped validated only as a no-op on hardware
where `gran == size`. If corruption appears there, A/B
`SDL_HINT_DOSKUTSU_BANK_GRAN_FIX=0` before blaming the VBE driver.

---

## Each BAT writes a hardware manifest

Before the `PAUSE`, every BAT here prints a hardware banner and writes
`LOGS\<M><SWEEP>.NFO` -- the same format the `PG`/`VB` sweeps now use, so one
parser reads them all:

    sweep=RB  cells=4  cpu=486DX2-66  log_tag_prefix=6
    sound=PicoGUS -- must match the banked C3/C4/C5 anchors
    video=S3 ViRGE

**Read the banner before pressing a key.** It states which CPU and cards the
run is supposed to be on, and the manifest records that claim next to the
logs. This is a declaration, not a probe -- it says what the operator
selected, not what the silicon is. It exists because the CPU was otherwise
identified only by a digit typed into a BAT, invisible in the logs
themselves. An engine-side measured witness is coming in the next binary;
when it lands, keep both and cross-check them, because a disagreement means
the wrong CPU arg was passed and every number in that run is mislabelled.

`GAP.BAT` deliberately says `sound=MIXED` -- only its `X8` cell requires the
Vibra, and running it on another card measures nothing.

## Logging discipline -- failures that already cost cells

**1. The `17`-tag collision is fixed at source.** `PG` now writes `<M>17P`
and `VB` writes `<M>17V`; previously both wrote `<M>17`, so running one after
the other silently destroyed the first -- which is what lost the
PicoGUS-header WaveBlaster cell on the DX2-66 and the Am5x86. The BATs here
use fresh tags (`R*`, `X*`, `E*`) that collide with nothing except a previous
run of themselves.

**2. Pull logs with a label.** `logback-qa.sh` now takes one and refuses to
run if the destination already exists, because its old fixed path would have
let a round-2 pull silently overwrite round-1 files of the same name:

    scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh round2-rb

It collects `*.NFO` as well as `*.LOG`, so the manifests above come back with
the logs rather than being left on the CF.

**3. Bank the logs the same day.** They land in `/tmp` on the analysis side,
which is tmpfs -- a reboot loses them. Five cells sat unreviewed for a day
once and were nearly lost.

## Reading the `DOSVESA-DACWIDTH` line (the Cirrus-vs-S3 colour question)

Every mode set now logs one line to the SDL channel:

    DOSVESA-DACWIDTH: 4F08.get ax=0xNNNN supported=D width_bits=W writer_assumes=6

The palette writer always emits 6-bit values (0-63). This line reports the
width the card's DAC is actually in. **Read it as a number, not from a
photograph** -- photo colour has already misled this project once, and this
readout is the evidence.

Per card:

| reading | meaning |
|---|---|
| `supported=1 width_bits=6` | HEALTHY. DAC matches our writes; palette correct on this card. |
| `supported=1 width_bits=8` | **SMOKING GUN.** DAC spans 0-255 but we feed 0-63, so colour covers only the bottom quarter of the range -- visibly washed out on this card. |
| `supported=0`, `width_bits=-1` | NO DATA. The BIOS does not implement 4F08. VBE says 6-bit is the post-modeset default so 6 is assumed, but unverified. This is an absence, not a pass. |

Verdict, with both cards run in the same configuration:

- **Both 6** -- the software palette is identical on both, so the DAC is not
  the cause. Fall back to the null hypothesis: analog output and monitor
  auto-gain differences after a physical card swap.
- **One 8, one 6** -- CONFIRMED. Consistency check that must hold: the card
  that LOOKS worse has to be the `width_bits=8` one. If the readout and the
  eye disagree, stop and re-check the run rather than believing either. The
  fix is then one line (request 6-bit at mode set, or scale the writes to the
  reported width) as a follow-up patch.
- **Both `supported=0`** -- 4F08 cannot answer it; inconclusive.

**Why this outranks a colour curiosity:** if either card has been running an
8-bit DAC while receiving 6-bit values, then every past Cirrus-versus-S3
visual comparison this project has made was between two different effective
palettes, so prior "looks the same" and "looks worse" judgements need
re-reading once both widths are known. It is a claim about work already
banked.

DOSBox-X reports `supported=1 width_bits=6` and therefore cannot exhibit the
fault. The per-card real-hardware pair is the entire deliverable.

## If you write more BATs

`DATE /T` and `TIME /T` are not reliably supported on MS-DOS 6.22, and an
unrecognised switch can PROMPT -- which would hang an unattended sweep at
cell 1. Do not put them in a manifest block. Run order is recoverable from
the `[HH:MM:SS]` prefixes in the cell logs anyway.

---

## New telemetry landing in the next binary

The next binary populates the `RUNMANIFEST` block that every cell already
emits but has so far left mostly empty. After it lands, most of what this
README asks you to reason about comes straight out of the log:

| field | what changes |
|---|---|
| `binary_sha12` | Currently carries the **Organya cache key**, not the binary hash. Becomes the real binary hash, with `organya_cache_key` as its own separate field. Both matter; they are different things. |
| `fps_p50` / `fps_p95` | Currently `NA`. Populated -- plus `per_loop_fps` and `overhead_s` emitted directly, so the two numbers every conclusion rests on stop being hand-computed. |
| `inter_flip_ms` | Emits `INVALID_PUMP` on `gus`/`adlib` cells instead of a plausible-looking number. Those values are corrupted by the PIT pump and quoting them has already cost two published findings. |
| CPU / speed class | **New.** Nothing currently witnesses which CPU ran a cell -- video and sound are both provable from the log, but the CPU rests entirely on the digit typed into the sweep BAT. |
| per-stage fps | **New.** Whole-reel averages and heavy-scene numbers are not interchangeable; this makes both readable from one run. |
| DAC palette width | **New.** VBE `4F08` readout per card. If one card runs an 8-bit DAC while the engine writes 6-bit values, its picture is a quarter as bright -- and every cross-card visual comparison so far compared two different effective palettes. |
| song changes | Promoted from DEBUG to INFO with a tick index, which makes the Shack question above answerable from the log in future rounds. |

Once that binary is in, `tools/qa-analyze.sh` reads a round directory and
produces the numbers directly, with loud gates on route truncation, on
quoting a pump cell's `inter_flip` median, on mixed binary/cache/reel hashes
inside one round, and on a tag appearing twice with different hardware.
