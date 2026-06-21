#!/usr/bin/env python3
# gen-opl3-bank.py -- convert a DMX OP2 (GENMIDI) FM bank into the
# doskutsu engine's DOPL3v1 runtime patch bank (data/opl3-patches.dat).
#
# WHY this exists (DOS port context): NXEngine-evo's `opl3` music backend
# (vendor/nxengine-evo/src/sound/MidiBackendOpl3.cpp) ships an 8-patch
# family-bucket PLACEHOLDER that sounds thin/clipped on real OPL3 hardware
# (g2k report). The ctor's loader `_try_load_opl3_patches_dat()` (patch
# 0141) will, when present, read a full 128-program General MIDI bank from
# data/opl3-patches.dat in the custom "DOPL3v1" binary format. This script
# generates that file from a license-clean source bank so the result is
# reproducible (the .dat is build output, not hand-authored bytes).
#
# SOURCE BANK: DMXOPL (DMXOPL3) by ConSiGno -- github.com/sneakernets/DMXOPL,
# MIT License (repo LICENSE file). A modern full-GM OPL3 FM patch set voiced
# to approximate the Roland SC-55/SC-88. MIT is GPLv3-compatible; provenance
# recorded in THIRD-PARTY.md. (DoomWiki additionally lists CC BY-SA 4.0,
# which is also one-way GPLv3-compatible -- either reading is fine for our
# GPLv3 binary.)
#
# ------------------------------------------------------------------------
# OP2 (DMX GENMIDI) input format
#   8         magic "#OPL_II#"
#   175 * 36  instrument records (128 GM melodic + 47 percussion)
#   175 * 32  instrument name strings (ignored here)
#
# Each 36-byte instrument record:
#   [0:2]   uint16 LE flags (bit0 = fixed pitch, bit2 = double-voice)
#   [2]     fine tuning (second-voice detune; unused for single 2-op)
#   [3]     fixed note (percussion note; unused for melodic)
#   [4:20]  voice #1 (the PRIMARY voice)
#   [20:36] voice #2 (only meaningful for double-voice instruments)
#
# Each 16-byte voice (raw OPL register bytes, mod then carrier):
#   [0] mod  char  (reg 0x20: AM/VIB/EG/KSR/MULT)
#   [1] mod  AR/DR (reg 0x60)
#   [2] mod  SL/RR (reg 0x80)
#   [3] mod  WS    (reg 0xE0)
#   [4] mod  KSL   (reg 0x40 bits 6-7, key-scale level)
#   [5] mod  TL    (reg 0x40 bits 0-5, total/output level)
#   [6] feedback/connection (reg 0xC0)
#   [7] car  char
#   [8] car  AR/DR
#   [9] car  SL/RR
#   [10] car WS
#   [11] car KSL
#   [12] car TL
#   [13] unused
#   [14:16] int16 LE note offset (base-note transpose)
#
# ------------------------------------------------------------------------
# DOPL3v1 output format (matches _try_load_opl3_patches_dat in
# MidiBackendOpl3.cpp; little-endian; loaded via fopen "rb"):
#   0    8     magic "DOPL3v1\n" (last byte 0x0A)
#   8    1     version = 1
#   9    1     num_programs (1..128)
#   10   2     reserved = 0,0
#   12   N*12  N x 12-byte patch records
#
# Each 12-byte DOPL3v1 record is the SDL_DOSOpl3VoiceWritePatch() layout --
# grouped PER OPERATOR (NOT the per-register-pair interleave of OP2/SBI):
#   [0]  op0 (mod) AM/VIB/EG/KSR/MULT
#   [1]  op0 KSL|TL    (recombined: KSL bits 6-7 | TL bits 0-5)
#   [2]  op0 AR|DR
#   [3]  op0 SL|RR
#   [4]  op0 WS         (masked to 0-7)
#   [5]  channel FB/CONN + stereo (low nibble = FB/CONN from OP2;
#                                  high nibble forced to 0xF0 so BOTH
#                                  OPL3 stereo banks are audible)
#   [6]  op1 (car) AM/VIB/EG/KSR/MULT
#   [7]  op1 KSL|TL
#   [8]  op1 AR|DR
#   [9]  op1 SL|RR
#   [10] op1 WS
#   [11] int8 semitone transpose (engine applies it before the fnum/block
#        calc; the SDL VoiceWritePatch helper does NOT write it to the chip)
#
# KNOWN APPROXIMATIONS (documented for the handoff, not bugs):
#  1. 2-op only. Our backend allocator is 18 x 2-op; OP2 "double-voice"
#     (fat) instruments carry a second 2-op voice we DROP -- we keep
#     voice #1, the primary. Still a large upgrade over the 8-patch set.
#  2. Per-patch transpose IS honored: OP2's int16 note-offset is emitted
#     into patch[11] as int8, and engine patch 0232 applies it in note_on
#     (playednote = midinote + patch[11]). All DMXOPL melodic offsets fit
#     int8 (-48..-7). This fixes the octave error that dropping the offset
#     would cause on ~110/128 programs. Diagnostics below count them.

