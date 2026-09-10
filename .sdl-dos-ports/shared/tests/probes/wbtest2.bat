@ECHO OFF
REM WBTEST2.BAT - WBTEST-002: WaveBlaster MIDI 4-path probe (v1.0.2 task #4).
REM
REM Revision of WBTEST-001 (tests/probes/wbtest.bat) with:
REM   - PATH=D byte-faithfulness fix (added 0x3F UART entry write)
REM   - PATH=DI + PATH=PI new (test H6 -- MPU full reset + ACK dance)
REM   - GM Master Volume + CC07 + drum-kit + CC120 added per-path (H3)
REM   - CT1745 mixer reg dump at boot, 38 regs verbatim from mpuwbprobe.c
REM     section 2 reg list. READ-ONLY (0x3C is sticky on CT2490; do NOT
REM     attempt to write-flip; the read value IS the H2 evidence).
REM   - MPU ACK polling switched to DATA-port direct-byte detect (~5ms cap)
REM     since W22-WB-F established status-bit-7 lies on this chip.
REM
REM Paths (sequential under default arg A):
REM   PATH=D   = byte-faithful direct-port blind  (synth.c L182 + L260)
REM   PATH=DI  = direct + full reset/ACK dance (HAZARD: 5ms bounded ACK polls)
REM   PATH=P   = byte-faithful DSP-mediated  (sb.c L568-580; 100us reset hold)
REM   PATH=PI  = MPU reset prelude + DSP-mediated
REM
REM Per-path musical phrase: GM Master Volume MAX + CC07 max ch0+ch9 +
REM piano triad C-E-G (~5s) + drum-kit hit ch 9 note 36 (~1s) + CC123 + CC120.
REM Drum-kit isolates "voice engine on?" from "program/bank correct?".
REM
REM Total runtime: ~28 sec (4 paths x ~6s + 3 inter-path gaps x ~1s + boot diag).
REM
REM CAVEAT: W22-WB-F audibility claims on these byte sequences were NEVER
REM ear-confirmed. Operator's ear on g2k is the SOLE truth for audibility.
REM
REM Operator: PicoGUS expected PHYSICALLY OUT for this iter.
REM
REM HAZARD class:
REM   PATH=D  = low  (blind outportb only)
REM   PATH=DI = MODERATE (bounded ACK polls on data port; 5ms wall cap; if
REM             bus stalls in inportb the cap may not save us. Last
REM             WBTEST2.LOG banner names the stall. Ctrl-Alt-Del safe.)
REM   PATH=P  = low  (DSP-mediated, mirrors W22-WB-F sec.10 which ran clean)
REM   PATH=PI = low  (outportb-only MPU write + DSP-mediated)
ECHO Running WBTEST-002 (4-path WaveBlaster MIDI)...
ECHO   ~0:00         boot diagnostics (BLASTER + DSP + 38-reg mixer dump)
ECHO   ~0:00-0:06   PATH=D   byte-faithful direct-port blind
ECHO   ~0:07-0:13   PATH=DI  direct + full reset/ACK dance (HAZARD)
ECHO   ~0:14-0:20   PATH=P   byte-faithful DSP-mediated
ECHO   ~0:21-0:27   PATH=PI  DSP-mediated + MPU reset prelude
ECHO Listen for: piano / drum_only / boops-beeps / silence / HANG per path.
WBTEST2.EXE A
ECHO ----
ECHO Probe finished. WBTEST2.LOG tail:
TYPE WBTEST2.LOG
ECHO DONE
