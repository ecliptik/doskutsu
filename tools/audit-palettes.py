#!/usr/bin/env python3
"""
audit-palettes.py — Phase 9 Lever 3 palette diversity audit for Cave Story sprites.

Reads every Windows BMP (.pbm / .bmp) under data/, extracts its color palette,
and reports cluster structure to drive the choice between Lever 3 approaches:

  A — single canonical palette  (only viable if palettes are already similar)
  B — per-stage palette swaps   (only viable if palettes cluster by stage)
  C — octree merge to a master  (always viable, most work)

stdlib only.

Output:
  - CSV at  tools/audit-palettes.csv         (per-sprite palette hash + stats)
  - CSV at  tools/audit-palettes-clusters.csv (cluster representatives + members)
  - Stats summary printed to stdout.

The "BMP" files Cave Story ships use the .pbm extension. They are NOT Netpbm —
they are standard Microsoft DIB (BITMAPINFOHEADER), which `SDL_LoadBMP()`
consumes directly in `vendor/nxengine-evo/src/graphics/Surface.cpp:28`.

The DIB palette is `biClrUsed` entries (256 if 0 for an 8bpp image, or 16 for
4bpp, or 2 for 1bpp), each entry is 4 bytes BGRA0 (the alpha-byte field is
always 0x00 in classic DIB).
"""

from __future__ import annotations

import csv
import glob
import hashlib
import os
import struct
import sys
from collections import defaultdict

DATA_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "data")
OUT_CSV = os.path.join(os.path.dirname(os.path.abspath(__file__)), "audit-palettes.csv")
CLUSTER_CSV = os.path.join(os.path.dirname(os.path.abspath(__file__)), "audit-palettes-clusters.csv")

# Tolerance: two palette entries are "the same" if all RGB channels match within +/- this.
CHANNEL_TOL = 8
# Cluster threshold: two sprites are in the same cluster if at least this fraction of
# entries (computed against the smaller palette) match within the tolerance.
CLUSTER_THRESH = 0.90


def parse_bmp_palette(path):
    """Return (width, height, bpp, palette[(R,G,B)*N], distinct_used) or None.

    palette is None if the BMP is truecolor (>= 16 bpp) — those have no palette.
    distinct_used is None for truecolor; for indexed images it's the count of
    distinct RGB triples actually present in the palette table.
    """
    with open(path, "rb") as f:
        data = f.read()

    if len(data) < 54 or data[0:2] != b"BM":
        return None

    # BITMAPFILEHEADER (14 bytes) then BITMAPINFOHEADER (variable).
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]

    if dib_size < 16:
        return None

    width = struct.unpack_from("<i", data, 18)[0]
    height = struct.unpack_from("<i", data, 22)[0]
    bpp = struct.unpack_from("<H", data, 28)[0]
    clr_used = 0
    if dib_size >= 36:
        clr_used = struct.unpack_from("<I", data, 46)[0]

    if bpp >= 16:
        return (width, height, bpp, None, None)

    palette_offset = 14 + dib_size
    if clr_used == 0:
        n = 1 << bpp
    else:
        n = clr_used

    palette = []
    seen = set()
    for i in range(n):
        off = palette_offset + i * 4
        if off + 4 > pixel_offset:
            break
        b, g, r, _a = data[off], data[off + 1], data[off + 2], data[off + 3]
        palette.append((r, g, b))
        seen.add((r, g, b))

    return (width, height, bpp, palette, len(seen))


def palette_hash(palette):
    if palette is None:
        return ""
    h = hashlib.sha1()
    for r, g, b in palette:
        h.update(bytes((r, g, b)))
    return h.hexdigest()[:12]


def palette_match_pct(pa, pb, tol=CHANNEL_TOL):
    """Pct of pa entries that have a within-tol match anywhere in pb.

    Computed against the smaller palette so a 16-color sprite isn't penalized
    for failing to cover a 256-color palette.
    """
    if not pa or not pb:
        return 0.0
    if len(pa) > len(pb):
        pa, pb = pb, pa
    set_b = pb  # linear scan; palettes are <= 256 entries
    matches = 0
    for r1, g1, b1 in pa:
        for r2, g2, b2 in set_b:
            if abs(r1 - r2) <= tol and abs(g1 - g2) <= tol and abs(b1 - b2) <= tol:
                matches += 1
                break
    return matches / len(pa)


def cluster_sprites(rows):
    """Single-link cluster: a sprite joins an existing cluster if it matches
    the cluster representative at >= CLUSTER_THRESH similarity."""
    clusters = []  # list of {"rep": filename, "rep_pal": palette, "members": [filenames]}
    for r in rows:
        if r["palette"] is None:
            continue  # truecolor handled separately
        placed = False
        for c in clusters:
            sim = palette_match_pct(r["palette"], c["rep_pal"])
            if sim >= CLUSTER_THRESH:
                c["members"].append(r["filename"])
                placed = True
                break
        if not placed:
            clusters.append({
                "rep": r["filename"],
                "rep_pal": r["palette"],
                "members": [r["filename"]],
            })
    clusters.sort(key=lambda c: -len(c["members"]))
    return clusters


