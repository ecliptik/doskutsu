# Cave Story Game Assets

DOSKUTSU requires the Cave Story game data (maps, sprites, music, dialogue) at runtime. **These are not redistributed in this repository** — they are freeware-licensed by Daisuke "Pixel" Amaya under his 2004 terms, but we keep them out of the repo and out of `dist/doskutsu-cf.zip` to avoid licensing ambiguity (see [PLAN.md § Licensing](../PLAN.md#licensing)).

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

# Also merge NXEngine-evo's engine support data (fonts, UI, PBM backgrounds,
# StageMeta, endpic) — these live at the same level as the Cave Story content:
cp -r vendor/nxengine-evo/data/* data/
```

Verify (using NXEngine-source-true paths — note: no `base/` subdir):
```
data/Stage/0.pxm          # Cave Story maps exist
data/Npc/NpcSym.pbm       # Cave Story NPC sprites exist
data/org/gravity.org      # Cave Story Organya music exists (lowercased)
data/pxt/fx02.pxt         # Cave Story Pixtone params exist
data/npc.tbl              # Cave Story NPC table exists
data/MyChar.pbm           # Cave Story player sprite exists
data/font_1.fnt           # NXEngine-evo engine bitmap font exists
data/StageMeta/*.json     # NXEngine-evo engine stage metadata exists
```

---

## Step 4: test

With assets in place, either:

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
