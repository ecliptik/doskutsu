@ECHO OFF
REM LFBNEAR.BAT - LFB nearptr VRAM-write throughput probe (wave-36 ceiling-bust A).
REM Tests whether 19 MB/s dosmemput ceiling is a banked-mode CPU-PIO artifact
REM by directly mapping the Cirrus 5434 LFB via DPMI + nearptr.
REM Output: LFBNEAR.LOG.
REM Runtime: under 10 sec on PODP83 (3 scenarios x 100 reps each, 76800 B/op).
REM HAZARD: enters VBE mode 0x4101 (LFB-mapped); screen looks corrupted during
REM scenarios; restores text mode before exit. May FAIL_NO_LFB cleanly if g2k
REM does not advertise LFB on mode 0x4101 (sentinel emit; no hang).
REM No env vars required. Pure DJGPP; CWSDPMI.EXE same dir.
ECHO Running LFBNEAR probe (LFB nearptr vs dosmemput throughput)...
LFBNEAR.EXE
ECHO ----
ECHO Probe finished. LFBNEAR.LOG tail:
TYPE LFBNEAR.LOG