def find_most_divergent(rows, top_n=10):
    """For each indexed sprite, compute its average similarity to all other
    indexed sprites. Lowest avg = most divergent."""
    indexed = [r for r in rows if r["palette"] is not None]
    scored = []
    for r in indexed:
        sims = []
        for r2 in indexed:
            if r2["filename"] == r["filename"]:
                continue
            sims.append(palette_match_pct(r["palette"], r2["palette"]))
        avg = sum(sims) / len(sims) if sims else 0.0
        scored.append((avg, r["filename"], r["bpp"], len(r["palette"])))
    scored.sort()
    return scored[:top_n]


def main():
    if not os.path.isdir(DATA_DIR):
        print(f"FATAL: {DATA_DIR} does not exist", file=sys.stderr)
        sys.exit(2)

    paths = []
    for ext in ("*.pbm", "*.bmp"):
        paths.extend(glob.glob(os.path.join(DATA_DIR, "**", ext), recursive=True))
    paths.sort()

    rows = []
    truecolor_count = 0
    skipped = []
    bpp_hist = defaultdict(int)

    for p in paths:
        rel = os.path.relpath(p, DATA_DIR)
        info = parse_bmp_palette(p)
        if info is None:
            skipped.append(rel)
            continue
        w, h, bpp, palette, distinct = info
        bpp_hist[bpp] += 1
        if palette is None:
            truecolor_count += 1
            rows.append({
                "filename": rel, "width": w, "height": h, "bpp": bpp,
                "palette": None, "palette_hash": "(truecolor)",
                "distinct_colors": "(truecolor)",
            })
        else:
            rows.append({
                "filename": rel, "width": w, "height": h, "bpp": bpp,
                "palette": palette, "palette_hash": palette_hash(palette),
                "distinct_colors": distinct,
            })

    # Per-sprite CSV
    with open(OUT_CSV, "w", newline="") as f:
        wr = csv.writer(f)
        wr.writerow(["filename", "width", "height", "bpp", "palette_hash",
                     "palette_size", "distinct_colors_used"])
        for r in rows:
            psize = len(r["palette"]) if r["palette"] is not None else ""
            wr.writerow([r["filename"], r["width"], r["height"], r["bpp"],
                         r["palette_hash"], psize, r["distinct_colors"]])

    # Cluster
    clusters = cluster_sprites(rows)

    with open(CLUSTER_CSV, "w", newline="") as f:
        wr = csv.writer(f)
        wr.writerow(["cluster_idx", "representative", "member_count", "member"])
        for i, c in enumerate(clusters):
            for m in c["members"]:
                wr.writerow([i, c["rep"], len(c["members"]), m])

    # Most-divergent
    divergent = find_most_divergent(rows, top_n=10)

    indexed_total = sum(1 for r in rows if r["palette"] is not None)
    largest_pct = 100.0 * len(clusters[0]["members"]) / indexed_total if clusters else 0.0

    # Stats summary to stdout
    print("=" * 70)
    print("Cave Story sprite palette audit — Phase 9 Lever 3")
    print("=" * 70)
    print(f"Data directory:      {DATA_DIR}")
    print(f"Total BMP files:     {len(rows)}")
    print(f"  Indexed (palette): {indexed_total}")
    print(f"  Truecolor (>=16bpp): {truecolor_count}")
    print(f"Skipped (non-BMP):   {len(skipped)}")
    print()
    print("BPP distribution:")
    for bpp in sorted(bpp_hist):
        print(f"  {bpp:3d} bpp: {bpp_hist[bpp]:3d} files")
    print()
    print(f"Cluster threshold:   >= {int(CLUSTER_THRESH*100)}% match within +/- {CHANNEL_TOL} per channel")
    print(f"Distinct clusters:   {len(clusters)}")
    print(f"Largest cluster:     {len(clusters[0]['members'])} sprites "
          f"({largest_pct:.1f}% of indexed)" if clusters else "(no clusters)")
    print()
    print("Cluster representatives (top 10 by member count):")
    for i, c in enumerate(clusters[:10]):
        print(f"  [{i:2d}] {c['rep']:<32s}  {len(c['members'])} members")
    print()
    print("Top 10 most-divergent sprites (lowest avg similarity to peers):")
    print("       avg_sim  filename                              bpp  pal_size")
    for avg, fn, bpp, psize in divergent:
        print(f"      {avg*100:6.1f}%  {fn:<36s}  {bpp:3d}  {psize:4d}")
    print()
    print(f"Truecolor sprites (no palette — gradient cutscene art):")
    for r in rows:
        if r["palette"] is None:
            print(f"  - {r['filename']}  ({r['width']}x{r['height']}x{r['bpp']})")
    print()
    print(f"CSVs written:")
    print(f"  {OUT_CSV}")
    print(f"  {CLUSTER_CSV}")

    # Exit with verdict in numeric form for any wrapping caller
    if clusters and largest_pct >= 90.0:
        print("\nVERDICT: HOMOGENEOUS  (Approach A is viable)")
    elif len(clusters) <= 10:
        print("\nVERDICT: STAGE-CLUSTERED  (Approach B candidate; check cluster names)")
    else:
        print("\nVERDICT: DIVERSE  (Approach C — octree merge — recommended)")


if __name__ == "__main__":
    main()
