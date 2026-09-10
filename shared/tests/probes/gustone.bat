@ECHO OFF
REM GUSTONE.BAT - GF1 test-tone OUTPUT-RATE sweep (22.05kHz DAC dead-zone hunt).
REM GUS Campaign 3 (#39) task #4. probe-suite. Pure DJGPP; CWSDPMI.EXE same dir.
REM
REM Operator prereqs (PicoGUS v2.0 firmware picogus-gus, jumpers IRQ7+DMA3):
REM   SET ULTRASND=240,3,3,7,7
REM   pgusinit /mode gus
REM   pgusinit /gusdma 12
REM
REM ROOT CAUSE (team-converged from the PicoGUS firmware + canonical driver):
REM the GF1 OUTPUT RATE = 617400 / active-voice-count (reg 0x0E). The PicoGUS
REM PCM510xA DAC is SILENT at exactly 22.05kHz on ~10 percent of cards -- and our
REM driver's default of 28 voices lands the DAC dead on 22050Hz. 14 voices =
REM 44100Hz dodges it. DOSBox-X has no real DAC so it never reproduced this.
REM
REM LOUDNESS (2026-06-24): the tone is now driven through the GF1 ramp engine by
REM default (VOL=ramp -- the driver gus-14 path g2k proved AUDIBLE). The old
REM default (ramp-stopped direct 0x09) was SILENT on a real GF1, which made the
REM last sweep too faint to ear-judge. So the working-rate cells should now be
REM LOUD and a silent V=28 cell unmistakable. (VOL=direct reproduces the faint
REM baseline if you want the A/B.)
REM
REM SWEEP plays the SAME 440Hz tone at voice-counts 14/16/20/24/28/32 to
REM rates 44100/38588/30870/25725/22050/19294, ~2.5s each, announced in the
REM log. LISTEN to each cell:
REM   - cells that SOUND  = rates this card's DAC handles
REM   - cell(s) that are SILENT = this card's DAC dead-zone (expect the
REM     V=28 / 22050Hz cell to be the silent one == #39 confirmed)
REM Record AUDIBLE/SILENT per cell BY EAR (the .LOG only proves the stream ran).
ECHO ===== GUSTONE output-rate SWEEP (listen for the SILENT cell) =====
GUSTONE.EXE SWEEP
ECHO ----
ECHO Sweep done. GUSTONE.LOG has per-cell rate + dead-zone flags:
TYPE GUSTONE.LOG
ECHO ----
ECHO Zero-build cross-check: reboot with  pgusinit /gus44k 1  (forces 44.1k in
ECHO firmware for ALL voice counts) -- if that makes every cell audible, the
ECHO 22.05kHz DAC dead-zone is confirmed and the driver fix is GUS_VOICES=14.
