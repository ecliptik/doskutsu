# Cave Story Game Assets

DOSKUTSU requires the Cave Story game data (maps, sprites, music, dialogue) at runtime. **These are not redistributed in this repository** — they are freeware-licensed by Daisuke "Pixel" Amaya under his 2004 terms, but we keep them out of the repo and out of `dist/doskutsu-cf.zip` to avoid licensing ambiguity (see [PLAN.md § Licensing](../PLAN.md#licensing)).

This document tells you how to obtain and extract the assets yourself, and where DOSKUTSU expects to find them.

---

## Target layout

DOSKUTSU expects the extracted data under `data/base/` at runtime, next to the binary. On DOS:

```
C:\DOSKUTSU\DOSKUTSU.EXE
C:\DOSKUTSU\CWSDPMI.EXE
C:\DOSKUTSU\DATA\
    NXEngine-evo engine support files (fonts, UI, etc.) — from vendor/nxengine-evo/data/
C:\DOSKUTSU\DATA\BASE\
    Stage\                  (map data: .pxm, .pxe, .pxa)
    Npc\                    (NPC sprite sheets: .pbm or .bmp)
    org\                    (Organya music: .org)
    wav\                    (sound effects: .wav or .pxt for Pixtone)
    npc.tbl                 (NPC table)
    MyChar.bmp              (player sprite)
    ArmsImage.bmp           (weapon sprites)
    ...                     (other root-level Cave Story assets)
```

On the Linux dev host (for DOSBox-X testing), the same layout under `<repo>/data/`:

```
data/
├── (engine data, copied from vendor/nxengine-evo/data/ at install time)
└── base/
    ├── Stage/
    ├── Npc/
    ├── org/
    ├── wav/
    ├── npc.tbl
    ├── MyChar.bmp
    └── ...
```

Both `data/` and `data/base/` are gitignored.

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

Copy the result to `<repo>/data/base/`.

### Option B: `NXExtract` (older, harder to find)

Original tool for the Cave Story fan scene. Predates doukutsu-rs. Works but increasingly hard to track down a trustworthy binary. If you find it on Macintosh Garden, RHDN, or Archive.org, verify the download checksum if one is provided.

### Option C: a pre-extracted `data/base/` from a trusted NXEngine-evo fork

Some NXEngine-evo forks (e.g., Debian packaging, retro-gaming community forks) ship pre-extracted `data/base/` trees. This is the fastest path but only trustworthy if the source is a known-good fork — **not** a random archive.

If in doubt, extract yourself via Option A.

---

## Step 3: drop the files in place

```bash
# On the Linux dev host, for DOSBox-X testing:
mkdir -p data/base
cp -r /path/to/extracted/base/* data/base/

# Also copy NXEngine-evo's engine support data (fonts, UI, PBM backgrounds)
# once vendor/nxengine-evo/ is cloned:
cp -r vendor/nxengine-evo/data/* data/
```

Verify:
```
data/base/Stage/0.pxm     # maps exist
data/base/Npc/NpcSym.pbm  # NPC sprites exist
data/base/org/Gravity.org # Organya music exists
data/base/wav/*           # or data/base/*.pxt — either is fine
data/base/npc.tbl         # NPC table exists
data/fonts/cour.fnt       # engine bitmap font (from NXEngine-evo)
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

If the title screen appears but stages don't load, `data/base/` is incomplete. If the title screen doesn't appear at all, the engine data (NXEngine-evo's `data/`, not `data/base/`) is likely missing.

---

## Deploying to real hardware

`make install CF=/mnt/cf` copies the binary + CWSDPMI + (if `data/base/` is present) the extracted Cave Story assets to `C:\DOSKUTSU\` on the mounted CF card. This is a convenience for your own use — the assets are being copied onto your own storage, not uploaded or redistributed.

`make dist` (for producing `dist/doskutsu-cf.zip` to share publicly) **does not include `data/base/`**. End users of the zip must follow this document themselves.

---

## Legal notes

- Cave Story is freeware per Pixel's 2004 terms. Personal use, extraction, and redistribution *of the data* are permitted within his terms.
- DOSKUTSU's choice not to redistribute the data from this repo is a deliberate legal-simplicity choice:
  1. It avoids any ambiguity about whether NXEngine-evo's GPLv3 (the dominant license of `DOSKUTSU.EXE`) could attempt to re-license game data by inclusion.
  2. It puts users directly in contact with Pixel's original release, which is the canonical way to obtain the game.
  3. It keeps the repo small and the dist bundle focused on the port itself.
- The NXEngine-evo-bundled engine data (fonts, PBM UI, etc.) **is** redistributed — that data is part of NXEngine-evo and thus GPLv3 under its upstream license. It's separate from Cave Story game content.

See [THIRD-PARTY.md](../THIRD-PARTY.md) for the full attribution matrix.
