@ECHO OFF
REM MPUSDL.BAT - reduced-scope SDL+MPU probe (wave-22-WB iter H).
REM Tests direct-port MPU-401 init AFTER SDL audio backend is running.
REM Output: MPUSDL.LOG (per-step BEGIN/DONE markers, fsync per line).
REM Runtime: ~3-5 sec if all sections DONE; HANGS if SDL audio init breaks
REM direct-port MPU access (that IS the diagnostic signal).
REM Operator: hit any key during a polling loop to abort.
REM Operator: hard-reset is OK after a hang; log is on disk via fsync.
REM Operator: LISTEN during section 9 hold-note (~3 sec in) for middle-C.
REM   Audible from DreamBlaster S2 = direct-port MIDI works under SDL load.
REM   Silent = direct-port writes succeed but routing wrong (outcome C).
ECHO Running MPUSDL probe (writes MPUSDL.LOG, ~3-5 sec or hang)...
MPUSDL.EXE
ECHO ----
ECHO Probe finished. MPUSDL.LOG contents:
TYPE MPUSDL.LOG
