@ECHO OFF
REM QHEXIT.BAT - DOS-exit hang isolation probe family, task #32 SECONDARY.
REM Tests whether the post-SDL_Quit DJGPP/CWSDPMI exit unwind (atexit /
REM uclock PIT-restore / INT 21h 0x4C) hangs in ISOLATION (no engine state).
REM Markers are raw write()+fsync to a kept-open fd, so the LAST line on disk
REM names the region that hung even if the wedge is in libc cleanup:
REM   M1 post-SDL_Quit (cell A only) / M4 pre-return / M2 atexit-runs-first /
REM   M3 atexit-runs-last / then a "clean exit" tail line.
REM
REM 3 cells, 2 binaries:
REM   Cell A  QHEXIT.EXE      -> QHEXITA.LOG  (SDL init+ticks+quit+exit; uclock armed by SDL)
REM   Cell U  QHEXITP.EXE U   -> QHEXITU.LOG  (pure DJGPP, uclock() armed + exit)
REM   Cell B  QHEXITP.EXE B   -> QHEXITB.LOG  (pure DJGPP, no uclock + exit; baseline)
REM Read: U-hang + B-clean = uclock PIT-restore hangs in isolation (decisive).
REM   A-hang + U-clean = wedge needs SDL exit teardown. all clean = inconclusive
REM   (one-way test; clean does NOT exonerate the exit path).
REM
REM Run B first (most likely clean -> proves the rig), then U, then A. Any cell
REM MAY HANG by design (that is the positive result). Hard-reset is OK; per-line
REM fsync means every LOG survives the hang. No env vars. CWSDPMI.EXE same dir.
ECHO === Cell B: QHEXITP B (baseline, no uclock) ===
QHEXITP.EXE B
ECHO Cell B returned to DOS. QHEXITB.LOG written.
ECHO.
ECHO === Cell U: QHEXITP U (uclock armed, SDL-free) ===
QHEXITP.EXE U
ECHO Cell U returned to DOS. QHEXITU.LOG written.
ECHO.
ECHO === Cell A: QHEXIT (SDL init+ticks+quit) ===
QHEXIT.EXE
ECHO Cell A returned to DOS. QHEXITA.LOG written.
ECHO ----
ECHO QHEXITB.LOG:
TYPE QHEXITB.LOG
ECHO ----
ECHO QHEXITU.LOG:
TYPE QHEXITU.LOG
ECHO ----
ECHO QHEXITA.LOG:
TYPE QHEXITA.LOG
