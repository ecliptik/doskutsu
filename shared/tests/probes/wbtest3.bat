@ECHO OFF
REM WBTEST3.BAT - WBTEST-003: 5-variant Reset SysEx probe (v1.0.2 task #9).
REM
REM Mission: identify the Reset SysEx that wakes Dream SAM2695 into GM
REM Capital Tone state (mappng program 0 -> Acoustic Grand Piano on ch 0
REM AND note 36 -> kick drum on ch 9). WBTEST-002b PATH=P established that
REM bytes reach the chip with correct pitches + ch9 routing works, but the
REM patch map is non-GM (organ on prog 0, cowbell on note 36).
REM
REM Single lever: Reset SysEx variant.
REM   V1 (control): GM Reset only       (matches WBTEST-002b PATH=P; baseline)
REM   V2:           GS Reset + GM Reset
REM   V3:           XG Reset + GM Reset
REM   V4:           GS Reset + ch9 drum bank
REM   V5:           XG Reset + ch9 drum bank
REM
REM PATH=P byte transport is unchanged from wbtest2.c (verbatim copy).
REM Operator listens; first V to produce piano + kick is the production fix.
REM
REM Per-variant phrase: CC07 vol max ch0+ch9 + prog ch0 piano + C-E-G triad
REM + drum-kit ch9 note 36 + CC123 + CC120. ~6 sec audio per variant.
REM
REM Total runtime: ~35 sec (5 variants x ~6s + 4 inter-variant gaps x ~1s
REM + boot diag).
REM
REM Operator: PicoGUS expected PHYSICALLY OUT (same config as WBTEST-002b).
REM
REM HAZARD: low. PATH=P body verbatim from wbtest2.c which ran clean on g2k.
REM No new I/O patterns; only the MIDI byte content varies (SysEx prefix).
ECHO Running WBTEST-003 (5-variant Reset SysEx probe)...
ECHO   ~0:00         boot diag (BLASTER + DSP + 39-reg mixer dump)
ECHO   ~0:01-0:07   V1   GM Reset only (control; expect organ + cowbell)
ECHO   ~0:08-0:14   V2   GS Reset + GM Reset
ECHO   ~0:15-0:21   V3   XG Reset + GM Reset
ECHO   ~0:22-0:28   V4   GS Reset + ch9 drum bank
ECHO   ~0:29-0:35   V5   XG Reset + ch9 drum bank
ECHO Listen for each: piano + kick = WIN; organ + cowbell = no change.
WBTEST3.EXE A
ECHO ----
ECHO Probe finished. WBTEST3.LOG tail:
TYPE WBTEST3.LOG
ECHO DONE
