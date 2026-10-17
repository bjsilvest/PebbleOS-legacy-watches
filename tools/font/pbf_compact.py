#!/usr/bin/env python
# SPDX-FileCopyrightText: 2026 Brian Silvestri
# SPDX-License-Identifier: Apache-2.0

"""Make a prebuilt v3 .pbf font smaller without changing what it draws.

- The hash table shrinks to the fewest buckets that keep every bucket at
  max_bucket entries or less (fonts are built with 255 buckets, about 1 KB).
- Glyphs are RLE4 encoded when that makes the font smaller and every glyph
  can be decoded in place in a max_glyph_size buffer, as the firmware does.
"""

import argparse
import collections
import itertools
import struct
import sys

try:
    from font.pbf_subset import Pbf
except ImportError:  # run as a script from tools/font
    from pbf_subset import Pbf

FEATURE_OFFSET_16 = 0x01
FEATURE_RLE4 = 0x02
GLYPH_HEADER = struct.Struct("<BBbbb")
RLE_MAX_RUN = 8
MAX_RLE_UNITS = 255  # the unit count is stored in the height byte


def table_size_for(codepoints, max_bucket):
    for size in range(1, 256):
        if (
            max(collections.Counter(cp % size for cp in codepoints).values())
            <= max_bucket
        ):
            return size
    return 255


def _bits(glyph):
    width, height = glyph[0], glyph[1]
    data = glyph[GLYPH_HEADER.size :]
    return [(data[i // 8] >> (i % 8)) & 1 for i in range(width * height)]


def _rle4_units(bits):
    units = []
    for bit, group in itertools.groupby(bits):
        length = len(list(group))
        while length:
            run = min(length, RLE_MAX_RUN)
            units.append((bit, run))
            length -= run
    return units


def _decodes_in_place(units, max_glyph_size):
    """Mirror of the firmware's in-place decode: output must never catch the input."""
    encoded = (len(units) + 1) // 2
    src = max_glyph_size - encoded
    dst = 0
    pending = 0
    for i, (_bit, run) in enumerate(units):
        if i % 2 == 0:
            src += 1  # the decoder reads a byte (two units) at a time
        pending += run
        while pending >= 8:
            if dst >= src:
                return False
            dst += 1
            pending -= 8
    return dst + (1 if pending else 0) <= max_glyph_size


def _rle4_glyph(glyph, max_glyph_size):
    """The glyph RLE4 encoded, or None when it cannot be."""
    if len(glyph) < GLYPH_HEADER.size:
        return None
    width, _height, left, top, advance = GLYPH_HEADER.unpack_from(glyph)
    units = _rle4_units(_bits(glyph))
    if len(units) > MAX_RLE_UNITS or not _decodes_in_place(units, max_glyph_size):
        return None
    padded = units + [(0, 1)] * (len(units) % 2)
    packed = bytes(
        (b1 << 3) | (r1 - 1) | (b2 << 7) | ((r2 - 1) << 4)
        for (b1, r1), (b2, r2) in zip(padded[::2], padded[1::2])
    )
    packed += bytes(-len(packed) % 4)
    return GLYPH_HEADER.pack(width, len(units), left, top, advance) + packed


def compact_pbf(data, max_glyph_size, max_bucket=16):
    font = Pbf(data)
    table_size = table_size_for(font.entries, max_bucket)
    best = font.rebuild(table_size=table_size)
    # Only plain 1-bit glyphs: not already RLE4, nor colour or other formats.
    if font.features & ~FEATURE_OFFSET_16:
        return best

    rle_glyphs = {}
    for offset, glyph in font.glyphs.items():
        encoded = _rle4_glyph(glyph, max_glyph_size)
        if encoded is None:
            return best
        rle_glyphs[offset] = encoded
    rle = font.rebuild(
        table_size=table_size, glyphs=rle_glyphs, features=font.features | FEATURE_RLE4
    )
    return rle if len(rle) < len(best) else best


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pbf")
    parser.add_argument("--output")
    parser.add_argument("--max-glyph-size", type=int, default=256)
    parser.add_argument("--max-bucket", type=int, default=16)
    args = parser.parse_args()

    with open(args.pbf, "rb") as f:
        data = f.read()
    out = compact_pbf(data, args.max_glyph_size, args.max_bucket)
    rle = bool(Pbf(out).features & FEATURE_RLE4)
    print(
        f"{args.pbf}: {len(data)} -> {len(out)} bytes{' (RLE4)' if rle else ''}",
        file=sys.stderr,
    )
    if args.output:
        with open(args.output, "wb") as f:
            f.write(out)


if __name__ == "__main__":
    main()
