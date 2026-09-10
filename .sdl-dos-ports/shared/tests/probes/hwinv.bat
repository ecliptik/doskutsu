@ECHO OFF
REM HWINV.BAT - One-shot DOS hardware inventory snapshot (wave-41 task #10).
REM Read-only probe: CPU/MEM/VID/AUD/DSK/IRQ+DMA/PORT/PCI sections.
REM Pattern-S1 WaveBlaster enumeration (DSP-only; no MPU port reads).
REM Per-section sentinel-emit + 500 ms watchdog; safe to run on g2k.
REM Runtime: ~3 sec total. No env vars. Output -> HWINV.LOG.
REM No HAZARD: pure read-only enumeration; MPU-401 ports explicitly skipped.
ECHO Running HWINV hardware inventory snapshot (read-only)...
HWINV.EXE
ECHO ----
ECHO Probe finished. HWINV.LOG sentinel summary:
TYPE HWINV.LOG | FIND "BEGIN"
TYPE HWINV.LOG | FIND "DONE"
TYPE HWINV.LOG | FIND "EXIT_OK"
