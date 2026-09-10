@ECHO OFF
REM AUDRQ.BAT - Pure SB16 IRQ-hook wall-clock probe v2 (wave-39 task #18).
REM Defensive re-author after wave-38 v1 truncated mid-first-variant on real HW.
REM v2 defenses: ISR deadman (self-masks IRQ-5 if storm) + atexit panic handler
REM + IRQ-vector verify + pre-mask-during-setup + per-stage progress emits.
REM Isolates SB16 IRQ-5 dispatch + minimal-ISR cost from SDL_mixer mix cost.
REM Refutation candidate vs wave-20 v3 "SB16 IRQ-5 IS the fps cost" prior.
REM Informs wave-39 P1 (OPL3) / P2 (WaveBlaster MIDI) / P9 (IRQ-hook fast-path).
REM 4 rate variants: 44100s/22050s/11025s/11025m. 1 sec measurement each (v2).
REM Output: AUDRQ.LOG (per-rate per-stage progress + RDTSC stats + status enum).
REM Status enum adds RATE_IRQ_STORM_DETECTED (v2 deadman fired).
REM Runtime: ~5-8 sec total (down from v1's 15-20). No env vars required.
REM HAZARD: directly programs SB16 (DSP reset, DMA, IRQ-5 hook). 60-sec
REM operator watchdog: if no DOS prompt after 60 sec, Ctrl-Alt-Del is safe.
REM v2 atexit panic handler restores IRQ vec + masks DMA + resets DSP on
REM any exit path (clean / panic / Ctrl-Break / DJGPP-assert).
ECHO Running AUDRQ v2 probe (defensive SB16 IRQ-hook wall-clock)...
AUDRQ.EXE
ECHO ----
ECHO Probe finished. AUDRQ.LOG tail:
TYPE AUDRQ.LOG
