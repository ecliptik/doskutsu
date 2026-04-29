# Cave Story Game Assets

DOSKUTSU requires the Cave Story game data (maps, sprites, music, dialogue) at runtime. **These are not redistributed in this repository** — they are freeware-licensed by Daisuke "Pixel" Amaya under his 2004 terms, but we keep them out of the repo and out of `dist/doskutsu-cf.zip` to avoid licensing ambiguity.

This document tells you how to obtain and extract the assets yourself, and where DOSKUTSU expects to find them.

---

## Target layout

DOSKUTSU's NXEngine-evo source resolves assets via `data/<filename>` relative to the runtime base. **There is no `base/` subdirectory** — all assets (Cave Story content + NXEngine engine support files) coexist under a single `data/` tree.

On DOS:

```
C:\DOSKUTSU\DOSKUTSU.EXE
C:\DOSKUTSU\CWSDPMI.EXE
C:\DOSKUTSU\DATA\
    Stage\                  (map data from Cave Story: .pxm, .pxe, .pxa, .tsc)
    Npc\                    (NPC sprite sheets: .pbm)
    org\                    (Organya music: .org — extracted from Doukutsu.exe PE resources)
    pxt\                    (Pixtone synth params: fxNN.pxt — extracted from Doukutsu.exe binary)
    StageMeta\              (NXEngine-evo engine: stage metadata JSON)
    endpic\                 (NXEngine-evo engine: end-game pictures)
    npc.tbl                 (NPC behaviour table — Cave Story)
    MyChar.pbm              (player sprite — Cave Story)
    ArmsImage.pbm           (weapon sprites — Cave Story)
    bk*.pbm, Caret.pbm, Bullet.pbm, ... (other Cave Story root assets)
    font_*.fnt, font_*.png  (NXEngine-evo engine bitmap fonts)
    ...                     (other engine + game assets, all flat under DATA\)
```

On the Linux dev host (for DOSBox-X testing), the same layout under `<repo>/data/`:

```
data/
├── Stage/                  (Cave Story maps)
├── Npc/                    (Cave Story NPC sprites)
├── org/                    (Cave Story Organya music, extracted from Doukutsu.exe PE)
├── pxt/                    (Cave Story Pixtone synth params, extracted from Doukutsu.exe data)
├── StageMeta/              (engine, from vendor/nxengine-evo/data/)
├── endpic/                 (engine, from vendor/nxengine-evo/data/)
├── npc.tbl                 (Cave Story root)
├── MyChar.pbm              (Cave Story root)
├── font_*.fnt              (engine, from vendor/nxengine-evo/data/)
└── ...                     (everything else, flat)
```

`data/` is gitignored — Cave Story content is freeware-from-Pixel and we don't redistribute, NXEngine engine data is GPLv3 and is shipped only via the dist bundle (see Phase 8).

**Historical note:** earlier docs and the Makefile install target used a `data/base/` subdirectory convention. That was inconsistent with NXEngine-evo's actual source-side path resolution (`getPath("Stage/0.pxm")` → `data/Stage/0.pxm`, no base prefix). The convention has been reconciled: Cave Story content and engine support files coexist directly under `data/`. The Makefile install target deploys `data/*` to `C:\DOSKUTSU\DATA\` (no `BASE\` subdirectory).

---

## Step 1: obtain the 2004 freeware `Doukutsu.exe`

The canonical source is **https://www.cavestory.org/**. The site hosts:

- The original Japanese version (Doukutsu.exe, ~1.4 MB self-contained)
- Aeon Genesis's English fan translation patch + patched binary
- Older mirrors of both

**Use the English translation** — that's what NXEngine-evo expects by default. Japanese original support would require Phase 2+ work (different font, different text encoding).

Verify the download:
- The English freeware `Doukutsu.exe` is roughly 1.4 MB
- Running it on a modern Linux under Wine should produce the title screen (a quick sanity check before extraction)

**The cavestory.org layout drifts.** Verify URLs manually before scripting any download — a hardcoded path that works today may 404 in six months.

