# QA run sheet -- tick as you go

Ordered to keep the CPU in the socket as long as possible: every card swap for
a given CPU happens before that CPU comes out. Cell definitions are in
`docs/QA-CELL-REFERENCE.md`; the harness itself is in
`docs/TAS-BENCHMARKING.md`.

Commands are typed **on the DOS box** unless marked *laptop*.

**Golden rule.** The number after `PG` / `VB` / `VID*` is the CPU in the box.
It is the only thing deciding which column a result lands in.

| CPU | number | tag |
|---|---|---|
| Pentium OverDrive 83 | 1 | `G..` |
| Am5x86-133 | 2 | `A..` |
| 486DX2-66 | 3 | `6..` |
| 486DX2-50 | 4 | `5..` |

**Do NOT run `RECORD`.** `QA.TAS` is the fixed workload for the whole
campaign. Re-recording makes every column non-comparable.

If something sounds or looks wrong, note the **cell number** on screen
(`[n/10]`) in Notes at the bottom. The logs cannot tell me what you heard.

---

# PART 1 -- Pentium OverDrive 83

Everything here runs with the POD-83 installed. Do not touch the CPU until
Part 2.

## 1.1  Lane A -- PicoGUS + S3 ViRGE  *(no swap, current setup)*

- [ ] `PG 1`   -- 10 cells, ~22 min, unattended
- [ ] Check cell 1 (`C3`) runs ~2 min with the character walking. If it ends
      in seconds, stop and report -- don't spend the other 9 cells.

Logs: `GC3 GC4 G31 G51 G17 G22 G23A G23B G41 GC5`

Listen for: cave-transition screech (`G31`), instruments dropping on the GUS
voice cells, real wavetable vs thin FM on `G17`.

## 1.2  Lane D -- video baseline, still ViRGE  *(no swap)*

- [ ] `VIDV 1`   -- 2 cells, ~10 min

Logs: `GC4V G51V`. Same hardware as lane A, run through the video launcher so
the ViRGE number is measured under identical conditions to the other cards.

## 1.3  Lane E -- Cirrus CL-GD5430  *(video swap)*

- [ ] Power off
- [ ] **S3 ViRGE OUT, Cirrus CL-GD5430 IN.** PicoGUS stays. CPU stays.
- [ ] Boot, `C:`, `CD \DOSKUTSU`
- [ ] `VIDC 1`   -- 2 cells, ~10 min

Logs: `GC4C G51C`. This is the bridge to the project's historical numbers,
which were measured on this card.

## 1.4  Lane F -- ATI Mach64  *(video swap)*

- [ ] Power off
- [ ] **Cirrus OUT, ATI Mach64 IN.** PicoGUS stays. CPU stays.
- [ ] Boot, `C:`, `CD \DOSKUTSU`
- [ ] `VIDM 1`   -- 2 cells, ~10 min

Logs: `GC4M G51M`

## 1.5  Lane B -- Vibra16 + ViRGE  *(video back, sound swap)*

