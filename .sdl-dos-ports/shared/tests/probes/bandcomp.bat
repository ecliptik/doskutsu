@ECHO OFF
REM BANDCOMP.BAT - banded L1-resident composition gate probe.
REM Phase 11 wave-52/53 (probe-engineer task #25). Gates MODEX-PLAN
REM sec.5.7 lever 1 (banded layer-major to band-major composition).
REM
REM What it does: pure-CPU cache probe, no chip I/O, no video mode set.
REM Runs 3 kernels (T_L1 ideal-resident / T_pressure realistic /
REM T_cold layer-major baseline) at 2 band sizes (8-row, 16-row), each
REM timed at least 0.3 sec. Emits per band size a [bandcomp] STAT line
REM carrying resid_frac = (T_pressure - T_cold) / (T_L1 - T_cold).
REM
REM Runtime: ~3 sec total. No operator observation needed -- the
REM verdict is read from BANDCOMP.LOG by flush-instr.
REM
REM No env vars. Pure DJGPP. CWSDPMI.EXE must be in the same dir.
REM
REM HAZARD: none. Allocates ~210 KB of plain sysmem (malloc); no
REM direct port I/O, no DAC/CRTC writes, no video mode change.
ECHO Running BANDCOMP banded-composition gate probe...
ECHO   3 kernels x 2 band sizes; pure-CPU cache measurement, ~3 sec.
ECHO   No screen observation needed -- result goes to BANDCOMP.LOG.
BANDCOMP.EXE
ECHO ----
ECHO Probe finished. BANDCOMP.LOG gate lines:
TYPE BANDCOMP.LOG | FIND "bandcomp"
TYPE BANDCOMP.LOG | FIND "SELFTEST"
