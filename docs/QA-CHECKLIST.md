# QA checklist -- round 2, full set

Detail lives in `docs/QA-RUN-SHEET.md` and `tests/qa/README.md`.
*laptop* = run on the laptop. Everything else is typed on the DOS box.

CPU number for `QA n`: POD-83 = 1, Am5x86 = 2, DX2-66 = 3, DX2-50 = 4.

**Every sweep waits for a keypress at its banner.** It prints the cell list,
then `PAUSE`. Nothing runs until a key is pressed. Walking away at that point
produces a `.NFO` manifest, zero cell logs, and a round that looks like it ran.
After the keypress the sweep is unattended to the end -- except `EAR`.

**S3 ViRGE in every part except the Mach64 lane.** The video card is worth
~1 fps, so it is part of the configuration, not a detail. Round 1's DX2-50
OPL3 cell measured 17.39 on the ViRGE and 16.90 on the Cirrus.

**Sound card rule.** Sweeps use whatever SB is in the box. Add `PG` only when a
PicoGUS is fitted -- `GAP PG`, `VIDM 4 PG`. On a Vibra use plain `GAP` /
`VIDM 4`. Passing `PG` without a PicoGUS overwrites BLASTER with jumper values
the Vibra cannot use.

---

## Save state

The reel does not start a new game. It opens Load Game, enters map 20 from a
save, and fires the Polar Star throughout. Both slots must read
**map 20 + weapon 2** or the round measures something else.

An unarmed save reproduces the route perfectly and still ruins the numbers:
bullets are drawn objects, so a run that fires nothing does ~6% less draw work
per frame and beats the anchor for a reason that has nothing to do with the
binary. The populate prints the state of both slots -- read those two lines
before starting.

No new TAS is needed. A reel is a list of (tick, input) replayed against
whatever the save provides, so restoring the state restores the run.
Re-recording would orphan the 89 round-1 cells.

---

## Order

| Part | CPU | Video | Sound | Boot | Type after boot | Time |
|---|---|---|---|---|---|---|
| 1 | DX2-50 | ViRGE | **PicoGUS** | menu **2** | `QA 4` -> `RB` `EAR` `PROVE` | 40 min |
| 2 | DX2-50 | **Mach64** | PicoGUS | menu **2** | `QA 4` -> `VIDM 4 PG` `VIDMK 4 PG` | 20 min |
| 3 | DX2-50 | ViRGE | **Vibra** | menu **5** | `QA 4` -> `GAP` | 12 min |
| 4 | **DX2-66** | ViRGE | Vibra | menu **5** | `QA 3` -> `GAP` | 12 min |

**`QA n` after EVERY boot.** It is not sticky -- it sets `QAM`, the tag every
log is filed under, and a hardware swap means a power cycle. Miss it and the
sweep stops with `ERROR: machine not set`. Running several sweeps without
rebooting in between needs it only once.

The Mach64 lane keeps the PicoGUS so the sound card is not a second variable:
round 1's video cells ran on it (`irq=7`), and Vibra-vs-PicoGUS is worth
0.90-1.30 per-loop on its own -- more than the video difference being measured.

One PicoGUS fit, one Mach64 swap, one Vibra fit, one CPU swap.

---

## Part 1 -- DX2-50 + ViRGE + PicoGUS

| | Swap / boot | Run |
|---|---|---|
| 1.0 | CF to laptop | populate |
| 1.1 | **ViRGE** + **PicoGUS**, CF to box, menu **2** | `QA 4` then **`RB`** |
| 1.2 | -- | pull logs *laptop* -- **stop, send them** |
| 1.3 | -- | **`EAR`** -- listen, see below |
| 1.4 | -- | `PROVE` |
| 1.5 | -- | pull logs *laptop* |

**1.2 is a real stop.** `RB` decides whether the ~89 banked round-1 cells still
compare against this binary. It passed once already (`R4` 17.19, `R4B` 17.39
against the 17.39 anchor, noise floor 0.21), so a repeat that lands in the same
place confirms it; one that does not means something changed since.

