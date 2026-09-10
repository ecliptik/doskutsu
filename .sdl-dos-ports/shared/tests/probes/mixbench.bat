@ECHO OFF
REM MIXBENCH.BAT - SDL_MixAudio mix-cost benchmark (wave-38 audio Tier 1).
REM Measures SDL3 mix-loop cost: 4 rate variants × 3 channel populations = 12 scenarios.
REM Sizes wave-39 P7 (Lever G rate reduction); discriminates P4 (silent-skip), P5 (batching).
REM SDL3-linked but does NOT open audio device (pure SDL_MixAudio benchmark).
REM Output: MIXBENCH.LOG (per-scenario RDTSC stats + ticks/sec + bytes/sec + status enum).
REM Runtime: ~30-60 sec total. No env vars required. No DSP/DMA/IRQ touched.
REM No HAZARD: no audio device opened; standard DJGPP runtime.
REM 90-sec operator watchdog: if no DOS prompt after 90 sec, Ctrl-Alt-Del is safe.
ECHO Running MIXBENCH probe (SDL_MixAudio mix-cost)...
MIXBENCH.EXE
ECHO ----
ECHO Probe finished. MIXBENCH.LOG tail:
TYPE MIXBENCH.LOG
