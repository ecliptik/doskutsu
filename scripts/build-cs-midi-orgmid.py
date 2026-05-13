#!/usr/bin/env python3
"""
Cave Story MIDI build via ORGMID + octave transpose (Phase 10 alternate set).

Generates data/orgmid/<n>.mid from the user's data/org/<n>.org using
Robert Hart's ORGMID converter (rnhart.net/orgmid), then transposes the
output +12 semitones to compensate for ORGMID's known octave-down quirk.

Output sits parallel to data/midi/ (the WiiWare arrangements from
fetch-cs-midi.py). The engine selects between them at runtime via
SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE -- `wiimidi` (default; data/midi/) vs
`orgmid` (data/orgmid/) -- for A/B operator listening on real HW.

Background
----------
ORGMID is a literal Organya-format -> SMF transcription, so the result
matches Pixel's original Cave Story aesthetic (chiptune, note-for-note)
rather than the WiiWare port's professional re-arrangement (orchestral).
Through OPL3 / DreamBlaster S2 GM rendering, the literal transcription
may end up sounding more authentic; the WiiWare arrangement may end up
sounding more polished. Operator A/B listening picks the canonical set.

ORGMID has a documented octave-down quirk: notes come out one octave
lower than the original Organya playback. This script applies +12
semitones to every note_on/note_off event to correct.

Usage:
    python3 scripts/build-cs-orgmid.py data/

Inputs:
    - data/org/*.org (Cave Story freeware data; user's local files)
    - gcc / cc on PATH (for compiling ORGMID)
    - network access (downloads ORGMID source from rnhart.net)

Outputs:
    - data/orgmid/*.mid (41 files; one per engine-relevant .org)

xxxx.org is excluded (leftover/test data not indexed by music.jsn).

License posture: ORGMID is rnhart.net's tool; we don't vendor or
redistribute its source. Output .mid files are derivative works of
Pixel's freeware compositions; they stay on the user's machine and
are bundled in iter tarballs alongside the WiiWare arrangements for
A/B comparison purposes.
"""

import argparse
import hashlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

ORGMID_URL = "http://rnhart.net/orgmid/orgmid-source-0.82.zip"
ORGMID_SHA256 = "151579170860c9f608327bd59cc4cbd2f6cc561a0a99ccaaddb144bc2264a3b3"
ORGMID_SIZE = 11_823

# xxxx.org is leftover from Pixel's freeware development, NOT indexed by
# vendor/nxengine-evo/data/music.jsn -- engine cannot play it. Skip.
SKIP_ORGS = {"xxxx.org"}


def _sha256(buf: bytes) -> str:
    return hashlib.sha256(buf).hexdigest()


def _download(url: str, expected_size: int, expected_sha: str,
              label: str, verbose: bool = True) -> bytes:
    if verbose:
        print(f"    downloading {label} ({expected_size} bytes)...")
    with urllib.request.urlopen(url) as r:
        buf = r.read()
    if len(buf) != expected_size:
        raise SystemExit(
            f"size mismatch: got {len(buf)}, expected {expected_size}"
        )
    actual = _sha256(buf)
    if actual != expected_sha:
        raise SystemExit(
            f"sha256 mismatch: got {actual}, expected {expected_sha}"
        )
    return buf


