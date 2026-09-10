@ECHO OFF
REM WBTEST4.BAT - WBTEST-004: 5-variant program-change matrix (v1.0.2 task #12).
REM
REM Mission: discriminate whether MIDI program-change (status byte 0xCn) is
REM honored at all by Dream SAM2695 on g2k. WBTEST-003 established that none
REM of GM/GS/XG/GS+drum/XG+drum Reset SysEx variants shift the chip out of
REM its power-up patch map. Per H16: maybe program-change is silently dropped.
REM
REM Single lever: program-change value. PATH=P byte transport unchanged from
REM wbtest3.c (verbatim copy). NO Reset SysEx prefix. NO drum hit.
REM
REM Variants (varies only the C0 vv byte; chosen to span GM tone palette):
REM   V1 (control): C0 00  GM Acoustic Grand Piano  -> g2k baseline = organ
REM   V2:           C0 10  GM Drawbar Organ
REM   V3:           C0 20  GM Acoustic Bass
REM   V4:           C0 40  GM Soprano Sax
REM   V5:           C0 60  GM FX1 Rain
REM
REM Per-variant phrase: CC07 vol max + prog change + C-E-G triad (~5 sec).
REM Total runtime: ~30 sec (5 variants x ~5s + 4 inter-variant gaps x ~1s
REM + boot diag).
REM
REM Operator listens for relative timbre changes V1->V5. Key question: does
REM V2 sound DIFFERENT from V1? Does V3 sound DIFFERENT from V2? If all 5
REM are indistinguishable, H16 confirmed and next iter = Doom-init replicate.
REM
REM Operator: PicoGUS expected PHYSICALLY OUT (same config as WBTEST-002b/003).
REM
REM HAZARD: low. PATH=P body verbatim from wbtest3.c which ran clean on g2k.
ECHO Running WBTEST-004 (5-variant program-change matrix)...
ECHO   ~0:00         boot diag (BLASTER + DSP + 39-reg mixer dump)
ECHO   ~0:01-0:06   V1   prog 0x00 (GM Piano; control = organ baseline)
ECHO   ~0:07-0:12   V2   prog 0x10 (GM Drawbar Organ)
ECHO   ~0:13-0:18   V3   prog 0x20 (GM Acoustic Bass)
ECHO   ~0:19-0:24   V4   prog 0x40 (GM Soprano Sax)
ECHO   ~0:25-0:30   V5   prog 0x60 (GM FX1 Rain)
ECHO Listen: are V2-V5 distinct from V1? All-identical = H16 confirmed.
WBTEST4.EXE A
ECHO ----
ECHO Probe finished. WBTEST4.LOG tail:
TYPE WBTEST4.LOG
ECHO DONE
