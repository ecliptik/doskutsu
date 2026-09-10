@ECHO OFF
REM IDLEPROB.BAT - DSP idle-pause CPU yield probe (wave-25 / iter J).
REM Measures wall-clock returned to engine when SDL_DOSAudioForcePause
REM halts IRQ-5. Verifies sdl-engine slot 0115 patch.
REM Output: IDLEPROB.LOG (active vs paused synth-loop iter counts +
REM         projected ms/flip yield at 60fps and 39fps).
REM Runtime: about 4 sec on PODP83 (init + 1s active + 1s paused).
REM HAZARD: ForcePause manipulates DSP DMA. If hang, BEGIN/DONE markers
REM in IDLEPROB.LOG identify the stalling instruction.
REM No env vars required. SDL3-linked probe; CWSDPMI.EXE same dir.
ECHO Running IDLEPROB probe (DSP idle-pause yield, hardware-IO)...
IDLEPROB.EXE
ECHO ----
ECHO Probe finished. IDLEPROB.LOG contents:
TYPE IDLEPROB.LOG
