@ECHO OFF
REM ===================================================================
REM  WC -- composite world cache, THREE ARMS ON ONE BINARY.
REM
REM  All three arms are in this one binary on purpose. This campaign has
REM  repeatedly found cross-binary comparison unreliable, so the control
REM  and both cache arms run on the same reel, same boot, same silicon.
REM
REM    cell 1  control    no hints        uncached, today's renderer
REM    cell 2  stage 3a   WORLD_CACHE=1   composites EVERY frame
REM    cell 3  stage 3b   + CACHE_KEY=1   tile-aligned key, hits possible
REM
REM  CELL 2 IS EXPECTED TO BE SLOWER THAN CELL 1. It rebuilds the
REM  composite unconditionally with no key and no possible hit, so it
REM  pays the compositing cost on top of everything else. That is what
REM  it is for -- it is the correctness arm. A slower cell 2 is the
REM  designed result, NOT a failure. The comparison that matters is
REM  cell 3 against cell 1.
REM
REM  THE NUMBER TO PULL IS THE HIT RATE, NOT THE FPS. In the log:
REM    [world-cache n=100 hits=.. composites=.. declined=.. hit_pct_x10=..]
REM  Break-even is hit_pct_x10=330. Under DOSBox it read 990-1000, but
REM  that route barely moves the camera and is NOT a gameplay figure --
REM  round N measured 71%% static frames on this reel, so expect roughly
REM  700-900. If it comes back near 1000 on a reel that scrolls, be
REM  suspicious rather than pleased: more likely the key is not
REM  invalidating than that the cache is perfect. A large declined=
REM  means the cache stood down and cell 3 measured almost nothing.
REM
REM  WATCH THE SCREEN ON CELL 3. The DOSBox screenshots are identical to
REM  the control, but that route is mostly stationary, so the
REM  invalidation paths are barely exercised there. Watch for:
REM    - scrolling edges: tiles smearing, tearing, or lagging the camera
REM    - a destructible tile being shot: hole must appear immediately
REM    - a sprite walking behind foreground geometry: must stay occluded
REM    - room transitions: no leftover tiles from the previous room
REM  Every prior cache in this codebase shipped a stale or black-region
REM  artifact at some point. Treat a clean run as evidence, not a given.
REM
REM  Usage:  QA [n]   then   WC [n]      (switches the PicoGUS itself)
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
ECHO  WC -- world cache, three arms
ECHO ============================================================
ECHO  Give it the CPU in the box:  WC 1  (1 POD-83  2 Am5x86
ECHO  3 DX2-66  4 DX2-50). Nothing ran.
GOTO END
:START
IF NOT EXIST LOGS\NUL MKDIR LOGS
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%WC.NFO
ECHO sweep=WC >> LOGS\%QAM%WC.NFO
ECHO cells=3 >> LOGS\%QAM%WC.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%WC.NFO
ECHO cpu_arg=%2 >> LOGS\%QAM%WC.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%WC.NFO
REM  Record the CONFIG.SYS boot profile. Unlike a SET, %config% is
REM  chosen at the boot menu and cannot be retrofitted onto a run that
REM  did not boot that way, so a log PROVES its own provenance rather
REM  than attesting to the absence of something. Catches a cell run on
REM  the wrong profile -- e.g. VIBRAUSB with no Vibra fitted, which
REM  yields no DAC at all and would otherwise look like silent audio
REM  with no record of why.
ECHO config=%config% >> LOGS\%QAM%WC.NFO
ECHO sound=PicoGUS forced to SB mode by this sweep >> LOGS\%QAM%WC.NFO
ECHO note=world cache 3 arms -- cell 2 EXPECTED slower; pull hit_pct_x10 >> LOGS\%QAM%WC.NFO
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
ECHO  WORLD CACHE -- three arms, one binary
ECHO  machine %QAM%   3 cells   tags %QAM%W0 %QAM%WA %QAM%WB
ECHO ============================================================
ECHO   1 control       2 stage 3a       3 stage 3b (keyed)
ECHO.
ECHO  CELL 2 IS SUPPOSED TO BE SLOWER. It is the correctness arm.
ECHO  Compare cell 3 against cell 1, not against cell 2.
ECHO.
ECHO  WATCH CELL 3: scrolling edges, a destructible tile being shot,
ECHO  a sprite behind foreground geometry, room transitions.
ECHO ============================================================
PAUSE
ECHO.
REM  Unconditional PicoGUS switch -- the old opt-in PG argument never
REM  fired and left sweeps hostage to run order; RB hands back an
REM  ADLIB-mode card and the next sweep died at sdl_init.
ECHO  switching PicoGUS to SB MODE
pgusinit /mode sb
pgusinit /sbenv
SET BLASTER=A220 I7 D3 P330 T3
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%W0
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [1/3]  CONTROL -- uncached   log %QAM%W0
ECHO ============================================
DOSKUTSU.EXE
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_WORLD_CACHE=1
SET DOSKUTSU_LOG_TAG=%QAM%WA
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [2/3]  STAGE 3a   log %QAM%WA
ECHO  EXPECTED TO BE SLOWER -- correctness arm, composites every frame
ECHO ============================================
DOSKUTSU.EXE
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_WORLD_CACHE=1
SET SDL_HINT_DOSKUTSU_WORLD_CACHE_KEY=1
SET DOSKUTSU_LOG_TAG=%QAM%WB
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [3/3]  STAGE 3b -- keyed   log %QAM%WB
ECHO  WATCH THE SCREEN. Pull hit_pct_x10 from the log.
ECHO ============================================
DOSKUTSU.EXE
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
SET SDL_HINT_DOSKUTSU_WORLD_CACHE=
SET SDL_HINT_DOSKUTSU_WORLD_CACHE_KEY=
SET DOSKUTSU_TAS_REPLAY=
ECHO.
ECHO ============================================================
ECHO  WC DONE -- logs %QAM%W0 %QAM%WA %QAM%WB
ECHO  Both hints cleared -- safe to run other sweeps.
ECHO ============================================================
:END
