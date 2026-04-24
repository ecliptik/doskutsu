# Target Hardware and DOSBox-X Calibration

The reference target is the **g2k** machine — a Gateway 2000 desktop from 1995 with a Pentium OverDrive CPU upgrade. DOSKUTSU's configuration defaults, memory budgets, and DOSBox-X calibration all trace back to this specific hardware.

---

## The g2k machine

| Component | Spec |
|---|---|
| CPU | Intel Pentium OverDrive PODP5V83 (Socket 3, P54C core, 83 MHz) — **no MMX**, ~Pentium-40-class effective integer throughput |
| Motherboard | Anigma LP4IP1 (486 chipset, Socket 3) |
| RAM | 48 MB (1 MB conventional, 47 MB XMS) |
| Video | ATI Mach64 215CT PCI with VESA 1.2+ via `M64VBE.COM` vendor VBE |
| Sound (primary) | Creative Vibra16S CT2490 (SB16-class, ISA PnP) on IRQ 5, DMA 1 / HDMA 5, base 220 |
| Sound (alternate) | Gateway 16MVCARD (Jazz16-class), swappable with Vibra16S |
| Sound (extras) | PicoGUS (always present), DreamBlaster S2 on WaveBlaster header |
| Storage | CF-to-IDE adapter, 2 GB CompactFlash, DOS 6.22 + WfW 3.11 |
| DPMI | CWSDPMI r7 |

### Relevant BIOS / DOS facts

- **DJGPP uses DPMI (via CWSDPMI), not EMS.** The correct boot profile is `NOEMS`. EMS profiles on g2k exist for other DOS games.
- **Mach64 video BIOS** occupies `C000-C7FF`. UMBs start at `C800`.
- **SMARTDRV** is configured as 4 MB write-through. XMS is plentiful (47 MB free).
- **BIOS Multi-Sector Transfers MUST stay Disabled** — verified hardware quirk on this board.
- **Plug & Play O/S must stay Yes** while Vibra16S is installed.
- **BLASTER env var:** `A220 I5 D1 H5 T6` under the `[VIBRA]` profile.

### The `[VIBRA]` boot profile on g2k

DOSKUTSU runs under the existing `[VIBRA]` CONFIG.SYS profile on the g2k machine. The canonical definition lives in g2k's `README.TXT`; **do not improvise memory layout** for a new `[DOSKUTSU]` profile — mirror `[VIBRA]` exactly if you ever add one. The current plan is to not add one at all; see [BOOT.md](./BOOT.md) for the profile expectations DOSKUTSU cares about.

---

## Hardware tiers

Three named configurations. The goal is to make the **Absolute Minimum** tier playable — that's what lets DOSKUTSU run on the widest range of vintage hardware.

### Tier 1 — Reference (tested)

The g2k machine as described above. This is where Phase 7 / 8 gates run. Comfortable headroom everywhere.

| Component | Spec |
|---|---|
| CPU | Pentium OverDrive 83 MHz (~Pentium-40-class effective) |
| RAM | 48 MB |
| Video | VESA 1.2+ linear framebuffer (Mach64 + M64VBE) |
| Sound | SB16 / Vibra16S, IRQ 5, DMA 1/5, base 220 |
| Audio mode | 22050 Hz stereo S16 |

### Tier 2 — Achievable Minimum (expected fallback)

A period-accurate mid-range DOS box. Should run DOSKUTSU with the 11025 mono audio fallback enabled. Untested until Phase 9 hardware validation.

| Component | Spec |
|---|---|
| CPU | 486DX2-66 with FPU |
| RAM | 16 MB (12 MB free XMS after HIMEM / SMARTDRV) |
| Video | VESA 1.2+ (UNIVBE loadable if card lacks it in firmware) |
| Sound | Any SB16-compatible |
| Audio mode | 11025 Hz mono S16 (Phase 9 fallback) |

### Tier 3 — Absolute Minimum (stretch target, aspirational)

The lowest-spec 486 that we think has a real chance of running DOSKUTSU. Tight on both CPU and memory; requires Phase 9 optimizations to be actually playable. **No real-hardware testing yet — treat as a research target.**

