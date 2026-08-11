@ECHO OFF
IF "%QAM%"=="" GOTO NOMACH
IF "%1"=="GO" GOTO GO
COMMAND /E:2048 /C %0 GO
GOTO END
:NOMACH
ECHO ERROR: machine not set. Run QA.BAT first (pick your CPU), then re-run.
GOTO END
:GO
ECHO ============================================================
ECHO  GAP cells -- machine %QAM%   4 cells  about 10 min
ECHO ============================================================
ECHO  Fills measurement gaps the main matrix left open.
ECHO  Unattended: no listening required, read the logs afterwards.
ECHO ============================================================
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
ECHO  RUN ON THE VIBRA ONLY. Separates card from DMA path.
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
ECHO === X8 is only meaningful on the Vibra. Skip/ignore it elsewhere. ===
ECHO.
:END
