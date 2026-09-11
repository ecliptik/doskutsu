# Phase 0b: the other 82% of a Mach64 frame

Measured 2026-08-21, same system and same binary as Phase 0 (`49344b16b9e9`).
Six `diag` cells on one daemon (425373, `NRestarts 0`). **Their fps does not
enter the perf matrix** — Round R's 27.6 fps is the perf figure.

Phase 0 found the flip was 17.8% of the frame and left 29.3 ms unexamined.
This is that 29.3 ms.

---

## The answer: the tilemap is the frame

    tilemap (bg + fg)   17.79 ms   49.8%   <-- HALF THE FRAME
    flip total           6.36 ms   17.8%
      of which VRAM      4.77 ms   13.4%

**The background tile layer alone (10.56 ms) is 2.2x the entire VRAM write.**
The LFB question Phase 0 could not settle is now clearly the smaller prize.

## The nesting, reconciled across three independent instruments

    E3F  mode_tick            23.48
           mode_tick_render     22.11
           clear_screen          2.06
           mode_tick_state       1.49
           tsc_interp            0.02
           debug_overlay         0.02

    E2T    mode_draw_scene    20.96      <- inside mode_tick_render
             mode_sim            1.48
             mode_draw_hud       0.83
             mode_post           0.02

    E1D      mds_tilemap_bg   10.56      <- inside mode_draw_scene
             mds_tilemap_fg    7.23
             mds_clear         2.14
             mds_object_loop   0.66
             mds_player        0.18
             mds_carets        0.05

All medians, milliseconds. **E1D's phases sum to 20.82 against E2T's 20.96;
E2T's sum to 23.29 against E3F's 23.48.** Residuals of +0.14 and +0.19 ms, and
every named residual bucket — `mds_residual`, `mode_residual`,
`engine_tick_residual`, `mdt_bg_residual` — reads **0.00**.

**The decomposition is complete.** The pre-registered "buckets do not sum ->
buy a further instrument" branch does not fire.

## E4B: "the tilemap" is half backdrop

    mdt_bg_backdrop     5.70
    mdt_bg_tilelayer    4.98
    mdt_bg_tintscreen   0.00
    mdt_bg_residual     0.00

These sum to 10.68 against E1D's `mds_tilemap_bg` of 10.56 — consistent. **But
the backdrop is the larger half**, which the name `mds_tilemap_bg` does not
suggest. D3's byte data agrees: backdrop 22.5% of scene bytes against
bg_tile_scan 19.6%.

**Anyone optimising "the tile loop" would find half the bucket is not tiles.**
The backdrop at 5.70 ms is on its own larger than the entire VRAM write.

## E5T: the fg layer is call volume, not per-call cost

    drawtile_calls      18,160 per block     drawtile_per_us      61.98
    blit_indexed_calls  16,915 per block     blit_indexed_per_us  38.02
    drawsprite_calls         0

**~62 us per hundred draw-tile calls, and 18,160 calls per block.** That is
the shape of the 7.23 ms fg layer. `drawsprite_calls = 0` throughout is a real
observation about this reel — the fg layer draws tiles, not sprites, in these
scenes — not an instrument failure.

**So the next question is algorithmic: why the tile loop touches that much.**
Not how to make each touch faster.

---

## THE CAVEAT THAT CHANGES HOW TO READ ALL OF THIS

**MIDI runs in an interrupt, so its cost has no bucket — it is smeared across
every other bucket.**

`MidiScheduler::tick()` has one call site, `SoundManager.cpp:1943`, gated:

    if (!SDL_DOSMidiIsrAnyActive())
      MidiScheduler::getInstance()->tick(...);

Under OPL3 the PIT/IRQ-0 pump drives `tick_isr` instead, so the main-loop tick
is deliberately skipped. **ISR time is stolen from whatever was executing when
the interrupt fired**, in proportion to how long each phase runs — so
`mds_tilemap_bg`, the largest bucket, absorbs the largest share.

**Every number above is an honest wall-clock account of elapsed time in its
phase.** But reading `mds_tilemap_bg = 10.56 ms` as *"the tile loop costs
10.56 ms of CPU work"* over-reads it: an unknown fraction is the OPL pump
running inside that window.

**This does not change what to optimise** — the tilemap is still half the
frame. **It does mean an optimisation there may recover less than the bucket
promises.**

**MEASURED. See the TAUD section below — the correction is ~3%, and the cell
found something larger than the correction it was for.**

## E6A: audio, and the three meanings of a zero

    pixtone_play_count   10.0     pixtone_play_us   288
    pixtone_stop_count   10.0     pixtone_stop_us   414
    midi_tick_count       0.0     midi_tick_us        0

**SFX cost is measured and negligible: ~700 us per 100 flips, ~7 us per flip.**