- [ ] Power off
- [ ] **Mach64 OUT, S3 ViRGE back IN** (lane B must match lane A's video)
- [ ] **PicoGUS OUT, Vibra16 IN**
- [ ] **Move the DreamBlaster onto the Vibra's WaveBlaster header**
- [ ] CPU stays POD-83. Boot, `C:`, `CD \DOSKUTSU`

### Sweep (~13 min)

- [ ] `VB 1`

Logs: `G02 G16 G17 G18 G111 G112`. `G16` is the interesting one -- auto-detect
should pick the WaveBlaster. If it picks something else, that is a finding.

### Hands-on -- you drive these (~1.5 h)

Splittable: the SETUP cells can be done now and the soaks later. None of them
block Part 2.

- [ ] `G11`  SETUP Express: detect, accept profile, video bench, SAVE.
      Should name the Vibra + DreamBlaster + ViRGE.
- [ ] `G12`  SETUP Sound menu: under **every** Music Type both pickers open
      and list all devices; bad combos WARN, never block.
- [ ] `G13`  SETUP audio tests. Organya preview = **real title theme**, full
      window, no 5-second loop, no arpeggio.
- [ ] `G14`  SETUP save-and-exit: toast says "Run DOSKUTSU.EXE to play", no
      auto-launch, clean exit, no stale SETs at the prompt after.
- [ ] `G15`  SETUP input remap: rebind one key, verify in game, restore.
- [ ] `G115` Joystick: calibrate, then axis + 4 buttons + invert-Y + keyboard
      still works alongside. Gamepad? test the D-pad too.
- [ ] `G116` Save / Load / quit-to-DOS x5. Watch for the quit hang.
- [ ] `G117` Free play 20-30 min (Mimiga -> First Cave -> Egg Corridor).

## 1.6  Pull the logs before changing CPU

*laptop:*

    scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh

- [ ] Logs pulled and sent

---

# PART 2 -- 486DX2-66

## 2.1  Lane C -- PicoGUS + ViRGE  *(sound back + CPU swap)*

Pairs with lane A: same card, same modes, same reel, **CPU the only
variable**. This pair is the point of the campaign.

- [ ] Power off
- [ ] **Vibra16 OUT, PicoGUS back IN**
- [ ] **Move the DreamBlaster back onto the PicoGUS header** -- miss this and
      `G17` silently measures nothing on every remaining CPU. The log must
      read `audio backend: wb`.
- [ ] ViRGE stays. **Swap CPU to 486DX2-66** (jumpers)
- [ ] Boot, `C:`, `CD \DOSKUTSU`
- [ ] `PG 3`   -- **3, not 1**. Expect it to take longer than lane A.

Logs: `6C3 6C4 631 651 617 622 623A 623B 641 6C5`

## 2.2  Video lanes on the 486 *(optional)*

Only if you want a CPU x video-card cross-section. Same swaps as 1.3 / 1.4.

- [ ] `VIDV 3`  (ViRGE, no swap)   -- logs `6C4V 651V`
- [ ] Cirrus IN, `VIDC 3`          -- logs `6C4C 651C`
- [ ] Mach64 IN, `VIDM 3`          -- logs `6C4M 651M`

---

# PART 3 -- extra CPUs *(optional, skip freely)*

PicoGUS + ViRGE, CPU swap only. Same 10 cells each.

- [ ] Am5x86-133 -> `PG 2`
- [ ] 486DX2-50  -> `PG 4`

---

# Send it back

*laptop:*

    scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh

or

    tar czf /tmp/qa-final.tar.gz -C /media/micheal/DOS/doskutsu LOGS QA.TAS \
      && scp /tmp/qa-final.tar.gz claude:/tmp/

- [ ] Logs sent
- [ ] Notes below sent

---

# Notes

Cell number and what you saw or heard. Terse is fine.

    lane A  cell __   ...
    lane B  cell __   ...
    lane C  cell __   ...

---

# If something goes wrong

| Symptom | Do this |
|---|---|
| Replay ends in seconds / character stuck | Stop, send the log, don't run the rest |
| "Bad command or file name" | Not in `C:\DOSKUTSU`, or the CPU number was missing |
| Unsure which CPU a cell filed under | The cell banner prints its tag: `G..` POD-83, `6..` DX2-66, `A..` Am5x86, `5..` DX2-50 |
| Sweep stalls on one cell | Ctrl-C aborts; earlier cells' logs are already written |
| `G17` sounds thin / synthetic | The DreamBlaster is not on the header the current sound card presents -- check the log for `audio backend: wb` |

---

# Swap count

Ordered this way, the CPU moves twice and the cards do the rest:

| Part | Video swaps | Sound swaps | CPU swaps |
|---|---|---|---|
| 1 (POD-83, 5 lanes) | 3 | 1 | 0 |
| 2 (DX2-66) | 0 | 1 | 1 |
| 3 (optional CPUs) | 0 | 0 | 1 each |
