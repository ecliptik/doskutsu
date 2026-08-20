@ECHO OFF
REM ===================================================================
REM  FINE -- engine decomposition on the FIXED-TIMESTEP path.
REM
REM  WHY THIS EXISTS. Patches 0304/0305/0306 moved the engine timers
REM  off Game::tick() -- which the default fixed-timestep GM_NORMAL
REM  path never calls -- so the engine bucket can finally be measured
REM  during actual gameplay instead of title/menu only.
REM
REM  All three are gated behind SDL_HINT_DOSKUTSU_FINE_INSTR, and in
REM  round 4 NO BAT SET IT. VIDMI sets FLIP_INSTR, which is a different
REM  hint. So the round-4 Mach64 cells ran 40 minutes with the engine
REM  timer switched off and produced the pre-0304 signature: two blocks
REM  on the 1:1 title path, then dark. This cell is the fix.
REM
REM  THREE HINTS, NOT ONE. The decomposition is a chain of independent
REM  layer toggles -- FINE_INSTR gives the coarse audio/engine split,
REM  MODE_TICK_INSTR gives mode_tick_render vs mode_tick_state, and
REM  MODE_DRAW_SCENE_INSTR gives the mds_* leaves inside render.
REM  Setting only the first yields a split with nothing under it. All
REM  three are set here, and all three verified emitting on the fixed
REM  path under DOSBox-X before this cell shipped.
REM
REM  They are set AFTER each CLRENV on purpose. CLRENV is generated
REM  from the hint list and does not clear them today, but if it ever
REM  does, setting them first would silently produce a cell with no
REM  instrumentation that still looks like it ran. That is exactly the
REM  failure this cell was written to stop repeating.
REM
REM  NOT AN FPS ROW. Instrumentation costs roughly 4% on a 486. Read
REM  the decomposition, do not bank the frame rate.
REM
REM  Card-independent -- the engine bucket is not a video question.
REM  Run it on the ViRGE, where there is no oversized-surface confound.
REM
REM  Usage:  QA [n]   then   FINE [n] [PG]
REM ===================================================================
IF "%1"=="GO" GOTO GO
COMMAND /E:2048 /C %0 GO %1 %2
GOTO END
:GO
IF "%2"=="1" SET QAM=G
IF "%2"=="2" SET QAM=A
IF "%2"=="3" SET QAM=6
IF "%2"=="4" SET QAM=5
IF "%2"=="1" SET QACPU=Pentium OverDrive 83
IF "%2"=="2" SET QACPU=Am5x86-133
IF "%2"=="3" SET QACPU=486DX2-66
IF "%2"=="4" SET QACPU=486DX2-50
IF NOT "%QAM%"=="" GOTO START
ECHO ============================================================
ECHO  FINE -- engine decomposition lane
ECHO ============================================================
ECHO  Give it the CPU in the box:  FINE 1  (1 POD-83  2 Am5x86
ECHO  3 DX2-66  4 DX2-50). Nothing ran.
GOTO END
:START
IF NOT EXIST LOGS\NUL MKDIR LOGS
IF "%QAVID%"=="" SET QAVID=UNDECLARED
IF "%QASND%"=="" SET QASND=UNDECLARED
IF "%QAIRQ%"=="" SET QAIRQ=UNDECLARED
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%FINE.NFO
ECHO sweep=FINE >> LOGS\%QAM%FINE.NFO
ECHO cells=2 >> LOGS\%QAM%FINE.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%FINE.NFO
ECHO cpu_arg=%2 >> LOGS\%QAM%FINE.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%FINE.NFO
ECHO config=%config% >> LOGS\%QAM%FINE.NFO
ECHO video_declared=%QAVID% >> LOGS\%QAM%FINE.NFO
ECHO sound_declared=%QASND% >> LOGS\%QAM%FINE.NFO
ECHO cell=%QAM%C4F class=diag >> LOGS\%QAM%FINE.NFO
ECHO cell=%QAM%51F class=diag >> LOGS\%QAM%FINE.NFO
IF "%3"=="PG" ECHO sound_note=PicoGUS in SB mode >> LOGS\%QAM%FINE.NFO
IF NOT "%3"=="PG" ECHO sound=SB already in the box >> LOGS\%QAM%FINE.NFO
ECHO video_note=whatever is in the box -- card-independent cell >> LOGS\%QAM%FINE.NFO
ECHO note=FINE_INSTR=1 -- decomposition cell, NOT an fps row >> LOGS\%QAM%FINE.NFO
SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=10000
ECHO ============================================================
ECHO  ENGINE DECOMPOSITION -- breakdown cells, NOT fps rows
ECHO  machine %QAM%   2 cells   logs tag %QAM%C4F and %QAM%51F
ECHO ============================================================
ECHO  These do NOT overwrite any other lane's rows.
ECHO  Afterwards each log carries, repeatedly and past block 1:
ECHO.
ECHO    engine split block: engine_tick_calls=.. audio_ms=..
ECHO    engine decomposition: mode_tick_render=.. mode_tick_state=..
ECHO.
ECHO  If engine_tick_calls goes to zero after the first two blocks,
ECHO  the cell FAILED -- say so, the run is not usable.
ECHO.
ECHO  These cells run SLOWER than a normal one by design.
ECHO ============================================================
PAUSE
ECHO.
IF NOT "%3"=="PG" GOTO SNDOK
ECHO  switching PicoGUS to SB MODE
pgusinit /mode sb > PGOUT.TMP
TYPE PGOUT.TMP
ECHO pgusmode_readback=begin >> LOGS\%QAM%FINE.NFO
TYPE PGOUT.TMP >> LOGS\%QAM%FINE.NFO
ECHO pgusmode_readback=end >> LOGS\%QAM%FINE.NFO
IF EXIST PGOUT.TMP DEL PGOUT.TMP
pgusinit /sbenv > PGOUT.TMP
TYPE PGOUT.TMP
ECHO pgusmode_readback=begin >> LOGS\%QAM%FINE.NFO
TYPE PGOUT.TMP >> LOGS\%QAM%FINE.NFO
ECHO pgusmode_readback=end >> LOGS\%QAM%FINE.NFO
IF EXIST PGOUT.TMP DEL PGOUT.TMP
SET BLASTER=A220 I7 D3 P330 T3
GOTO SNDOK2
:SNDOK
ECHO  using the SB already in the box (BLASTER left as the boot set it)
:SNDOK2
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_FINE_INSTR=1
SET SDL_HINT_DOSKUTSU_MODE_TICK_INSTR=1
SET SDL_HINT_DOSKUTSU_MODE_DRAW_SCENE_INSTR=1
SET DOSKUTSU_LOG_TAG=%QAM%C4F
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [1/2]  CELL C4 decomposed   log %QAM%C4F
ECHO  same OPL3 config as the C4M anchor
ECHO ============================================
DOSKUTSU.EXE
ECHO  CELL DONE %QAM%C4F
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\AUTO.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_FINE_INSTR=1
SET SDL_HINT_DOSKUTSU_MODE_TICK_INSTR=1
SET SDL_HINT_DOSKUTSU_MODE_DRAW_SCENE_INSTR=1
SET DOSKUTSU_LOG_TAG=%QAM%51F
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [2/2]  CELL G51 decomposed   log %QAM%51F
ECHO  auto-detect audio, same reel
ECHO ============================================
DOSKUTSU.EXE
ECHO  CELL DONE %QAM%51F
SET SDL_HINT_DOSKUTSU_FINE_INSTR=
SET SDL_HINT_DOSKUTSU_MODE_TICK_INSTR=
SET SDL_HINT_DOSKUTSU_MODE_DRAW_SCENE_INSTR=
SET DOSKUTSU_TAS_REPLAY=
ECHO.
ECHO ============================================================
ECHO  FINE DONE -- logs %QAM%C4F and %QAM%51F
ECHO  FINE_INSTR cleared -- safe to run other sweeps.
ECHO ============================================================
:END
