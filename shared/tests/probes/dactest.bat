@ECHO OFF
REM DACTEST.BAT - VGA DAC pixel-mask (0x3C6) detect probe (S3-VIRGE task #2).
REM probe-engineer. Confirms the S3 SDAC 0x3C6 DetectVGA failure that makes
REM SDL_Init report "No available video device" on the ViRGE, and validates
REM the 0x3C8-reset fix for the SDL patch.
REM
REM What it does: pure VGA port I/O in text mode -- (T1) replicates SDL's exact
REM DetectVGA round-trip on 0x3C6, (T1B) the same after arming the 4-read DAC
REM overlay the way SDL's earlier init does, (T2) the same after a 0x3C8 reset,
REM (T3) reads 0x3C6 six times to surface the SDAC hidden-command overlay. No
REM graphics mode, no mapping, no hang risk. Result -> DACTEST.LOG (fopen-direct).
REM
REM Runtime: <1 sec. No env vars, no args. Pure DJGPP; CWSDPMI.EXE same dir.
REM Safe to run on ANY card (Cirrus / S3 / DOSBox) -- it reports raw values.
ECHO Running DACTEST 0x3C6 DAC-detect probe (writes DACTEST.LOG)...
DACTEST.EXE
ECHO ----
ECHO Probe finished. DACTEST.LOG verdict:
TYPE DACTEST.LOG | FIND "dactest"