| Component | Spec |
|---|---|
| CPU | **486DX2-50 with FPU** (or any 486DX/DX2/DX4 ≥ 50 MHz effective with hardware FPU) |
| RAM | **8 MB** (4-6 MB free XMS after HIMEM) |
| Video | VESA 1.2+ linear framebuffer (UNIVBE acceptable) |
| Sound | SB Pro / SB 2.0 / SB16 |
| Audio mode | 11025 Hz mono S16 |
| Display mode | 8bpp indexed (Phase 9 lever 3) |

**What it takes to hit Tier 3:**
1. 11025 mono audio (Phase 9 lever 1) — mandatory for Organya on 486DX2-50.
2. 8bpp indexed video (Phase 9 lever 3) — halves per-pixel bandwidth; 486 memory interface is the bottleneck.
3. Direct surface-to-surface blit path (Phase 9 lever 2) — skips the per-frame texture upload.
4. Disable per-sprite alpha blending (Phase 9 lever 4) — colorkey is sufficient for Cave Story.
5. Tight NXEngine-evo working-set budget: keep extracted Cave Story data loaded lazily, and avoid loading all sprite sheets at startup. This may require port-side work beyond the Phase 9 levers above.
6. Probably `.pxm` / `.pxe` files streamed from disk rather than fully cached.

**Absolute floors below Tier 3:**
- **No 486SX without a 487 coprocessor.** DJGPP emits x87 code; a pure 486SX will trap on every FP instruction.
- **Probably no 386.** DJGPP targets i386+ so the binary will load, but FP performance on a 386DX (no integrated FPU even with 387) plus no on-die cache will be catastrophic for Organya. Not a stated target.
- **No pre-VESA video.** Plain VGA without a VESA 1.2+ BIOS has no linear framebuffer; the SDL3 DOS backend requires it.
- **< 4 MB RAM is not viable.** Cave Story's extracted assets alone are ~5 MB of sprite/map data; NXEngine-evo needs to keep at least the current stage's maps + actively-referenced sprites in memory.

### Why 486DX2-50 instead of 486DX-33?

