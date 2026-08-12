# QA checklist -- round 2

Ordered to minimise swaps and start from the box as it stands. Commands are
typed on the **DOS box** unless marked *laptop*. Cells:
`docs/QA-CELL-REFERENCE.md`. Round 1: `docs/QA-RUN-SHEET.md`.

**`QA n` decides which CPU the logs are filed under, and nothing else does.**
No log records the processor.

| CPU | `n` | tag |
|---|---|---|
| Pentium OverDrive 83 | 1 | `G..` |
| Am5x86-133 | 2 | `A..` |
| 486DX2-66 | 3 | `6..` |
| 486DX2-50 | 4 | `5..` |

| Sweep | Sound card | Ears | Useful now |
|---|---|---|---|
| `GAP` | **Vibra** -- `X8` needs it | no | yes |
| `EAR` | **PicoGUS required** | **yes** | yes |
| `PROVE` | **PicoGUS required** | no | 4 of 6 cells |
| `RB` | PicoGUS required | no | **no -- refuses** |

Card holds the **round-1 binary `09e449c5a81d`**, on purpose: the harness is
proven against a known-good build first. `RB` refusing is correct.

---

## Run order

| Part | Hardware | Swaps | Time |
|---|---|---|---|
| **1** | DX2-50 + ViRGE + Vibra -> PicoGUS | 1 sound | ~45 min |
| **2** | Mach64 + VBE driver change | 2 video | ~28 min |
| **3** | DX2-66 + Vibra | 1 CPU, 1 sound | ~12 min |
| **4** | deferred -- needs the round-2 payload | -- | -- |

---

## PART 1 -- 486DX2-50 (already in the box)

| | Hardware change | Run | Time |
|---|---|---|---|
| 1.0 | CF to *laptop* | **populate -- required** | ~5 min |
| 1.1 | CF back in box | `QA 4` then `GAP` | ~12 min |
| 1.2 | sound -> PicoGUS | `EAR` | ~9 min + listening |
| 1.3 | none | `PROVE` | ~18 min |
| 1.4 | none | pull logs (*laptop*) | 2 min |

| | |
|---|---|
| Why `GAP` first | `X8` is the only cell that *requires* the Vibra; `XH1`/`XH2` re-measure the Organya-HQ cell that contradicted itself on this CPU |
| Run it as | plain `GAP`, **not** `GAP PG` -- no PicoGUS in the box at 1.1 |
| Why 1.0 is required | five fixes landed after the last populate: `RB` guard, `BINARY.NFO`, `ROUND2.OK`, `CLRENV` `PUMP_TIMEBASE`, byte-level CRLF gate |

---

## PART 2 -- ATI Mach64

Root cause settled from round-1 logs, no bench time: **the card's VBE offered
no mode below `0x01F3 512x384`**, so the engine drew 320x240 into the top-left.
`has_lfb=1 use_lfb=1 banked=0`, `bpp=8`, `pitch=512` -- flush, bpp and pitch
were all correct. Open question: does a different VBE driver offer 320x240.

**Use `M64VBE.COM`, not UNIVBE.** Round 1 failed *under* UNIVBE 6.70 -- its
19-mode list for this card starts at 512x384. ATI's driver documents
**320x200 + 320x240 as a default-on feature**, which is exactly the mode
DOSKUTSU wants.

Installed as a **6th boot-menu entry**, so the five existing profiles are
untouched and still load UniVBE. Pick entry 6 for Mach64 work, anything else
for normal operation -- nothing to remember to undo.

| | Hardware change | Run | Time |
|---|---|---|---|
| 2.1 | video -> Mach64 | swap card | 5 min |
| 2.2 | CF to *laptop* | `bash install-mach64.sh` (once, ever) | 2 min |
| 2.3 | CF back in box | reboot, pick menu entry **6** | 2 min |
| 2.4 | none | `VESATEST` | 1 min |
| 2.5 | none | `QA 4` then `VIDM 4` | ~10 min |
| 2.6 | none | pull logs (*laptop*) | 2 min |
| 2.7 | video -> ViRGE | reboot, pick any other entry | 5 min |

