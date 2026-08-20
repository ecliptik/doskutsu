@ECHO OFF
REM ===================================================================
REM  BDIAG -- localize the Mach64 black right/bottom defect.
REM
REM  AN INSTRUMENT, NOT A FIX. Nothing in this binary attempts to
REM  correct the backdrop. Four proposed mechanisms have been wrong:
REM  0297's per-drawcall gate, 0308's thrash arm, a dirty-rect
REM  coordinate theory, and the flush-shape theory behind the retracted
REM  0309. Six draw/flush code paths each read as correctly centred and
REM  the flush rect is witnessed correct in the round-4 logs, so this
REM  round stops guessing and measures.
REM
REM  Neither emulator can stand in for this cell. DOSBox-X engages the
REM  same centring path at 640x480 and renders CORRECTLY. 86Box's Mach64
REM  advertises the VESA double-scan bit that this card clears, so it
REM  offers 320x240, never centres, and cannot express the defect. This
REM  machine is the only place the question can be asked.
REM
REM  What it does: reads the window surface back after drawing and
REM  reports 16 sampled pixel values from inside the game box, once per
REM  100 flips. Answers whether the missing pixels are absent from the
REM  SURFACE or only from the SCREEN.
REM
REM  NOT AN FPS ROW.
REM
REM  Usage:  QA [n]   then   BDIAG [n]
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
ECHO  BDIAG -- Mach64 backdrop diagnostic lane
ECHO ============================================================
ECHO  Give it the CPU in the box:  BDIAG 1  (1 POD-83  2 Am5x86
ECHO  3 DX2-66  4 DX2-50). Nothing ran.
GOTO END
:START
IF NOT EXIST LOGS\NUL MKDIR LOGS
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%BDIAG.NFO
ECHO sweep=BDIAG >> LOGS\%QAM%BDIAG.NFO
ECHO cells=1 >> LOGS\%QAM%BDIAG.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%BDIAG.NFO
ECHO cpu_arg=%2 >> LOGS\%QAM%BDIAG.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%BDIAG.NFO
ECHO video=ATI Mach64 -- REQUIRED, cell is meaningless on any other card >> LOGS\%QAM%BDIAG.NFO
ECHO note=SURFACE_EXTENT=1 -- diagnostic, NOT an fps row >> LOGS\%QAM%BDIAG.NFO
SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=10000
ECHO ============================================================
ECHO  BACKDROP DIAGNOSTIC -- Mach64 only, NOT an fps row
ECHO  machine %QAM%   1 cell   log %QAM%BD
ECHO ============================================================
ECHO  The log should carry, repeatedly:
ECHO.
ECHO    surface-extent: box=320x240@96,72 pitch=512 probes(...)=[..]
ECHO.
ECHO  If that line never appears, centring did not engage -- check the
ECHO  center-oversized line and say so; the cell answered nothing.
ECHO ============================================================
PAUSE
ECHO.
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_SURFACE_EXTENT=1
SET DOSKUTSU_LOG_TAG=%QAM%BD
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  CELL BD   log %QAM%BD
ECHO  watch the screen: the defect should be VISIBLE while this runs
ECHO ============================================
DOSKUTSU.EXE
SET SDL_HINT_DOSKUTSU_SURFACE_EXTENT=
SET DOSKUTSU_TAS_REPLAY=
ECHO.
ECHO ============================================================
ECHO  BDIAG DONE -- log %QAM%BD
ECHO  SURFACE_EXTENT cleared -- safe to run other sweeps.
ECHO ============================================================
:END
