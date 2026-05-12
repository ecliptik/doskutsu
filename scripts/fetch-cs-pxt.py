#!/usr/bin/env python3
"""
Cave Story Pixtone (PXT) source-fetch + extract orchestrator.

Background
----------
The freeware Cave Story 2004 EN soundboard ships 86 Pixtone synth parameter
blocks embedded in `Doukutsu.exe`'s data section. NXEngine-evo's stock
audio path reads these at runtime as ASCII text files under
`data/pxt/fx<HEX-ID>.pxt` — the format `scripts/extract-pxt.py` produces.

The canonical extractor (`scripts/extract-pxt.py`) operates on `Doukutsu.exe`
at fixed byte offsets. Before today, the source-acquisition step was a manual
prerequisite — "stage Doukutsu.exe somewhere, then run extract-pxt.py".
This script closes the gap: it fetches `cavestoryen.zip` from the project's
documented source (the Cave Story Tribute Site / cavestory.one), extracts
`Doukutsu.exe` to a tempdir, runs `extract-pxt.py` against it, and emits
the 86 fx*.pxt files into `<data-dir>/pxt/`. The downloaded zip and the
extracted Doukutsu.exe do NOT persist on the user's machine after the script
completes — they live in a tempdir that's removed on exit (or on error).

Source
------
`cavestoryen.zip` from <https://www.cavestory.one/downloads/cavestoryen.zip>
(Cave Story Tribute Site). The archive ships the 2004 EN Aeon-Genesis-
patched `Doukutsu.exe` plus loose `data/` files. See docs/ASSETS.md § Option
D for the broader context — that path covers Stage/, Npc/, and root .pbm /
.tsc / npc.tbl entries. This script handles the embedded PXT subset only;
ORG extraction is a separate flow (7z PE-resource extraction per ASSETS.md),
and engine-data extraction (wavetbl.dat, stage.dat, pixel.bmp, credit*.bmp)
is handled by `scripts/extract-engine-data.py`.

Sibling scripts
---------------
- `scripts/extract-pxt.py` — canonical PXT extractor. Called by this script
  via subprocess so the SND[] offset table stays defined in exactly one
  place.
- `scripts/extract-engine-data.py` — sibling extractor for wavetbl.dat /
  stage.dat / pixel.bmp / credit*.bmp. Also consumes `Doukutsu.exe`.
- `scripts/fetch-cs-midi.py` — the analogous fetch-and-process script for
  hardware-MIDI tracks (Phase 10). Same URL+SHA pin convention.

Coverage: 86 of NXEngine-evo's 117 Pixtone slots
------------------------------------------------
`Doukutsu.exe` defines parameter blocks for exactly 86 slots; NXEngine-evo
iterates `slot = 1..NUM_SOUNDS` where `NUM_SOUNDS = 0x75` (117) and so
emits one `LOG_WARN("pxt->load: file ... not found.")` per absent slot
at boot. The 37 absent slots (`fx08, fx09, fx0a, fx0d, fx13, fx24,
fx42-45, fx49-63`) are unnamed gaps in the engine's SFX enum — no
engine code ever calls `Pixtone::play()` with those slot numbers, so
the absent files have zero gameplay impact. This is a known upstream
limitation, not a build bug. Don't chase it. See `docs/ASSETS.md`
§ Option D for the full source-hunt audit trail (NXEngine-evo issue #4,
doukutsu-rs wiki, GitHub indexing, Cave Story+ licensing block) and
the affected-slot enum gap analysis.

License posture
---------------
- `cavestoryen.zip` is Cave Story 2004 EN freeware per Daisuke "Pixel"
  Amaya's 2004 terms (personal use, extraction, redistribution OK within
  Pixel's terms). The Aeon-Genesis English translation patch is fan-work
  under separate terms maintained by Aeon Genesis Productions.
- We DO NOT redistribute `cavestoryen.zip` or `Doukutsu.exe` from this
  repo — same posture as Pixel's original release. User-fetches-locally
  via this script; download stays in a tempdir.
- The extracted `data/pxt/fx*.pxt` files are derivative Pixel freeware
  data; they land in `data/` which is gitignored. They also never land
  in our public `dist/doskutsu-cf.zip` (see Makefile's dist target).
- See `docs/ASSETS.md § Legal notes` for the full reasoning.

Usage
-----
    python3 scripts/fetch-cs-pxt.py data/

The script creates `data/pxt/` if missing and writes 86 `fx<HEX>.pxt`
files into it. Re-running is idempotent; existing files are overwritten.
Prints a sha-manifest summary at the end for verification.

Prerequisites
-------------
- Network reachability to <https://www.cavestory.one/> (~1.1 MB download).
- Python 3 (standard library only; no external packages).
- Sibling script `scripts/extract-pxt.py` in the same `scripts/` dir.
"""

import hashlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

# ============================================================================
# Source URL + integrity pin
# ============================================================================

# cavestoryen.zip — Cave Story 2004 EN freeware bundle (Aeon-Genesis-patched
# Doukutsu.exe + loose data files). Pin recorded 2026-05-11; if cavestory.one
# refreshes the archive, verify the new SHA-256 reflects an intentional
# upstream change (not a defacement) before bumping the constant.
CAVESTORYEN_URL = "https://www.cavestory.one/downloads/cavestoryen.zip"
CAVESTORYEN_SHA256 = "aa87fa30bee9b4980640c7e104791354e0f1f6411ee0d45a70af70046aa0685f"
CAVESTORYEN_SIZE = 1_136_575

# Path inside the zip to the freeware binary.
CAVESTORYEN_EXE_MEMBER = "CaveStory/Doukutsu.exe"

# Expected post-extraction count. SND[] in scripts/extract-pxt.py has 87
# entries; id 0x68 appears twice (second overwrites first on disk) so the
# unique output set is 86 files. If extract-pxt.py emits a different count,
# either upstream cavestoryen.zip changed or our extractor regressed —
# either way, investigate before bumping this constant.
EXPECTED_PXT_COUNT = 86

# ============================================================================
# Helpers (mirrored from scripts/fetch-cs-midi.py for cross-script consistency)
# ============================================================================


def _sha256(buf: bytes) -> str:
    h = hashlib.sha256()
    h.update(buf)
    return h.hexdigest()


