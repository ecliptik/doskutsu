@ECHO OFF
IF "%QAM%"=="" GOTO NOMACH
IF "%1"=="GO" GOTO GO
COMMAND /E:2048 /C %0 GO
GOTO END
:NOMACH
ECHO ERROR: machine not set. Run QA.BAT first (pick your CPU), then re-run.
GOTO END
:GO
REM --- QA-INSTRUMENT v1 (matches PG/VB sweep manifests) ---
IF "%QAM%"=="G" SET QACPU=Pentium OverDrive 83
IF "%QAM%"=="A" SET QACPU=Am5x86-133
IF "%QAM%"=="6" SET QACPU=486DX2-66
IF "%QAM%"=="5" SET QACPU=486DX2-50
IF NOT EXIST LOGS\NUL MKDIR LOGS
ECHO ============================================================
ECHO  SWEEP : GAP cells -- closes four measurement gaps
ECHO  CELLS : 4 cells, ~10 min
ECHO  CPU   : %QACPU%
ECHO  SOUND : MIXED -- X8 needs the VIBRA; the rest run on any card
ECHO  VIDEO : S3 ViRGE
ECHO  LOGS  : tagged %QAM%..  in LOGS\
ECHO ============================================================
ECHO  Check the CPU and cards above match the box before starting.
ECHO  X8 is MEANINGLESS off the Vibra -- skip or ignore it there.
ECHO ============================================================
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%GAP.NFO
ECHO sweep=GAP >> LOGS\%QAM%GAP.NFO
ECHO cells=4 >> LOGS\%QAM%GAP.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%GAP.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%GAP.NFO
ECHO sound=MIXED -- X8 needs the VIBRA; the rest run on any card >> LOGS\%QAM%GAP.NFO
ECHO video=S3 ViRGE >> LOGS\%QAM%GAP.NFO
ECHO note=run order is recoverable from the [HH:MM:SS] prefixes in the cell logs >> LOGS\%QAM%GAP.NFO
SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=10000
PAUSE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_AUDIO_OFF=1
SET DOSKUTSU_LOG_TAG=%QAM%X0
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [1/4] CELL X0  TRUE audio floor (AUDIO_OFF=1)
ECHO  tag %QAM%X0
ECHO  Device-level off. NOT the same as SILENT.CFG.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_AUDIO_SB_FORCE_8BIT=1
SET DOSKUTSU_LOG_TAG=%QAM%X8
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [2/4] CELL X8  Vibra forced to 8-bit DMA
ECHO  tag %QAM%X8
ECHO  VIBRA ONLY. Separates card from DMA path.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ORGHQ.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%XH1
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [3/4] CELL XH1  Organya-HQ re-measure, run 1 of 2
ECHO  tag %QAM%XH1
ECHO  5112 gave an impossible ratio. Two runs test reproducibility.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ORGHQ.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%XH2
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [4/4] CELL XH2  Organya-HQ re-measure, run 2 of 2
ECHO  tag %QAM%XH2
ECHO  If XH1 and XH2 disagree, ORGHQ is non-reproducible.
ECHO ============================================
DOSKUTSU.EXE
ECHO.
ECHO === GAP CELLS DONE. Logs %QAM%X0 %QAM%X8 %QAM%XH1 %QAM%XH2 ===
ECHO.
:END