def build_orgmid(verbose: bool = True):
    """Compile ORGMID into a temp dir. Returns (tmpdir_path, binary_path).
    Caller is responsible for shutil.rmtree(tmpdir_path) when done."""
    gcc = shutil.which("gcc") or shutil.which("cc")
    if gcc is None:
        raise SystemExit(
            "no gcc/cc on PATH; install build-essential (Linux), "
            "Xcode CLI tools (macOS), or equivalent."
        )

    tmp = tempfile.mkdtemp(prefix="orgmid-")
    zip_bytes = _download(ORGMID_URL, ORGMID_SIZE, ORGMID_SHA256,
                          "orgmid-source-0.82.zip", verbose)
    with zipfile.ZipFile(io.BytesIO(zip_bytes)) as zf:
        zf.extractall(tmp)

    orgmid_src = os.path.join(tmp, "orgmid", "main.c")
    orgmid_bin = os.path.join(tmp, "orgmid-bin")
    if not os.path.isfile(orgmid_src):
        shutil.rmtree(tmp, ignore_errors=True)
        raise SystemExit(
            f"ORGMID source not at expected layout (main.c missing at {orgmid_src})"
        )

    if verbose:
        print(f"    building ORGMID with {gcc}...")
    r = subprocess.run(
        [gcc, "-O2", "-Wno-incompatible-pointer-types",
         "-o", orgmid_bin, orgmid_src, "-lm"],
        capture_output=True, text=True,
    )
    if r.returncode != 0:
        shutil.rmtree(tmp, ignore_errors=True)
        raise SystemExit(
            f"ORGMID build failed (returncode {r.returncode}):\n{r.stderr}"
        )
    return tmp, orgmid_bin


def _read_var_len(data: bytes, idx: int):
    """Read MIDI variable-length quantity starting at data[idx].
    Returns (value, new_idx)."""
    val = 0
    while True:
        b = data[idx]
        idx += 1
        val = (val << 7) | (b & 0x7F)
        if not (b & 0x80):
            break
    return val, idx


def _transpose_track(track: bytes, semitones: int) -> bytes:
    """Walk SMF track events; shift note_on / note_off note byte by `semitones`.
    Other event types pass through unchanged. Note bytes clamp to [0, 127]."""
    out = bytearray()
    idx = 0
    running_status = 0

    while idx < len(track):
        # Each event starts with a variable-length delta time.
        delta_start = idx
        _, idx = _read_var_len(track, idx)
        out.extend(track[delta_start:idx])

        # Status byte (or running status if MSB clear).
        status_byte = track[idx]
        if status_byte & 0x80:
            running_status = status_byte
            out.append(status_byte)
            idx += 1
        else:
            # Running status: re-use prior status; data bytes start at idx.
            status_byte = running_status

        if status_byte == 0xFF:
            # Meta event: meta_type (1 byte) + var_len + data
            meta_type = track[idx]
            out.append(meta_type)
            idx += 1
            data_len, new_idx = _read_var_len(track, idx)
            out.extend(track[idx:new_idx])
            idx = new_idx
            out.extend(track[idx:idx + data_len])
            idx += data_len
        elif status_byte in (0xF0, 0xF7):
            # SysEx event: var_len + data
            data_len, new_idx = _read_var_len(track, idx)
            out.extend(track[idx:new_idx])
            idx = new_idx
            out.extend(track[idx:idx + data_len])
            idx += data_len
        elif (status_byte & 0xF0) in (0x80, 0x90):
            # Note off (0x80) or note on (0x90): note byte + velocity byte.
            note = track[idx]
            new_note = note + semitones
            if new_note > 127:
                new_note = 127
            elif new_note < 0:
                new_note = 0
            out.append(new_note)
            idx += 1
            out.append(track[idx])
            idx += 1
        elif (status_byte & 0xF0) in (0xA0, 0xB0, 0xE0):
            # Polyphonic key pressure / control change / pitch bend: 2 data bytes
            out.extend(track[idx:idx + 2])
            idx += 2
        elif (status_byte & 0xF0) in (0xC0, 0xD0):
            # Program change / channel pressure: 1 data byte
            out.append(track[idx])
            idx += 1
        else:
            raise ValueError(
                f"unhandled MIDI status 0x{status_byte:02x} at track offset {idx}"
            )

    return bytes(out)