def _download(url: str, expected_size: int, expected_sha: str,
              label: str, verbose: bool = True) -> bytes:
    if verbose:
        print(f"  fetching {url} ({expected_size:,} bytes expected)...")
    req = urllib.request.Request(
        url,
        headers={"User-Agent": "doskutsu-fetch-cs-pxt/1.0"},
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        data = resp.read()

    if len(data) != expected_size:
        sys.exit(
            f"{label}: size mismatch — expected {expected_size} bytes, "
            f"got {len(data)} bytes. Upstream may have changed; verify the "
            f"change is benign and update the size + SHA-256 constants."
        )

    actual_sha = _sha256(data)
    if actual_sha != expected_sha:
        sys.exit(
            f"{label}: SHA-256 mismatch — expected {expected_sha}, "
            f"got {actual_sha}. Upstream content has changed; verify before "
            f"bumping the SHA-256 constant."
        )
    if verbose:
        print(f"    fetched {len(data):,} bytes, SHA-256 verified")
    return data


def _file_sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fp:
        for chunk in iter(lambda: fp.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


# ============================================================================
# Orchestrator
# ============================================================================


def fetch_and_extract_pxt(out_dir: str, verbose: bool = True) -> int:
    """Download cavestoryen.zip, extract Doukutsu.exe, run extract-pxt.py.
    Returns the number of fx*.pxt files written to <out_dir>/pxt/."""

    pxt_dir = os.path.join(out_dir, "pxt")
    os.makedirs(pxt_dir, exist_ok=True)

    script_dir = os.path.dirname(os.path.abspath(__file__))
    extractor = os.path.join(script_dir, "extract-pxt.py")
    if not os.path.isfile(extractor):
        sys.exit(
            f"missing sibling script: {extractor}\n"
            f"This orchestrator depends on scripts/extract-pxt.py for the "
            f"actual PXT extraction. Re-clone or re-fetch the doskutsu repo."
        )

    # Work in a tempdir so cavestoryen.zip + Doukutsu.exe NEVER persist on
    # the user's filesystem (per docs/ASSETS.md § Legal notes — Pixel's
    # freeware stays out of our repo / dist / user-machine-residue).
    work = tempfile.mkdtemp(prefix="doskutsu-cs-pxt-")
    try:
        # 1. Fetch the zip into memory + SHA-256-verify.
        zip_bytes = _download(
            CAVESTORYEN_URL, CAVESTORYEN_SIZE, CAVESTORYEN_SHA256,
            label="cavestoryen.zip", verbose=verbose,
        )

        # 2. Extract Doukutsu.exe to the tempdir.
        with zipfile.ZipFile(io.BytesIO(zip_bytes)) as zf:
            if CAVESTORYEN_EXE_MEMBER not in zf.namelist():
                sys.exit(
                    f"cavestoryen.zip: expected member "
                    f"{CAVESTORYEN_EXE_MEMBER!r} not present. Upstream "
                    f"layout may have changed."
                )
            exe_data = zf.read(CAVESTORYEN_EXE_MEMBER)

        exe_path = os.path.join(work, "Doukutsu.exe")
        with open(exe_path, "wb") as fp:
            fp.write(exe_data)
        if verbose:
            exe_sha = _sha256(exe_data)
            print(f"  extracted Doukutsu.exe ({len(exe_data):,} bytes, "
                  f"SHA-256 {exe_sha[:12]}…) to tempdir")

        # 3. Run scripts/extract-pxt.py against the freeware binary.
        if verbose:
            print(f"  running {os.path.relpath(extractor)} -> {pxt_dir}/")
        try:
            subprocess.run(
                [sys.executable, extractor, exe_path, pxt_dir],
                check=True,
                stdout=sys.stdout, stderr=sys.stderr,
            )
        except subprocess.CalledProcessError as e:
            sys.exit(
                f"extract-pxt.py failed (exit {e.returncode}). "
                f"The tempdir at {work} has been preserved for diagnosis; "
                f"remove it manually after investigation."
            )

        # 4. Verify count + emit sha-manifest.
        pxt_files = sorted(
            os.path.join(pxt_dir, name)
            for name in os.listdir(pxt_dir)
            if name.startswith("fx") and name.endswith(".pxt")
        )
        if len(pxt_files) < EXPECTED_PXT_COUNT:
            sys.exit(
                f"only {len(pxt_files)} fx*.pxt files produced "
                f"(expected {EXPECTED_PXT_COUNT}). extract-pxt.py output "
                f"is below the canonical Pixtone-slot count; "
                f"investigate the SND[] table or the source binary."
            )

        if verbose:
            total_bytes = sum(os.path.getsize(p) for p in pxt_files)
            print(f"  installed {len(pxt_files)} fx*.pxt files "
                  f"({total_bytes:,} bytes) to {pxt_dir}/")

        return len(pxt_files)

    finally:
        # Tempdir is removed unconditionally (Doukutsu.exe + zip-in-memory
        # gone). On the extract-pxt.py failure path we sys.exit() before
        # reaching here, leaving the tempdir for diagnosis.
        shutil.rmtree(work, ignore_errors=True)


def print_sha_manifest(out_dir: str) -> None:
    pxt_dir = os.path.join(out_dir, "pxt")
    pxt_files = sorted(
        name for name in os.listdir(pxt_dir)
        if name.startswith("fx") and name.endswith(".pxt")
    )
    fileset_h = hashlib.sha256()
    print()
    print(f"  fileset manifest ({len(pxt_files)} entries):")
    for name in pxt_files:
        sha = _file_sha256(os.path.join(pxt_dir, name))
        fileset_h.update(f"{sha}  {name}\n".encode("ascii"))
    print(f"    first  : {pxt_files[0]}  {_file_sha256(os.path.join(pxt_dir, pxt_files[0]))[:16]}…")
    print(f"    last   : {pxt_files[-1]}  {_file_sha256(os.path.join(pxt_dir, pxt_files[-1]))[:16]}…")
    print(f"    fileset SHA-256: {fileset_h.hexdigest()}")


def main():
    if len(sys.argv) != 2:
        sys.exit(f"usage: {sys.argv[0]} <output-data-dir>")
    out_dir = sys.argv[1]
    if not os.path.isdir(out_dir):
        sys.exit(f"{out_dir}: not a directory (create it first, or run after "
                 f"Step 3 of docs/ASSETS.md)")

    n = fetch_and_extract_pxt(out_dir)
    print_sha_manifest(out_dir)
    print()
    print(f"installed {n} of {EXPECTED_PXT_COUNT} Cave Story Pixtone slots to "
          f"{os.path.join(out_dir, 'pxt')}/")
    print()
    print("Note: cavestoryen.zip and the extracted Doukutsu.exe live only in")
    print("a tempdir during this script's run and are removed on exit.")
    print("Per docs/ASSETS.md § Legal notes, Pixel's freeware never lands in")
    print("this repo or dist/doskutsu-cf.zip.")


if __name__ == "__main__":
    main()
