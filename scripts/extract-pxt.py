#!/usr/bin/env python3
"""
Cave Story Pixtone (PXT) extractor.

Mirrors vendor/nxengine-evo/src/extract/extractpxt.cpp's `extract_pxt()`
algorithm exactly: reads parameter blocks at fixed offsets in the 2004 EN
freeware Doukutsu.exe and emits one ASCII text file per SFX slot under
data/pxt/fx<HEX-ID>.pxt — the format Pixtone.cpp expects at runtime.

Usage: extract_pxt.py <path-to-Doukutsu.exe> <output-pxt-dir>
"""

import os
import struct
import sys

# fields[]: name + is_integer (1=int32 LE, 0=double LE)
FIELDS = [
    ("use  ", 1), ("size ", 1),
    ("main_model   ", 1), ("main_freq    ", 0),
    ("main_top     ", 1), ("main_offset  ", 1),
    ("pitch_model  ", 1), ("pitch_freq   ", 0),
    ("pitch_top    ", 1), ("pitch_offset ", 1),
    ("volume_model ", 1), ("volume_freq  ", 0),
    ("volume_top   ", 1), ("volume_offset", 1),
    ("initialY", 1), ("ax      ", 1),
    ("ay      ", 1), ("bx      ", 1),
    ("by      ", 1), ("cx      ", 1),
    ("cy      ", 1),
]

# snd[]: (id, nchanl, offset) -- transcribed verbatim from extractpxt.cpp
SND = [
    (0x01, 1, 0x0907b0), (0x02, 1, 0x0909e0), (0x03, 1, 0x0934c0), (0x04, 1, 0x090890),
    (0x05, 1, 0x090660), (0x06, 1, 0x093530), (0x07, 1, 0x0935a0), (0x0b, 1, 0x090740),
    (0x0c, 2, 0x090c80), (0x0e, 1, 0x090a50), (0x0f, 1, 0x08fbe0), (0x10, 2, 0x090350),
    (0x11, 3, 0x090430), (0x12, 1, 0x090820), (0x14, 2, 0x090900), (0x15, 1, 0x090c10),
    (0x16, 1, 0x0906d0), (0x17, 1, 0x08fcc0), (0x18, 1, 0x08fc50), (0x19, 2, 0x090d60),
    (0x1a, 2, 0x090b30), (0x1b, 1, 0x090e40), (0x1c, 2, 0x0910e0), (0x1d, 1, 0x0911c0),
    (0x1e, 1, 0x091ee0), (0x1f, 1, 0x091310), (0x20, 2, 0x08f940), (0x21, 2, 0x08fa20),
    (0x22, 2, 0x08fb00), (0x23, 3, 0x090eb0), (0x25, 2, 0x092810), (0x26, 2, 0x091230),
    (0x27, 3, 0x091000), (0x28, 2, 0x092730), (0x29, 2, 0x092730), (0x2a, 1, 0x091380),
    (0x2b, 1, 0x0913f0), (0x2c, 3, 0x091460), (0x2d, 1, 0x0915b0), (0x2e, 1, 0x091620),
    (0x2f, 1, 0x091700), (0x30, 1, 0x091770), (0x31, 2, 0x0917e0), (0x32, 2, 0x08fd30),
    (0x33, 2, 0x08fe10), (0x34, 2, 0x08fef0), (0x35, 2, 0x090580), (0x36, 2, 0x091a80),
    (0x37, 2, 0x092ea0), (0x38, 2, 0x092650), (0x39, 2, 0x0928f0), (0x3a, 2, 0x092dc0),
    (0x3b, 1, 0x093060), (0x3c, 1, 0x0930d0), (0x3d, 1, 0x093140), (0x3e, 2, 0x0931b0),
    (0x3f, 2, 0x093290), (0x40, 2, 0x093370), (0x41, 1, 0x093450), (0x46, 2, 0x08ffd0),
    (0x47, 2, 0x0900b0), (0x48, 2, 0x090190), (0x64, 1, 0x0918c0), (0x65, 3, 0x091930),
    (0x66, 2, 0x091b60), (0x67, 2, 0x091c40), (0x68, 1, 0x091cb0), (0x68, 1, 0x092c00),
    (0x69, 1, 0x091d20), (0x6a, 2, 0x091d90), (0x6b, 1, 0x091e70), (0x6c, 1, 0x091f50),
    (0x6d, 1, 0x091fc0), (0x6e, 1, 0x092030), (0x6f, 1, 0x0920a0), (0x70, 1, 0x092110),
    (0x71, 1, 0x092180), (0x72, 2, 0x0921f0), (0x73, 3, 0x092ab0), (0x74, 3, 0x092c70),
    (0x75, 2, 0x092f80), (0x96, 2, 0x0922d0), (0x97, 2, 0x0923b0), (0x98, 1, 0x092490),
    (0x99, 1, 0x092500), (0x9a, 2, 0x092570), (0x9b, 2, 0x0929d0),
]


def main():
    if len(sys.argv) != 3:
        sys.exit(f"usage: {sys.argv[0]} <Doukutsu.exe> <output-pxt-dir>")

    exe_path, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)

    with open(exe_path, "rb") as fp:
        for sid, nchanl, offset in SND:
            chan = [[None] * len(FIELDS) for _ in range(4)]
            fp.seek(offset)
            for c in range(nchanl):
                for i, (_name, is_int) in enumerate(FIELDS):
                    if is_int:
                        chan[c][i] = struct.unpack("<i", fp.read(4))[0]
                    else:
                        # fgetfloat: skip 4 bytes, then read 8-byte double LE
                        fp.read(4)
                        chan[c][i] = struct.unpack("<d", fp.read(8))[0]
                # padding sentinel — must be 0
                pad = struct.unpack("<I", fp.read(4))[0]
                if pad != 0:
                    sys.exit(f"PXT out of sync at id=0x{sid:02x} channel {c}: pad={pad}")
            # fill remaining channels with zeros (matches memset(chan, 0, ...))
            for c in range(nchanl, 4):
                for i, (_name, is_int) in enumerate(FIELDS):
                    chan[c][i] = 0 if is_int else 0.0

            out_path = os.path.join(out_dir, f"fx{sid:02x}.pxt")
            with open(out_path, "wb") as fpo:
                # human-readable section
                for c in range(4):
                    for i, (name, is_int) in enumerate(FIELDS):
                        v = chan[c][i]
                        line = f"{name}:{v}\r\n" if is_int else f"{name}:{v:.2f}\r\n"
                        fpo.write(line.encode("ascii"))
                    fpo.write(b"\r\n")
                # machine-readable section
                for c in range(4):
                    fpo.write(b"{")
                    parts = []
                    for i, (name, is_int) in enumerate(FIELDS):
                        v = chan[c][i]
                        parts.append(f"{v}" if is_int else f"{v:.2f}")
                    fpo.write(",".join(parts).encode("ascii"))
                    fpo.write(b"},\r\n")

    print(f"extracted {len(SND)} pxt files to {out_dir}/")


if __name__ == "__main__":
    main()