import struct
import sys

OP2_MAGIC = b"#OPL_II#"
DOPL3_MAGIC = b"DOPL3v1\n"
NUM_GM_MELODIC = 128
OP2_REC_SIZE = 36
VOICE_SIZE = 16


def convert_voice(v, note_offset):
    """Map one 16-byte OP2 voice -> 12-byte DOPL3v1 record (voice #1 primary).

    note_offset is the OP2 int16 base-note transpose; it is emitted into
    patch[11] as a SIGNED int8 (two's complement). The engine (patch 0232)
    reads patch[11] as an int8 semitone offset and adds it to the MIDI note
    before computing (fnum, block) -- this reproduces the DMX `playednote =
    midinote + offset` convention (~110/128 DMXOPL programs use -12 etc.).
    """
    # Clamp to int8; all DMXOPL melodic offsets are in [-48, -7] so this is
    # lossless here, but guard defensively for other source banks.
    xpose = max(-128, min(127, note_offset))
    mod_char = v[0]
    mod_ardr = v[1]
    mod_slrr = v[2]
    mod_ws   = v[3] & 0x07
    mod_ksl  = v[4]
    mod_tl   = v[5]
    feedback = v[6]
    car_char = v[7]
    car_ardr = v[8]
    car_slrr = v[9]
    car_ws   = v[10] & 0x07
    car_ksl  = v[11]
    car_tl   = v[12]

    rec = bytes([
        mod_char,                                   # [0]
        (mod_ksl & 0xC0) | (mod_tl & 0x3F),         # [1] op0 KSL|TL
        mod_ardr,                                   # [2]
        mod_slrr,                                   # [3]
        mod_ws,                                     # [4]
        (feedback & 0x0F) | 0xF0,                   # [5] FB/CONN + stereo
        car_char,                                   # [6]
        (car_ksl & 0xC0) | (car_tl & 0x3F),         # [7] op1 KSL|TL
        car_ardr,                                   # [8]
        car_slrr,                                   # [9]
        car_ws,                                     # [10]
        xpose & 0xFF,                               # [11] int8 semitone transpose
    ])
    assert len(rec) == 12
    return rec


def main():
    if len(sys.argv) != 3:
        sys.stderr.write("usage: gen-opl3-bank.py <GENMIDI.op2> <out.dat>\n")
        return 2
    src_path, out_path = sys.argv[1], sys.argv[2]

    with open(src_path, "rb") as f:
        data = f.read()

    if data[:8] != OP2_MAGIC:
        sys.stderr.write("error: source is not an OP2 bank (#OPL_II# magic missing)\n")
        return 1

    base = 8  # past magic
    records = bytearray()
    n_double = 0
    n_offset = 0
    offset_list = []
    for prog in range(NUM_GM_MELODIC):
        off = base + prog * OP2_REC_SIZE
        rec = data[off:off + OP2_REC_SIZE]
        if len(rec) < OP2_REC_SIZE:
            sys.stderr.write("error: OP2 truncated at program %d\n" % prog)
            return 1
        flags = struct.unpack_from("<H", rec, 0)[0]
        if flags & 0x04:
            n_double += 1
        voice1 = rec[4:4 + VOICE_SIZE]
        note_off = struct.unpack_from("<h", voice1, 14)[0]
        if note_off != 0:
            n_offset += 1
            offset_list.append((prog, note_off))
        records += convert_voice(voice1, note_off)

    assert len(records) == NUM_GM_MELODIC * 12

    header = DOPL3_MAGIC + bytes([0x01, NUM_GM_MELODIC, 0x00, 0x00])
    out = header + bytes(records)
    assert len(out) == 12 + NUM_GM_MELODIC * 12

    with open(out_path, "wb") as f:
        f.write(out)

    # Diagnostics to stderr (build log; not the file).
    sys.stderr.write("gen-opl3-bank: wrote %s\n" % out_path)
    sys.stderr.write("  source       : %s (%d bytes)\n" % (src_path, len(data)))
    sys.stderr.write("  num_programs : %d\n" % NUM_GM_MELODIC)
    sys.stderr.write("  file size    : %d bytes (expect %d)\n"
                     % (len(out), 12 + NUM_GM_MELODIC * 12))
    sys.stderr.write("  double-voice : %d/%d programs (2nd voice dropped, 2-op only)\n"
                     % (n_double, NUM_GM_MELODIC))
    sys.stderr.write("  nonzero xpose: %d/%d programs (emitted into patch[11] int8; engine 0232 applies)\n"
                     % (n_offset, NUM_GM_MELODIC))
    if offset_list:
        sys.stderr.write("    offsets: %s\n"
                         % ", ".join("p%d=%+d" % (p, o) for p, o in offset_list))
    return 0


if __name__ == "__main__":
    sys.exit(main())