### 1.3 `EAR` -- the only sweep that needs ears

Three cells. The question is the Shack, and it is the single most useful
observation left in the round.

**The Shack is the LAST map change, ~87 s into a 103 s cell** -- a small hut
interior, roughly 15 seconds before the cell ends. It is on screen for 6-8 s.

**Answered on DX2-50, 2026-08-13.** The expected pattern is now:

| Cell | Backend | Expected |
|---|---|---|
| 1 `E4` | OPL3 (MIDI) | a note or two, not distinctive music |
| 2 `E3` | Organya (PCM) | correct music, sounds right |
| 3 `EA` | AdLib (MIDI, OPL2) | silence |

The Shack song opens with 7.68 s of channel-10 percussion before its first
melodic note, and the visit is 6-8 s, so the MIDI backends spend the whole
scene on drums -- which both OPL paths render as one repeated noise burst.
Organya plays the original samples and never touches GM channel mapping.

On the DX2-66 and POD-83 arms this only needs **confirming**, since it is a
property of the arrangement rather than the CPU. Report just: same pattern, or
not. A different pattern on another CPU would mean the explanation is wrong.

Root cause is settled, so nothing further is needed by ear beyond the pattern
above. `vivi.org` opens with four percussion tracks from t=0.00 s and no
melodic note until 7.68 s; Organya renders those four as distinct pitched
samples (the "full music"), while both OPL paths collapse them into one shared
noise-burst patch. The drums are the music. Fix is five or six FM drum
envelopes, queued as Tier 3.3c.

---

## Part 2 -- Mach64 verification (PicoGUS stays in)

**Use UniVBE, not M64VBE.** The card cannot do 320x240 -- the Mach64-CT has no
double scanning -- so the engine centres the 320x240 picture inside the 512x384
surface (`patches/nxengine-evo/0296`). M64VBE additionally hangs SDL_Init.

Two independent questions. A correct picture does not mean a usable card.

| | Swap | Run |
|---|---|---|
| 2.1 | **Mach64** in, ViRGE out, PicoGUS stays, menu **2** | `QA 4` then `VIDM 4 PG` |
| 2.2 | -- | `VIDMK 4 PG` -- same cells, `VBLANK_BOUND=0` |
| 2.3 | -- | pull logs *laptop* |

**`PG` is required on both.** The PicoGUS SB-mode switch is opt-in in each BAT;
without it the lane runs on whatever BLASTER the boot menu set and the Mach64
numbers stop comparing against round 1's video cells.

**a) Does the picture land right?** Expect the image centred with a black
border, not in the corner. The log proves it engaged:

    center-oversized: ENGAGED surface=...
    center-oversized: flush rect=320x240@96,72 bytes=76800

Killswitch for an A/B: `SET SDL_HINT_DOSKUTSU_CENTER_OVERSIZED=0`.

**b) Do the stalls survive?** Round 1 had 40% of frames over 300 ms while the
unstalled ones hit 20 ms. The letterbox does nothing for that; it is what
decides whether the card is usable. `VIDMK` tags `5C4MK` / `551MK` so the A/B
cannot overwrite its own baseline.

---

## Part 3 -- DX2-50 + ViRGE + Vibra

| | Swap / boot | Run |
|---|---|---|
| 3.1 | **ViRGE** back in, Mach64 + PicoGUS out, fit **Vibra**, menu **5** | `QA 4` then `GAP` |
| 3.2 | -- | pull logs *laptop* |

`GAP`, not `GAP PG` -- the PicoGUS is out by now.

---

## Part 4 -- DX2-66 + ViRGE + Vibra

| | Swap | Run |
|---|---|---|
| 4.1 | CPU -> **DX2-66**, ViRGE + Vibra, menu **5** | `QA 3` then `GAP` |
| 4.2 | -- | pull logs *laptop* |

---

## Pre-flight -- *laptop*, every populate

Read these off the populate output; do not go hunting for them.