---

## Step 2: extract the assets

Cave Story's data is embedded in the resource section of `Doukutsu.exe`, plus a set of external `.pxm` / `.pxe` / `.pxa` map files and `.org` music files in the distribution's `data/` folder. You need a tool that understands both.

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

Original tool for the Cave Story fan scene. Predates doukutsu-rs. Works but increasingly hard to track down a trustworthy binary. If you find it on Macintosh Garden, RHDN, or Archive.org, verify the download checksum if one is provided.

### Option C: a pre-extracted `data/` from a trusted NXEngine-evo fork

Some NXEngine-evo forks (e.g., Debian packaging, retro-gaming community forks) ship pre-extracted `data/` trees. This is the fastest path but only trustworthy if the source is a known-good fork — **not** a random archive. **Note:** older forks may use a `data/base/` layout; you'll need to flatten it (`mv data/base/* data/ && rmdir data/base`) since NXEngine-evo's source-side path resolution doesn't honour a `base/` subdir.

If in doubt, extract yourself via Option A.

### Option D: a pre-patched English archive (cavestory.one fastest path)

The Cave Story Tribute Site at <https://www.cavestory.one/downloads/cavestoryen.zip> ships an Aeon-Genesis-pre-patched `Doukutsu.exe` plus the loose `data/` files (Stage/, Npc/, root .pbm/.tsc/npc.tbl). It does **not** include the embedded ORG/PXT data — those still need extraction from `Doukutsu.exe` itself via `7z x` (PE resources) for the `.org` files plus `scripts/extract-pxt.py` (binary-table-driven) for the `.pxt` files. The `scripts/extract-pxt.py` script is doskutsu-authored, transcribed from `vendor/nxengine-evo/src/extract/extractpxt.cpp`'s `extract_pxt()` algorithm — operates on file offsets, no Rust toolchain needed.

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

