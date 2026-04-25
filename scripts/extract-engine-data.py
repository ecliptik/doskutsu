#!/usr/bin/env python3
"""
Cave Story engine-data extractor (wavetable.dat + stage.dat).

Mirrors the relevant parts of vendor/nxengine-evo/src/extract/extractfiles.cpp
and extractstages.cpp:

  - wavetable.dat:  raw 25600-byte blob from offset 0x110664 in the 2004 EN
                    freeware Doukutsu.exe (CRC 0xb3a3b7ef per files[] in
                    extractfiles.cpp).
  - stage.dat:      generated from the EXE's embedded EXEMapRecord[NMAPS]
                    table at offset 0x937B0; converted to the engine's
                    runtime MapRecord layout (filename[32] + stagename[35] +
                    6 trailing uint8 indices) and prefixed with one byte
                    NMAPS, exactly the way load_stages() in map.cpp expects.

This script is the freestanding-Python sibling of NXEngine's own extract
tools — we don't ship those because (a) we don't want to cross-build them
through DJGPP and (b) running the extractor at user install-time would mean
shipping a binary that touches Pixel's freeware EXE, which we prefer to do
on the developer side once.

Usage: extract-engine-data.py <Doukutsu.exe> <output-data-dir>
"""

import os
import struct
import sys

# --- wavetable.dat ----------------------------------------------------------

WAVETABLE_OFFSET = 0x110664
WAVETABLE_LENGTH = 25600
WAVETABLE_CRC = 0xB3A3B7EF  # not verified — we trust the offset+length

# --- stage.dat --------------------------------------------------------------

DATA_OFFSET = 0x937B0
NMAPS = 95

# EXEMapRecord layout (extractstages.cpp):
#   char     tileset[32];     // 0..31
#   char     filename[32];    // 32..63
#   int      scroll_type;     // 64..67  (4-byte LE)
#   char     background[32];  // 68..99
#   char     NPCset1[32];     // 100..131
#   char     NPCset2[32];     // 132..163
#   uint8_t  bossNo;          // 164
#   char     caption[35];     // 165..199
# Total: 200 bytes per record.
EXE_RECORD_SIZE = 200
EXE_RECORD_FMT = "<32s32si32s32s32sB35s"
assert struct.calcsize(EXE_RECORD_FMT) == EXE_RECORD_SIZE

# Lookup tables — transcribed verbatim from
# vendor/nxengine-evo/src/extract/extractstages.cpp (npcsetnames) and
# vendor/nxengine-evo/src/stagedata.cpp (tileset_names, backdrop_names).
TILESET_NAMES = [
    "0", "Pens", "Eggs", "EggX", "EggIn", "Store", "Weed", "Barr",
    "Maze", "Sand", "Mimi", "Cave", "River", "Gard", "Almond", "Oside",
    "Cent", "Jail", "White", "Fall", "Hell", "Labo",
]

BACKDROP_NAMES = [
    "bk0", "bkBlue", "bkGreen", "bkBlack", "bkGard", "bkMaze", "bkGray",
    "bkRed", "bkWater", "bkMoon", "bkFog", "bkFall", "bkLight", "bkSunset",
    "bkHellish",
]

NPCSET_NAMES = [
    "guest", "0", "eggs1", "ravil", "weed", "maze", "sand", "omg",
    "cemet", "bllg", "plant", "frog", "curly", "stream", "ironh", "toro",
    "x", "dark", "almo1", "eggs2", "twind", "moon", "cent", "heri",
    "red", "miza", "dr", "almo2", "kings", "hell", "press", "priest",
    "ballos", "island",
]


def cstr(buf):
    """Trim a fixed-width C buffer at first NUL."""
    n = buf.find(b"\x00")
    if n >= 0:
        buf = buf[:n]
    return buf.decode("cp1252")  # Cave Story strings are CP1252 / Shift-JIS-safe ASCII


def find_index(name, table, field, stage_no):
    # extractstages.cpp uses strcasecmp.
    lc = name.lower()
    for i, candidate in enumerate(table):
        if candidate.lower() == lc:
            return i
    sys.exit(
        f"stage {stage_no}: unknown {field} name {name!r} "
        f"(not in {table})"
    )


def extract_wavetable(exe, out_dir):
    out = os.path.join(out_dir, "wavetable.dat")
    exe.seek(WAVETABLE_OFFSET)
    blob = exe.read(WAVETABLE_LENGTH)
    if len(blob) != WAVETABLE_LENGTH:
        sys.exit(f"wavetable: short read at 0x{WAVETABLE_OFFSET:x}")
    with open(out, "wb") as fp:
        fp.write(blob)
    print(f"extracted wavetable.dat ({WAVETABLE_LENGTH} bytes) to {out_dir}/")


def extract_stages(exe, out_dir):
    exe.seek(DATA_OFFSET)
    raw = exe.read(EXE_RECORD_SIZE * NMAPS)
    if len(raw) != EXE_RECORD_SIZE * NMAPS:
        sys.exit(f"stage.dat: short read at 0x{DATA_OFFSET:x}")

    out = os.path.join(out_dir, "stage.dat")
    with open(out, "wb") as fp:
        fp.write(bytes([NMAPS]))  # fputc(NMAPS, fpo)
        for i in range(NMAPS):
            rec = raw[i * EXE_RECORD_SIZE:(i + 1) * EXE_RECORD_SIZE]
            (tileset_b, filename_b, scroll_type, background_b,
             npcset1_b, npcset2_b, bossNo, caption_b) = struct.unpack(EXE_RECORD_FMT, rec)

            tileset = cstr(tileset_b)
            filename = cstr(filename_b)
            background = cstr(background_b)
            npcset1 = cstr(npcset1_b)
            npcset2 = cstr(npcset2_b)
            caption = cstr(caption_b)

            tileset_idx = find_index(tileset, TILESET_NAMES, "tileset", i)
            bg_idx = find_index(background, BACKDROP_NAMES, "backdrop", i)
            npc1_idx = find_index(npcset1, NPCSET_NAMES, "NPCset1", i)
            npc2_idx = find_index(npcset2, NPCSET_NAMES, "NPCset2", i)

            # MapRecord on-disk = same as in-memory (no padding):
            #   char    filename[32]
            #   char    stagename[35]
            #   uint8_t tileset, bg_no, scroll_type, bossNo, NPCset1, NPCset2
            # Total: 73 bytes.
            out_rec = struct.pack(
                "<32s35sBBBBBB",
                filename.encode("cp1252")[:32].ljust(32, b"\x00"),
                caption.encode("cp1252")[:35].ljust(35, b"\x00"),
                tileset_idx,
                bg_idx,
                scroll_type & 0xFF,
                bossNo,
                npc1_idx,
                npc2_idx,
            )
            fp.write(out_rec)

    size = 1 + NMAPS * 73
    print(f"extracted stage.dat ({size} bytes, {NMAPS} stages) to {out_dir}/")


def main():
    if len(sys.argv) != 3:
        sys.exit(f"usage: {sys.argv[0]} <Doukutsu.exe> <output-data-dir>")

    exe_path, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)

    with open(exe_path, "rb") as exe:
        extract_wavetable(exe, out_dir)
        extract_stages(exe, out_dir)


if __name__ == "__main__":
    main()
