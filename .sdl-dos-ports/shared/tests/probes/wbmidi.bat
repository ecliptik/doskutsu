@ECHO OFF
REM WBMIDI.BAT - WaveBlaster MIDI sanity probe (wave-40 task #29).
REM Mission: send Note On + wait 1 sec + Note Off to MPU-401 ports 0x330/0x331;
REM verify g2k DreamBlaster S2 daughterboard plays audible middle C.
REM First sanity gate for wave-41+ WaveBlaster MIDI offload pipeline.
REM Output: WBMIDI.LOG (per-stage emits + verdict).
REM Runtime: ~2 sec total (1 sec wait + setup). No env vars.
REM No HAZARD: pure output-only port writes; no IRQ/DMA/SDL/audio-device.
REM 5-sec operator watchdog: if no DOS prompt after 5 sec, Ctrl-Alt-Del is safe.
REM OPERATOR: listen for a 1-second middle-C tone during the wait stage.
ECHO Running WBMIDI probe (WaveBlaster MIDI sanity)...
ECHO Listen for a 1-second middle-C tone from DreamBlaster S2 output...
WBMIDI.EXE
ECHO ----
ECHO Probe finished. WBMIDI.LOG tail:
TYPE WBMIDI.LOG
