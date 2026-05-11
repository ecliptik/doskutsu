#!/usr/bin/env python3
"""
Cave Story MIDI fetch + conversion (Phase 10 audio offload — Stage 1.5).

Background
----------
NXEngine-evo's stock audio path on DOS plays the freeware Organya soundtrack
(`data/org/*.org`) via the Organya synthesizer running inside the SDL3_mixer
audio callback. Phase 9 wave 20 measured that audio IRQ wall-clock + mix work
costs ~14 ms per render flip on the g2k Pentium-OD-83 — large enough that
Phase 10 plans to bypass per-IRQ Organya synth for the music track entirely
by routing music to a hardware MIDI device (WaveBlaster header / DreamBlaster
S2 daughterboard for Tier-1 g2k; OPL3 register programming for Tier-2/3
fallback). Pixtone SFX continues on the SB16 DAC unchanged.

The hardware-MIDI route needs Cave Story tracks in Standard MIDI File (.mid)
form. NXEngine-evo's repo, Cave Story 2004 freeware, and our `extract-engine-
data.py` all produce ORG, not MIDI — so this script is the user-facing fetch
path.

Sources (Option B locked 2026-05-06; team-lead direction)
---------------------------------------------------------
Three components combine to give 41/41 = 100% engine-relevant track coverage:

1. **wiimidi2.zip** (cavestory.one, ~5.4 MB) — primary WiiWare arrangement
   archive by Yann van der Cruyssen ("Monocozzi"), 2010. Provides 39 tracks.
2. **wiimidi.zip** (cavestory.one, ~138 KB) — predecessor archive by the
   same arranger, 2010. Provides curly.mid (the only engine-track NOT in
   wiimidi2.zip; Stage 1.5 re-verify finding 2026-05-06).
3. **ORGMID local conversion** — runs Robert Hart's `orgmid` tool against
   the user's existing `data/org/white.org` to produce a faithful note-
   for-note MIDI rendering. Only used for white.org (the one engine-track
   not in either WiiWare archive). License posture for ORGMID is unspecified
   on the upstream page; we use the user-fetches-locally pattern (download
   source archive + build on user's machine; never redistribute the binary
   or source from this repo).

Engine-relevant coverage breakdown
----------------------------------
The freeware Cave Story has 42 ORG files in `data/org/*.org`; of these, 41
are indexed by `vendor/nxengine-evo/data/music.jsn` (the engine's song-slot
table). The 42nd, `xxxx.org`, is leftover/test data from Pixel's freeware
data dir but has zero references in NXEngine-evo's source — the engine
cannot play it. xxxx.org is correspondingly OUT OF SCOPE for Phase 10
audio offload (sourcing a MIDI for an engine-unreferenced track would be
wasted effort). xxxx IS a real Pixel track per the official Studio Pixel
soundtrack (`Studio Pixel - XXXX.mp3` in cavestory.one's soundtrack.zip);
we just cannot reach it through the current music.jsn slot space.

Of the 41 engine-relevant tracks:
- 39 covered by wiimidi2.zip (per Stage 1 finding 2026-05-06)
- 1 (curly) covered by wiimidi.zip (per Stage 1.5 re-verify finding)
- 1 (white) covered by ORGMID-from-local-org (per Stage 1.5 Option B)
- Total: 41/41 = 100% engine-relevant

License posture
---------------
- WiiWare MIDI arrangements: Yann van der Cruyssen (Monocozzi); placed by
  their creator on the Cave Story Tribute Site for community archival.
  Hosted publicly since 2010 with no removal request — implies tacit
  redistribution permission. Same posture as Cave Story 2004 freeware:
  user-fetches-locally; never in our repo or dist zip.
- ORGMID tool: Robert Hart, rnhart.net/orgmid/, last updated 2015-03-08.
  No license statement on the page; no LICENSE/README/COPYING file in the
  source archive; no copyright notice in main.c / findkey.c / volpanconv.h.
  Per team-lead's decision tree 2026-05-06 + the absence of explicit license
  language: we DO NOT vendor or redistribute ORGMID. Instead the script
  downloads the source archive at user-install time from the upstream URL,
  builds it on the user's machine, runs it on the user's local data, and
  produces an output that stays on the user's machine. We make no claim
  about ORGMID's license; the user's interaction with rnhart.net handles
  that.
- Output `data/midi/white.mid`: derivative work of the user's local
  `data/org/white.org` (Pixel's freeware), produced by ORGMID on the
  user's machine. Stays on user's machine (gitignored under data/).
- THIRD-PARTY.md gets entries crediting Yann van der Cruyssen (WiiWare
  arrangements) + Robert Hart (ORGMID) for Phase 10 closeout.

Usage
-----
   python3 scripts/fetch-cs-midi.py data/

Prerequisites:
- The user has run `scripts/extract-engine-data.py` (or otherwise extracted
  Cave Story 2004 freeware data) so `data/org/white.org` exists. Without
  it, the ORGMID step fails gracefully with instructions; rest of the
  fetch (39 + 1 = 40 tracks from WiiWare) still completes.
- For ORGMID compilation: a working `gcc` (any modern version; tested
  against gcc 12.2). On Linux: pre-installed or `apt install build-
  essential`. On Windows / WSL: same. On macOS: Xcode CLI tools.
  Without gcc, ORGMID step fails gracefully; rest still completes.

Idempotent: safe to re-run; existing files are overwritten. Downloads
re-run each invocation (no cache); cost is ~5.5 MB for the WiiWare set
+ ~12 KB for the ORGMID source.
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
# Source URLs + integrity pins
# ============================================================================

# wiimidi2.zip — primary WiiWare arrangement archive (39 of 41 engine tracks).
WIIMIDI2_URL = "https://www.cavestory.one/downloads/wiimidi2.zip"
WIIMIDI2_SHA256 = "e74337b7c4aabaae120d941b49f970896ce4996b3102c66551241e93767ce1ef"
WIIMIDI2_SIZE = 5_646_796

# wiimidi.zip — predecessor archive (provides curly.mid, the only engine
# track NOT in wiimidi2.zip; per Stage 1.5 re-verify finding 2026-05-06).
WIIMIDI1_URL = "https://www.cavestory.one/downloads/wiimidi.zip"
WIIMIDI1_SHA256 = "dd3d697cad1afb29cff8cf790d962723b02696b5460b5bc4ff17f5de8dfa0f46"
WIIMIDI1_SIZE = 137_892

# orgmid-source-0.82.zip — Robert Hart's ORG → MIDI converter source
# archive. Built locally on the user's machine; binary never redistributed.
ORGMID_URL = "http://rnhart.net/orgmid/orgmid-source-0.82.zip"
ORGMID_SHA256 = "151579170860c9f608327bd59cc4cbd2f6cc561a0a99ccaaddb144bc2264a3b3"
ORGMID_SIZE = 11_823

# ============================================================================
# wiimidi2.zip filename mapping (in-zip name -> engine-side normalized name)
# ============================================================================
#
# Mapping rules:
#   - Drop the space in "game over.mid" (DOS 8.3 — and matches gameover.org).
#   - "lastbtl3.mid" -> "lastbt3.mid": .org name is "lastbt3" (DOS 8.3 trim
#     in upstream NXEngine-evo's data dir; we follow suit).
#   - "jenka1.mid" -> "jenka.mid": the freeware ORG set has only "jenka.org"
#     and "jenka2.org". jenka1b.mid is the WiiWare alternate intro variant
#     and has no ORG counterpart — SKIPPED.
#   - All other names already 8.3-clean and match their ORG counterpart.
#
# A value of None means "skip this entry" (extras with no ORG counterpart).
WIIMIDI2_NAME_MAP = {
    # Direct matches (no rename) — 36 entries.
    "access.mid": "access.mid",
    "anzen.mid": "anzen.mid",
    "balcony.mid": "balcony.mid",
    "ballos.mid": "ballos.mid",
    "bdown.mid": "bdown.mid",
    "cemetery.mid": "cemetery.mid",
    "dr.mid": "dr.mid",
    "ending.mid": "ending.mid",
    "escape.mid": "escape.mid",
    "fanfale1.mid": "fanfale1.mid",
    "fanfale2.mid": "fanfale2.mid",
    "fanfale3.mid": "fanfale3.mid",
    "fireeye.mid": "fireeye.mid",
    "ginsuke.mid": "ginsuke.mid",
    "grand.mid": "grand.mid",
    "gravity.mid": "gravity.mid",
    "hell.mid": "hell.mid",
    "ironh.mid": "ironh.mid",
    "jenka2.mid": "jenka2.mid",
    "kodou.mid": "kodou.mid",
    "lastbtl.mid": "lastbtl.mid",
    "lastcave.mid": "lastcave.mid",
    "marine.mid": "marine.mid",
    "maze.mid": "maze.mid",
    "mdown2.mid": "mdown2.mid",
    "mura.mid": "mura.mid",
    "oside.mid": "oside.mid",
    "plant.mid": "plant.mid",
    "quiet.mid": "quiet.mid",
    "requiem.mid": "requiem.mid",
    "toroko.mid": "toroko.mid",
    "vivi.mid": "vivi.mid",
    "wanpak2.mid": "wanpak2.mid",
    "wanpaku.mid": "wanpaku.mid",
    "weed.mid": "weed.mid",
    "zonbie.mid": "zonbie.mid",
    # Renames — 3 entries.
    "game over.mid": "gameover.mid",
    "jenka1.mid": "jenka.mid",
    "lastbtl3.mid": "lastbt3.mid",
    # Skipped extras — no ORG counterpart in the freeware track set.
    "credit wii.mid": None,
    "intro menu.mid": None,
    "jenka1b.mid": None,
    "plantation.mid": None,
    # Skipped soundfont — not redistributed; not needed for our hardware-MIDI
    # route (WaveBlaster onboard GM bank / OPL3 patch bank substitutes).
    "Cave Story.dls": None,
}

assert len(WIIMIDI2_NAME_MAP) == 44

# ============================================================================
# wiimidi.zip filename mapping (only curly extracted; rest superseded by wiimidi2)
# ============================================================================
#
# wiimidi.zip is the 2010-04-09 predecessor archive. Has 47 .mid files; 46 of
# them have higher-quality / more-polished equivalents in wiimidi2.zip
# (which post-dates wiimidi.zip by weeks-to-months). Only `Curly.mid` (22 KB,
# SMF format-0, 480 ticks/quarter — same time-base as wiimidi2.zip files)
# has no equivalent in wiimidi2.zip; we extract that one and skip the rest.
#
# All other entries have value None (skip — wiimidi2.zip is preferred or
# track has no ORG counterpart).
WIIMIDI1_NAME_MAP = {
    # The one engine-track unique to wiimidi.zip:
    "Curly.mid": "curly.mid",
    # All others — superseded by wiimidi2.zip (or have no ORG counterpart):
    "Access.mid":     None,  # wiimidi2 has access.mid
    "Anzen.mid":      None,  # wiimidi2 has anzen.mid
    "Balcony.mid":    None,  # wiimidi2 has balcony.mid
    "Ballos.mid":     None,  # wiimidi2 has ballos.mid
    "Bdown.mid":      None,  # wiimidi2 has bdown.mid
    "cave story.mid": None,  # WiiWare-specific extra; no ORG counterpart
    "Cemetery.mid":   None,  # wiimidi2 has cemetery.mid
    "charge.mid":     None,  # WiiWare-specific extra; no ORG counterpart
    "credit wii.mid": None,  # WiiWare-specific extra (also in wiimidi2)
    "Dr.mid":         None,  # wiimidi2 has dr.mid
    "Ending.mid":     None,  # wiimidi2 has ending.mid
    "Escape.mid":     None,  # wiimidi2 has escape.mid
    "fanfale1.mid":   None,  # wiimidi2 has fanfale1.mid
    "fanfale2.mid":   None,  # wiimidi2 has fanfale2.mid
    "fanfale3.mid":   None,  # wiimidi2 has fanfale3.mid
    "FireEye.mid":    None,  # wiimidi2 has fireeye.mid
    "Gameover.mid":   None,  # wiimidi2 has "game over.mid"
    "Ginsuke.mid":    None,  # wiimidi2 has ginsuke.mid
    "Grand.mid":      None,  # wiimidi2 has grand.mid
    "Gravity.mid":    None,  # wiimidi2 has gravity.mid
    "Hell.mid":       None,  # wiimidi2 has hell.mid
    "IronH.mid":      None,  # wiimidi2 has ironh.mid
    "item.mid":       None,  # WiiWare-specific extra; no ORG counterpart
    "jenka.mid":      None,  # wiimidi2 has jenka1.mid (renamed to jenka.mid)
    "Jenka2.mid":     None,  # wiimidi2 has jenka2.mid
    "Kodou.mid":      None,  # wiimidi2 has kodou.mid
    "LastBt.mid":     None,  # wiimidi2 has lastbtl.mid (this is shorter variant)
    "LastBt3.mid":    None,  # wiimidi2 has lastbtl3.mid (renamed to lastbt3.mid)
    "LastBtl.mid":    None,  # wiimidi2 has lastbtl.mid
    "LastBtl3.mid":   None,  # wiimidi2 has lastbtl3.mid
    "LastCave.mid":   None,  # wiimidi2 has lastcave.mid
    "Marine.mid":     None,  # wiimidi2 has marine.mid
    "Maze.mid":       None,  # wiimidi2 has maze.mid
    "MDown2.mid":     None,  # wiimidi2 has mdown2.mid
    "Mura.mid":       None,  # wiimidi2 has mura.mid
    "Oside.mid":      None,  # wiimidi2 has oside.mid
    "Plant.mid":      None,  # wiimidi2 has plant.mid
    "plantation.mid": None,  # WiiWare-specific extra (also in wiimidi2)
    "Quiet.mid":      None,  # wiimidi2 has quiet.mid
    "Requiem.mid":    None,  # wiimidi2 has requiem.mid
    "Toroko.mid":     None,  # wiimidi2 has toroko.mid
    "Vivi.mid":       None,  # wiimidi2 has vivi.mid
    "Wanpak2.mid":    None,  # wiimidi2 has wanpak2.mid
    "wanpaku.mid":    None,  # wiimidi2 has wanpaku.mid
    "weed.mid":       None,  # wiimidi2 has weed.mid
    "Zonbie.mid":     None,  # wiimidi2 has zonbie.mid
}

assert len(WIIMIDI1_NAME_MAP) == 47


# ============================================================================
# Helpers
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
        headers={"User-Agent": "doskutsu-fetch-cs-midi/1.0"},
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


def _extract_from_zip(zip_bytes: bytes, name_map: dict, midi_dir: str,
                      label: str, verbose: bool = True) -> int:
    """Extract entries from the zip per name_map (in-zip name -> output name,
    None to skip). Return number of .mid files written."""
    written = 0
    with zipfile.ZipFile(io.BytesIO(zip_bytes)) as zf:
        zip_names = set(zf.namelist())
        unknown = zip_names - set(name_map.keys())
        if unknown:
            sys.exit(
                f"{label}: unexpected entry/entries {sorted(unknown)} — "
                f"upstream may have changed. Audit before adding to the map."
            )
        missing = set(name_map.keys()) - zip_names
        if missing:
            sys.exit(
                f"{label}: expected entries missing {sorted(missing)} — "
                f"upstream may have changed."
            )

        for in_name, out_name in name_map.items():
            if out_name is None:
                continue
            blob = zf.read(in_name)
            if not blob.startswith(b"MThd"):
                sys.exit(
                    f"{label} {in_name!r}: not a Standard MIDI File "
                    f"(missing MThd header). Upstream is corrupt; bail."
                )
            out_path = os.path.join(midi_dir, out_name)
            with open(out_path, "wb") as fp:
                fp.write(blob)
            written += 1
            if verbose:
                print(f"    -> data/midi/{out_name} ({len(blob)} bytes)")
    return written


def fetch_wiimidi2(midi_dir: str, verbose: bool = True) -> int:
    print("[1/3] wiimidi2.zip (primary WiiWare archive — 39 tracks)")
    zip_bytes = _download(WIIMIDI2_URL, WIIMIDI2_SIZE, WIIMIDI2_SHA256,
                          "wiimidi2.zip", verbose)
    n = _extract_from_zip(zip_bytes, WIIMIDI2_NAME_MAP, midi_dir,
                          "wiimidi2.zip", verbose)
    if n != 39:
        sys.exit(f"wiimidi2.zip: expected 39 .mid files, wrote {n}")
    return n


def fetch_wiimidi1(midi_dir: str, verbose: bool = True) -> int:
    print("[2/3] wiimidi.zip (predecessor archive — 1 unique track: curly)")
    zip_bytes = _download(WIIMIDI1_URL, WIIMIDI1_SIZE, WIIMIDI1_SHA256,
                          "wiimidi.zip", verbose)
    n = _extract_from_zip(zip_bytes, WIIMIDI1_NAME_MAP, midi_dir,
                          "wiimidi.zip", verbose)
    if n != 1:
        sys.exit(f"wiimidi.zip: expected 1 .mid file (curly), wrote {n}")
    return n


def convert_white_via_orgmid(out_dir: str, midi_dir: str,
                             verbose: bool = True) -> bool:
    """Run ORGMID locally on the user's data/org/white.org to produce
    data/midi/white.mid. Returns True on success, False on graceful skip
    (missing prerequisites). Failures of the ORGMID step do NOT abort the
    overall fetch — the WiiWare fetch (40 tracks) is still useful; white
    just degrades to silence-with-Pixtone via Stage 2's runtime-error path."""
    print("[3/3] white.mid via ORGMID local conversion")
    org_path = os.path.join(out_dir, "org", "white.org")
    if not os.path.isfile(org_path):
        print(f"    SKIP: {org_path} not found")
        print(f"          Run scripts/extract-engine-data.py + Cave Story 2004")
        print(f"          freeware extraction first; then re-run this script.")
        return False

    # Detect gcc presence (any modern version works)
    gcc = shutil.which("gcc") or shutil.which("cc")
    if gcc is None:
        print(f"    SKIP: no `gcc` or `cc` found on PATH")
        print(f"          Install build-essential / Xcode CLI / your-platform-equiv,")
        print(f"          then re-run this script.")
        return False
    if verbose:
        print(f"    using compiler: {gcc}")

    # Stage source archive in a temp dir; never persist or redistribute.
    with tempfile.TemporaryDirectory(prefix="orgmid-") as tmp:
        # Step 1: download source archive (sha-pinned)
        try:
            zip_bytes = _download(ORGMID_URL, ORGMID_SIZE, ORGMID_SHA256,
                                  "orgmid-source-0.82.zip", verbose)
        except SystemExit as e:
            print(f"    SKIP: ORGMID source download failed: {e}")
            print(f"          Network problem or upstream change; ORGMID step")
            print(f"          aborted. The 40 WiiWare tracks above are still")
            print(f"          installed; white.org degrades to silence-with-Pixtone.")
            return False

        # Step 2: unpack source
        with zipfile.ZipFile(io.BytesIO(zip_bytes)) as zf:
            zf.extractall(tmp)

        # Step 3: build orgmid binary on user's machine
        # ORGMID's source has a 2010-era qsort prototype that modern GCC
        # rejects with -Wincompatible-pointer-types as error. The flag
        # -Wno-incompatible-pointer-types downgrades to warning. -lm needed
        # for math.h (sin, etc.). main.c #includes findkey.c directly so
        # we only build main.c.
        orgmid_src = os.path.join(tmp, "orgmid", "main.c")
        orgmid_bin = os.path.join(tmp, "orgmid-bin")
        if not os.path.isfile(orgmid_src):
            print(f"    SKIP: orgmid source missing expected layout "
                  f"(main.c not at {orgmid_src})")
            return False

        if verbose:
            print(f"    building ORGMID...")
        try:
            r = subprocess.run(
                [gcc, "-O2", "-Wno-incompatible-pointer-types",
                 "-o", orgmid_bin, orgmid_src, "-lm"],
                check=False, capture_output=True, text=True,
            )
        except OSError as e:
            print(f"    SKIP: failed to invoke {gcc}: {e}")
            return False
        if r.returncode != 0:
            print(f"    SKIP: ORGMID build failed (returncode {r.returncode}):")
            for line in r.stderr.splitlines()[-5:]:
                print(f"      {line}")
            print(f"          Local toolchain incompatible with ORGMID 0.82;")
            print(f"          white.org degrades to silence-with-Pixtone.")
            return False

        # Step 4: convert white.org → data/midi/white.mid
        out_path = os.path.join(midi_dir, "white.mid")
        if verbose:
            print(f"    converting {org_path} -> {out_path}...")
        try:
            r = subprocess.run(
                [orgmid_bin, org_path, out_path],
                check=False, capture_output=True, text=True,
            )
        except OSError as e:
            print(f"    SKIP: failed to invoke orgmid binary: {e}")
            return False
        if r.returncode != 0:
            print(f"    SKIP: ORGMID conversion failed (returncode {r.returncode}):")
            for line in (r.stderr or r.stdout).splitlines()[-5:]:
                print(f"      {line}")
            return False

        # Validate output is a real SMF
        if not os.path.isfile(out_path):
            print(f"    SKIP: ORGMID produced no output file at {out_path}")
            return False
        with open(out_path, "rb") as fp:
            head = fp.read(4)
        if head != b"MThd":
            print(f"    SKIP: ORGMID output is not a valid SMF (header={head!r})")
            os.remove(out_path)
            return False

        size = os.path.getsize(out_path)
        if verbose:
            print(f"    -> data/midi/white.mid ({size} bytes, MThd verified)")

    # Tempdir auto-removed at end of `with`. ORGMID source + binary do NOT
    # persist on the user's machine after this function returns.
    return True


