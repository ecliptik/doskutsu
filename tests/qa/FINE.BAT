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
REM  Usage:  QA [n]   then   FINE [n]      (switches the PicoGUS itself)
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
ECHO schema=harness-1 >> LOGS\%QAM%FINE.NFO
ECHO sweep=FINE >> LOGS\%QAM%FINE.NFO
ECHO cells=2 >> LOGS\%QAM%FINE.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%FINE.NFO
ECHO cpu_arg=%2 >> LOGS\%QAM%FINE.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%FINE.NFO
REM  Record the CONFIG.SYS boot profile. Unlike a SET, %config% is
REM  chosen at the boot menu and cannot be retrofitted onto a run that
REM  did not boot that way, so a log PROVES its own provenance rather
REM  than attesting to the absence of something. Catches a cell run on
REM  the wrong profile -- e.g. VIBRAUSB with no Vibra fitted, which
REM  yields no DAC at all and would otherwise look like silent audio
REM  with no record of why.
ECHO config=%config% >> LOGS\%QAM%FINE.NFO
ECHO video_declared=%QAVID% >> LOGS\%QAM%FINE.NFO
ECHO sound_declared=%QASND% >> LOGS\%QAM%FINE.NFO
ECHO irq_source=%QAIRQ% >> LOGS\%QAM%FINE.NFO
ECHO dktcap=%DKTCAP% >> LOGS\%QAM%FINE.NFO
ECHO cell=%QAM%C4F class=diag >> LOGS\%QAM%FINE.NFO
ECHO cell=%QAM%51F class=diag >> LOGS\%QAM%FINE.NFO
ECHO sound_note=PicoGUS switched to SB mode by this sweep >> LOGS\%QAM%FINE.NFO
ECHO video=whatever is in the box -- card-independent cell >> LOGS\%QAM%FINE.NFO
ECHO note=FINE_INSTR=1 -- decomposition cell, NOT an fps row >> LOGS\%QAM%FINE.NFO
REM  DKTCAP=1 marks an UNATTENDED CAPTURE session. Default (unset) is
REM  byte-identical to every banked round: the 10 s banner delay stays, and
REM  no mode switch happens. Only an explicit SET DKTCAP=1 changes anything.
IF NOT "%DKTCAP%"=="1" SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=10000
IF "%DKTCAP%"=="1" SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=0
REM  Switch to mode 12h HERE, before the banner below, not only after cells.
REM  The banner is the capture harness's positive witness that DKTCAP took --
REM  but printed into text mode 03h (720x400 @70 Hz) no capture device can
REM  lock it, so the witness would be invisible to the only thing that needs
REM  to read it. Firing the switch first makes the banner, the PAUSE and any
REM  error text capturable. Costs a slow planar redraw of the banner, which
REM  is outside every timed bracket.
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
IF "%DKTCAP%"=="1" ECHO  [DKTCAP=1] capture session: banner delay off, console
IF "%DKTCAP%"=="1" ECHO  returns to mode 12h between cells (text will be SLOW).
IF "%DKTCAP%"=="1" ECHO  SET DKTCAP= to turn off; VGACAP\MODE03 restores text.
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
REM  UNCONDITIONAL, deliberately. This was gated on a `PG` argument that in
REM  practice never fired -- every manifest this campaign has written says
REM  "SB already in the box". The cells worked anyway by INHERITING SB mode
REM  from an earlier sweep, which made them hostage to run order. Round M
REM  scheduled MINE straight after RB, whose last act is `pgusinit /mode
REM  adlib`, so no SB DSP answered at 0x220 and both cells died at sdl_init
REM  one second in. RB switches unconditionally at its top; so does this.
ECHO  switching PicoGUS to SB MODE
pgusinit /mode sb > PGOUT.TMP
TYPE PGOUT.TMP
ECHO pgusmode_readback=begin >> LOGS\%QAM%FINE.NFO
TYPE PGOUT.TMP >> LOGS\%QAM%FINE.NFO
ECHO pgusmode_readback=end >> LOGS\%QAM%FINE.NFO
IF EXIST PGOUT.TMP DEL PGOUT.TMP
pgusinit /sbenv > PGOUT.TMP
TYPE PGOUT.TMP
ECHO sbenv_readback=begin >> LOGS\%QAM%FINE.NFO
TYPE PGOUT.TMP >> LOGS\%QAM%FINE.NFO
ECHO sbenv_readback=end >> LOGS\%QAM%FINE.NFO
IF EXIST PGOUT.TMP DEL PGOUT.TMP
SET BLASTER=A220 I7 D3 P330 T3
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
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
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
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
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