`midi_tick_count = 0` is **honest, not a defect** — see the ISR gate above.
Getting there passed through two other explanations, and the sequence is worth
recording because a zero from an instrument means at least three things:

    1  measured, and genuinely zero
    2  NOT COLLECTED -- print gate open, collect gate shut
    3  NOT EXECUTED  -- the instrumented path is not the path taken

**Phase 0's D1 hit case 2.** `[p12-audio` printed 28 well-formed blocks of
zeros because its *print* gate is `FLIP_INSTR` (set) while its *collect* gate
is `P12_AUDIO_INSTR` (not set) — the RAII timer destructors early-returned
before accumulating. **A perfectly formatted statistic meaning "not
collected"**, which would have entered the decomposition as "audio is free"
had the counts not been read alongside the times.

**E6A hit case 3.** Both gates open, pixtone collecting, MIDI still zero —
because the instrumented function genuinely never runs under an active ISR.

**Only case 1 is a finding.** Every other failure this week announced itself by
absence; these two produce output that looks like data.

---

## Recommended next, on the evidence

1. **The tilemap**, at 17.79 ms and half the frame. Characterise before
   optimising: E5T says call volume, so the question is why the loop touches
   18,160 tiles per block.
2. **The music-off cell**, one cell, to size the smeared ISR share and put an
   error bar on every bucket above.
3. **The LFB** is the smaller prize and should not go ahead of either.

**A large bucket licenses a characterisation, not an optimisation.**
Pre-registered by the benchmarking session before this round ran, and it holds:
nothing here says how to make the tilemap faster, only that it is where the
time is.


---

# TAUD: audio is 2.4 ms of the frame, and it was in nobody's bucket

One cell, `SDL_HINT_DOSKUTSU_AUDIO_OFF=1` plus E1D's instrument, compared
against E1D. Same reel, same instrument, within-session.

**`MUSIC_OFF` is the wrong lever** and was rejected before running: it is a
dispatch-level gate, the device and mixer still come up, and the OPL pump keeps
running. `AUDIO_OFF=1` is the device-level both-off.

**The gate was verified by hand because nobody knew what `AUDIO_OFF` prints:**

    "4-state audio"  in E1D.LOG   count: 1     <- the pattern works
    "4-state audio"  in TAUD.LOG  count: 0     <- genuinely absent
    "Sound system"   in TAUD.LOG  count: 1     <- pipeline still live

Presence proven before absence was believed, on an output string whose spelling
was unknown.

## The correction Phase 0b needed

    phase                 E1D med   TAUD med    drop    drop %
    mds_tilemap_bg          10.56      10.24   +0.32     +3.0%
    mds_tilemap_fg           7.23       6.92   +0.31     +4.3%
    mds_clear                2.14       2.10   +0.04     +1.9%
    mds_object_loop          0.66       0.66   +0.00      0.0%
    SCENE TOTAL             20.82      20.15   +0.67     +3.2%

**Proportional to bucket duration — the signature of a smeared cost.** So the
tilemap buckets above need roughly a 3% haircut. This is an **upper bound** on
smeared audio: `AUDIO_OFF` removes main-thread audio work too.

## The larger finding

    E1D    28.2 fps   35.46 ms
    TAUD   30.2 fps   33.11 ms      -2.35 ms

**Audio costs ~2.35 ms of the frame, about 6.6%, and no decomposition had it.**
That is larger than anything the LFB was going to buy.

**THE 30.2 IS A DIAG NUMBER AND DOES NOT TRANSFER.** Both cells carry
instrumentation, so the *difference* is sound and the *absolute* is not the
shipping configuration. Applied to Round R's clean 27.6:

    36.23 ms - 2.35 ms = 33.88 ms = 29.5 fps      target 30.0 = 33.33 ms

**Still 0.55 ms short. Audio is worth ~1.9 fps and does not on its own reach
the target.** Carry the 2.35 ms delta, never the 30.2.

## The smear is NOT uniform

The scene draw is 59% of the frame. A uniform smear would put 1.38 ms of the
2.35 inside it. **It absorbed 0.67 — 49% of its proportional share.** So ~1.7 ms
lands outside the scene draw.

**First suspect, flagged as a hypothesis: the yield.** Phase 0's D2 measured
`fb_yield` at 1.41 ms inside the flip, close to the missing 1.7. Under a
cooperative scheduler a yield is where other work is *allowed to run*, and at
28 fps the game is nowhere near frame-pacing idle — so that 1.41 ms is likely
work rather than waiting.

**Testable with instruments that already exist**: one `FLIP_BODY_INSTR` cell
with `AUDIO_OFF=1`. If `fb_yield` collapses, the pump's location is settled
rather than inferred.

## What it does and does not license