**2.4 answers the question before the game runs.** `VESATEST` lists every mode
the card currently offers -- if 320x240 is there, Path A has already succeeded.
It lives at `C:\ATI\SUPPORT\64VBE221\`, which entry 6 puts on the PATH, so
the bare name works. `M64VBE` does too, for the fallback switches below.

### Read `DOSVESA-CTRL` in `LOGS\5C4MSDL.LOG`

**Match on the RESOLUTION, not the mode number.** M64VBE exposes 320x240 8bpp
as **`0x0212`**, not the `0x01F8` SciTech/UniVBE uses. Looking for `0x01F8`
would read a success as a failure.

| Mode list | Verdict |
|---|---|
| a 320x240 8bpp mode + `has_lfb=1` | Done -- a docs line in `BUILDING.md`, no engine work |
| 320x240 present but **banked only** | **Stop** -- see trap |
| no 320x240 at all | Engine fix needed; already approved |

| Trap | |
|---|---|
| Banked-only 320x240 | First real-hardware exercise of SDL/0115's `gran < size` bank walk, shipped validated only where `gran == size` |
| So | Capture `win_gran` + `win_size` from `DOSVESA-MODESET`; if corrupt, A/B `SDL_HINT_DOSKUTSU_BANK_GRAN_FIX=0` **before** blaming `M64VBE.COM` |

ATI documents fallbacks for exactly our symptom. Unload with `M64VBE U`, then:

| Symptom | Reload as |
|---|---|
| image in a corner / partial | `M64VBE VW VGA` -- their fix for "1/4 of the image visible" |
| black screen or hang | `M64VBE VGA` -- standard VGA CRT timing |
| mouse trails / pointer issues | `M64VBE S VGA` -- single read+write window |

### Free data while the card is seated

| | |
|---|---|
| `DOSVESA-DACWIDTH` | The Cirrus-vs-S3 colour question needs per-card readings |
| `present` stalls | Episodic: 18 ms frames alternating with 0.5-2.5 s plateaus, flush a healthy 7.4 ms. Unstalled stretches hit **50 fps at 512x384**. Chase with `VBLANK_BOUND=0` only *after* the mode question |

Evidence: `qa-results/2026-08-11-mach64-corruption/`.

---

## PART 3 -- 486DX2-66

| | Hardware change | Run | Time |
|---|---|---|---|
| 3.1 | CPU -> DX2-66, sound -> Vibra | `QA 3` then `GAP` | ~10 min |
| 3.2 | none | pull logs (*laptop*) | 2 min |

`GAP` only. `XH1`/`XH2` here are the other half of the round-1 Organya-HQ
contradiction, where the *slower* DX2-50 scored higher on the same cell.
`EAR` and `PROVE` need one CPU only, done in part 1.

---

## PART 4 -- after the round-2 payload exists

`RB` refuses until a non-round-1 binary is installed, then runs first, per CPU.

| Cell | Catches |
|---|---|
| `R4` OPL3 | control -- no music timer, isolates observer effect |
| `R5` AdLib | the music-timer path, which `R4` cannot see |
| `R3` Organya | the re-rendered PCM cache, which nothing else exercises |
| `R4B` | repeat of `R4` -- measures the noise floor directly |

---

## Pre-flight gate -- *laptop*, every time

| | Check | If wrong |
|---|---|---|
| [ ] | CF mounted at `/media/micheal/DOS` | -- |
| [ ] | `sha256sum .../doskutsu/QA.TAS` begins **`4118561edf26`** (1956 B) | fix below |
| [ ] | `cat .../doskutsu/BINARY.NFO` -- the binary you expect | re-populate |
| [ ] | `ls .../doskutsu/CACHE/22050_2` exists (for `GAP`) | re-populate |
| [ ] | `LOGS/` empty | installer clears it |
| [ ] | logback label chosen, never reused | -- |
| [ ] | correct CPU, sound card, video card seated | -- |

Wrong reel:

```
scp claude:/tmp/QA-ROUND1.TAS claude:/tmp/fix-reel.sh /tmp/ && bash /tmp/fix-reel.sh
```

A populate fixes it too -- the installer writes this reel at step 5d whatever
the payload tarball holds, which is still the old fallback.

---

## Commands

| What | Where | Command |
|---|---|---|
| Populate | *laptop* | `scp claude:/tmp/install-qa-v163.sh /tmp/ && bash /tmp/install-qa-v163.sh` |
| Run a sweep | DOS | `C:` then `CD \DOSKUTSU` then `QA n` then the sweep |
| Pull logs | *laptop* | `scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh <label>` |

Labels: `r2-gap-dx250`, `r2-ear-dx250`, `r2-prove-dx250`, `r2-vidm-dx250`,
`r2-gap-dx266`. **Never reuse one** -- a fixed destination once merged a later
round over an earlier one. Pull between sweeps, not just between parts.

Populate output -- stop if any is wrong:

| Line | Means |
|---|---|
| `PASS: DOSKUTSU.EXE <sha>` | the build you intended |
| `installed QA.TAS (1956 B, sha 4118561edf26...)` | correct reel |
| `binary is the ROUND-1 build` / `ROUND2.OK written` | which round you are in |
| `PASS: all NN BATs CRLF + ASCII` | kit intact |
| `PASS: unmounted` | safe to pull the card |

Each sweep prints CPU / SOUND / VIDEO before the prompt. **Read it and stop if
it disagrees with the box** -- the only check that catches a wrong CPU number.

---

## Expected -- NOT broken

| Looks like | Actually |
|---|---|
| `X0` / `P0` silent | Correct -- `AUDIO_OFF=1` is the true floor. That *is* the measurement |
| Keyboard inert during a cell | Correct -- the reel drives input, the cell ends itself |
| `RB REFUSES TO RUN` | Correct on a round-1 card |
| "no PicoGUS detected" | You ran `GAP PG`, or `EAR`/`PROVE`, with no PicoGUS |
| `X8` odd on a PicoGUS | `X8` is meaningless off the Vibra -- ignore it there |
| Minutes on the title screen | Organya-HQ cold-rendering -- 22050 cache missing. Abort |
| Hung immediately | Check the reel sha -- a stub reel ends early while replay keeps feeding input |

---

## Do not bank

| | Why |
|---|---|
| Anything run before `28218c0` | fallback reel, different workload |
| `RB` output from a round-1 card | guard bypassed |
| Round-1 `5112` / `6112` | self-contradictory; `XH1`/`XH2` replace them |

---

## Building the round-2 payload

New binary `812447456e9a`. In order:

| | Step | Miss it and |
|---|---|---|
| 1 | new binary into the kit | -- |
| 2 | `make convert-music` | old MIDI sets ship with a new binary, nothing reveals it |
| 3 | Organya cache re-render at the new key | every Organya cell cold-renders on the bench |
| 4 | `tests/qa/embed-bats.sh`, re-stage the installer | the artifact ships stale BATs -- caused two of three failed attempts |
| 5 | update `EXP_DOSKUTSU_SHA` + `TARBALL` | sha assert fails, or asserts the wrong build |

`ROUND2.OK` then writes itself and `RB` enables.
