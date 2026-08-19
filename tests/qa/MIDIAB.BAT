@ECHO OFF
REM ===================================================================
REM  MIDIAB -- attribute the round-2 AdLib gain (queue item 3.6).
REM
REM  AdLib rose 0.70-1.31 fps over round 1 while the timebase A/B
REM  (PROVE P5/P5K) accounts for only 0.13-0.88 of it. The leading
REM  hypothesis is the loop_reps=1 MIDI regeneration: vivi.mid went
REM  11188 events to 4558, and AdLib dispatches MIDI from the PIT pump
REM  ISR, so fewer events is directly less ISR work. OPL3 dispatches
REM  from IRQ-5 and did not gain, which fits.
REM
REM  Same binary, same boot, two MIDI sets. No rebuild involved --
REM  the MIDI set is not the Organya cache, so nothing the binary is
REM  keyed to moves.
REM
REM  Run this on the 486DX2-66. It is the only CPU where the residual
REM  clears the noise convincingly (10x its spread; the POD-83 sees
REM  it at 0.2x and cannot answer the question at all).
REM
REM  Usage:  QA 3   then   MIDIAB
REM ===================================================================
IF "%QAM%"=="" GOTO NOMACH
IF "%1"=="GO" GOTO GO
COMMAND /E:2048 /C %0 GO
GOTO END
:NOMACH
ECHO ERROR: machine not set. Run QA.BAT first (pick your CPU), then re-run.
GOTO END
:GO
IF "%QAM%"=="G" SET QACPU=Pentium OverDrive 83
IF "%QAM%"=="A" SET QACPU=Am5x86-133
IF "%QAM%"=="6" SET QACPU=486DX2-66
IF "%QAM%"=="5" SET QACPU=486DX2-50
IF NOT EXIST DATA\ORGMID4\NUL GOTO NOSET
IF NOT EXIST LOGS\NUL MKDIR LOGS
ECHO ============================================================
ECHO  SWEEP : MIDIAB -- AdLib gain attribution (item 3.6)
ECHO  CELLS : 2 cells, ~7 min
ECHO  CPU   : %QACPU%
ECHO  SOUND : PicoGUS or Vibra -- AdLib backend either way
ECHO  VIDEO : S3 ViRGE
ECHO  LOGS  : %QAM%MA1 (shipped set) and %QAM%MA4 (loop_reps=4 set)
ECHO ============================================================
ECHO  Same binary both cells. Only the MIDI arrangement changes.
ECHO  MA1 - MA4 is the arrangement's contribution to the AdLib gain.
ECHO ============================================================
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%MIDIAB.NFO
ECHO sweep=MIDIAB >> LOGS\%QAM%MIDIAB.NFO
ECHO cells=2 >> LOGS\%QAM%MIDIAB.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%MIDIAB.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%MIDIAB.NFO
ECHO config=%config% >> LOGS\%QAM%MIDIAB.NFO
ECHO video_declared=%QAVID% >> LOGS\%QAM%MIDIAB.NFO
ECHO sound_declared=%QASND% >> LOGS\%QAM%MIDIAB.NFO
ECHO irq_source=%QAIRQ% >> LOGS\%QAM%MIDIAB.NFO
ECHO dktcap=%DKTCAP% >> LOGS\%QAM%MIDIAB.NFO
ECHO cell=%QAM%MA1 class=perf >> LOGS\%QAM%MIDIAB.NFO
ECHO cell=%QAM%MA4 class=perf >> LOGS\%QAM%MIDIAB.NFO
ECHO note=MA1 = shipped orgmid2 (loop_reps=1); MA4 = ORGMID4 (loop_reps=4) >> LOGS\%QAM%MIDIAB.NFO
PAUSE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ADLIB.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%MA1
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [1/2] CELL MA1   tag %QAM%MA1
ECHO  AdLib, SHIPPED MIDI set (loop_reps=1)
ECHO ============================================
DOSKUTSU.EXE
ECHO  CELL DONE %QAM%MA1
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ADLIB.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE=ORGMID4
SET DOSKUTSU_LOG_TAG=%QAM%MA4
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [2/2] CELL MA4   tag %QAM%MA4
ECHO  AdLib, OLD MIDI set (loop_reps=4, more events)
ECHO ============================================
DOSKUTSU.EXE
ECHO  CELL DONE %QAM%MA4
SET SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE=
ECHO.
ECHO ============================================================
ECHO  MIDIAB DONE. Logs %QAM%MA1 %QAM%MA4
ECHO  Check each log for: parsed 'vivi.mid' = N events
ECHO    MA1 should read 4558, MA4 should read 11188.
ECHO ============================================================
GOTO END
:NOSET
ECHO ERROR: DATA\ORGMID4 not on this card. The loop_reps=4 MIDI set
ECHO        ships with the round-3 payload; re-populate first.
:END
