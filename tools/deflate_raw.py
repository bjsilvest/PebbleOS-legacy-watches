#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Brian Silvestri
# SPDX-License-Identifier: Apache-2.0

"""Compress a file to a raw DEFLATE stream with a fixed window.

The window is fixed, not searched, because the firmware inflates the stream
with a uzlib dictionary of that size.
"""

import argparse
import zlib
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input", type=Path)
    ap.add_argument("output", type=Path)
    ap.add_argument("--window-bits", type=int, default=9, choices=range(9, 16))
    args = ap.parse_args()

    co = zlib.compressobj(9, zlib.DEFLATED, -args.window_bits, 9)
    data = args.input.read_bytes()
    args.output.write_bytes(co.compress(data) + co.flush())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
