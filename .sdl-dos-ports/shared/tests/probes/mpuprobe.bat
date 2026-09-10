@ECHO OFF
REM MPUPROBE.BAT - SB16 PnP CTL0026 + DreamBlaster S2 MPU-401 init probe.
REM Phase 10 wave W22-WB-E. Standalone DJGPP probe; no SDL, no engine.
REM Output: MPUPROBE.LOG (per-step BEGIN/DONE markers, fsync per line).
REM Runtime: ~3-5 sec if all sections DONE; HANGS if MPU bus-stall hit.
REM Operator: hit any key during a polling loop to abort that loop.
REM Operator: hard-reset is OK after a hang; log is on disk via fsync.
ECHO Running MPUPROBE (writes MPUPROBE.LOG, ~3-5 sec or hang)...
MPUPROBE.EXE
ECHO ----
ECHO Probe finished. MPUPROBE.LOG contents:
TYPE MPUPROBE.LOG