def transpose_smf(buf: bytes, semitones: int) -> bytes:
    """Parse a complete Standard MIDI File and return a new buffer with all
    note events shifted by `semitones`. Track lengths preserved (note byte
    is 1 byte regardless of value)."""
    if buf[:4] != b"MThd":
        raise ValueError(f"not a SMF (header: {buf[:4]!r})")
    out = bytearray()
    out.extend(buf[:14])  # MThd + 4-byte length + 6 bytes of header data

    idx = 14
    while idx < len(buf):
        if buf[idx:idx + 4] != b"MTrk":
            raise ValueError(f"expected MTrk at offset {idx}, got {buf[idx:idx + 4]!r}")
        track_len = int.from_bytes(buf[idx + 4:idx + 8], "big")
        track_data = bytes(buf[idx + 8:idx + 8 + track_len])
        new_track = _transpose_track(track_data, semitones)
        if len(new_track) != track_len:
            raise RuntimeError(
                f"transposed track length changed: {track_len} -> {len(new_track)}"
            )
        out.extend(b"MTrk")
        out.extend(track_len.to_bytes(4, "big"))
        out.extend(new_track)
        idx += 8 + track_len

    return bytes(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    ap.add_argument("data_dir",
                    help="path to data/ root (must contain org/ subdir)")
    ap.add_argument("--out-subdir", default="orgmid",
                    help="output dir name relative to data_dir (default: orgmid)")
    ap.add_argument("--transpose", type=int, default=12,
                    help="semitones to transpose (default: +12 to correct ORGMID octave-down)")
    args = ap.parse_args()

    org_dir = os.path.join(args.data_dir, "org")
    out_dir = os.path.join(args.data_dir, args.out_subdir)
    if not os.path.isdir(org_dir):
        print(f"ERROR: {org_dir} not found", file=sys.stderr)
        print(f"       Run scripts/extract-engine-data.py first to populate "
              f"data/org/ from your Cave Story 2004 freeware install.",
              file=sys.stderr)
        return 1

    os.makedirs(out_dir, exist_ok=True)

    print(f"[setup] building ORGMID...")
    tmp, orgmid_bin = build_orgmid()

    try:
        org_files = sorted(
            f for f in os.listdir(org_dir)
            if f.endswith(".org") and f not in SKIP_ORGS
        )
        print(f"[convert] {len(org_files)} ORG -> MIDI "
              f"(transpose {args.transpose:+d} semitones) -> {out_dir}/")

        success = 0
        failures = []
        for org_name in org_files:
            org_path = os.path.join(org_dir, org_name)
            mid_name = org_name[:-4] + ".mid"
            mid_path = os.path.join(out_dir, mid_name)

            # ORGMID refuses to overwrite existing files. Get a unique name
            # but delete the placeholder so orgmid can write it fresh.
            with tempfile.NamedTemporaryFile(suffix=".mid", delete=False) as f:
                tmp_mid = f.name
            os.remove(tmp_mid)

            try:
                r = subprocess.run(
                    [orgmid_bin, org_path, tmp_mid],
                    capture_output=True, text=True,
                )
                if r.returncode != 0:
                    failures.append(
                        (org_name, "orgmid: " + (r.stderr or r.stdout).strip()[:200])
                    )
                    continue

                with open(tmp_mid, "rb") as f:
                    raw = f.read()
                if raw[:4] != b"MThd":
                    failures.append((org_name, "orgmid output not SMF"))
                    continue

                transposed = transpose_smf(raw, args.transpose)

                with open(mid_path, "wb") as f:
                    f.write(transposed)
                print(f"  {org_name:>14} -> {mid_name:<14} ({len(transposed)} bytes)")
                success += 1
            finally:
                if os.path.exists(tmp_mid):
                    os.remove(tmp_mid)

        print()
        print(f"[done] {success}/{len(org_files)} converted to {out_dir}/")
        if failures:
            print(f"[warn] {len(failures)} failures:")
            for name, reason in failures:
                print(f"  {name}: {reason}")
            return 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    return 0


if __name__ == "__main__":
    sys.exit(main() or 0)
