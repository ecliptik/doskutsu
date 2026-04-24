# DOS Boot Profile for DOSKUTSU

DOSKUTSU does not ship a dedicated `CONFIG.SYS` profile. Instead, this document describes the DOS environment it requires so you can run it under an existing profile on your target machine (the g2k reference `[VIBRA]` profile works out of the box).

For the full g2k hardware reference, see [HARDWARE.md](./HARDWARE.md).

---

## What DOSKUTSU needs

### Memory

- **`HIMEM.SYS` loaded** — DJGPP uses the XMS memory pool via CWSDPMI's DPMI host; no HIMEM, no XMS, no DOSKUTSU.
- **`NOEMS`** — DJGPP uses DPMI (protected mode), not EMS (real-mode paging). An EMS page frame wastes 64 KB of UMB space that could otherwise host mouse drivers, SMARTDRV, or stay free.
- **UMB available** — helps keep conventional memory free for any TSRs you load (CTMOUSE, BLASTER setup, etc.). `DOS=HIGH,UMB` and `EMM386 NOEMS` is the usual incantation on real hardware; under a clean boot diskette `HIMEM.SYS` alone is enough since DOSKUTSU doesn't care about UMB.
- **At least ~20 MB of free XMS** — NXEngine-evo working set is ~16-24 MB. On the 48 MB g2k this is never close.

### CPU

- **486DX or later with a hardware FPU.** 486SX + 487 coprocessor is untested but should work. Pure 486SX (no FPU) will not work — DJGPP emits x87 code.
- No MMX / SSE / SIMD instructions are used (target flag is `-march=i486 -mtune=pentium`), so any 486+ CPU with an FPU is acceptable.

### Video

- **VESA 1.2+ linear framebuffer support.** Most PCI-era cards have this in their BIOS. For ISA or old PCI cards without linear framebuffer support, load UNIVBE or the vendor VBE TSR (e.g., `M64VBE.COM` for ATI Mach64) before running DOSKUTSU.
- On the g2k, `M64VBE.COM` is loaded in AUTOEXEC.BAT. **Load it before `DOSKUTSU.EXE` runs, not after** — SDL3-DOS probes VBE at init time.

### Sound

- **SB16-compatible card** on IRQ 5, DMA 1, HDMA 5, base 220. Other configurations work but need the `BLASTER` environment variable adjusted to match — SDL3-DOS reads `BLASTER` at init.
- Sound Blaster Pro and SB 2.0 are supported by the SDL3 DOS backend but not tested by DOSKUTSU; expect 8-bit mono-only on SB Pro and 8-bit mono on SB 2.0.
- The `BLASTER` env var typically looks like:
  ```
  SET BLASTER=A220 I5 D1 H5 T6
  ```
  Where `A220` is the base port, `I5` is the IRQ, `D1` is the 8-bit DMA, `H5` is the 16-bit DMA, and `T6` is the card type (SB16).

### Mouse (optional)

- DOSKUTSU does not require a mouse; keyboard-only play is fully supported.
- If you want mouse support (unused by Cave Story's UI, but present in NXEngine-evo), load an INT 33h mouse driver like `CTMOUSE` before `DOSKUTSU.EXE`.

### DPMI

- **CWSDPMI r7 must be available.** Options:
  - `CWSDPMI.EXE` in the current directory when `DOSKUTSU.EXE` runs (simplest).
  - `CWSDPMI.EXE` somewhere on `PATH` (e.g., `C:\DOS\`).
  - CWSDPMI pre-loaded (self-installs into XMS on first invocation; subsequent runs don't need the `.EXE` on disk until reboot).

---

## Example AUTOEXEC.BAT on the g2k

Real example from the g2k machine's `[VIBRA]` profile (abbreviated — the full AUTOEXEC.BAT in the g2k repo does more):

```bat
@ECHO OFF
SET PATH=C:\DOS;C:\UTIL;C:\DOSKUTSU
SET BLASTER=A220 I5 D1 H5 T6

C:\UTIL\M64VBE.COM        REM load ATI Mach64 VESA BIOS TSR
C:\UTIL\CTMOUSE.EXE       REM optional: INT 33h mouse driver
LH C:\DOS\SMARTDRV.EXE 4096 /X   REM 4 MB write-through disk cache

ECHO Ready. Type DOSKUTSU to play.
```

Then:

```
C:\>CD \DOSKUTSU
C:\DOSKUTSU>DOSKUTSU
```

---

## Example minimal AUTOEXEC.BAT (clean boot disk)

For testing on a boot floppy or a minimal configuration:

```bat
@ECHO OFF
SET PATH=A:\;C:\DOS;C:\DOSKUTSU
SET BLASTER=A220 I5 D1 H5 T6
```

With a CONFIG.SYS like:

```
DEVICE=C:\DOS\HIMEM.SYS
DOS=HIGH,UMB
FILES=30
BUFFERS=20
STACKS=9,256
```

This is enough to run DOSKUTSU if you've already loaded VESA via the card's BIOS ROM (most PCI cards) or are willing to forgo it (not an option for a 320x240 SVGA linear-framebuffer game — don't skip VBE).

---

## Troubleshooting

### "DPMI host not available" / "No DPMI server found"

`CWSDPMI.EXE` is not in the current directory or on `PATH`. Either:
```
C:\DOSKUTSU>CWSDPMI
```
to self-install before running DOSKUTSU, or ensure `CWSDPMI.EXE` is in `C:\DOSKUTSU\` next to `DOSKUTSU.EXE`.

### Black screen / VESA mode failure

VESA BIOS is not loaded or not supported on your hardware. Load `M64VBE.COM` (ATI Mach64), `S3VBE.COM` (S3 cards), or `UNIVBE.EXE` (generic) before running DOSKUTSU. Verify from the DOS prompt with a VBE tool like `VBETEST` if available.

### "Sound Blaster not detected" or audio silence

Check `SET` for the `BLASTER` variable. If missing or wrong, set it manually:
```
SET BLASTER=A220 I5 D1 H5 T6
```
Then re-run. If the variable matches your hardware but audio is still silent, check the sound card's jumpers / PnP config for the actual IRQ / DMA in use.

### Game starts but audio speeds up / slows down / stutters

Usually `BLASTER` IRQ mismatch — the card is wired for IRQ 7 but `BLASTER` says `I5` (or vice versa). Verify with the card's config utility.

### "Runtime error" on startup before title screen

Likely insufficient XMS. Check `HIMEM.SYS` loaded cleanly (no errors at boot), and that no other program (Windows for Workgroups, SMARTDRV with a huge cache) has consumed it. 32 MB+ free XMS is safe.

### Frame rate drops dramatically in Mimiga Village

Organya CPU cost is the usual culprit. This is expected on 486-class hardware and marginal on PODP83. The [Phase 9](../PLAN.md#phase-9--performance-tuning) lever is to drop to `Mix_OpenAudio(11025, AUDIO_S16SYS, 1, 2048)` — mono 11025 Hz, matching Cave Story's 2004 original spec. That halves Organya's work.

---

## Why no `[DOSKUTSU]` CONFIG.SYS profile

The g2k machine already has a `[VIBRA]` profile that provides everything DOSKUTSU needs: SB16 at the right address, NOEMS, VESA loaded, SMARTDRV tuned, mouse optional. Adding a redundant `[DOSKUTSU]` profile would duplicate that setup and drift from the canonical `[VIBRA]` version over time.

For target machines other than g2k, you'll want to mirror your own existing game profile that provides SB16 + NOEMS + VESA. This document describes the environmental expectations; the actual CONFIG.SYS / AUTOEXEC.BAT layout is yours to own.
