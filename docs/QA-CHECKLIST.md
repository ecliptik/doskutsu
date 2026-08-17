# QA checklist

`QA n` after every boot: POD-83 = 1, Am5x86 = 2, DX2-66 = 3, DX2-50 = 4.
Boot 1 = Vibra16. Boot 2 = PicoGUS.
Every sweep waits for a keypress at its banner, then runs unattended.
Never reuse a logback label.
Re-run UniVBE after every video card swap and check its banner names the card.

## The loop

    populate once per binary -> run the round -> logback -> feedback -> next round

Logs accumulate across a round; one pull collects the lot.
Do not re-populate mid-round -- it clears `LOGS\`.

---

## Populate -- *laptop*, CF mounted at `/media/micheal/DOS`

    scp claude:/tmp/install-qa-v163.sh /tmp/ && bash /tmp/install-qa-v163.sh

Unmounts the CF when done. Logback does not -- unmount that by hand.

Check in the output:

- [ ] `PASS: DOSKUTSU.EXE 6971e9f73bc9`
- [ ] `PASS: Organya 11025 cache keyed 3ba36d8dd56d`
- [ ] `PASS: Organya-HQ 22050 cache extracted` -- not `SKIPPING` / `no HQ cache`
- [ ] `PASS: QA.TAS = benchmark reel (1956 B)`
- [ ] `PROFILE3.DAT: map 20 'Save Point', weapon 2` -- `PROFILE5.DAT` same
- [ ] `already staged and current (sha 162fb8b55a4c)` or a re-fetch
- [ ] `PASS: all N BATs CRLF + ASCII`

A cache-key `WARN` means every Organya cell cold-renders. Stop, re-populate.

---

## Round K -- new binary, DX2-66 (`QA 3`)

Card starts in g2k on **ViRGE + PicoGUS** from Round J.

| Step | Hardware | Boot | Type after boot | Time |
|---|---|---|---|---|
| K0 | laptop | -- | populate | 6 min |
| K1 | ViRGE + PicoGUS | 2 | `QA 3` then `RB` | 12 min |
| K2 | laptop | -- | logback `r10-rb-dx266`, send, **STOP** | 3 min |
| K3 | ViRGE + PicoGUS | 2 | `ADIAG 3 PG` | 12 min |
| K4 | ViRGE + PicoGUS | 2 | `LEV 3 PG` | 9 min |
| K5 | Mach64 + Vibra | 1 | re-run UniVBE, check its banner | -- |
| K6 | Mach64 + Vibra | 1 | `QA 3` then `BDIAG 3` | 5 min |
| K7 | laptop | -- | logback `r10-mach64`, send | 3 min |

All three *laptop* commands, CF mounted:

    scp claude:/tmp/install-qa-v163.sh /tmp/ && bash /tmp/install-qa-v163.sh
    scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh r10-rb-dx266
    scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh r10-mach64

**K0 repopulates.** New binary, new cache key. Check the pre-flight lines.

**K2 is a hard stop.** Wait for feedback before K3.

**K1 `RB`** -- confirms `0310` (off-screen tile-slot skip, now default ON)
on the reference card. Round J already banked its A/B at +0.6 fps, so this
is confirmation, not discovery. Expect the `RB` cells slightly above their
Round-I figures.

**K4 `LEV 3 PG`** -- two shipped levers never A/B'd in 572 logged runs,
both default-OFF, no rebuild needed. All three cells are fps rows; do not
stack them. **Cell 3 (`PERF_MODE`) drops decorative foreground tiles** --
watch the picture and say whether it looks wrong. An fps gain that costs
fidelity is a product call, and it is yours.

**K3 `ADIAG 3 PG`** -- audio ablation, all four cells ARE fps rows.
Cells 2-4 will sound wrong; that is the point. The fps gap from cell 1 is
what audio costs. Nothing in this campaign has ever measured that: the one
audio counter brackets a single function, and the SDL-side audio brackets
have never been enabled. **Cell 4 caveat** -- the audio IRQ fires at the
buffer-refill rate whenever the device is open, so if `AUDIO_OFF` only
silences the mixer, cell 4 is a FLOOR on audio cost, not a ceiling.

**K6 `BDIAG 3`** -- Mach64 REQUIRED; meaningless on any other card. An
instrument, not a fix: nothing in this binary tries to correct the
backdrop, so **the defect should still be visible on screen**. Report the
`surface-extent:` line's 16 probe values. If that line never appears,
centring did not engage -- say so, the cell answered nothing.

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
| `ADIAG` | 4 | ~12 min | any card + `PG`, tags `A0`/`AM`/`AS`/`AX`, all fps rows |
| `LEV` | 3 | ~9 min | any card + `PG`, tags `L0`/`LA`/`LP`, all fps rows |

Add `PG` to a Mach64 sweep only if the PicoGUS is in.

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

| | Round 4 | Round 5 | Round 8 |
|---|---|---|---|
| Binary | `03e053712a6c` | `c1729d2fe065` | `6971e9f73bc9` |
| Tarball | `97491a731ec6` | `d49dccd49558` | `162fb8b55a4c` |
| Cache key | `1ab4e612a715` | `482d88eba8b2` | `3ba36d8dd56d` |

The Round-8 binary carries `0310` (tile-slot skip, default ON) and `0311`
(surface-extent probe, default OFF). `0309` is retracted -- out of the
series AND out of the binary -- and the installer deletes the stale
`VIDMR.BAT` from the CF, since tar extraction never removes files and a
cell for a vanished lever would measure two identical arms.

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

All labels above are spent. Nothing in Rounds A-J is to be re-run.

Results and analysis live in `docs/internal/POST-BENCHMARK-PLAN.md` and
`docs/internal/HANDOFF-ENGINE-AUDIO-BUCKET.md`.
