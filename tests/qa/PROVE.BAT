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
ECHO  CELLS : 6 cells, ~15 min, fully unattended
ECHO  CPU   : %QACPU%
ECHO  SOUND : PicoGUS REQUIRED -- this sweep switches it sb -> adlib
ECHO  VIDEO : whichever card is installed -- run once per card
ECHO  LOGS  : tagged %QAM%P..  in LOGS\
ECHO ============================================================
ECHO  P5 vs P5K is the KEY pair: it A/Bs the timebase fix on real
ECHO  hardware, the one thing DOSBox structurally cannot test.
ECHO  The AdLib cells NEED /mode adlib -- with an SB hot the music
ECHO  pump refuses to start and the A/B would measure nothing.
ECHO ============================================================
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%PR.NFO
ECHO sweep=PROVE >> LOGS\%QAM%PR.NFO
ECHO cells=6 >> LOGS\%QAM%PR.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%PR.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%PR.NFO
ECHO sound=PicoGUS (sb for P4/P4B/P3/P0, adlib for P5/P5K) >> LOGS\%QAM%PR.NFO
ECHO video=run once per card >> LOGS\%QAM%PR.NFO
ECHO note=run order is recoverable from the [HH:MM:SS] prefixes in the cell logs >> LOGS\%QAM%PR.NFO
SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=10000
PAUSE
ECHO.
ECHO ##########################################################
ECHO  switching PicoGUS -> SB MODE  (P4 P4B P3 P0 need a live SB DAC)
ECHO ##########################################################
pgusinit /mode sb
pgusinit /sbenv
SET BLASTER=A220 I7 D3 P330 T3
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%P4
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [1/6] CELL P4   tag %QAM%P4
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
ECHO  [2/6] CELL P4B   tag %QAM%P4B
ECHO  OPL3 repeat -- the noise floor
ECHO  P4 vs P4B spread IS the noise floor.
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
ECHO  [3/6] CELL P3   tag %QAM%P3
ECHO  Organya -- the re-rendered PCM cache
ECHO  Catches a silently different re-render. Match banked C3.
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
ECHO  [4/6] CELL P0   tag %QAM%P0
ECHO  TRUE audio floor (AUDIO_OFF=1)
ECHO  Device-level off. SILENT.CFG is NOT a floor.
ECHO ============================================
DOSKUTSU.EXE
ECHO.
ECHO ##########################################################
ECHO  switching PicoGUS -> ADLIB MODE  (no SB, so the PIT pump can own ch0)
ECHO ##########################################################
pgusinit /mode adlib
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ADLIB.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%P5
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [5/6] CELL P5   tag %QAM%P5
ECHO  AdLib, timebase fix ON -- THE KEY CELL
ECHO  Expect 10-40ms band POPULATED, pump_clock_state=pump-timebase-ok.
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
ECHO  [6/6] CELL P5K   tag %QAM%P5K
ECHO  AdLib, timebase fix OFF -- the A/B arm
ECHO  Expect that band EXACTLY EMPTY. Proves the lever on real silicon.
ECHO ============================================
DOSKUTSU.EXE
ECHO.
ECHO ============================================================
ECHO  PROVE-OUT DONE. Logs %QAM%P4 %QAM%P4B %QAM%P3 %QAM%P0 %QAM%P5 %QAM%P5K
ECHO  The PicoGUS is left in ADLIB mode -- re-run pgusinit if you
ECHO  next want sb or gus.
ECHO  Vibra card-vs-DMA-path check is GAP.BAT cell X8, not this sweep.
ECHO ============================================================
ECHO.
:END
