@ECHO OFF
REM WBTEST.BAT - WBTEST-001: WaveBlaster MIDI A/B probe (v1.0.2 task #1).
REM HISTORICAL: this is the v1 probe; PATH=D omits the production 0x3F UART
REM entry write (probe-faithfulness defect). Corrected version at WBTEST2.BAT.
REM Output: WBTEST.LOG. Runtime ~12 sec.
ECHO Running WBTEST (WaveBlaster MIDI A/B; HISTORICAL v1)...
WBTEST.EXE A
ECHO ----
TYPE WBTEST.LOG
ECHO DONE