# Add the binary-extracted Pixtone params (use scripts/extract-pxt.py
# or the doukutsu-rs / NXEngine-evo extract tool):
# produces data/pxt/fxNN.pxt files

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
#                             wavetable.dat to fit DOS 8.3, see § 8.3)
#   data/stage.dat           (6936 bytes, 95 stages)
#   data/endpic/pixel.bmp    (1398 bytes — 25-byte BMP file-header
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
# StgMeta, endpic) — these live at the same level as the Cave Story content:
cp -r vendor/nxengine-evo/data/* data/

# Rename the user-extracted Cave Story content files that violate DOS 8.3
# (PrtAlmond.pbm, Almond.pxa, NpcBallos.pbm, NpcIsland.pbm, NpcPriest.pbm,
# NpcStream.pbm, ArmsImage.pbm, ItemImage.pbm, StageImage.pbm,
# StageSelect.tsc). Idempotent — safe to run on already-renamed trees.
scripts/rename-user-data-83.sh data
```

---

## Step 4: build the master palette (Phase 9 wave 16 / Lever 3)

Once `data/` contains the full extracted asset tree (Step 3 finished), build
the 8bpp master palette and per-asset remap LUTs that the wave-16 INDEX8
renderer consumes at boot:

```bash
python3 tools/build-master-palette.py data/
# Outputs:
#   data/master.pal     — 768 bytes, 256 RGB triples (reserved: index 0 = black,
#                         indices 1..16 = gradient ramp slots, indices 17..254 =
#                         octree-quantized leaves, index 255 = magenta colorkey)
#   data/master.map     — ~5 KB, per-asset source-palette → master remap LUTs
#                         (PMAP/v1 header + 12 bytes overhead + 110 entries).
#                         8.3-clean filename — survives DOSBox-X lfn=false and
#                         real DOS 6.22 (where the legacy 13-char `master.palmap`
#                         would alias onto `master.pal`).
```

The tool runs Gervautz–Purgathofer octree quantization across the corpus and
validates each indexed asset's remap quality via PSNR. It exits non-zero if
any indexed sprite falls below 28 dB (or 30 dB for `Face*.pbm` portraits) so
you'll know immediately if a mod's assets broke the gate. Truecolor
backgrounds (`bkHellsh.pbm` / `bkLight.pbm` / `bkSunset.pbm`) take the slow
per-pixel nearest-color path at boot and have no PSNR floor (they dither by
design — see `docs/PHASE9-PALETTE-AUDIT.md`).

These two files are **derivative works of Cave Story freeware data**; they
are NOT redistributed via this repo or `dist/doskutsu-cf.zip`. Re-run the
tool any time you change `data/` (e.g. installing a mod or replacing a
sprite sheet).

Verify (using NXEngine-source-true paths — note: no `base/` subdir):
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
                          # 8.3 trims; see § 8.3 below)
data/master.pal           # 256-color master palette (768 bytes; Step 4)
data/master.map           # per-asset source → master remap LUTs (Step 4;
                          # 8.3-clean filename for DOS LFN-off compatibility)
```

---

## 8.3 filename convention {#8.3}

Real MS-DOS 6.22 (no LFN driver) enforces 8.3 at the filesystem layer.
DOSKUTSU runs against this constraint by renaming all engine and
extractor-emitted assets to fit. The renames live in three patches:

- `patches/nxengine-evo/0033-asset-renames-source.patch` — source-side
  string-literal updates (Organya.cpp, SoundManager.cpp, translate.cpp,
  tsc.cpp, map.cpp, credits.cpp, stagedata.cpp).
- `patches/nxengine-evo/0034-asset-renames-data-files.patch` — physical
  rename of the engine-bundled data tree (`StageMeta/` → `StgMeta/`,
  `*.json` → `*.jsn`, `bkHellish.pbm` → `bkHellsh.pbm`, etc.).
- `patches/nxengine-evo/0035-asset-renames-sprites-sif.patch` — binary
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
| `bk*480fix.pbm` (×5) | (excluded from staging — dead code at 320×240) |
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

## Step 5: test

With assets in place AND `data/master.pal` + `data/master.map` generated by
Step 4, either:

```bash
make                                                     # build DOSKUTSU.EXE
tools/dosbox-launch.sh --fast --exe build/doskutsu.exe   # DOSBox-X, fast config
```

Expected behavior:
1. Title screen appears
2. Pressing Z enters the stage
3. Quote is visible, moves with arrow keys, jumps with Z
4. Organya music plays in Mimiga Village

If the title screen appears but stages don't load, the Cave Story content under `data/Stage/`, `data/Npc/`, etc. is incomplete. If the title screen doesn't appear at all, the engine data merged from `vendor/nxengine-evo/data/` is likely missing.

---

## Deploying to real hardware

`make install CF=/mnt/cf` copies the binary + CWSDPMI + (if `data/` is present) the full extracted asset tree (Cave Story content + NXEngine-evo engine data) to `C:\DOSKUTSU\` on the mounted CF card. This is a convenience for your own use — the assets are being copied onto your own storage, not uploaded or redistributed.

`make dist` (for producing `dist/doskutsu-cf.zip` to share publicly) **does not include `data/`**. End users of the zip must follow this document themselves to assemble Cave Story assets.

---

## Legal notes

- Cave Story is freeware per Pixel's 2004 terms. Personal use, extraction, and redistribution *of the data* are permitted within his terms.
- DOSKUTSU's choice not to redistribute the data from this repo is a deliberate legal-simplicity choice:
  1. It avoids any ambiguity about whether NXEngine-evo's GPLv3 (the dominant license of `DOSKUTSU.EXE`) could attempt to re-license game data by inclusion.
  2. It puts users directly in contact with Pixel's original release, which is the canonical way to obtain the game.
  3. It keeps the repo small and the dist bundle focused on the port itself.
- The NXEngine-evo-bundled engine data (fonts, PBM UI, etc.) **is** redistributed — that data is part of NXEngine-evo and thus GPLv3 under its upstream license. It's separate from Cave Story game content.

See [THIRD-PARTY.md](../THIRD-PARTY.md) for the full attribution matrix.