On memory-bound workloads (which is what DOSKUTSU's Organya synth and 320x240 blit path both are), the CPU multiplier matters less than the FSB and memory interface. Between a 486DX-33 and 486DX2-50:

- DX-33: 33 MHz internal, 33 MHz FSB
- DX2-50: 50 MHz internal, **25 MHz FSB** (lower!)

The DX-33 actually has a *faster* FSB than the DX2-50 — but the DX2-50's 50 MHz core covers more cycles per memory access, which is what matters for Organya's PCM resampling (memory-bound at the voice-mix accumulator, not memory-interface-bound at the sample-fetch side).

We call Tier 3 "486DX2-50" to be concrete; practically, any 486 with FPU, effective integer throughput ≥ ~20 MIPS, and enough RAM to avoid CWSDPMI.SWP thrash will hit the same target. Real-hardware calibration will replace this guess in Phase 9.

---

## The gate between tiers

**Tier 1 → Tier 2**: enable the 11025 mono audio fallback. One patch against NXEngine-evo's `Mix_OpenAudio` call.

**Tier 2 → Tier 3**: requires the full Phase 9 optimization pass — mono audio, 8bpp indexed video, direct-surface blits, no alpha blending. A build option like `-DDOSKUTSU_TIER3_MIN=ON` could apply all of these at compile time. Not yet planned; see `PLAN.md § Phase 9`.

---

## DOSBox-X calibration

DOSKUTSU ships two DOSBox-X configurations. Both live under `tools/`. Use the right one for the job.

### `tools/dosbox-x.conf` — parity config

Approximates PODP83 real-hardware performance. Used for Phase 7 playtest gate, audio-dropout investigation, and any work where real-HW-like timing matters.

| Setting | Value | Rationale |
|---|---|---|
| `machine` | `svga_s3` | VESA 1.2+ SVGA linear framebuffer — what SDL3-DOS targets |
| `memsize` | `48` | Matches g2k's 48 MB |
| `cputype` | `pentium_slow` | P5 FPU, no MMX; scheduler approximates P5 pipeline |
| `core` | `normal` | Deterministic timing; `dynamic` can mask audio-DMA edge cases |
| `cycles` | `fixed 40000` | **PODP83 Pentium-40-class approximation** — see below for calibration math |
| `sbtype` | `sb16` | Matches Vibra16S |
| `sbbase/irq/dma/hdma` | `220/5/1/5` | Matches `[VIBRA]` profile |

### `tools/dosbox-x-fast.conf` — fast iteration

Identical to parity config except `cycles=max` and `core=dynamic`. Runs ~4-8x faster than real hardware. Use only for debugging logic / UI / crash bugs where you just need to reach the repro state quickly. **Do not** use for performance claims — audio timing, FPS, Organya CPU cost all need the parity config.

### Why `cycles=fixed 40000`?

DOSBox-X's `cycles=fixed N` targets roughly N Pentium-scale integer instructions per second. Calibration history:

- **Pentium at 83 MHz** in ideal conditions would be ~83-150 million Pentium-effective cycles/sec (depending on pipeline utilization and branch prediction). But the "Pentium OverDrive" part matters: the PODP5V83 is a P54C Pentium core on a 486-class motherboard, so the *effective* integer throughput is limited by the board's 33 MHz FSB and 486-style memory interface. Empirically PODP83 benchmarks at roughly Pentium-40-class throughput on memory-bound workloads — which describes Cave Story rendering and Organya synth.
- **DOSBox-X cycles=40000** targets approximately that effective throughput. Starting point, not a measurement.
- **Recalibrate during Phase 7.** Once DOSKUTSU runs to the title screen in DOSBox-X and we have real-HW comparison data from Phase 8, adjust the `cycles` value to minimize the DOSBox-vs-g2k timing delta on a reference scene (e.g., Mimiga Village 30-second idle frame timing).

For sibling-project context: vellm (llama2.c on DOS) uses `cycles=fixed 90000` for the same g2k target. vellm is CPU-bound on integer matmul kernels with high ILP; Cave Story is memory-bound on blits and Organya resampling. Different workloads calibrate to different cycle counts on the same nominal hardware — this is expected.

---

## Memory budget

NXEngine-evo's C++11 heap behavior under DPMI is uncharacterized. Phase 7 playtest watches:

- **Working set** (measured via CWSDPMI's per-session XMS use): expected ~16-24 MB for 320x240 with WAV + Organya + PNG-loaded sprites. 48 MB gives comfortable headroom.
- **CWSDPMI.SWP growth:** should stay at 0. Any growth indicates we're paging, which on a CF-IDE setup means catastrophic stuttering.
- **Fragmentation over 30-60 min sessions:** C++ `new`/`delete` churn could fragment. NXEngine-evo does no pooling; if fragmentation is a problem, a Phase 9 fix would be to add a scratchpad arena for per-frame allocations.

---

## Video timing

- VESA 1.2+ linear framebuffer at 320x240 x 16bpp is the default render target. SDL3-DOS programs the DAC for this mode via the vendor VBE.
- The Mach64 is known to have a flaky DPMS state; `M64VBE.COM` must be loaded *before* DOSKUTSU starts (in AUTOEXEC.BAT), not after.
- Switching to 8bpp indexed mode (Phase 9 lever) would halve per-pixel bandwidth and let us use VGA DAC palette programming for Cave Story's sprites. The palette management work is non-trivial but tractable.

---

## Audio timing

- SB16 at 22050 Hz stereo S16: DMA IRQ every ~2048 samples (~46 ms at 22050). Mix_OpenAudio's default buffer size of 2048 samples = ~46 ms of latency, which is fine for Cave Story (no tight audio-visual sync).
- Organya plays 8 voices synthesized at frame rate. Each frame advances all 8 voice phases, resamples into the mix buffer. Memory-bandwidth bound more than CPU bound.
- On 486DX class hardware, expect to drop to `Mix_OpenAudio(11025, AUDIO_S16SYS, 1, 2048)` (mono 11025) — matches Cave Story's 2004 audio spec. This is a Phase 9 tuning step.

---

## Reference links

- [vellm's docs/hardware.md](https://forgejo.ecliptik.com/ecliptik/vellm) — sibling project on the same g2k target; DOSBox-X calibration methodology was borrowed from there
- [SDL3 PR #15377](https://github.com/libsdl-org/SDL/pull/15377) — the DOS backend that makes this port possible. Author's note: "*tested extensively with DevilutionX in DOSBox. But no real hardware testing.*"
- [DOSBox-X documentation](https://dosbox-x.com/wiki/) — `cputype`, `cycles`, `sbtype` reference
- [~/emulators/docs/DOSBOX.md](/home/claude/emulators/docs/DOSBOX.md) — hub-level DOSBox-X config patterns and conventions
