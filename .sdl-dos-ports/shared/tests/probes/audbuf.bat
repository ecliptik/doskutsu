@ECHO OFF
REM AUDBUF.BAT - SDL audio buffer-size sweep (wave-25 / iter J).
REM Verifies SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES is honored by sdl-engine
REM slot 0116 patch on the DOS audio backend.
REM Output: AUDBUF.LOG (per-buffer-size IRQ rate + ratio verdict).
REM Runtime: about 8 sec on PODP83 (5 sweeps x 1.4 sec each).
REM No env vars required. SDL3-linked probe; CWSDPMI.EXE same dir.
ECHO Running AUDBUF probe (SDL audio buffer sweep)...
AUDBUF.EXE
ECHO ----
ECHO Probe finished. AUDBUF.LOG contents:
TYPE AUDBUF.LOG
