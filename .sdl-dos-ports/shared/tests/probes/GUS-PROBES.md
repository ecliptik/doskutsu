# GUS standalone probe suite (Campaign 3, bug #39)

Standalone DJGPP DOS test utilities for the native Gravis GF1 (PicoGUS) audio
path. Pure DJGPP -- **no SDL, no NXEngine, no C++**. They let the operator
binary-search the GF1 init delta on g2k via fast CF-swap, WITHOUT the whole
engine and WITHOUT the diag-wedge risk that on-card GF1 port-READS during engine
song-load cause.

Author: probe-suite (task #4). Sources are gitignored under `tests/probes/`.
Build: `make probes-gus` (or `make gustone` / `make gusdet` / `make gusdump`).
Binaries land in `build/probes/`. Bundle alongside `CWSDPMI.EXE` in the iter
tarball (realhw owns packing).

## Why this suite exists

Our GF1 driver plays correctly in DOSBox-X but was TOTAL SILENCE on the real
PicoGUS. Four g2k fix-iters on doc-derived guesses (reap, double-write, vol-ramp,
0x4C IRQ-bit) all failed. The research convergence (tasks #1/#2/#3) points at a
**root cause the emulator structurally cannot reproduce**:

> The driver default of **28 active voices** programs the GF1 DAC output rate to
> `617400 / 28 = 22050 Hz`. The PicoGUS PCM510xA DAC is **silent at exactly
> 22.05 kHz** on ~10% of cards (a hardware dead-zone). DOSBox-X has no real DAC,
> so it plays our stream regardless and never reproduced the silence.

This suite isolates that hypothesis (and the other init knobs) into a ~200-line
binary the operator can A/B by ear in one trip -- no engine, no SDL.

## Operator setup (g2k, every cell)

PicoGUS v2.0, firmware `picogus-gus`, jumpers IRQ7 + DMA3:

```
SET ULTRASND=240,3,3,7,7
pgusinit /mode gus
pgusinit /gusdma 12
```

Positive control: **Gravis MIDIDEMO is already known-audible** on this card/boot
-- if a probe is silent but MIDIDEMO plays, the card + DAC + line-out all work
and the difference is in what the probe programs (or the dead-zone).

## Hard safety rule (diag-wedge)

The PicoGUS WEDGES when a GF1 port-READ is concurrent with an active voice, or
under a hot read loop / heavy logging during song-load (4 separate wedges
observed -- `memory/gus_campaign_picogus_diag_wedge.md`). Every probe here obeys:

- **No GF1 port-READS while a voice is active.** GUSTONE's only reads are the
  optional pre-play DRAM verify (`RDV=1`), done BEFORE the voice starts.
- Reads are **bounded single-pass**, never a hot loop.
- PIO upload bursts 64 bytes per cli/sti window (never holds cli across a long
  ISA stall, the documented wedge mode).
- Per-stage progress is flushed to the `.LOG` + stdout BEFORE each port group,
  so a wedge leaves the last-completed stage on disk.

## The tools

### GUSTONE.EXE -- PRIMARY (the output-rate dead-zone hunt) -> GUSTONE.LOG

Minimal by-the-book GF1 init (mirrors the PROVEN-in-DOSBox command stream of
`vendor/SDL/src/audio/dos/SDL_dosaudio_gus.c`): one-time bring-up (reset ->
IRQ/DMA latch -> terminal reset `0x4C` -> mix-control `P2X0` written LAST), then
per cell: program voice-count `0x0E` (= the DAC rate) -> upload a short
square/sine to DRAM via PIO (per-byte address re-select; PicoGUS does NOT
auto-increment) -> play ONE voice. Volume is driven through the GF1 **ramp
engine by DEFAULT** (`VOL=ramp`, byte-identical to the driver's gus-14
`SetVoiceVol`): a real GF1's volume DAC tracks the ramp accumulator, so a
ramp-stopped direct `0x09` write never reaches the DAC and the voice runs SILENT
(g2k-confirmed: TONE8/TONE16 ran at max-vol yet silent while MIDIDEMO, which
ramps, played on the SAME card). `VOL=direct` flips back to that silent A/B
baseline; leave the default for a LOUD, ear-judgeable sweep. Pan `0x0C` centered.

**THE LEVER is output rate via voice count `0x0E`.** The team converged (PicoGUS
firmware read + canonical Linux/SDK driver + driver audit) that most of the
timing knobs -- reset double-write gap, 100-vs-160us settle, mixctrl write order,
the `0x8F` ready-poll -- are a **no-op for audibility on the PicoGUS firmware**.
The single thing that mutes the card by RATE is the DAC dead-zone at 22.05kHz
(= 28 voices), so GUSTONE centers on the **rate sweep**. NOTE the volume PATH is
NOT in that no-op set: g2k proved the ramp-stopped direct-`0x09` write is silent
on a real GF1 (gus-14), so GUSTONE defaults to the ramp path -- otherwise the
working-rate cells come out faint and the dead-zone cell can't be told apart by
ear. The remaining timing knobs stay only as cheap completeness flags.

> NOTE: GUSTONE does **not** poll reg `0x8F` (reset-ready). The firmware read
> confirmed that poll is a no-op AND its read is destructive + takes the RP2040
> audio critical section -- i.e. exactly the GF1-port-read hammering that wedges
> the PicoGUS. It is omitted by design.

| knob | default | meaning |
|------|---------|---------|
| `SWEEP` | (off) | rate-sweep mode: play the tone at voice-counts 14/16/20/24/28/32 |
| `V=<n>` | 14 | single-shot active voices -> **GF1 DAC rate = 617400/n** (=44100Hz at 14) |
| `VOL=ramp\|direct` | ramp | `ramp` drives the GF1 ramp engine (driver gus-14, g2k-AUDIBLE) -- the LOUD default; `direct` writes a ramp-stopped `0x09` (the g2k-SILENT A/B baseline) |
| `B=8\|16` | 8 | voice sample width (16-bit adds the `>>1` encode + `0x04` DAC-mode bit) |
| `HZ=<n>` | 440 | tone pitch in Hz (held constant across sweep cells) |
| `W=sq\|sin` | sq | waveform |
| `L=0\|1` | 1 | loop (sustained tone) vs one-shot |
| `DUR=<ms>` | 3000 / 2500 | play duration per cell (sweep default 2500) |
| `RV=<hex>` | 07 | terminal reset value (`0x07` master+DAC+IRQ; `0x03` also works) -- de-emphasized |
| `MIX=<hex>` | 08 | final `P2X0` mix-control, written LAST (bit1=0 -> line-out enabled) -- NOT gated by PicoGUS firmware |
| `R=full\|single` | full | reset `0x4C` seq -- de-emphasized (no-op on PicoGUS) |
| `BASE=<hex>` | 240 | GF1 base port |
| `IRQ=<n>` `DMA=<n>` | 7 / 3 | latch-program values |
| `LATCH=0\|1` | 1 | (re)program IRQ/DMA latches (`pgusinit` already latches; `0` skips) |
| `RDV=0\|1` | 0 | pre-play DRAM read-back (safe: BEFORE voice start) |

**The rate sweep** (run `GUSTONE.BAT`, or `GUSTONE SWEEP` directly -- one binary,
one `GUSTONE.LOG`, all six cells announced):

| cell | `V` | DAC rate | expected on a dead-zone card |
|------|-----|----------|------------------------------|
| 1 | 14 | 44100 Hz | **AUDIBLE** (the candidate task-#5 driver default) |
| 2 | 16 | 38588 Hz | AUDIBLE |
| 3 | 20 | 30870 Hz | AUDIBLE |
| 4 | 24 | 25725 Hz | AUDIBLE (also >=14 -> rules out any "min 14 voices" theory) |
| 5 | 28 | 22050 Hz | **SILENT** -- the dead-zone reproducer (= our current driver default) |
| 6 | 32 | 19294 Hz | AUDIBLE |

> The `.LOG` cannot hear the card -- it only proves the register stream ran.
> **Record AUDIBLE / SILENT per cell BY EAR.** GUSTONE flags the dead-zone cell
> with a `*** DEAD-ZONE` line (out_rate within +/-300 Hz of 22050).

If only cell-5 (22050 Hz) is silent and the others sound, task #5 is confirmed
and unblocked: change `GUS_DEF_VOICES` off the 22050-producing count + fix the
`g_gus_rate` recompute bug. **Zero-build cross-check:** reboot with
`pgusinit /gus44k 1` (forces 44.1k in firmware for all voice counts) -- if that
alone restores audible music in the full game, same root cause confirmed.

Further single-shot knobs let the operator keep probing if the sweep does NOT
confirm: e.g. `GUSTONE V=14 B=16` (16-bit path), `GUSTONE V=14 VOL=ramp`
(ramp-engine path), `GUSTONE V=14 MIX=00` (alt line-out), `GUSTONE V=14 W=sin`.

### GUSDET.EXE -- read-only detect/report -> GUSDET.LOG

Resets the GF1 and does a DRAM peek/poke roundtrip + 256K-boundary size walk
(the SAME ops the driver already runs non-wedging on g2k -- NO voice is ever
started). Cross-checks the parsed `ULTRASND` env against what the card reports.
Run FIRST to confirm the card answers at the base before chasing silence:
`present=1` + a sane DRAM size = the card is alive at that port.

### GUSDUMP.EXE -- bounded register snapshot -> GUSDUMP.LOG

Single-pass snapshot of GF1 global (`0x4C`/`0x0E`/`0x41`/`0x45`/`0x49`) + voice-0
read-alias registers, for diffing "after our by-the-book init" against research-
supplied known-good values.

- Default (own-init): brings up the GF1 with NO voice, then reads each register
  ONCE (bounded). Safe.
- `GUSDUMP NOINIT=1`: snapshots whatever a prior program left. **HAZARD** -- only
  run AFTER MIDIDEMO has fully EXITED and the card is idle; never while a note is
  sustaining (read-concurrent-with-active-voice == the wedge). Default OFF.

## Which hypothesis each tool discriminates

| question | tool + invocation |
|----------|-------------------|
| Is the card alive at the ULTRASND base? | `GUSDET` |
| Is the 22.05 kHz DAC dead-zone the #39 root cause? | `GUSTONE SWEEP` -- only the 22050 Hz (V=28) cell silent confirms it |
| Is it instead a "min 14 voices" gate? | same sweep -- the V=24 cell (>=14) audible refutes min-voices |
| Does the firmware `/gus44k` force fix it in the full game? | reboot `pgusinit /gus44k 1` then run the game (zero-build cross-check) |
| Is the 16-bit voice path specifically broken? | `GUSTONE V=14 B=16` vs `B=8` |
| Does the ramp-vs-direct volume path matter? | ANSWERED (gus-14, g2k): ramp AUDIBLE, direct SILENT -> GUSTONE now defaults to ramp; `GUSTONE V=14 VOL=direct` reproduces the silent baseline |
| Is line-out mis-programmed (mix-control)? | `GUSTONE V=14 MIX=00` / `MIX=08` / `MIX=02` |
| Does our init leave a register wrong vs a known-good one? | `GUSDUMP` own-init vs research values |

## Correctness-smoke status

Built clean with DJGPP 12.2.0 (`-march=i486 -mtune=pentium -O2 -Wall`, no
warnings). All three run under DOSBox-X (2025.02) headless, exit cleanly (no
hang), and write parseable structured logs; the GUSTONE dead-zone derived-metric
(617400/V + the +/-300 Hz dead-zone flag) is verified (V=28 -> 22050 fires the
dead-zone flag; V=24 -> 25725 and V=14 -> 44100 report "clears"). With a `[gus]`
section enabled in the conf (`gus=true gusbase=240 gusirq=7 gusdma=3`), the
DRAM roundtrip detects (`present=1`) and the FULL upload + voice-GO path runs for
every cell -- the 2026-06-24 smoke log shows `vol=ramp` (the new default), each
cell at `vol=0xFFE0` (max) with `ctrl=0x08` (LOOP), `fc=0x0400` (1.0 since
playback==out_rate). The volume-PATH change (default direct -> ramp) is the
loudness fix; its AUDIBILITY can only be judged on g2k -- per
`memory/dosbox_not_proxy.md`, **g2k is the authoritative judge** for this
campaign (the emulator has no real DAC, so it neither reproduces the dead-zone
nor distinguishes ramp-vs-direct loudness; it confirms only that the register
stream runs). The probes handle the no-card case gracefully (`NO_CARD`, no crash).
