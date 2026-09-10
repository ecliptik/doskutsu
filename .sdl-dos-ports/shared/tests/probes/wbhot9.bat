@ECHO OFF
REM WBHOT9.BAT - wbhot v3 cell P9: hot-0x3F undrained-vs-drained differential.
REM SETUP-EXE campaign T48 / iter-5 rev-3 sec 7.4. SDL3-linked probe.
REM Output: LOGS\WP9PROBE.LOG (per-line fsync; last line survives a freeze).
ECHO ============================================================
ECHO WBHOT cell 9 (P9) -- WaveBlaster hot-0x3F mechanism + fix test
ECHO.
ECHO PHASE p9a: SB16 opens + 2 sec baseline (faint 440 Hz tone).
ECHO PHASE p9b: hot MPU UART entry, ACK LEFT UNDRAINED (production
ECHO            shape) + 5 sec watch.
ECHO   EXPECT IF MECHANISM REAL: audio stalls, OR THE MACHINE
ECHO   HARD-FREEZES. A FREEZE HERE IS A VALID RESULT.
ECHO   POWER-CYCLE after a freeze -- the log keeps the verdict.
ECHO PHASE p9c: only if still running -- drain + second hot entry
ECHO            WITH drain (the fix shape) + watches.
ECHO   EXPECT: audio keeps playing = fix shape proven.
ECHO.
ECHO Total runtime if it survives: ~20 sec. ESC skips a watch.
ECHO ============================================================
SET DOS_PORT_LOG_TAG=WP9
WBHOT.EXE 9
ECHO ----
ECHO Probe finished (no freeze). LOGS\WP9PROBE.LOG last lines:
IF EXIST LOGS\WP9PROBE.LOG TYPE LOGS\WP9PROBE.LOG