def main():
    if len(sys.argv) != 2:
        sys.exit(f"usage: {sys.argv[0]} <output-data-dir>")
    out_dir = sys.argv[1]
    if not os.path.isdir(out_dir):
        sys.exit(f"{out_dir}: not a directory (run after Step 3 of docs/ASSETS.md)")

    midi_dir = os.path.join(out_dir, "midi")
    os.makedirs(midi_dir, exist_ok=True)

    n_wii2 = fetch_wiimidi2(midi_dir)
    n_wii1 = fetch_wiimidi1(midi_dir)
    white_ok = convert_white_via_orgmid(out_dir, midi_dir)

    total = n_wii2 + n_wii1 + (1 if white_ok else 0)
    print()
    print(f"installed {total} of 41 engine-relevant Cave Story tracks "
          f"to {midi_dir}/")
    if not white_ok:
        print()
        print("Note: white.mid not produced (ORGMID step skipped or failed).")
        print("The MIDI scheduler (Phase 10 Stage 2) handles missing white.mid")
        print("by falling back to silence-with-Pixtone for that song slot.")
        print("This is documented degradation; not a bug.")
    elif total == 41:
        print()
        print("100% engine-relevant track coverage. Phase 10 hardware-MIDI")
        print("backends (WaveBlaster / OPL3) can now route every Cave Story")
        print("song through the audio-device's GM bank.")

    print()
    print("Note: xxxx.org is leftover/test data from Pixel's freeware data")
    print("dir; it is NOT indexed by NXEngine-evo's music.jsn (engine has no")
    print("song slot for it). xxxx is intentionally out of scope for Phase 10.")


if __name__ == "__main__":
    main()