- [ ] `PROFILE3.DAT: map 20 'Save Point', weapon 2 -- benchmark state`
- [ ] `PROFILE5.DAT:` same
- [ ] `PASS: QA.TAS = benchmark reel (1956 B)`
- [ ] `PASS: DOSKUTSU.EXE fe44805fb603`
- [ ] `already staged and current (sha d7d1d01dbbd9)` or a re-fetch
- [ ] right CPU and sound card; **S3 ViRGE** unless in Part 2

---

## Commands

The sweeps are the commands. `QA n` first, once per boot, sets which CPU the
logs are filed under.

    C:
    CD \DOSKUTSU
    QA 4          <- 4 = DX2-50. Once per boot, before any sweep.
    RB            <- then the sweep name, on its own
                  <- PRESS A KEY at the banner

| Sweep | Cells | Time | Hardware it needs | What it is |
|---|---|---|---|---|
| `RB` | 4 | ~12 min | DX2-50 + ViRGE + PicoGUS | re-baseline against round 1 |
| `EAR` | 3 | ~9 min | DX2-50 + ViRGE + PicoGUS + **ears** | the Shack question |
| `PROVE` | 6 | ~18 min | DX2-50 + ViRGE + PicoGUS | timebase fix A/B |
| `GAP` | 4 | ~12 min | DX2-50 or DX2-66 + ViRGE + Vibra | four round-1 measurement gaps |
| `VIDM 4 PG` | 2 | ~10 min | DX2-50 + **Mach64** + PicoGUS | letterbox + stalls |
| `VIDMK 4 PG` | 2 | ~10 min | DX2-50 + **Mach64** + PicoGUS | same, `VBLANK_BOUND=0` |

`VIDM`/`VIDMK` take the CPU number as an argument because they are video lanes;
the others read it from `QA n`.

---

Populate -- *laptop*:
`scp claude:/tmp/install-qa-v163.sh /tmp/ && bash /tmp/install-qa-v163.sh`

Logs -- *laptop*, after each sweep:
`scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh <label>`

Labels -- never reuse one, the logback refuses to overwrite:
`r2-rb-dx250`, `r2-ear-dx250`, `r2-prove-dx250`, `r2-gap-dx250`,
`r2-vidm-dx250`, `r2-gap-dx266`.

Pull between sweeps. Logs accumulate in `LOGS\`; the populate archives and
clears them, so pull before re-populating.

---

## Not broken

| | |
|---|---|
| `X0` / `P0` silent | correct -- that is the measurement |
| keyboard dead in a cell | correct -- the reel drives it |
| nothing happens after typing the sweep | the `PAUSE` -- press a key |
| "no PicoGUS detected" | wrong sweep for the card in the box |
| minutes on the title screen | HQ cache missing -- abort |
| hangs at once | wrong reel -- check the sha |
| route is not `72 20 11 17 11 15 11 19 11 14 11` | wrong save state -- abort |

---

## Do not bank

- anything run before commit `28218c0`
- `RB` output from a card whose `BINARY.NFO` is not the round-2 build
- round-1 `5112` / `6112`
- the `r2-rb-dx250-noweapon` set -- unarmed save, inflated numbers

---

## Payload -- BUILT 2026-08-12

| | |
|---|---|
| Binary | `fe44805fb603` -- letterbox `0296` present |
| Build fingerprint | `1f79ce20e4ee` |
| Payload tarball | `d7d1d01dbbd9` |
| Reel | `4118561edf26`, 1956 B |
| Saves | `32529e291e0f`, map 20 + Polar Star, both slots |
| Organya cache | both tiers, keyed `1f79ce20e4ee` |
| MIDI sets | `loop_reps=1` set (`vivi.mid` 4558 events, was 11188) |

**Do not rebuild the engine without re-rendering the Organya cache.** The cache
is keyed to this exact binary; a rebuild changes the fingerprint and every
Organya cell then cold-renders on the bench, which looks like a hang. It also
voids the `RB` re-baseline, which costs another 12 minutes of bench time.
