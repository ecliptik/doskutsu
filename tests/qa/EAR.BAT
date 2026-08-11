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
ECHO  BY-EAR pairs -- machine %QAM%   3 cells  about 8 min
ECHO ============================================================
ECHO  THESE NEED YOUR EARS. Logs cannot answer them.
ECHO  Run back-to-back so the comparison is fresh.
ECHO ============================================================
ECHO  Q1  THE SHACK (near the end of the reel, a short visit):
ECHO      Does music play for the WHOLE Shack visit, or start late,
ECHO      or never start? Answer for E4 and E3 separately.
ECHO  Q2  ADLIB vs OPL3: is AdLib acceptable as the 486 default?
ECHO      It is MUSIC-ONLY -- PC-speaker SFX, no DAC SFX.
ECHO ============================================================
SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=10000
PAUSE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%E4
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [1/3] CELL E4  OPL3 -- Shack reference
ECHO  tag %QAM%E4
ECHO  LISTEN at the Shack. Music present the whole visit?
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ORGANYA.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%E3
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [2/3] CELL E3  Organya -- Shack suspect
ECHO  tag %QAM%E3
ECHO  LISTEN at the Shack. Silent? Late? Whole visit?
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ADLIB.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%EA
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [3/3] CELL EA  AdLib -- 486 default candidate
ECHO  tag %QAM%EA
ECHO  Compare against E4 by ear. Good enough to recommend?
ECHO ============================================
DOSKUTSU.EXE
ECHO.
ECHO === BY-EAR PAIRS DONE. Write the answers down NOW, while fresh: ===
ECHO ===  E4 Shack music:  whole visit / late / never ===
ECHO ===  E3 Shack music:  whole visit / late / never ===
ECHO ===  EA vs E4:        acceptable / not acceptable as 486 default ===
ECHO.
:END
