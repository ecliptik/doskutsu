@ECHO OFF
IF "%1"=="GO" GOTO RUN
COMMAND /E:2048 /C %0 GO
GOTO END
:RUN
REM ==== QA SHOWCASE RECORDER + IMMEDIATE VERIFY ====
REM The take is only usable if it was recorded under EXACTLY the environment
REM the watch cells replay it under. This BAT pins that environment (CLRENV +
REM OPL3.CFG + seed 12345 + fresh save) for BOTH the record and the verify.
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_TAS_REPLAY=
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_RECORD=QA.TAS
ECHO.
ECHO ***************************************************************
ECHO *  STEP 1 of 2 -- RECORDING. Fresh New Game, seed 12345.      *
ECHO *  Play about 5 min: intro, reach Mimiga Village, 2+ cave     *
ECHO *  doors, get a weapon, some combat, a spread of SFX.        *
ECHO *                                                             *
ECHO *  GET THROUGH THE TITLE SCREEN QUICKLY -- a long idle at     *
ECHO *  the title makes the reel harder to replay in sync.        *
ECHO *                                                             *
ECHO *  WHEN DONE: QUIT VIA THE IN-GAME MENU (ESC) so QA.TAS is    *
ECHO *  saved. DO NOT power off or reset -- that LOSES the take.   *
ECHO ***************************************************************
ECHO.
PAUSE
DOSKUTSU.EXE
SET DOSKUTSU_TAS_RECORD=
ECHO.
DIR QA.TAS
ECHO.
ECHO ***************************************************************
ECHO *  STEP 2 of 2 -- VERIFY. About to REPLAY what you just       *
ECHO *  recorded, under the exact config the watch cells use.      *
ECHO *                                                             *
ECHO *  WATCH IT. It must retrace YOUR route. If the character     *
ECHO *  jumps on the spot, walks into a wall, or the scene is      *
ECHO *  plainly wrong, the take is NOT usable -- re-run RECORD.    *
ECHO *  If it retraces your run to the end, you are good.          *
ECHO ***************************************************************
ECHO.
PAUSE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
SET DOSKUTSU_LOG_TAG=%QAM%RC
DOSKUTSU.EXE
SET DOSKUTSU_TAS_REPLAY=
ECHO.
ECHO ============================================================
ECHO  Did the replay retrace your run?
ECHO    YES - reel is good. Continue with the wizard.
ECHO    NO  - run RECORD again (it overwrites cleanly).
ECHO  Verify log: LOGS\%QAM%RC.LOG
ECHO ============================================================
:END
