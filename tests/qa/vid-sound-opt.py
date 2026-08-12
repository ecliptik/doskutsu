#!/usr/bin/env python3
"""Make the PicoGUS setup in VIDV/VIDC/VIDM opt-in, so the lanes run on
whatever Sound Blaster is in the box. Idempotent, CRLF-preserving."""
import sys, os
MARK = "REM --- SOUND-OPT v1 ---"
OLD = ["ECHO  switching PicoGUS -> SB MODE (what the other lanes use)",
       "pgusinit /mode sb", "pgusinit /sbenv",
       "SET BLASTER=A220 I7 D3 P330 T3"]
def patch(p):
    raw = open(p,'rb').read().decode('ascii')
    if MARK in raw: return "skip (already patched)"
    lines = raw.split("\r\n")
    if lines and lines[-1]=='': lines.pop()
    try:
        i = lines.index(OLD[0])
    except ValueError:
        return "FAIL: sound block not found"
    if lines[i:i+4] != OLD: return "FAIL: sound block shape changed"
    # the self-relaunch forwards only %1, so a second argument never arrives
    try:
        r = lines.index("COMMAND /E:2048 /C %0 GO %1")
        lines[r] = "COMMAND /E:2048 /C %0 GO %1 %2"
    except ValueError:
        return "FAIL: relaunch line not found"
    new = [
      MARK,
      'REM PicoGUS setup is OPT-IN. Without it the lane uses whatever SB is',
      'REM already in the box -- a Vibra16 needs its own BLASTER (I5 D1 H5),',
      'REM and the PicoGUS jumper values below would break it.',
      'IF NOT "%3"=="PG" GOTO SNDOK',
      'ECHO  switching PicoGUS -> SB MODE',
      'pgusinit /mode sb',
      'pgusinit /sbenv',
      'SET BLASTER=A220 I7 D3 P330 T3',
      'GOTO SNDOK2',
      ':SNDOK',
      'ECHO  using the SB already in the box (BLASTER left as the boot set it)',
      ':SNDOK2',
      MARK,
    ]
    lines[i:i+4] = new
    body = "".join(l+"\r\n" for l in lines)
    if "\r\r" in body: return "FAIL double-CR"
    open(p+".BAK","wb").write(raw.encode('ascii'))
    open(p,"wb").write(body.encode('ascii'))
    return "patched"
d = sys.argv[1]
for b in ("VIDV.BAT","VIDC.BAT","VIDM.BAT"):
    p = os.path.join(d,b)
    print(f"  {b:10s} {patch(p) if os.path.exists(p) else 'not present'}")
