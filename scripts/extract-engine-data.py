#!/usr/bin/env python3
"""
Cave Story engine-data extractor (wavetbl.dat + stage.dat + pixel.bmp +
credit*.bmp end-credits images).

Mirrors the relevant parts of vendor/nxengine-evo/src/extract/extractfiles.cpp
and extractstages.cpp:

  - wavetbl.dat:  raw 25600-byte blob from offset 0x110664 in the 2004 EN
                    freeware Doukutsu.exe (CRC 0xb3a3b7ef per files[] in
                    extractfiles.cpp).
  - stage.dat:      generated from the EXE's embedded EXEMapRecord[NMAPS]
                    table at offset 0x937B0; converted to the engine's
                    runtime MapRecord layout (filename[32] + stagename[35] +
                    6 trailing uint8 indices) and prefixed with one byte
                    NMAPS, exactly the way load_stages() in map.cpp expects.
  - data/endpic/pixel.bmp: 25-byte BMP file-header prefix + 1373 bytes of
                    palette+pixel data from offset 0x16722f (CRC 0x6181d0a1).
                    160x16 4bpp palette image used as a generic blanking
                    sprite by Cave Story's title-screen sprite-sheet list
                    (referenced from data/sprites.sif). Without this file,
                    Sprites::init() leaves the relevant Surface in a
                    NULL-texture state and the engine emits a runtime
                    drawSurface flood (silenced by NXEngine patch 0030,
                    but the missing-file warning still appears in
                    debug.log).
  - data/endpic/credit01.bmp..credit18.bmp: 17 end-credits images
                    (160x240 4bpp), each 25-byte BMP file-header prefix +
                    19293 bytes of palette+pixel data = 19318 bytes per
                    output file. Offsets and CRCs lifted verbatim from
                    extractfiles.cpp's files[] table. credit13.bmp is
                    intentionally absent from the table (the EXE has
                    contiguous slots only for credit01..12 and 14..18).
                    Without these, the post-game credits scene
                    (vendor/nxengine-evo/src/endgame/credits.cpp) shows
                    blank images.

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
import zlib  # CRC-32 verification of extracted blobs

# --- wavetbl.dat ----------------------------------------------------------

WAVETABLE_OFFSET = 0x110664
WAVETABLE_LENGTH = 25600
WAVETABLE_CRC = 0xB3A3B7EF  # not verified — we trust the offset+length

# --- data/endpic/pixel.bmp --------------------------------------------------
#
# Cave Story embeds bitmap resources in the .exe stripped of their 14-byte
# BMP file header (Windows resource format — DIB header onward only). The
# extractor prepends a per-file fixed header to reconstruct a valid BMP.
# pixel.bmp is 160x16 4bpp (16-color palette + 1280 bytes pixel data).
#
# Constants verbatim from extractfiles.cpp's files[] entry for pixel.bmp
# and the pixel_header[] array. CRC-32 is the standard polynomial
# 0x04C11DB7 (reflected input/output, init 0xFFFFFFFF, xor-out 0xFFFFFFFF)
# — i.e., zlib.crc32 — verified by reading vendor/nxengine-evo/src/extract/
# crc.cpp.
PIXEL_BMP_OFFSET = 0x16722F
PIXEL_BMP_LENGTH = 1373
PIXEL_BMP_CRC = 0x6181D0A1
PIXEL_BMP_HEADER = bytes([
    0x42, 0x4D, 0x76, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x76, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0xA0, 0x00,
    0x00, 0x00, 0x10, 0x00, 0x00,
])
assert len(PIXEL_BMP_HEADER) == 25, "pixel.bmp header must be 25 bytes"

# --- data/endpic/credit01.bmp..credit18.bmp ---------------------------------
#
# Same shape as pixel.bmp but for the larger 160x240 4bpp end-credits images.
# 17 entries (credit13 intentionally absent — the EXE's contiguous run jumps
# straight from credit12 to credit14). Each output file is the 25-byte
# CREDIT_HEADER + 19293 bytes lifted from the EXE = 19318 bytes total.
# CRC-32 is computed over the EXE-resident 19293 bytes only (header is not
# CRC'd), matching extractfiles.cpp's behaviour where crc_calc(file, ...) is
# called on the buffer position past the prepended header.
CREDIT_PAYLOAD_LENGTH = 19293
CREDIT_HEADER = bytes([
    0x42, 0x4D, 0x76, 0x4B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x76, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0xA0, 0x00,
    0x00, 0x00, 0xF0, 0x00, 0x00,
])
assert len(CREDIT_HEADER) == 25, "credit*.bmp header must be 25 bytes"

# (filename, offset, crc32) — verbatim from extractfiles.cpp lines 33-49.
CREDIT_FILES = [
    ("credit01.bmp", 0x117047, 0xEB87B19B),
    ("credit02.bmp", 0x11BBAF, 0x239C1A37),
    ("credit03.bmp", 0x120717, 0x4398BBDA),
    ("credit04.bmp", 0x12527F, 0x44BAE3AC),
    ("credit05.bmp", 0x129DE7, 0xD1B876AD),
    ("credit06.bmp", 0x12E94F, 0x5A60082E),
    ("credit07.bmp", 0x1334B7, 0xC1E9DB91),
    ("credit08.bmp", 0x13801F, 0xCBBCC7FA),
    ("credit09.bmp", 0x13CB87, 0xFA7177B1),
    ("credit10.bmp", 0x1416EF, 0x56390A07),
    ("credit11.bmp", 0x146257, 0xFF3D6D83),
    ("credit12.bmp", 0x14ADBF, 0x9E948DC2),
    # credit13.bmp intentionally absent — not in extractfiles.cpp's files[].
    ("credit14.bmp", 0x14F927, 0x32B6CE2D),
    ("credit15.bmp", 0x15448F, 0x88539803),
    ("credit16.bmp", 0x158FF7, 0xC0EF9ADF),
    ("credit17.bmp", 0x15DB5F, 0x8C5A003D),
    ("credit18.bmp", 0x1626C7, 0x66BCBF22),
]
assert len(CREDIT_FILES) == 17, "expected exactly 17 credit*.bmp entries"

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
    out = os.path.join(out_dir, "wavetbl.dat")
    exe.seek(WAVETABLE_OFFSET)
    blob = exe.read(WAVETABLE_LENGTH)
    if len(blob) != WAVETABLE_LENGTH:
        sys.exit(f"wavetable: short read at 0x{WAVETABLE_OFFSET:x}")
    with open(out, "wb") as fp:
        fp.write(blob)
    print(f"extracted wavetbl.dat ({WAVETABLE_LENGTH} bytes) to {out_dir}/")


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


def extract_pixel_bmp(exe, out_dir):
    """
    Extract data/endpic/pixel.bmp by reading 1373 bytes at offset 0x16722f
    in Doukutsu.exe and prepending the 25-byte BMP file-header reconstruction.
    Verifies CRC-32 over the EXE-resident bytes (header is not CRC'd).
    """
    exe.seek(PIXEL_BMP_OFFSET)
    blob = exe.read(PIXEL_BMP_LENGTH)
    if len(blob) != PIXEL_BMP_LENGTH:
        sys.exit(f"pixel.bmp: short read at 0x{PIXEL_BMP_OFFSET:x} "
                 f"({len(blob)}/{PIXEL_BMP_LENGTH} bytes)")

    actual_crc = zlib.crc32(blob) & 0xFFFFFFFF
    if actual_crc != PIXEL_BMP_CRC:
        sys.exit(f"pixel.bmp: CRC mismatch at 0x{PIXEL_BMP_OFFSET:x} — "
                 f"expected 0x{PIXEL_BMP_CRC:08x}, got 0x{actual_crc:08x}. "
                 "The Doukutsu.exe at the given path is probably not the "
                 "2004 EN freeware build extractfiles.cpp's offsets target.")

    out_subdir = os.path.join(out_dir, "endpic")
    os.makedirs(out_subdir, exist_ok=True)
    out_path = os.path.join(out_subdir, "pixel.bmp")
    with open(out_path, "wb") as fp:
        fp.write(PIXEL_BMP_HEADER)
        fp.write(blob)
    total = len(PIXEL_BMP_HEADER) + PIXEL_BMP_LENGTH
    print(f"extracted endpic/pixel.bmp ({total} bytes, CRC verified) to {out_dir}/")


def extract_credit_bmps(exe, out_dir):
    """
    Extract data/endpic/credit01.bmp..credit18.bmp (17 files; credit13 is
    intentionally absent). Each output file is the 25-byte CREDIT_HEADER +
    19293 bytes from the EXE at the entry's offset = 19318 bytes per file.
    CRC-32 is verified over the EXE-resident 19293 bytes only — the header
    is reconstructed locally and not CRC'd, matching extractfiles.cpp's
    crc_calc(file, files[i].length) call which runs on the buffer position
    past the prepended header.
    """
    out_subdir = os.path.join(out_dir, "endpic")
    os.makedirs(out_subdir, exist_ok=True)
    total_size = len(CREDIT_HEADER) + CREDIT_PAYLOAD_LENGTH  # 19318

    for filename, offset, expected_crc in CREDIT_FILES:
        exe.seek(offset)
        blob = exe.read(CREDIT_PAYLOAD_LENGTH)
        if len(blob) != CREDIT_PAYLOAD_LENGTH:
            sys.exit(f"{filename}: short read at 0x{offset:x} "
                     f"({len(blob)}/{CREDIT_PAYLOAD_LENGTH} bytes)")

        actual_crc = zlib.crc32(blob) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            sys.exit(f"{filename}: CRC mismatch at 0x{offset:x} — "
                     f"expected 0x{expected_crc:08x}, got 0x{actual_crc:08x}. "
                     "The Doukutsu.exe at the given path is probably not the "
                     "2004 EN freeware build extractfiles.cpp's offsets target.")

        out_path = os.path.join(out_subdir, filename)
        with open(out_path, "wb") as fp:
            fp.write(CREDIT_HEADER)
            fp.write(blob)
        print(f"extracted endpic/{filename} ({total_size} bytes, CRC verified) to {out_dir}/")


def main():
    if len(sys.argv) != 3:
        sys.exit(f"usage: {sys.argv[0]} <Doukutsu.exe> <output-data-dir>")

    exe_path, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)

    with open(exe_path, "rb") as exe:
        extract_wavetable(exe, out_dir)
        extract_stages(exe, out_dir)
        extract_pixel_bmp(exe, out_dir)
        extract_credit_bmps(exe, out_dir)


if __name__ == "__main__":
    main()
