# Cave Story Game Assets

DOSKUTSU requires the Cave Story game data (maps, sprites, music, dialogue) at runtime. **These are not redistributed in this repository** -- they are freeware-licensed by Daisuke "Pixel" Amaya under his 2004 terms, but we keep them out of the repo and out of `dist/doskutsu-cf.zip` to avoid licensing ambiguity.

This document covers obtaining and extracting the assets, and where DOSKUTSU expects to find them.

---

## Quick path (most people want this)

For a working `data/` tree for DOSBox-X or a CF deploy, this is the shortest
reliable route on a Linux/WSL dev host (from the repo root, after a successful `make`):

```bash
# 1. SFX params: downloads the SHA-pinned 2004 freeware bundle, extracts Doukutsu.exe to a
#    tempdir, emits data/pxt/fx*.pxt, then cleans up (no freeware persists on disk):
python3 scripts/fetch-cs-pxt.py data/

# 2. Engine blobs (Organya wavetable, stage index, end picture) from a Doukutsu.exe on disk:
scripts/extract-engine-data.py /path/to/Doukutsu.exe data/

# 3. Maps / sprites / music / .org: download cavestoryen.zip from cavestory.one and copy its
#    data/* into ./data/ (or extract with doukutsu-rs, Option A below). Then add the engine
#    support files (fonts, UI, metadata) that ship with NXEngine-evo:
cp -r vendor/nxengine-evo/data/* data/

# 4. Optional MIDI music (for the opl3 / wb / GUS backends; skip if only using organya).
#    (a) WiiWare arrangement set -> data/midi/ (downloaded, SHA-pinned):
python3 scripts/fetch-cs-midi.py data/
#    (b) OrgMIDI sets -> data/orgmid1/ (v1) + data/orgmid2/ (v2). These are NOT downloaded --
#    our tracked org2mid tool CONVERTS them from data/org/*.org, so they are reproducible from
#    source at any time. data/orgmid2 has native GM percussion (best drums on the GUS). See
#    "MIDI music sets" below -- this is the step that is easy to forget and leaves stale drums:
make org2mid && make convert-music

# 5. Normalize names to DOS 8.3 + build the render palette:
scripts/rename-user-data-83.sh data
python3 tools/build-master-palette.py data/
```

Verify with the checklist at the end of Step 4. The rest of this document explains each step
in full, with the licensing posture and the alternatives. The internal "Phase / wave / Tier /
Lever" labels in the headings below are development history -- safe to ignore when just
assembling assets to play.

---

## Target layout

DOSKUTSU's NXEngine-evo source resolves assets via `data/<filename>` relative to the runtime base. **There is no `base/` subdirectory** -- all assets (Cave Story content + NXEngine engine support files) coexist under a single `data/` tree.

On DOS:

```
C:\DOSKUTSU\DOSKUTSU.EXE
C:\DOSKUTSU\CWSDPMI.EXE
C:\DOSKUTSU\DATA\
    Stage\                  (map data from Cave Story: .pxm, .pxe, .pxa, .tsc)
    Npc\                    (NPC sprite sheets: .pbm)
    org\                    (Organya music: .org -- extracted from Doukutsu.exe PE resources)
    pxt\                    (Pixtone synth params: fxNN.pxt -- extracted from Doukutsu.exe binary)
    StageMeta\              (NXEngine-evo engine: stage metadata JSON)
    endpic\                 (NXEngine-evo engine: end-game pictures)
    npc.tbl                 (NPC behaviour table -- Cave Story)
    MyChar.pbm              (player sprite -- Cave Story)
    ArmsImage.pbm           (weapon sprites -- Cave Story)
    bk*.pbm, Caret.pbm, Bullet.pbm, ... (other Cave Story root assets)
    font_*.fnt, font_*.png  (NXEngine-evo engine bitmap fonts)
    ...                     (other engine + game assets, all flat under DATA\)
```

On the Linux dev host (for DOSBox-X testing), the same layout under `<repo>/data/`:

