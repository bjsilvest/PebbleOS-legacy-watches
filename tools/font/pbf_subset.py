#!/usr/bin/env python
# SPDX-FileCopyrightText: 2026 Brian Silvestri
# SPDX-License-Identifier: Apache-2.0

"""Subset a prebuilt v3 .pbf font to a set of codepoints.

Glyph bitmaps are copied byte for byte; only the hash, offset and glyph
tables are rebuilt (see fontgen.py for the format). The wildcard glyph is
always kept.
"""

import argparse
import itertools
import json
import struct
import sys

FONT_VERSION_3 = 3
FEATURE_OFFSET_16 = 0x01
OFFSET_TABLE_MAX_SIZE = 128


class Pbf:
    def __init__(self, data):
        (
            self.version,
            self.max_height,
            self.num_glyphs,
            self.wildcard,
            self.table_size,
            self.codepoint_bytes,
            self.info_size,
            self.features,
        ) = struct.unpack_from("<BBHHBBBB", data, 0)
        if self.version != FONT_VERSION_3:
            raise ValueError(f"unsupported pbf version {self.version}")

        off_bytes = 2 if self.features & FEATURE_OFFSET_16 else 4
        entry_fmt = "<" + ("L" if self.codepoint_bytes == 4 else "H")
        entry_fmt += "H" if off_bytes == 2 else "L"
        entry_size = struct.calcsize(entry_fmt)

        offsets_start = self.info_size + self.table_size * 4
        glyphs_start = offsets_start + self.num_glyphs * entry_size
        glyph_data = data[glyphs_start:]

        # codepoint -> offset into the glyph table
        self.entries = {}
        for i in range(self.num_glyphs):
            cp, off = struct.unpack_from(
                entry_fmt, data, offsets_start + i * entry_size
            )
            self.entries[cp] = off

        # Each glyph runs up to the next distinct offset (glyphs may be shared).
        bounds = sorted(set(self.entries.values()) | {len(glyph_data)})
        ends = dict(itertools.pairwise(bounds))
        self.glyphs = {
            off: glyph_data[off : ends[off]] for off in set(self.entries.values())
        }

    def glyph_size(self, codepoint):
        return len(self.glyphs[self.entries[codepoint]])

    def subset(self, keep):
        return self.rebuild(keep)

    def rebuild(self, keep=None, table_size=None, glyphs=None, features=None):
        """Serialize the font again.

        keep: codepoints to keep (all when None; the wildcard always stays).
        table_size: hash table size (the current one when None).
        glyphs: glyph data by old offset, replacing self.glyphs.
        features: feature flags apart from FEATURE_OFFSET_16.
        """
        if keep is None:
            keep = self.entries
        keep = set(keep) | {self.wildcard}
        keep &= set(self.entries)
        if table_size is None:
            table_size = self.table_size
        if glyphs is None:
            glyphs = self.glyphs
        if features is None:
            features = self.features

        # Keep glyph data in its original order so a full subset is a no-op.
        glyph_table = [struct.pack("<I", 0)]
        new_offsets = {}
        next_offset = 4
        for old in sorted({self.entries[cp] for cp in keep}):
            new_offsets[old] = next_offset
            glyph_table.append(glyphs[old])
            next_offset += len(glyphs[old])
        cp_entries = [(cp, new_offsets[self.entries[cp]]) for cp in sorted(keep)]

        features &= ~FEATURE_OFFSET_16
        off_bytes = 4
        if next_offset < 65536:
            features |= FEATURE_OFFSET_16
            off_bytes = 2
        cp_bytes = 4 if max(keep) > 0xFFFF else 2
        entry_fmt = (
            "<" + ("L" if cp_bytes == 4 else "H") + ("H" if off_bytes == 2 else "L")
        )
        entry_size = struct.calcsize(entry_fmt)

        buckets = [[] for _ in range(table_size)]
        for cp, off in cp_entries:
            buckets[cp % table_size].append(struct.pack(entry_fmt, cp, off))

        hash_table = []
        acc = 0
        for i, bucket in enumerate(buckets):
            if len(bucket) > OFFSET_TABLE_MAX_SIZE:
                raise ValueError(f"hash bucket {i} has {len(bucket)} entries")
            hash_table.append(struct.pack("<BBH", i, len(bucket), acc))
            acc += len(bucket) * entry_size

        info = struct.pack(
            "<BBHHBBBB",
            self.version,
            self.max_height,
            len(cp_entries),
            self.wildcard,
            table_size,
            cp_bytes,
            self.info_size,
            features,
        )
        return (
            info
            + b"".join(hash_table)
            + b"".join(b"".join(b) for b in buckets)
            + b"".join(glyph_table)
        )


def load_codepoints(path):
    with open(path) as f:
        return [int(cp) for cp in json.load(f)["codepoints"]]


def subset_pbf(data, codepoints):
    return Pbf(data).subset(codepoints)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pbf")
    parser.add_argument("--codepoints", help="JSON file with a 'codepoints' list")
    parser.add_argument("--output")
    parser.add_argument(
        "--list", action="store_true", help="print codepoint and glyph size"
    )
    args = parser.parse_args()

    with open(args.pbf, "rb") as f:
        font = Pbf(f.read())

    if args.list:
        for cp in sorted(font.entries):
            print(f"{cp:#x} {font.glyph_size(cp)}")
        return 0

    if not (args.codepoints and args.output):
        parser.error("--codepoints and --output are required unless --list")
    with open(args.output, "wb") as f:
        f.write(font.subset(load_codepoints(args.codepoints)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
