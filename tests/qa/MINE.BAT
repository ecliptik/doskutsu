@ECHO OFF
REM ===================================================================
REM  MINE -- measure the milliseconds nothing has ever seen.
REM
REM  The frame is ~44 ms and three regions of it have never been
REM  measured during actual gameplay:
REM
REM    mode layer   ~4.6-5.6 ms  HUD, post, textbox, fade, ScreenEffects
REM    flip-inner   ~2.4 ms      the part of flip that is not the present
REM    inter-flip   ~5.9 ms      event pump, input poll, FT accumulator
REM
REM  This cell attacks the first two. The third has NO instrumentation
REM  at all and needs a patch -- it is not measured here, and its
REM  absence from this log is expected rather than a fault.
REM
REM  WHY THE MODE LAYER WAS BLIND: patch 0152 moved the mode_tick parent
REM  composition into Game::tick's tail, which the default fixed-timestep
REM  path never calls, so the slice timers fired but the parent was never
REM  assembled and the residual never pushed. Round J saw 2 emitted
REM  blocks in a 271 s run, both on the title screen. Patch 0312 fixes
REM  it; this cell is the first hardware measurement of that layer.
REM
REM  FLIP_BODY_INSTR (patch 0124) needed NO patch -- it has shipped for
REM  many waves and has simply never been switched on in any cell.
REM
REM  FOUR HINTS, ONE CHAIN. The decomposition is a chain of independent
REM  layer toggles; setting only the top yields a split with nothing
REM  under it. All are set AFTER CLRENV so a future CLRENV regeneration
REM  cannot silently blind the cell.
REM
REM  NOT AN FPS ROW. Read the decomposition, do not bank the frame rate.
REM
REM  Usage:  QA [n]   then   MINE [n]      (switches the PicoGUS itself)
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
ECHO  MINE -- unmeasured-milliseconds lane
ECHO ============================================================
ECHO  Give it the CPU in the box:  MINE 1  (1 POD-83  2 Am5x86
ECHO  3 DX2-66  4 DX2-50). Nothing ran.
GOTO END
:START
IF NOT EXIST LOGS\NUL MKDIR LOGS
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%MINE.NFO
ECHO sweep=MINE >> LOGS\%QAM%MINE.NFO
ECHO cells=2 >> LOGS\%QAM%MINE.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%MINE.NFO
ECHO cpu_arg=%2 >> LOGS\%QAM%MINE.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%MINE.NFO
REM  Record the CONFIG.SYS boot profile. Unlike a SET, %config% is
REM  chosen at the boot menu and cannot be retrofitted onto a run that
REM  did not boot that way, so a log PROVES its own provenance rather
REM  than attesting to the absence of something. Catches a cell run on
REM  the wrong profile -- e.g. VIBRAUSB with no Vibra fitted, which
REM  yields no DAC at all and would otherwise look like silent audio
REM  with no record of why.
ECHO config=%config% >> LOGS\%QAM%MINE.NFO
ECHO sound=PicoGUS forced to SB mode by this sweep >> LOGS\%QAM%MINE.NFO
ECHO note=mode layer + flip-body -- NOT an fps row >> LOGS\%QAM%MINE.NFO
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
ECHO  UNMEASURED MILLISECONDS -- breakdown cells, NOT fps rows
ECHO  machine %QAM%   2 cells   logs tag %QAM%MN and %QAM%MF
ECHO ============================================================
ECHO  Afterwards each log should carry, repeatedly and past block 1:
ECHO.
ECHO    [mode-tick-stat n=.. phase=mode_draw_hud ..]
ECHO    [flip-body-stat ..]
ECHO.
ECHO  The mode-tick lines must show n in the DOZENS. If n is 2 or the
ECHO  lines are title-screen only, patch 0312 did not take -- say so,
ECHO  the cell answered nothing.
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
pgusinit /mode sb
pgusinit /sbenv
SET BLASTER=A220 I7 D3 P330 T3
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_FINE_INSTR=1
SET SDL_HINT_DOSKUTSU_MODE_TICK_INSTR=1
SET SDL_HINT_DOSKUTSU_MODE_DRAW_SCENE_INSTR=1
SET DOSKUTSU_LOG_TAG=%QAM%MN
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [1/2]  MODE LAYER   log %QAM%MN
ECHO  first hardware measurement of HUD/post/fade during gameplay
ECHO ============================================
DOSKUTSU.EXE
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_FLIP_INSTR=1
SET SDL_HINT_DOSKUTSU_FLIP_BODY_INSTR=1
SET DOSKUTSU_LOG_TAG=%QAM%MF
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [2/2]  FLIP BODY   log %QAM%MF
ECHO  the 2.4 ms of flip that is not the present call
ECHO ============================================
DOSKUTSU.EXE
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
SET SDL_HINT_DOSKUTSU_FINE_INSTR=
SET SDL_HINT_DOSKUTSU_MODE_TICK_INSTR=
SET SDL_HINT_DOSKUTSU_MODE_DRAW_SCENE_INSTR=
SET SDL_HINT_DOSKUTSU_FLIP_INSTR=
SET SDL_HINT_DOSKUTSU_FLIP_BODY_INSTR=
SET DOSKUTSU_TAS_REPLAY=
ECHO.
ECHO ============================================================
ECHO  MINE DONE -- logs %QAM%MN and %QAM%MF
ECHO  All hints cleared -- safe to run other sweeps.
ECHO ============================================================
:END