**Audio cannot be removed**, so the lever is making it cheaper. Numeric knobs
exist — `ORG_PUMP_TARGET_MS`, `ORG_PUMP_MAX_CHUNKS`, `PIXTONE_IRQ_RATEDIV`,
`AUDIO_DEVICE_FRAMES`, `AUDIO_MIDGAP_PUMP`, `FORCE_PUMP_YIELD` — several of
them pump-rate or budget controls, which is the shape that trades audio quality
for CPU.

**A 2.35 ms finding from one cell changes the value of twelve tilemap cells**,
and that is a priority question rather than a measurement one.

---

# The yield pair: where audio's missing 1.7 ms goes

Two cells, adjacent, one sitting, 2026-08-21. `FLIP_BODY_INSTR=1` on both;
`AUDIO_OFF=1` on YB only. Twelve forbids per cell including all three T1
levers, because a leaked `AUDIO_OFF` in the **control** arm would have made
both arms audio-off and voided the pair with no A-to-A logic to catch it.

**Run as a PAIR, not against Phase 0's D2** — that would be a cross-session
comparison across two daemon redeploys and a splitter fitted and removed.
"The effect is large enough to survive that" is how the first Round R looked.

## The void row was checked FIRST

Pre-registered: **if YB's frame does not drop ~2.35 ms, the `AUDIO_OFF` effect
is not reproducing and every `fb_yield` reading is meaningless whichever way it
moves.**

    YA (audio on)    28.0 fps   35.71 ms
    YB (audio off)   29.9 fps   33.44 ms
    frame drop      +2.27 ms    <- TAUD independently measured 2.35

**Reproduces within 0.08 ms of a measurement taken hours earlier, on a
different daemon, across a splitter fit and removal.** The pair is valid.

The audio gate itself was verified by hand with a positive control, since
`AUDIO_OFF`'s printed output is unknown:

    "4-state audio"  YA.LOG  count: 1     <- pattern works
    "4-state audio"  YB.LOG  count: 0     <- genuinely absent
    "Sound system"   YB.LOG  count: 1     <- pipeline live

## The answer

    phase                YA med   YB med     drop   drop %
    fb_yield               1.41     0.02    +1.39   +98.6%
    fb_residual            4.75     4.76    -0.01    -0.2%
    fb_audit_flushes       0.03     0.03    +0.00     0.0%
    fb_setup               0.02     0.02    +0.00     0.0%
    fb_prev_frame_blit     0.02     0.02    +0.00     0.0%
    fb_diag_logs           0.01     0.01    +0.00     0.0%
    FLIP BODY TOTAL        6.24     4.86    +1.38   +22.1%

**`fb_yield` collapses by 98.6% and NOTHING ELSE IN THE FLIP BODY MOVES.** The
VRAM write is identical to within 0.01 ms. This is one bucket, cleanly, not a
diffuse effect.

**61% of the frame's 2.27 ms audio cost lands in `fb_yield`.**

## What it licenses

Per the pre-registration: **the pump runs at yield points. Audio is
targetable — pump scheduling and rate, not the mixer.**

Under a cooperative scheduler a yield is where other work is *allowed to run*,
and at 28 fps the game is nowhere near frame-pacing idle, so that 1.41 ms was
work rather than waiting. The shipping knobs are the right class of lever:
`ORG_PUMP_TARGET_MS`, `ORG_PUMP_MAX_CHUNKS`, `PIXTONE_IRQ_RATEDIV`,
`AUDIO_DEVICE_FRAMES`, `AUDIO_MIDGAP_PUMP`, `FORCE_PUMP_YIELD` — all numeric,
several of them pump-rate or budget controls.

**An audio round is now designable. It was not before this pair.**

**Still true and unchanged:** audio cannot be removed, so the lever is making
it cheaper, which is a quality trade. And `27.6 - 2.35` transfers to **29.5 fps
in the perf configuration, not 30** — audio alone does not reach the target.

## Incidental: the second decode error ever recorded

`bad_frames_kept` fired at 18:57:16, 13 s before YA exited. **The first error
fired 11 s before S2B exited during T1.** Both at the same point in a cell:
the game tearing down 640x480 and DOS returning to text.

The artifact is a **short** frame, not a corrupt one — every marker valid, SOF
correctly declaring 640x480, 23,636 bytes of entropy data, ending RST6 then a
genuine EOI. Exactly what a capture device produces when it flushes a partial
frame at a mode change and closes it properly. **The daemon's SOI+EOI+128-byte
filter cannot catch these**, because a frame missing half its picture satisfies
all three.

**And the daemon did not fall over** — decoded as far as it could, raised,
counted, kept the bytes, carried on, with two browser tabs driving
`/timeline.json` through the same window. See `lab/OPEN-FAULTS.md`: that is
evidence **against** malformed frames causing the abort.
