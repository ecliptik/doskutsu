@ECHO OFF
REM PCSPKPWM.BAT - PC-speaker PWM-DAC CPU-steal + ear-check probe (task #21).
REM Campaign 2 Phase-2 (P2.0) gate for the shared-PIT-ch0 PWM-SFX design
REM (docs/internal/ADLIB-PCSPK-SFX-DESIGN.md sec.4.3). Measures the REAL per-fire
REM ISR cost on g2k BEFORE the multi-day P2.1/P2.2 build.
REM
REM Reproduces the SDL/0110-extended ISR: ch0 at the PCM rate, per fire emits one
REM ~6-bit PWM sample to ch2 (0x42) with the speaker routed via 0x61, derives the
REM ~120 Hz music tick every Mth fire, and chains BIOS INT8 (keeps DOS time).
REM Reports stolen_pct = (1 - count_on/count_off)*100 per rate + plays a 440 Hz
REM PWM tone per rate for the operator ear-check (quality + carrier-buzz).
REM
REM Default sweep is 6000 8000 11025 Hz. Pass rates to override, e.g.
REM   PCSPKPWM.EXE 6000
REM
REM Output: PCSPKPWM.LOG (one "PCSPKPWM rate=N stolen_pct=.." line per cell +
REM plausibility flags + CLOCK-RESTORE=OK/SUSPECT). Per-line fsync.
REM Runtime: ~18-25 sec for the 3-cell sweep.
REM
REM HAZARD: reprograms PIT ch0 (the DOS clock) + drives the PC speaker. atexit +
REM normal teardown both restore PIT ch0 (to 18.2065 Hz), ch2 + port 0x61
REM (speaker off), and unhook IRQ-0 (NEVER masked). ~30-sec operator watchdog:
REM if no DOS prompt after 30 sec, Ctrl-Alt-Del is safe.
REM AFTER: run TIME and confirm the DOS clock is correct (a fast clock = the PIT
REM restore failed -- report it; that is the quit-path risk this probe gates).
ECHO Running PCSPKPWM probe (PC-speaker PWM-DAC CPU-steal + ear-check)...
ECHO Listen for the 440 Hz tone at each rate; judge PWM quality + carrier buzz.
PCSPKPWM.EXE
ECHO ----
ECHO Probe finished. PCSPKPWM.LOG tail:
TYPE PCSPKPWM.LOG
ECHO ----
ECHO Now run TIME to confirm the DOS clock was restored (not running fast).
