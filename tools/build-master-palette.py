#!/usr/bin/env python3
"""
build-master-palette.py — Phase 9 Wave 16 / Lever 3 master palette builder.

Reads every Microsoft DIB BMP (.pbm / .bmp) under data/, runs Gervautz–
Purgathofer octree quantization across the whole pixel-frequency corpus, and
emits two files:

  data/master.pal     — 768 bytes, 256 RGB triples. Reserved indices:
                          0       = pure black (colorkey for Surface::loadImage)
                          1..16   = pre-baked gradient ramp from the 3 truecolor
                                    backgrounds (bkHellsh / bkLight / bkSunset)
                          17..254 = octree-quantized leaves, sorted by count
                                    descending
                          255     = magenta colorkey for fonts / PNG transparency

  data/master.map     — per-asset source-palette → master-palette index remap
                        LUTs. Format: 12-byte PMAP/v1 header + N entries of
                          uint16 path_len, ascii path, uint16 src_palette_size,
                          remap[src_palette_size].
                        Truecolor sources (no source palette) are written with
                        src_palette_size=0 and no LUT — runtime falls back to
                        per-pixel nearest-color search at load time.

                        Filename is `master.map` (8.3-clean) by default so it
                        survives DOSBox-X `lfn=false` and real DOS 6.22 without
                        truncation/aliasing onto `master.pal`. Use
                        --output-palmap to emit the legacy 13-char name on a
                        host with LFN support; the byte format is identical.

Colorkey-stable special-case rules (per /tmp/wave-16-1-palette-format.md §3.4):
  * Master index 0  is FORCED to (0, 0, 0)         — colorkey for Surface::loadImage
  * Master index 255 is FORCED to (255, 0, 255)    — magenta colorkey for fonts / PNG α
  * Source-palette entry == (0, 0, 0)        → master index 0   (direct, no nearest-search)
  * Source-palette entry == (255, 0, 255)    → master index 255 (direct, no nearest-search)
  * All other source colors run through CCIR-601 weighted-Euclidean nearest match.

Without these special cases, octree may pick a near-black (e.g. (0,0,32)) into
slot 0 or near-magenta into slot 255, and the renderer's `pixel == 0` /
`pixel == 255` colorkey skip would treat real pixels as transparent (or vice
versa). The reserved-slot enforcement + direct-assignment forms a closed loop
that the wave-16-2 INDEX8 blitter relies on.

stdlib only. Format spec: /tmp/wave-16-1-palette-format.md.

Usage:
  tools/build-master-palette.py [data_dir]
                                [--output-pal PATH]
                                [--output-palmap PATH]   (default: data/master.map)
                                [--no-fail-on-psnr]

Exits 1 if any indexed asset's remap fails its PSNR gate (28 dB general /
30 dB for Face*.pbm portraits) unless --no-fail-on-psnr is passed.
"""

from __future__ import annotations

import argparse
import glob
import math
import os
import struct
import sys


# === Reserved master-palette slot allocation ===
# Per /tmp/wave-16-1-palette-format.md §1.2.
RESERVED_BLACK_IDX = 0
RESERVED_MAGENTA_IDX = 255
RAMP_START = 1
RAMP_COUNT = 16                                  # indices 1..16 inclusive
RAMP_END = RAMP_START + RAMP_COUNT - 1            # 16
OCTREE_START = RAMP_END + 1                       # 17
OCTREE_END = RESERVED_MAGENTA_IDX - 1             # 254
OCTREE_SLOTS = OCTREE_END - OCTREE_START + 1      # 238

# === PSNR gates ===
PSNR_FLOOR_GENERAL = 28.0
PSNR_FLOOR_FACE = 30.0

# === master.map (a.k.a. legacy master.palmap) header ===
# Magic is the four ASCII bytes 'P','M','A','P' written in that order to the
# file. Read as a little-endian uint32 they form 0x50414D50. (NOT 0x504D4150,
# which would write the bytes 'P','A','M','P' — the parent spec text was
# imprecise about endianness.)
PMAP_MAGIC_BYTES = b"PMAP"
PMAP_MAGIC_LE_U32 = 0x50414D50
PMAP_VERSION = 1


# ----------------------------------------------------------------------------
# BMP parsing
# ----------------------------------------------------------------------------

