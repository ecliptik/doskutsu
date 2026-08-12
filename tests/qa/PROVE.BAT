@ECHO OFF
IF "%QAM%"=="" GOTO NOMACH
IF "%1"=="GO" GOTO GO
COMMAND /E:2048 /C %0 GO
GOTO END
:NOMACH
ECHO ERROR: machine not set. Run QA.BAT first (pick your CPU), then re-run.
GOTO END
:GO
REM --- QA-INSTRUMENT v1 (same banner + .NFO contract as PG/VB) ---
IF "%QAM%"=="G" SET QACPU=Pentium OverDrive 83
IF "%QAM%"=="A" SET QACPU=Am5x86-133
IF "%QAM%"=="6" SET QACPU=486DX2-66
IF "%QAM%"=="5" SET QACPU=486DX2-50
IF NOT EXIST LOGS\NUL MKDIR LOGS
ECHO ============================================================
ECHO  SWEEP : PROVE-OUT -- validates the round-2 binary on real silicon
ECHO  CELLS : 7 cells, ~20 min, fully unattended
ECHO  CPU   : %QACPU%
ECHO  SOUND : PicoGUS for P3/P4/P4B/P5/P5K/P0; P8 needs the VIBRA
ECHO  VIDEO : whichever card is installed -- BOTH are wanted, one run each
ECHO  LOGS  : tagged %QAM%P..  in LOGS\
ECHO ============================================================
ECHO  Check the CPU and cards above match the box before starting.
ECHO  P5 vs P5K is the KEY pair: it A/Bs the timebase fix on real
ECHO  hardware, which is the one thing DOSBox structurally cannot test.
ECHO ============================================================
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%PR.NFO
ECHO sweep=PROVE >> LOGS\%QAM%PR.NFO
ECHO cells=7 >> LOGS\%QAM%PR.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%PR.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%PR.NFO
ECHO sound=PicoGUS (P8 cell needs Vibra) >> LOGS\%QAM%PR.NFO
ECHO video=see operator note; run once per card >> LOGS\%QAM%PR.NFO
ECHO note=run order is recoverable from the [HH:MM:SS] prefixes in the cell logs >> LOGS\%QAM%PR.NFO
SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=10000
PAUSE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%P4
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [1/7] CELL P4   tag %QAM%P4
ECHO  OPL3 -- the control
ECHO  Non-pump. Must match the banked C4 anchor for this CPU.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%P4B
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [2/7] CELL P4B   tag %QAM%P4B
ECHO  OPL3 repeat -- the noise floor
ECHO  P4 vs P4B spread IS the noise floor. Nothing is significant without it.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ADLIB.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%P5
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [3/7] CELL P5   tag %QAM%P5
ECHO  AdLib, timebase fix ON -- THE KEY CELL
ECHO  Expect inter_flip 10-40ms band POPULATED, pump_clock_state=pump-timebase-ok.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ADLIB.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_PUMP_TIMEBASE=0
SET DOSKUTSU_LOG_TAG=%QAM%P5K
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [4/7] CELL P5K   tag %QAM%P5K
ECHO  AdLib, timebase fix OFF -- the A/B arm
ECHO  Expect that band EXACTLY EMPTY. Proves the lever on real silicon.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ORGANYA.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%P3
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [5/7] CELL P3   tag %QAM%P3
ECHO  Organya -- the re-rendered PCM cache
ECHO  Catches a silently different re-render. Must match banked C3.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_AUDIO_OFF=1
SET DOSKUTSU_LOG_TAG=%QAM%P0
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [6/7] CELL P0   tag %QAM%P0
ECHO  TRUE audio floor (AUDIO_OFF=1)
ECHO  Device-level off. SILENT.CFG is NOT a floor.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_AUDIO_SB_FORCE_8BIT=1
SET DOSKUTSU_LOG_TAG=%QAM%P8
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [7/7] CELL P8   tag %QAM%P8
ECHO  Forced 8-bit DMA -- VIBRA ONLY
ECHO  Proves SDL/0124 records identity, override and path SEPARATELY.
ECHO ============================================
DOSKUTSU.EXE
ECHO.
ECHO ============================================================
ECHO  PROVE-OUT DONE. Logs %QAM%P4 %QAM%P4B %QAM%P5 %QAM%P5K %QAM%P3 %QAM%P0 %QAM%P8
ECHO  Pull them with a LABEL, then run prove-verdict.sh on the pull:
ECHO    bash /tmp/logback-qa.sh prove-%QAM%
ECHO  P8 is only meaningful on the Vibra -- ignore it on other cards.
ECHO ============================================================
ECHO.
:END
