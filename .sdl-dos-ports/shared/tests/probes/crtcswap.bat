@ECHO OFF
REM CRTCSWAP.BAT - Cirrus 5434 CRTC start-address encoding probe.
REM Wave-50 cycle 1 (probe-engineer task #52). Gates SDL/0061 helper.
REM
REM What it does: enters VBE mode 0x0101 (640x480x8), fills 4 VRAM slabs
REM with distinct sentinel pixel values, then for each of 4 candidate
REM CRTC start-address encodings (byte/word/dword/scanline) programs the
REM chip to point at offset 76800. Waits one VBL between each, reads back
REM the latched register values + emits to CRTCSWAP.LOG. Operator watches
REM the screen during each 2-second pause: the encoding that produces a
REM visible region change to ORANGE pixels (palette index 0x30) is the
REM correct one for Cirrus 5434 + UNIVBE 6.7.
REM
REM Runtime: ~12 sec total (4 scenarios x ~3 sec each).
REM
REM No env vars. Pure DJGPP. CWSDPMI.EXE must be in same dir.
REM
REM HAZARD: low. Side effects bounded to non-destructive register writes.
REM Probe atexit-restores text mode + zeros the CRTC start-address. If
REM the screen blanks for any reason, Ctrl-Alt-Del recovers cleanly.
ECHO Running CRTCSWAP encoding probe (Cirrus 5434 + UNIVBE 6.7)...
ECHO -----
ECHO BEFORE pressing a key: be ready to OBSERVE the screen during each
ECHO 2-second pause. The encoding that produces VISIBLE ORANGE pixels
ECHO (palette index 0x30) is the correct one. Other encodings will
ECHO likely show no change (dark blue continues) or scrambled output.
ECHO -----
ECHO Press any key to start (or Ctrl-C to abort)...
PAUSE >NUL
CRTCSWAP.EXE
ECHO ----
ECHO Probe finished. CRTCSWAP.LOG tail:
TYPE CRTCSWAP.LOG | FIND "CRTCSWAP"