def parse_bmp(path):
    """Parse a Microsoft DIB BMP. Returns dict with keys:

        width, height, bpp                — image dimensions and bit depth
        palette                           — list[(r,g,b)] for indexed (None for truecolor)
        palette_pixel_count               — list[int] per-palette-index count
                                             (None for truecolor)
        pixels                            — list[(r,g,b)] row-major top-to-bottom
                                             (only populated for truecolor; indexed
                                             gets None to save memory)

    Raises ValueError on unsupported BMPs.
    """
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 54 or data[0:2] != b"BM":
        raise ValueError("not a Microsoft DIB BMP")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    width = struct.unpack_from("<i", data, 18)[0]
    height = struct.unpack_from("<i", data, 22)[0]
    bpp = struct.unpack_from("<H", data, 28)[0]
    clr_used = 0
    if dib_size >= 36:
        clr_used = struct.unpack_from("<I", data, 46)[0]

    abs_height = abs(height)
    bottom_up = height > 0

    palette = None
    if bpp <= 8:
        n_pal = clr_used if clr_used > 0 else (1 << bpp)
        po = 14 + dib_size
        palette = []
        for i in range(n_pal):
            off = po + i * 4
            if off + 3 >= pixel_offset:
                break
            b, g, r = data[off], data[off + 1], data[off + 2]
            palette.append((r, g, b))
        # Some 4bpp images carry an "incomplete" palette that's smaller than
        # 16; pad with black so pixel indices that exceed the table don't OOB.
        full = 1 << bpp
        while len(palette) < full:
            palette.append((0, 0, 0))

    if bpp == 1:
        row_bytes = ((width + 31) // 32) * 4
    elif bpp == 4:
        row_bytes = ((width * 4 + 31) // 32) * 4
    elif bpp == 8:
        row_bytes = ((width + 3) // 4) * 4
    elif bpp == 24:
        row_bytes = ((width * 3 + 3) // 4) * 4
    elif bpp == 32:
        row_bytes = width * 4
    else:
        raise ValueError(f"unsupported bpp {bpp}")

    n_pixels = width * abs_height
    palette_pixel_count = [0] * len(palette) if palette is not None else None
    pixels = [(0, 0, 0)] * n_pixels if palette is None else None

    for row_idx in range(abs_height):
        src_row = (abs_height - 1 - row_idx) if bottom_up else row_idx
        row_off = pixel_offset + src_row * row_bytes
        dst_base = row_idx * width

        if bpp == 1:
            for x in range(width):
                byte = data[row_off + (x >> 3)]
                idx = (byte >> (7 - (x & 7))) & 1
                palette_pixel_count[idx] += 1
        elif bpp == 4:
            for x in range(width):
                byte = data[row_off + (x >> 1)]
                idx = (byte >> 4) & 0xF if (x & 1) == 0 else byte & 0xF
                palette_pixel_count[idx] += 1
        elif bpp == 8:
            for x in range(width):
                idx = data[row_off + x]
                palette_pixel_count[idx] += 1
        elif bpp == 24:
            for x in range(width):
                off = row_off + x * 3
                pixels[dst_base + x] = (data[off + 2], data[off + 1], data[off])
        elif bpp == 32:
            for x in range(width):
                off = row_off + x * 4
                pixels[dst_base + x] = (data[off + 2], data[off + 1], data[off])

    return {
        "width": width,
        "height": abs_height,
        "bpp": bpp,
        "palette": palette,
        "palette_pixel_count": palette_pixel_count,
        "pixels": pixels,
    }


# ----------------------------------------------------------------------------
# Octree quantizer (Gervautz–Purgathofer 1988)
# ----------------------------------------------------------------------------

class _OctreeNode:
    __slots__ = ("level", "is_leaf", "r_sum", "g_sum", "b_sum", "count",
                 "children", "parent", "in_reducible")

    def __init__(self, level, parent):
        self.level = level
        self.is_leaf = False
        self.r_sum = 0
        self.g_sum = 0
        self.b_sum = 0
        self.count = 0
        self.children = [None] * 8
        self.parent = parent
        self.in_reducible = False


class OctreeQuantizer:
    """Pixel-frequency-weighted octree color quantizer.

    Standard 8-level octree. Each leaf carries (R_sum, G_sum, B_sum, count);
    `count` is the sum of pixel-frequency weights, NOT distinct-color count.

    Reduction strategy: at each step, find the deepest interior node whose
    children are all leaves and whose combined child count is smallest. Merge
    children into that node (it becomes a leaf). Repeat until leaf_count
    drops to the target.
    """

    MAX_DEPTH = 8

    def __init__(self):
        self.root = _OctreeNode(0, None)
        self.leaf_count = 0
        # reducible[level] = list of nodes at that level whose children are
        # all leaves (and which therefore can be reduced into a single leaf).
        self.reducible = [[] for _ in range(self.MAX_DEPTH + 1)]

    @staticmethod
    def _color_index(r, g, b, level):
        shift = 7 - level
        return (((r >> shift) & 1) << 2) | (((g >> shift) & 1) << 1) | ((b >> shift) & 1)

    def add_color(self, r, g, b, weight=1):
        """Insert a color into the octree, weighted by `weight`."""
        node = self.root
        level = 0
        while True:
            if node.is_leaf or level == self.MAX_DEPTH:
                if not node.is_leaf:
                    node.is_leaf = True
                    self.leaf_count += 1
                node.r_sum += r * weight
                node.g_sum += g * weight
                node.b_sum += b * weight
                node.count += weight
                if node.parent is not None:
                    self._check_reducible(node.parent)
                return
            idx = self._color_index(r, g, b, level)
            if node.children[idx] is None:
                node.children[idx] = _OctreeNode(level + 1, node)
            node = node.children[idx]
            level += 1

    def _check_reducible(self, node):
        if node.is_leaf or node.in_reducible:
            return
        for c in node.children:
            if c is not None and not c.is_leaf:
                return
        self.reducible[node.level].append(node)
        node.in_reducible = True

    def reduce_to(self, target_leaves):
        """Merge nodes until leaf_count <= target_leaves."""
        while self.leaf_count > target_leaves:
            if not self._reduce_one():
                break

    def _reduce_one(self):
        # Find deepest level with reducible nodes.
        for level in range(self.MAX_DEPTH, -1, -1):
            bucket = self.reducible[level]
            if not bucket:
                continue
            # Pick the smallest-combined-count node.
            best = None
            best_count = None
            for node in bucket:
                total = sum(c.count for c in node.children if c is not None)
                if best is None or total < best_count:
                    best = node
                    best_count = total
            self._merge(best)
            return True
        return False

    def _merge(self, node):
        leaves_subsumed = sum(1 for c in node.children if c is not None and c.is_leaf)
        for c in node.children:
            if c is not None:
                node.r_sum += c.r_sum
                node.g_sum += c.g_sum
                node.b_sum += c.b_sum
                node.count += c.count
                c.parent = None
        node.children = [None] * 8
        node.is_leaf = True
        if node.in_reducible:
            self.reducible[node.level].remove(node)
            node.in_reducible = False
        self.leaf_count -= (leaves_subsumed - 1)
        if node.parent is not None:
            self._check_reducible(node.parent)

    def collect_leaves(self):
        out = []
        self._collect(self.root, out)
        return out

    def _collect(self, node, out):
        if node.is_leaf:
            out.append(node)
            return
        for c in node.children:
            if c is not None:
                self._collect(c, out)


# ----------------------------------------------------------------------------
# Color matching (CCIR 601 weighted Euclidean)
# ----------------------------------------------------------------------------

def color_distance_sq(r1, g1, b1, r2, g2, b2):
    """Weighted-Euclidean distance squared. Coefficients × 1000 for integer math."""
    dr = r1 - r2
    dg = g1 - g2
    db = b1 - b2
    return 299 * dr * dr + 587 * dg * dg + 114 * db * db


def nearest_index(palette, r, g, b):
    """Find the index in `palette` minimizing weighted-Euclidean distance to
    (r,g,b). Special cases (per format spec §3.4):
      (0, 0, 0)        → RESERVED_BLACK_IDX (0)
      (0xFF, 0, 0xFF)  → RESERVED_MAGENTA_IDX (255)
    """
    if r == 0 and g == 0 and b == 0:
        return RESERVED_BLACK_IDX
    if r == 0xFF and g == 0 and b == 0xFF:
        return RESERVED_MAGENTA_IDX
    best_idx = 0
    best_dist = 1 << 30
    for i, (pr, pg, pb) in enumerate(palette):
        d = color_distance_sq(r, g, b, pr, pg, pb)
        if d < best_dist:
            best_dist = d
            best_idx = i
    return best_idx


# ----------------------------------------------------------------------------
# Gradient ramp sampling
# ----------------------------------------------------------------------------

def luminance(rgb):
    r, g, b = rgb
    return 0.299 * r + 0.587 * g + 0.114 * b


def sample_gradient_ramp(pixels, width, height, n_samples):
    """Find the row OR column with the largest luminance range (the dominant
    gradient axis), then sample n_samples evenly-spaced points along it.
    """
    lums = [luminance(p) for p in pixels]

    best_axis = "row"
    best_range = -1.0
    best_idx = 0

    # Scan rows
    for y in range(height):
        s = y * width
        rl = lums[s:s + width]
        rng = max(rl) - min(rl)
        if rng > best_range:
            best_range = rng
            best_axis = "row"
            best_idx = y

    # Scan columns
    for x in range(width):
        cl = [lums[y * width + x] for y in range(height)]
        rng = max(cl) - min(cl)
        if rng > best_range:
            best_range = rng
            best_axis = "column"
            best_idx = x

    samples = []
    denom = max(1, n_samples - 1)
    if best_axis == "row":
        for i in range(n_samples):
            x = (i * (width - 1)) // denom
            samples.append(pixels[best_idx * width + x])
    else:
        for i in range(n_samples):
            y = (i * (height - 1)) // denom
            samples.append(pixels[y * width + best_idx])
    return samples


def dedupe_close(colors, channel_threshold=12):
    """Remove colors whose channel-sum-distance to an earlier kept color is
    within channel_threshold. Order-preserving."""
    out = []
    for c in colors:
        keep = True
        for ec in out:
            if (abs(c[0] - ec[0]) + abs(c[1] - ec[1]) + abs(c[2] - ec[2])
                    <= channel_threshold):
                keep = False
                break
        if keep:
            out.append(c)
    return out


# ----------------------------------------------------------------------------
# PSNR
# ----------------------------------------------------------------------------

def compute_psnr_from_mse(mse):
    """Standard PSNR formula. mse is mean of channel-squared-error per pixel
    (averaged over RGB channels). Returns dB."""
    if mse <= 0:
        return float("inf")
    return 10.0 * math.log10(255.0 * 255.0 / mse)


# ----------------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(
        description="Build master palette + per-asset remap LUTs (wave 16/Lever 3).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("data_dir", nargs="?", default="data",
                    help="Path to data/ directory (default: data)")
    ap.add_argument("--output-pal", default=None,
                    help="Override master.pal output path "
                         "(default: <data_dir>/master.pal)")
    ap.add_argument("--output-palmap", default=None,
                    help="Override remap-LUT output path "
                         "(default: <data_dir>/master.map). The default 8.3-clean "
                         "name survives DOSBox-X lfn=false and real DOS 6.22; pass "
                         "--output-palmap=<data_dir>/master.palmap if you want the "
                         "legacy 13-char name on an LFN-capable host.")
    ap.add_argument("--no-fail-on-psnr", action="store_true",
                    help="Don't exit nonzero on PSNR-gate failures (warn only).")
    args = ap.parse_args()

    data_dir = os.path.abspath(args.data_dir)
    if not os.path.isdir(data_dir):
        print(f"FATAL: {data_dir} is not a directory", file=sys.stderr)
        sys.exit(2)

    out_pal = args.output_pal or os.path.join(data_dir, "master.pal")
    # Default to the 8.3-clean filename `master.map`. The 13-char legacy name
    # `master.palmap` aliases onto `master.pal` under DOS LFN-off. See
    # /tmp/wave-16-1-palette-format.md §3.5 (corrigendum).
    out_palmap = args.output_palmap or os.path.join(data_dir, "master.map")

    # Walk corpus
    paths = []
    for ext in ("*.pbm", "*.bmp"):
        paths.extend(glob.glob(os.path.join(data_dir, "**", ext), recursive=True))
    paths.sort()

    if not paths:
        print(f"FATAL: no .pbm/.bmp files found under {data_dir}", file=sys.stderr)
        sys.exit(2)

    print("=" * 70)
    print("build-master-palette  (Phase 9 Wave 16, Lever 3)")
    print("=" * 70)
    print(f"data_dir:    {data_dir}")
    print(f"out_pal:     {out_pal}")
    print(f"out_palmap:  {out_palmap}  (8.3-clean default; DOS LFN-safe)")
    print(f"corpus:      {len(paths)} BMP files")
    print()

    # === Step 1: parse all images ===
    print("[1/8] Parsing BMP corpus...")
    images = []
    for p in paths:
        rel = os.path.relpath(p, data_dir).replace(os.sep, "/")
        try:
            info = parse_bmp(p)
        except Exception as e:
            print(f"  SKIP {rel}: {e}", file=sys.stderr)
            continue
        info["rel_path"] = rel
        images.append(info)

    indexed_images = [im for im in images if im["palette"] is not None]
    truecolor_images = [im for im in images if im["palette"] is None]
    print(f"  parsed {len(images)} ({len(indexed_images)} indexed, "
          f"{len(truecolor_images)} truecolor)")
    print()

    # === Step 2: feed pixel-frequency-weighted colors into octree ===
    # Skip the two reserved-slot colors so the octree doesn't waste leaves on
    # them — and, more importantly, doesn't pull near-black colors (e.g.
    # Fade.pbm's (0,0,32)) toward the (0,0,0) centroid via merge averaging.
    # Without this skip, near-black colors get a centroid shift from the
    # massive pixel-count of pure-black backgrounds across the corpus.
    print("[2/8] Building octree from pixel-frequency corpus...")
    print("       (skipping pure black / pure magenta — handled by reserved slots)")
    quant = OctreeQuantizer()

    skipped_black_pixels = 0
    skipped_magenta_pixels = 0

    # Per-image normalization: each image contributes a fixed total weight to
    # the octree regardless of image size. Without this, large-area assets
    # (e.g. 320×240 truecolor backgrounds at 76800 pixels) drown out
    # small-area assets (e.g. bkMaze.pbm at 4096 pixels), causing the
    # latter's distinctive gradients to merge into oblivion during
    # octree reduction. Per-image normalization keeps every asset's color
    # gradients audible in the histogram.
    #
    # Face*.pbm portrait sheets have a tighter 30 dB floor (vs general 28),
    # so their per-image weight is doubled to give skin-tone gradients
    # more resolution.
    PER_IMAGE_WEIGHT = 100000   # arbitrary; ratios are what matter
    FACE_BOOST = 2

    for im in indexed_images:
        rel = im["rel_path"]
        boost = FACE_BOOST if rel.startswith("Face") else 1
        n_pixels = im["width"] * im["height"]
        if n_pixels <= 0:
            continue
        # Per-pixel weight scaled so this image contributes
        # PER_IMAGE_WEIGHT * boost total to the octree.
        # Using integer math: multiply by per-image-weight, then divide.
        for idx, (r, g, b) in enumerate(im["palette"]):
            cnt = im["palette_pixel_count"][idx]
            if cnt <= 0:
                continue
            if r == 0 and g == 0 and b == 0:
                skipped_black_pixels += cnt
                continue
            if r == 0xFF and g == 0 and b == 0xFF:
                skipped_magenta_pixels += cnt
                continue
            w = (cnt * PER_IMAGE_WEIGHT * boost) // n_pixels
            if w <= 0:
                w = 1
            quant.add_color(r, g, b, w)

    for im in truecolor_images:
        n_pixels = im["width"] * im["height"]
        if n_pixels <= 0:
            continue
        # Each truecolor pixel contributes PER_IMAGE_WEIGHT/n_pixels.
        per_px_weight = PER_IMAGE_WEIGHT // n_pixels
        if per_px_weight <= 0:
            per_px_weight = 1
        for r, g, b in im["pixels"]:
            if r == 0 and g == 0 and b == 0:
                skipped_black_pixels += 1
                continue
            if r == 0xFF and g == 0 and b == 0xFF:
                skipped_magenta_pixels += 1
                continue
            quant.add_color(r, g, b, per_px_weight)

    print(f"  initial leaves: {quant.leaf_count}  "
          f"(skipped {skipped_black_pixels} black px, "
          f"{skipped_magenta_pixels} magenta px)")
    print()

    # === Step 3: sample gradient ramps from truecolor backgrounds ===
    print("[3/8] Sampling gradient ramps from truecolor backgrounds...")
    ramp_candidates = []
    for im in truecolor_images:
        s = sample_gradient_ramp(im["pixels"], im["width"], im["height"], RAMP_COUNT)
        print(f"  {im['rel_path']}: {len(s)} candidate samples along dominant axis")
        ramp_candidates.extend(s)

    ramp_unique = dedupe_close(ramp_candidates, channel_threshold=12)
    ramp_unique.sort(key=luminance)
    ramp_colors = ramp_unique[:RAMP_COUNT]
    print(f"  candidates total={len(ramp_candidates)}  "
          f"unique={len(ramp_unique)}  kept={len(ramp_colors)} of {RAMP_COUNT} slots")
    print()

    # === Step 4: reduce octree to the right number of leaves ===
    n_octree_needed = OCTREE_SLOTS + (RAMP_COUNT - len(ramp_colors))
    print(f"[4/8] Reducing octree to {n_octree_needed} leaves "
          f"(238 main + {RAMP_COUNT - len(ramp_colors)} ramp-fill)...")
    quant.reduce_to(n_octree_needed)
    print(f"  final leaves: {quant.leaf_count}")
    print()

    leaves = quant.collect_leaves()
    leaves.sort(key=lambda l: -l.count)
    octree_colors = []
    for l in leaves:
        if l.count <= 0:
            continue
        r = l.r_sum // l.count
        g = l.g_sum // l.count
        b = l.b_sum // l.count
        octree_colors.append((r, g, b))

    # === Step 5: assemble master palette ===
    print("[5/8] Assembling master palette...")
    print("       (colorkey-stable: index 0 := (0,0,0), index 255 := (255,0,255), forced)")
    palette = [None] * 256
    # Colorkey-stable enforcement (per format spec §3.4): these slots are FORCED
    # to the dedicated colorkey colors regardless of what octree picked. The
    # octree already skips pure-black and pure-magenta pixels (Step 2) so it
    # cannot produce a leaf at exactly (0,0,0) or (255,0,255), but we override
    # here too to make the contract explicit and survive any future refactor.
    palette[RESERVED_BLACK_IDX] = (0, 0, 0)
    palette[RESERVED_MAGENTA_IDX] = (0xFF, 0x00, 0xFF)

    octree_iter = iter(octree_colors)

    # Fill ramp slots 1..16 (with octree leaves backfilling unused ramp slots)
    for slot_offset in range(RAMP_COUNT):
        slot = RAMP_START + slot_offset
        if slot_offset < len(ramp_colors):
            palette[slot] = ramp_colors[slot_offset]
        else:
            try:
                palette[slot] = next(octree_iter)
            except StopIteration:
                palette[slot] = (0, 0, 0)

    # Fill octree slots 17..254
    for slot in range(OCTREE_START, OCTREE_END + 1):
        try:
            palette[slot] = next(octree_iter)
        except StopIteration:
            palette[slot] = (0, 0, 0)

    assert all(p is not None for p in palette), "palette has holes"
    print(f"  palette assembled, 256 entries")
    print()

    # === Step 6: write master.pal ===
    print(f"[6/8] Writing {out_pal}...")
    pal_bytes = bytearray()
    for r, g, b in palette:
        pal_bytes.append(r & 0xFF)
        pal_bytes.append(g & 0xFF)
        pal_bytes.append(b & 0xFF)
    assert len(pal_bytes) == 768, f"master.pal must be 768 bytes, got {len(pal_bytes)}"
    with open(out_pal, "wb") as f:
        f.write(pal_bytes)
    print(f"  wrote {len(pal_bytes)} bytes")
    print()

    # === Step 7: build per-asset remap LUTs and write master.map ===
    print(f"[7/8] Building per-asset remap LUTs and writing {out_palmap}...")
    palmap_entries = []  # (rel_path, src_palette_size, remap_bytes)

    # Cache: rel_path -> remap (also used by PSNR step below)
    remap_by_path = {}

    for im in indexed_images:
        src_pal = im["palette"]
        # src_palette_size is one of {2, 16, 256} per format spec §2.2.
        src_size = len(src_pal)
        if src_size <= 2:
            target_size = 2
        elif src_size <= 16:
            target_size = 16
        else:
            target_size = 256

        remap = bytearray(target_size)
        for i in range(min(target_size, len(src_pal))):
            r, g, b = src_pal[i]
            remap[i] = nearest_index(palette, r, g, b)
        # Padding entries that exceed src_pal length stay at 0 (=master black,
        # harmless because no source pixel can reference them — palette would
        # have been padded by parse_bmp).

        palmap_entries.append((im["rel_path"], target_size, bytes(remap)))
        remap_by_path[im["rel_path"]] = bytes(remap)

    for im in truecolor_images:
        # Truecolor: src_palette_size=0, no remap
        palmap_entries.append((im["rel_path"], 0, b""))

    pm_bytes = bytearray()
    pm_bytes += PMAP_MAGIC_BYTES
    pm_bytes += struct.pack("<I", PMAP_VERSION)
    pm_bytes += struct.pack("<I", len(palmap_entries))
    for path, src_size, remap in palmap_entries:
        path_bytes = path.encode("ascii")
        pm_bytes += struct.pack("<H", len(path_bytes))
        pm_bytes += path_bytes
        # uint16 (not uint8, since 256-entry palettes don't fit in a byte).
        # Per format spec §2.2.
        pm_bytes += struct.pack("<H", src_size)
        pm_bytes += remap

    with open(out_palmap, "wb") as f:
        f.write(pm_bytes)
    print(f"  wrote {len(pm_bytes)} bytes ({len(palmap_entries)} entries)")
    print()

    # === Step 7b: colorkey-stability verification ===
    # For every indexed asset whose source palette contains exactly (0,0,0),
    # the LUT entry must point to master index 0; same for (255,0,255) →
    # master 255. This is the contract the wave-16-2 INDEX8 colorkey skip
    # path relies on. Hard-fail if violated — that's a tool bug.
    print("[7b/8] Colorkey-stability verification...")
    assert palette[RESERVED_BLACK_IDX] == (0, 0, 0), \
        f"master[0] must be (0,0,0), got {palette[RESERVED_BLACK_IDX]}"
    assert palette[RESERVED_MAGENTA_IDX] == (0xFF, 0x00, 0xFF), \
        f"master[255] must be (255,0,255), got {palette[RESERVED_MAGENTA_IDX]}"
    n_black_assets = 0
    n_magenta_assets = 0
    n_violations = 0
    for im in indexed_images:
        rel = im["rel_path"]
        remap = remap_by_path[rel]
        for src_idx, (r, g, b) in enumerate(im["palette"]):
            if src_idx >= len(remap):
                break
            if r == 0 and g == 0 and b == 0:
                n_black_assets += 1
                if remap[src_idx] != RESERVED_BLACK_IDX:
                    print(f"  VIOLATION: {rel} src[{src_idx}]=(0,0,0) "
                          f"maps to master {remap[src_idx]} (expected 0)",
                          file=sys.stderr)
                    n_violations += 1
            elif r == 0xFF and g == 0 and b == 0xFF:
                n_magenta_assets += 1
                if remap[src_idx] != RESERVED_MAGENTA_IDX:
                    print(f"  VIOLATION: {rel} src[{src_idx}]=(255,0,255) "
                          f"maps to master {remap[src_idx]} (expected 255)",
                          file=sys.stderr)
                    n_violations += 1
    print(f"  source-(0,0,0)   → master 0:   {n_black_assets} occurrences across corpus")
    print(f"  source-(255,0,255) → master 255: {n_magenta_assets} occurrences across corpus")
    print(f"  violations: {n_violations}")
    if n_violations > 0:
        print("FATAL: colorkey-stability violated. Tool bug — investigate "
              "nearest_index() and reserved-slot assignment.", file=sys.stderr)
        sys.exit(2)
    print("  PASS — all source colorkey colors map to dedicated master slots.")
    print()

    # === Step 8: PSNR validation ===
    print("[8/8] PSNR validation pass...")
    print("-" * 70)
    psnr_results = []  # list of (psnr_db, rel_path, gate_db, passed_bool, kind_str)

    for im in indexed_images:
        remap = remap_by_path[im["rel_path"]]
        n_pixels = im["width"] * im["height"]
        if n_pixels == 0:
            continue
        mse_sum = 0
        for idx, (r, g, b) in enumerate(im["palette"]):
            cnt = im["palette_pixel_count"][idx]
            if cnt == 0 or idx >= len(remap):
                continue
            mr, mg, mb = palette[remap[idx]]
            dr = r - mr
            dg = g - mg
            db = b - mb
            mse_sum += cnt * (dr * dr + dg * dg + db * db)
        # Mean-squared error per pixel, averaged across RGB channels.
        mse = mse_sum / (n_pixels * 3)
        psnr = compute_psnr_from_mse(mse)
        is_face = im["rel_path"].startswith("Face")
        gate = PSNR_FLOOR_FACE if is_face else PSNR_FLOOR_GENERAL
        passed = psnr >= gate
        psnr_results.append((psnr, im["rel_path"], gate, passed, "indexed"))

    for im in truecolor_images:
        n_pixels = im["width"] * im["height"]
        if n_pixels == 0:
            continue
        # Per-pixel nearest-color distance (the slow load-time path
        # exercised at runtime).
        mse_sum = 0
        for r, g, b in im["pixels"]:
            idx = nearest_index(palette, r, g, b)
            mr, mg, mb = palette[idx]
            dr = r - mr
            dg = g - mg
            db = b - mb
            mse_sum += dr * dr + dg * dg + db * db
        mse = mse_sum / (n_pixels * 3)
        psnr = compute_psnr_from_mse(mse)
        # Truecolor backgrounds have no hard floor — they dither by design.
        psnr_results.append((psnr, im["rel_path"], 0.0, True, "truecolor"))

    psnr_results.sort(key=lambda r: r[0])

    fails = [r for r in psnr_results if not r[3]]
    valid = [r[0] for r in psnr_results if r[0] != float("inf")]
    mean_psnr = sum(valid) / len(valid) if valid else 0.0

    print()
    print(f"  WORST 15 (sorted ascending):")
    print(f"  {'kind':<10s} {'gate':>6s}  {'PSNR_dB':>9s}  {'status':<5s}  asset")
    for psnr, path, gate, passed, kind in psnr_results[:15]:
        status = "FAIL" if not passed else "ok"
        psnr_str = f"{psnr:9.2f}" if psnr != float("inf") else "      inf"
        gate_str = f"{gate:6.1f}" if gate > 0 else "  none"
        print(f"  {kind:<10s} {gate_str}  {psnr_str}  {status:<5s}  {path}")

    print()
    print(f"  total assets:    {len(psnr_results)}")
    print(f"  mean PSNR:       {mean_psnr:6.2f} dB")
    if psnr_results:
        worst_psnr, worst_path, *_ = psnr_results[0]
        worst_str = f"{worst_psnr:.2f}" if worst_psnr != float("inf") else "inf"
        print(f"  worst PSNR:      {worst_str} dB  ({worst_path})")
    n_pass_indexed = sum(1 for r in psnr_results if r[3] and r[4] == "indexed")
    n_total_indexed = sum(1 for r in psnr_results if r[4] == "indexed")
    print(f"  indexed pass:    {n_pass_indexed}/{n_total_indexed} "
          f"(general gate {PSNR_FLOOR_GENERAL} dB / face gate {PSNR_FLOOR_FACE} dB)")
    print(f"  truecolor:       {sum(1 for r in psnr_results if r[4] == 'truecolor')} "
          "(no hard PSNR floor — dithering is by design)")
    print(f"  failures:        {len(fails)}")
    print()

    if fails:
        print("FAILED ASSETS:")
        for psnr, path, gate, _, _ in fails:
            psnr_str = f"{psnr:.2f}" if psnr != float("inf") else "inf"
            print(f"  {path}: PSNR {psnr_str} dB < gate {gate} dB")
        if not args.no_fail_on_psnr:
            print()
            print("Exiting 1 due to PSNR-gate failures "
                  "(use --no-fail-on-psnr to override).")
            sys.exit(1)
        else:
            print()
            print("(--no-fail-on-psnr set; continuing despite failures.)")

    print()
    print("All gates passed. master.pal + master.map ready.")


if __name__ == "__main__":
    main()