```
data/
+-- Stage/                  (Cave Story maps)
+-- Npc/                    (Cave Story NPC sprites)
+-- org/                    (Cave Story Organya music, extracted from Doukutsu.exe PE)
+-- pxt/                    (Cave Story Pixtone synth params, extracted from Doukutsu.exe data)
+-- StageMeta/              (engine, from vendor/nxengine-evo/data/)
+-- endpic/                 (engine, from vendor/nxengine-evo/data/)
+-- npc.tbl                 (Cave Story root)
+-- MyChar.pbm              (Cave Story root)
+-- font_*.fnt              (engine, from vendor/nxengine-evo/data/)
`-- ...                     (everything else, flat)
```

`data/` is gitignored -- Cave Story content is freeware-from-Pixel and we don't redistribute, NXEngine engine data is GPLv3 and is shipped only via the dist bundle (see Phase 8).

**Historical note:** earlier docs and the Makefile install target used a `data/base/` subdirectory convention. That was inconsistent with NXEngine-evo's actual source-side path resolution (`getPath("Stage/0.pxm")` -> `data/Stage/0.pxm`, no base prefix). The convention has been reconciled: Cave Story content and engine support files coexist directly under `data/`. The Makefile install target deploys `data/*` to `C:\DOSKUTSU\DATA\` (no `BASE\` subdirectory).

---

## Step 1: obtain the 2004 freeware `Doukutsu.exe`

The canonical source is **https://www.cavestory.org/**. The site hosts:

- The original Japanese version (Doukutsu.exe, ~1.4 MB self-contained)
- Aeon Genesis's English fan translation patch + patched binary
- Older mirrors of both

**Use the English translation** -- that's what NXEngine-evo expects by default. Japanese original support would require Phase 2+ work (different font, different text encoding).

Verify the download:
- The English freeware `Doukutsu.exe` is roughly 1.4 MB
- Running it on a modern Linux under Wine should produce the title screen (a quick sanity check before extraction)

**The cavestory.org layout drifts.** Verify URLs manually before scripting any download -- a hardcoded path that works today may 404 in six months.

---

## Step 2: extract the assets

Cave Story's data is embedded in the resource section of `Doukutsu.exe`, plus a set of external `.pxm` / `.pxe` / `.pxa` map files and `.org` music files in the distribution's `data/` folder. Extraction needs a tool that understands both.

### Option A: `doukutsu-rs` (recommended)

[doukutsu-rs](https://github.com/doukutsu-rs/doukutsu-rs) is a modern, maintained Rust re-implementation of the Cave Story engine. It includes extraction tooling that handles the 2004 EN freeware cleanly.

```bash
# Clone and build
git clone https://github.com/doukutsu-rs/doukutsu-rs.git
cd doukutsu-rs
cargo build --release

# (See doukutsu-rs's README for the exact extraction incantation;
# it has drifted across releases. The current flow is usually:
#   place Doukutsu.exe + its data/ directory under a `base/` subdir,
#   then run doukutsu-rs pointed at it. It extracts the PE resources
#   into loose files that NXEngine-evo understands.)
```

Copy the result to `<repo>/data/`.

### Option B: `NXExtract` (older, harder to find)

Original tool for the Cave Story fan scene. Predates doukutsu-rs. Works but increasingly hard to track down a trustworthy binary. If found on Macintosh Garden, RHDN, or Archive.org, verify the download checksum if one is provided.

### Option C: a pre-extracted `data/` from a trusted NXEngine-evo fork

Some NXEngine-evo forks (e.g., Debian packaging, retro-gaming community forks) ship pre-extracted `data/` trees. This is the fastest path but only trustworthy if the source is a known-good fork -- **not** a random archive. **Note:** older forks may use a `data/base/` layout; flatten it (`mv data/base/* data/ && rmdir data/base`) since NXEngine-evo's source-side path resolution doesn't honour a `base/` subdir.

If in doubt, extract from scratch via Option A.

### Option D: a pre-patched English archive (cavestory.one fastest path)

The Cave Story Tribute Site at <https://www.cavestory.one/downloads/cavestoryen.zip> ships an Aeon-Genesis-pre-patched `Doukutsu.exe` plus the loose `data/` files (Stage/, Npc/, root .pbm/.tsc/npc.tbl). It does **not** include the embedded ORG/PXT data -- those still need extraction from `Doukutsu.exe` itself via `7z x` (PE resources) for the `.org` files plus `scripts/extract-pxt.py` (binary-table-driven) for the `.pxt` files. The `scripts/extract-pxt.py` script is doskutsu-authored, transcribed from `vendor/nxengine-evo/src/extract/extractpxt.cpp`'s `extract_pxt()` algorithm -- operates on file offsets, no Rust toolchain needed.

For PXT specifically, **`scripts/fetch-cs-pxt.py`** is the one-shot
orchestrator: it downloads `cavestoryen.zip` (SHA-256-pinned), extracts
`Doukutsu.exe` to a tempdir, runs `scripts/extract-pxt.py` against it,
emits the 86 `fx<HEX>.pxt` files into `data/pxt/`, and cleans up the
tempdir so the freeware archive + binary do not persist on the user's
machine. Same fetch-and-pin convention as `scripts/fetch-cs-midi.py`
(Step 4.5 below). Usage:

```bash
python3 scripts/fetch-cs-pxt.py data/
```

#### Known upstream limitation: 37 Pixtone slots are gaps in Pixel's 2004 freeware

The Cave Story 2004 EN freeware `Doukutsu.exe` defines Pixtone parameter
blocks for exactly **86 of the 117 slots** NXEngine-evo's runtime
iterates over (`NUM_SOUNDS = 0x75` per
`vendor/nxengine-evo/src/sound/Pixtone.h:178`). The 37 absent slots are:

```
decimal:  8, 9, 10, 13, 19, 36, 66-69, 73-99
hex:      fx08, fx09, fx0a, fx0d, fx13, fx24, fx42-45, fx49-63
```

These are **unnamed gaps in the engine's SFX enum** --
`vendor/nxengine-evo/src/sound/SoundManager.h:63` declares
`SND_<NAME> = <decimal-id>` constants for the 80 named SFX (1-7, 11-12,
14-18, 20-35, 37-65, 70-72, 100-117, 150-155), and the 37 missing-PXT
IDs land in the gaps between named ranges. **No engine code calls
`Pixtone::play()` with these slot numbers.** They show up only because
`Pixtone::Pixtone()` blindly iterates `slot = 1 .. NUM_SOUNDS` and
attempts to load each one regardless of whether it has a named constant --
a defensive iteration that lets mods add `fxNN.pxt` files for
previously-unused slots without code changes.

Verified at the TSC-script level too: decrypting all 95 vanilla
`data/Stage/*.tsc` files and scanning for `<SOU` (Cave Story's
SFX-trigger TSC command -- note: NOT `<SND`, that's a different opcode
in NXEngine's TSC interpreter), Pixel's freeware scripts reference
**exactly 18 unique SFX slot numbers**:

```
TSC <SOU> args used in vanilla Cave Story:
  [4, 11, 12, 16, 20, 22, 23, 26, 29, 35, 43, 44, 45, 70, 71, 72, 101, 105]
```

All 18 are present in our 86-file extracted set; zero overlap with the
missing 37. So even the scripted SFX surface -- independent of the
engine's C++ SFX-by-name dispatch -- never asks for the missing slots.
The vanilla game runs to completion without ever needing a single
parameter block from the 37 absent IDs.

**Gameplay impact: zero.** The `LOG_WARN("pxt->load: file ... not
found.")` line for each of the 37 absent slots is a one-time-per-boot
warning emitted from `vendor/nxengine-evo/src/sound/Pixtone.cpp:76`
during initialization. `stPXSound::load` returns `false` and Pixtone
continues to the next slot. Runtime `Pixtone::play()` is silent on
missing slots (no-op + no log line per call) because no game code ever
references those slot numbers.

**Sourcing**: there is no upstream-distributable source for these 37
slots. Verified:

- NXEngine-evo's own `vendor/nxengine-evo/src/extract/extractpxt.cpp`
  SND[] table is byte-for-byte identical to our `scripts/extract-pxt.py`
  table -- the same 86 IDs, in the same order, at the same byte offsets.
  The gaps are gaps in `Doukutsu.exe`, not gaps in our extractor.
- NXEngine-evo's bundled `vendor/nxengine-evo/data/` ships zero `.pxt`
  files; upstream doesn't bundle the missing 37 either.
- NXEngine-evo [issue #4](https://github.com/nxengine/nxengine-evo/issues/4)
  (open since 2017) reports the same `pxt_load: file not found`
  warn-spam. Maintainers don't document an upstream source -- the
  implication is that the gap is accepted as upstream behavior.
- [doukutsu-rs](https://github.com/doukutsu-rs/doukutsu-rs) is the
  sibling Cave Story re-implementation; their wiki documents an
  architectural divergence (WAV samples for drums instead of PXT) but
  no separate community PXT pack for the missing IDs.
- GitHub search for the specific filenames (`fx49.pxt`, `fx4b.pxt`,
  `fx5a.pxt`) returns zero indexed hits -- no community PXT pack is
  published anywhere on GitHub.
- The Cave Story+ / Wii / 3DS DSiWare commercial releases contain
  additional SFX, but they're WAV samples owned by NICALIS -- neither
  PXT-format nor GPLv3-compatible per `CLAUDE.md sec. Licensing`.

The 37 warn lines at boot are cosmetic. When debugging an actual
"no SFX" bug, look for slots that *do* exist in the canonical 86 set
but fail to load (path issue, file corruption, etc.) -- the missing 37
are noise, not signal.

---

## Step 3: drop the files in place

```bash
# On the Linux dev host, for DOSBox-X testing:
mkdir -p data
cp -r /path/to/extracted/CaveStory/data/* data/

# Add the PE-extracted Organya music (lowercase filenames):
mkdir -p data/org
for f in /path/to/extracted/PE-resources/ORG/*; do
  name=$(basename "$f" | tr '[:upper:]' '[:lower:]')
  cp "$f" "data/org/${name}.org"
done

# Add the binary-extracted Pixtone params. Two equivalent paths:
#
#   (a) one-shot orchestrator (recommended -- handles source fetch + extract
#       in one step, cleans up the freeware binary on exit):
#         python3 scripts/fetch-cs-pxt.py data/
#         # downloads cavestoryen.zip (SHA-256-pinned), extracts Doukutsu.exe
#         # to a tempdir, runs scripts/extract-pxt.py, removes tempdir.
#
#   (b) manual path (with Doukutsu.exe already staged from any source):
#         mkdir -p data/pxt
#         scripts/extract-pxt.py /path/to/Doukutsu.exe data/pxt
#
# Either produces 86 data/pxt/fx<HEX>.pxt files.

# Add the binary-extracted Organya wavetable + stage index + the
# endpic/pixel.bmp blanking sprite. These three blobs live inside
# Doukutsu.exe (wavetable at offset 0x110664, stage table at offset
# 0x937B0, pixel.bmp data at offset 0x16722f) and are consumed verbatim
# (or with a small reconstructed BMP header for pixel.bmp) by
# NXEngine-evo at runtime. scripts/extract-engine-data.py is the
# doskutsu-authored sibling of extract-pxt.py; it transcribes the
# algorithm from vendor/nxengine-evo/src/extract/extractstages.cpp +
# extractfiles.cpp and produces:
#   data/wavetbl.dat         (25600 bytes; renamed from upstream's
#                             wavetable.dat to fit DOS 8.3, see sec. 8.3)
#   data/stage.dat           (6936 bytes, 95 stages)
#   data/endpic/pixel.bmp    (1398 bytes -- 25-byte BMP file-header
#                             reconstruction + 1373 bytes of palette and
#                             pixel data; CRC-32 verified against the
#                             0x6181d0a1 value in extractfiles.cpp's
#                             files[] table)
#
# pixel.bmp is referenced from data/sprites.sif as a sprite-sheet entry;
# without it, the engine emits a runtime "drawSurface NULL texture"
# diagnostic that is silenced cosmetically by NXEngine patch 0030 but
# better cleared at the source.
scripts/extract-engine-data.py /path/to/Doukutsu.exe data/

# Also merge NXEngine-evo's engine support data (fonts, UI, PBM backgrounds,
# StgMeta, endpic) -- these live at the same level as the Cave Story content:
cp -r vendor/nxengine-evo/data/* data/

# Rename the user-extracted Cave Story content files that violate DOS 8.3
# (PrtAlmond.pbm, Almond.pxa, NpcBallos.pbm, NpcIsland.pbm, NpcPriest.pbm,
# NpcStream.pbm, ArmsImage.pbm, ItemImage.pbm, StageImage.pbm,
# StageSelect.tsc). Idempotent -- safe to run on already-renamed trees.
scripts/rename-user-data-83.sh data
```

---

## Step 4: build the master palette

Once `data/` contains the full extracted asset tree (Step 3 finished), build
the 8bpp master palette and per-asset remap LUTs that DOSKUTSU's 8bpp indexed
renderer consumes at boot:

```bash
python3 tools/build-master-palette.py data/
# Outputs:
#   data/master.pal     -- 768 bytes, 256 RGB triples (reserved: index 0 = black,
#                         indices 1..16 = gradient ramp slots, indices 17..254 =
#                         octree-quantized leaves, index 255 = magenta colorkey)
#   data/master.map     -- ~5 KB, per-asset source-palette -> master remap LUTs
#                         (PMAP/v1 header + 12 bytes overhead + 110 entries).
#                         8.3-clean filename -- survives DOSBox-X lfn=false and
#                         real DOS 6.22 (where the legacy 13-char `master.palmap`
#                         would alias onto `master.pal`).
```

The tool runs Gervautz-Purgathofer octree quantization across the corpus and
validates each indexed asset's remap quality via PSNR. It exits non-zero if
any indexed sprite falls below 28 dB (or 30 dB for `Face*.pbm` portraits) so
a mod's assets breaking the gate shows up immediately. Truecolor
backgrounds (`bkHellsh.pbm` / `bkLight.pbm` / `bkSunset.pbm`) take the slow
per-pixel nearest-color path at boot and have no PSNR floor (they dither by
design).

These two files are **derivative works of Cave Story freeware data**; they
are NOT redistributed via this repo or `dist/doskutsu-cf.zip`. Re-run the
tool any time `data/` changes (e.g. installing a mod or replacing a
sprite sheet).

Verify (using NXEngine-source-true paths -- note: no `base/` subdir):
```
data/Stage/0.pxm          # Cave Story maps exist
data/Npc/NpcSym.pbm       # Cave Story NPC sprites exist
data/org/gravity.org      # Cave Story Organya music exists (lowercased)
data/pxt/fx02.pxt         # Cave Story Pixtone params exist
data/wavetbl.dat          # Organya PCM wavetable (extract-engine-data.py)
data/stage.dat            # 95-record stage index (extract-engine-data.py)
data/endpic/pixel.bmp     # blanking sprite (extract-engine-data.py)
data/endpic/credit01.bmp..credit18.bmp  # 17 credit images (extract-engine-data.py; credit13 intentionally absent)
data/StgSel.tsc           # stage-select TSC (renamed from StageSelect.tsc)
data/StgMeta/Start.jsn    # engine stage-metadata (renamed from StageMeta/Start.json)
data/Stage/PrtAlmnd.pbm   # Almond tileset (renamed from PrtAlmond.pbm)
data/npc.tbl              # Cave Story NPC table exists
data/MyChar.pbm           # Cave Story player sprite exists
data/font_1.fnt           # NXEngine-evo engine bitmap font exists
data/StgMeta/*.jsn        # NXEngine-evo engine stage metadata
                          # (Cave Story stems are all 8.3-clean; only the
                          # extension and the parent directory needed
                          # 8.3 trims; see sec. 8.3 below)
data/master.pal           # 256-color master palette (768 bytes; Step 4)
data/master.map           # per-asset source -> master remap LUTs (Step 4;
                          # 8.3-clean filename for DOS LFN-off compatibility)
```

---

## 8.3 filename convention {#8.3}

Real MS-DOS 6.22 (no LFN driver) enforces 8.3 at the filesystem layer.
DOSKUTSU runs against this constraint by renaming all engine and
extractor-emitted assets to fit. The renames live in three patches:

- `patches/nxengine-evo/0033-asset-renames-source.patch` -- source-side
  string-literal updates (Organya.cpp, SoundManager.cpp, translate.cpp,
  tsc.cpp, map.cpp, credits.cpp, stagedata.cpp).
- `patches/nxengine-evo/0034-asset-renames-data-files.patch` -- physical
  rename of the engine-bundled data tree (`StageMeta/` -> `StgMeta/`,
  `*.json` -> `*.jsn`, `bkHellish.pbm` -> `bkHellsh.pbm`, etc.).
- `patches/nxengine-evo/0035-asset-renames-sprites-sif.patch` -- binary
  regeneration of `sprites.sif` to update the embedded sheet-path
  strings via `scripts/rename-sif.py`.

User-extracted Cave Story content is renamed by
`scripts/rename-user-data-83.sh` (idempotent), which Step 3 above runs
as the last extraction step. The full rename map:

| Long name | 8.3 form |
|---|---|
| `wavetable.dat` | `wavetbl.dat` |
| `music.json` | `music.jsn` |
| `music_dirs.json` | `musicdir.jsn` |
| `system.json` | `system.jsn` |
| `StageSelect.tsc` | `StgSel.tsc` |
| `ArmsImage.pbm` | `ArmImg.pbm` |
| `ItemImage.pbm` | `ItmImg.pbm` |
| `StageImage.pbm` | `StgImg.pbm` |
| `bkHellish.pbm` | `bkHellsh.pbm` |
| `bk*480fix.pbm` (x5) | (excluded from staging -- dead code at 320x240) |
| `StageMeta/` | `StgMeta/` |
| `StageMeta/<name>.json` | `StgMeta/<name>.jsn` |
| `Stage/PrtAlmond.pbm` | `Stage/PrtAlmnd.pbm` |
| `Stage/Almond.pxa` | `Stage/Almnd.pxa` |
| `Npc/NpcBallos.pbm` | `Npc/NpcBalls.pbm` |
| `Npc/NpcIsland.pbm` | `Npc/NpcIslnd.pbm` |
| `Npc/NpcPriest.pbm` | `Npc/NpcPrst.pbm` |
| `Npc/NpcStream.pbm` | `Npc/NpcStrm.pbm` |
| `endpic/credit01m.bmp` | `endpic/credt01m.bmp` |
| `endpic/credit02m.bmp` | `endpic/credt02m.bmp` |
| `endpic/credit03m.bmp` | `endpic/credt03m.bmp` |

Mod compatibility note: Cave Story mods that ship their own canonical
long-named assets (the modding scene's convention since 2004) won't load
under DOSKUTSU on real DOS without renaming. The current decision is
8.3-only; mod-side compatibility may be revisited later.

---

## Step 4.5: optional -- fetch MIDI tracks for hardware-MIDI audio (Phase 10)

Phase 10 introduces a hardware-MIDI audio backend (WaveBlaster daughterboard
on Tier-1 g2k; OPL3 register programming on Tier-2/3 fallback) that bypasses
the per-IRQ Organya synth and offloads music rendering to the audio
hardware. This requires Cave Story tracks in Standard MIDI File (.mid)
form; the freeware ORG files we extracted in Step 3 are not directly
playable by a MIDI device.

The fetch + conversion script combines three sources to give 41/41 = 100%
engine-relevant track coverage. Run it after Step 3:

```bash
python3 scripts/fetch-cs-midi.py data/
```

The script does three things in sequence (no flags or env vars required):

1. **Downloads `wiimidi2.zip`** from cavestory.one (~5.4 MB; SHA-256-pinned).
   Yann van der Cruyssen's WiiWare MIDI arrangements (2010); extracts 39 of
   41 engine-relevant tracks with filename normalization (e.g.
   `game over.mid` -> `gameover.mid`, `lastbtl3.mid` -> `lastbt3.mid`).

2. **Downloads `wiimidi.zip`** from cavestory.one (~138 KB; SHA-256-pinned).
   The 2010 predecessor archive by the same arranger; extracts the one
   engine-track NOT present in wiimidi2.zip -- `Curly.mid` -> `curly.mid`.

3. **Builds Robert Hart's ORGMID converter locally** (~12 KB source archive;
   SHA-256-pinned at the rnhart.net upstream URL) and runs it against
   the local `data/org/white.org` to produce `data/midi/white.mid`. This
   is the one engine-track for which no community .mid exists in any
   cavestory.one archive; ORGMID converts ORG -> MIDI deterministically
   locally. ORGMID source + binary live in a tempdir during the
   build and are removed when the script finishes; never persist on disk
   or in this repo.

**Prerequisites:**

- The user has run `scripts/extract-engine-data.py` (Step 3) so
  `data/org/white.org` exists. Without it, the ORGMID step skips
  gracefully with instructions; the WiiWare fetch (40 tracks) still
  completes.
- For the ORGMID build: a working `gcc` (any modern version). On Linux:
  `apt install build-essential` (or distro equivalent) if not installed.
  On Windows / WSL: same. On macOS: Xcode CLI tools. Without gcc, the
  ORGMID step skips gracefully; the 40 WiiWare tracks still install.

If the ORGMID step skips for any reason, the MIDI scheduler (Phase 10
Stage 2) handles missing `white.mid` by falling back to silence-with-
Pixtone for that song slot -- Pixtone SFX continues; only the background
music is silent during white.org's gameplay segments. Documented
degradation, not a bug.

**Note on `xxxx.org`:** The Cave Story 2004 freeware data dir contains
42 ORG files, but `xxxx.org` is leftover/test data with zero references
in NXEngine-evo's `music.jsn` song-slot table or anywhere in the engine
source. The engine cannot reach `xxxx.org` regardless of whether a MIDI
exists for it, so xxxx is intentionally OUT OF SCOPE for Phase 10
(sourcing a MIDI for an engine-unreachable track would be wasted
effort). xxxx IS a real Pixel track per the official Studio Pixel
soundtrack release; we just cannot play it through the current
NXEngine-evo song-slot space.

**This step is OPTIONAL.** Without it, DOSKUTSU runs the original
Organya synth path (the only path before Phase 10), which works on all
tiers but spends ~14 ms per render flip on audio IRQ wall-clock work on
the g2k Pentium-OD-83 (per Phase 9 wave 20 measurement). The
hardware-MIDI route trades that wall-clock cost for offloaded rendering
on the audio device, with the aesthetic trade-off that the GM patches
in the audio device's onboard bank don't sound like Organya waveforms.

**Provenance + license posture:** same redistribution
posture throughout: user-fetches-locally; sources never in our repo or
dist zip. ORGMID source archive's license is unspecified on the
upstream page; we use the user-fetches-locally pattern (download +
build + run on user's machine) precisely because the absence of an
explicit license means we make no claim on usage rights; the user's
interaction with rnhart.net handles that.

### Choosing the MIDI music set

The MIDI backends (`opl3` / `wb` / `gus`) can play more than one music set.
The engine resolves the set from `SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE` (see
`docs/CONFIG.md`), and SETUP exposes the choice as the **MIDI music set**
row on the Sound -> Music Options screen. The row appears only when a MIDI
backend is selected AND at least two sets are installed; with a single set
the row is hidden and the engine's default resolution applies (OrgMIDI v2
when present, falling back to WiiWare).

Three sets are supported today:

- **OrgMIDI v2** (`data/orgmid2/`, hint value `orgmid2`, **the default**) --
  our org2mid native-GM conversion, generated by `make convert-music`. The
  operator's g2k A/B picked it over WiiWare (distinct GM drums).
- **WiiWare** (`data/midi/`, hint value `wiimidi`) -- the polished
  re-arrangements installed by `scripts/fetch-cs-midi.py` above.
- **OrgMIDI** (shown as "OrgMIDI" in SETUP; `data/orgmid/`, hint value
  `orgmid`) -- a note-for-note transcription of Pixel's original Organya
  music, generated locally by:

  ```bash
  python3 scripts/build-cs-midi-orgmid.py data/
  ```

  Same fetch-and-pin, user-fetches-locally posture as the WiiWare script
  (it downloads + builds Robert Hart's ORGMID converter in a tempdir and
  runs it against the local `data/org/*.org`; the `.mid` output stays
  local and is never redistributed from this repo). After it runs,
  re-launch SETUP and the **MIDI music set** row offers OrgMIDI alongside
  the other installed sets.

If a set is missing some of the 41 engine tracks (e.g. a partial fetch),
SETUP flags it in the picker's description ("`N` fewer than the fullest
set; those songs will play no music"), and the engine warns + skips the
missing track at play time.

### Custom MIDI set (drop-in)

Beyond the built-in sets, a custom MIDI arrangement can be supplied:

1. Make a directory under `data/` and drop the `.mid` files in it, each
   named after the engine track it replaces (the same base names the
   scripts above produce in `data/midi/`, e.g. `curly.mid`, `access.mid`,
   `oside.mid`). On real DOS hardware the directory name must be 8.3-legal
   -- 8 characters or fewer, letters/digits only is safest:

   ```
   data/mymidi/curly.mid
   data/mymidi/access.mid
   ...
   ```

2. Re-launch SETUP. The **MIDI music set** row now lists the directory as
   `Custom (mymidi)` alongside the built-in sets; pick it and save. (Or set
   `MIDI_SET=mymidi` in `DOSKUTSU.CFG` / `SET SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE=mymidi`
   by hand -- see `docs/CONFIG.md`.)

The engine accepts the directory only when the name is a single safe path
segment and the directory holds at least one `.mid`; otherwise it falls
back to WiiWare. Tracks not provided simply stay silent for those
songs (same graceful skip as a partial built-in set). The killswitch
`SDL_HINT_DOSKUTSU_AUDIO_MIDI_CUSTOM_DIRS=0` restricts selection back to the
built-in sets if the old behavior is ever needed.

**Provenance:** custom sets are entirely user-supplied -- we redistribute
nothing and make no claim on the user's `.mid` files. Same user-fetches/supplies-
locally posture as everything else in this step.

### The OrgMIDI sets (generated by `org2mid` -- reproducible; do not hand-edit)

Besides the downloaded WiiWare set, doskutsu ships a tool that **converts the
original Organya music to Standard MIDI itself**, so there is always a
first-party MIDI arrangement with no external download. Generate it after
Step 3 (`data/org/*.org` must exist):

```bash
make org2mid          # builds tools/org2mid/org2mid (host C compiler)
make convert-music    # converts every data/org/*.org -> data/orgmid1/ + data/orgmid2/
```

This produces two sets from the same sources, via the `--gm-table` option:

| Dir | `org2mid` table | Notes |
|-----|-----------------|-------|
| `data/orgmid1/` | `--gm-table=v1` | first GM-instrument mapping |
| `data/orgmid2/` | `--gm-table=v2` (default) | tuned melody mapping **and native GM percussion** -- the best drums on the GUS |

> **CRITICAL -- these dirs are GENERATED, not stored.** `data/midi/` (WiiWare,
> downloaded) and `data/orgmid*/` (converted) are **not** tracked in git and are
> **not** in any release archive. The durable inputs are the tracked
> `tools/org2mid/org2mid.c` tool plus the extracted `data/org/*.org` sources --
> from those, `make convert-music` recreates the MIDI sets byte-for-byte at any
> time. If the generated dirs are ever deleted, or a track turns up with wrong
> or missing drums, the fix is always **re-run `make org2mid && make
> convert-music`** -- never hand-edit a generated `.mid`. (A pre-tool `data/orgmid/`
> may linger from an older conversion with stale, out-of-GM-range drum notes;
> regenerate `orgmid2` and prefer it.)

**Selecting a set at runtime** (SETUP's MIDI-set row, or by hand): OrgMIDI v2 =
`MIDI_SET=orgmid2` (**the default**) -> `data/orgmid2/`; WiiWare =
`MIDI_SET=wiimidi` -> `data/midi/` (the `SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE`
/ `..._MIDI_GM_VARIANT` env vars, see `docs/CONFIG.md`). On the GUS the two sets
voice drums differently -- see [docs/SOUND.md](./SOUND.md#midi-music-sets).

**Deploying:** copy the generated dirs into the CF's `DATA\` next to `midi\`
(`DATA\orgmid1\`, `DATA\orgmid2\`), then run `scripts/rename-user-data-83.sh`.

---

## Step 5: test

With assets in place AND `data/master.pal` + `data/master.map` generated by
Step 4, either:

```bash
make                                                     # build DOSKUTSU.EXE
tools/dosbox-launch.sh --stage --fast --exe DOSKUTSU.EXE  # DOSBox-X, fast config
```

Expected behavior:
1. Title screen appears
2. Pressing Z enters the stage
3. Quote is visible, moves with arrow keys, jumps with Z
4. Organya music plays in Mimiga Village

If the title screen appears but stages don't load, the Cave Story content under `data/Stage/`, `data/Npc/`, etc. is incomplete. If the title screen doesn't appear at all, the engine data merged from `vendor/nxengine-evo/data/` is likely missing.

---

## Deploying to real hardware

`make install CF=/mnt/cf` copies the binary + CWSDPMI + (if `data/` is present) the full extracted asset tree (Cave Story content + NXEngine-evo engine data) to `C:\DOSKUTSU\` on the mounted CF card. This is a convenience for personal use -- the assets are copied onto personally-owned storage, not uploaded or redistributed.

`make dist` (for producing `dist/doskutsu-cf.zip` to share publicly) bundles NXEngine-evo's GPLv3 engine support data (fonts, UI, StgMeta, endpic) but **not** any Cave Story game content (maps, sprites, music, SFX). End users of the zip must follow this document to assemble the Cave Story assets themselves -- the engine ships, the game data does not, exactly like a Doom port and its WAD.

---

## Legal notes

- Cave Story is freeware per Pixel's 2004 terms. Personal use, extraction, and redistribution *of the data* are permitted within his terms.
- DOSKUTSU's choice not to redistribute the data from this repo is a deliberate legal-simplicity choice:
  1. It avoids any ambiguity about whether NXEngine-evo's GPLv3 (the dominant license of `DOSKUTSU.EXE`) could attempt to re-license game data by inclusion.
  2. It puts users directly in contact with Pixel's original release, which is the canonical way to obtain the game.
  3. It keeps the repo small and the dist bundle focused on the port itself.
- The NXEngine-evo-bundled engine data (fonts, PBM UI, etc.) **is** redistributed -- that data is part of NXEngine-evo and thus GPLv3 under its upstream license. It's separate from Cave Story game content.

See [THIRD-PARTY.md](../THIRD-PARTY.md) for the full attribution matrix.
