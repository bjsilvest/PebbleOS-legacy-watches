#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Brian Silvestri
# SPDX-License-Identifier: Apache-2.0

"""Boot a silk or snowy build in QEMU and check the stock-owned flash regions survive.

Fills PRF, stock shared PRF storage and MFG info in a copy of the external
flash image with random bytes, boots the firmware for a while, then checks
those regions are byte-for-byte unchanged. Exits non-zero if any changed or
the firmware hit a flash guard assertion.

Targets:
  qemu_silk (default): the flint emulator machine, firmware loaded as ELF.
  silk (--machine pebble-silk-bb): the emulated STM32F412, booted through
    the stock silk bootloader, which is taken from git history and only
    ever used here.
  snowy (--board snowy --machine pebble-snowy-bb): the emulated STM32F439
    with its parallel NOR, booted through the stock snowy bootloader.
"""

import argparse
import os
import random
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path

BOARDS = {
    "silk": {
        "protected": (
            ("PRF", 0x200000, 0x280000),
            ("stock shared PRF storage", 0x7FE000, 0x7FF000),
            ("MFG info", 0x7FF000, 0x800000),
        ),
        "bootloader": "85a10e0b5^:bin/boot/boot_silk@1478015115.hex",
        "flash_size": 0x800000,
        "resources_begin": 0x100000,
        "flash_args": lambda path: ["-drive", f"if=mtd,format=raw,file={path}"],
    },
    "snowy": {
        "protected": (
            ("PRF", 0x200000, 0x300000),
            ("stock shared PRF storage", 0x0C0000, 0x0E0000),
            ("MFG info", 0x0E0000, 0x100000),
        ),
        "bootloader": "c6288702d^:bin/boot/boot_snowy_dvt@1478015115.hex",
        "flash_size": 0x1000000,
        "resources_begin": 0x300000,
        "flash_args": lambda path: ["-drive", f"if=none,id=spi-flash,file={path},format=raw"],
    },
}
GUARD_MESSAGE = b"protected flash"


def stock_v2_sprf(rng):
    """A stock snowy (v2) shared PRF storage entry with a legacy BLE pairing.

    Root keys and pairing values are random. The fields are as the stock
    firmware laid them out; the firmware must import them without writing the
    stock region.
    """
    er = rng.randbytes(16)
    ir = rng.randbytes(16)
    name = b"canary".ljust(20, b"\0")
    ble = (
        b"phone".ljust(20, b"\0")
        + struct.pack("<HH", rng.getrandbits(16), rng.getrandbits(16))  # local EDIV, DIV
        + rng.randbytes(16)  # remote LTK
        + struct.pack("<QH", rng.getrandbits(64), rng.getrandbits(16))  # rand, EDIV
        + rng.randbytes(16)  # IRK
        + rng.randbytes(6)
        + b"\x00\x00"  # identity: public LE address
        + rng.randbytes(16)  # CSRK
        + bytes([0b0111])  # local enc, remote enc, remote identity valid
    )
    assert len(ble) == 91
    data = struct.pack("<I", 3) + name + er + ir + ble + bytes(43) + b"\x01"
    assert len(data) == 191
    return data


def build_stock_images(board, build_dir, tmp):
    """Stock bootloader + firmware, and an external flash with resources."""
    from intelhex import IntelHex

    boot_hex = os.path.join(tmp, "boot.hex")
    with open(boot_hex, "wb") as f:
        f.write(subprocess.check_output(["git", "show", board["bootloader"]]))

    boot = IntelHex(boot_hex)
    fw = IntelHex(os.path.join(build_dir, "pebbleos.hex"))
    fw.start_addr = None
    if not boot.maxaddr() < fw.minaddr():
        sys.exit("firmware overlaps the bootloader")

    micro = IntelHex()
    micro.merge(boot)
    micro.merge(fw, overlap="error")
    micro.padding = 0xFF
    end = ((micro.maxaddr() + 511) // 512) * 512
    micro_bin = os.path.join(tmp, "micro.bin")
    micro.tobinfile(micro_bin, start=0x08000000, end=end - 1)

    spi = bytearray(b"\xff" * board["flash_size"])
    res = Path(build_dir, "system_resources.pbpack").read_bytes()
    begin = board["resources_begin"]
    spi[begin : begin + len(res)] = res
    return micro_bin, spi


def ppm_to_png(ppm_path, png_path):
    data = Path(ppm_path).read_bytes()
    _header, width_height, _maxval, pixels = data.split(b"\n", 3)
    width, height = map(int, width_height.split())
    raw = b"".join(
        b"\x00" + pixels[y * width * 3 : (y + 1) * width * 3] for y in range(height)
    )

    def chunk(kind, body):
        return (
            struct.pack(">I", len(body))
            + kind
            + body
            + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
        )

    with open(png_path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw)))
        f.write(chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", choices=BOARDS, default="silk")
    parser.add_argument("--build-dir", default="build-silk")
    parser.add_argument("--qemu", required=True, help="path to qemu-pebble")
    parser.add_argument("--machine", default="pebble-flint")
    parser.add_argument("--seconds", type=int, default=40)
    parser.add_argument("--screenshot", help="write a PNG of the screen here")
    parser.add_argument("--console", help="copy the console log here")
    parser.add_argument(
        "--key",
        action="append",
        default=[],
        help="press a key before the screenshot (left=back, up, right=select, down); repeatable",
    )
    parser.add_argument(
        "--stock-sprf-v2",
        action="store_true",
        help="put a stock v2 shared PRF storage entry in place (snowy)",
    )
    args = parser.parse_args()

    board = BOARDS[args.board]
    stock_hw = args.machine in ("pebble-silk-bb", "pebble-snowy-bb")

    with tempfile.TemporaryDirectory() as tmp:
        spi = os.path.join(tmp, "spi.bin")
        if stock_hw:
            micro_bin, image = build_stock_images(board, args.build_dir, tmp)
            boot_args = ["-cpu", "cortex-m4", "-kernel", micro_bin]
        else:
            shutil.copyfile(os.path.join(args.build_dir, "qemu_spi_flash.bin"), spi)
            image = bytearray(Path(spi).read_bytes())
            boot_args = ["-kernel", os.path.join(args.build_dir, "pebbleos.elf")]
        rng = random.Random(0x5111C)
        for _name, lo, hi in board["protected"]:
            image[lo:hi] = rng.randbytes(hi - lo)
        if args.stock_sprf_v2:
            sprf = stock_v2_sprf(rng)
            lo = dict((name, lo) for name, lo, _hi in board["protected"])["stock shared PRF storage"]
            image[lo : lo + len(sprf)] = sprf
        Path(spi).write_bytes(image)

        console = os.path.join(tmp, "console.log")
        mon = os.path.join(tmp, "mon.sock")
        qemu = subprocess.Popen(
            [
                args.qemu,
                "-rtc",
                "base=localtime",
                "-monitor",
                f"unix:{mon},server=on,wait=off",
                "-serial",
                "null",
                "-serial",
                "null",
                "-serial",
                f"file:{console}",
                "-machine",
                args.machine,
                *boot_args,
                *board["flash_args"](spi),
                "-display",
                "none",
            ]
        )
        time.sleep(args.seconds)

        if args.key or args.screenshot:
            s = socket.socket(socket.AF_UNIX)
            s.connect(mon)
            for key in args.key:
                s.sendall(f"sendkey {key}\n".encode())
                time.sleep(2)
        if args.screenshot:
            ppm = os.path.join(tmp, "screen.ppm")
            s.sendall(f"screendump {ppm}\n".encode())
            time.sleep(1)
        if args.key or args.screenshot:
            s.close()
            if os.path.exists(ppm):
                ppm_to_png(ppm, args.screenshot)

        qemu.terminate()
        qemu.wait()

        after = Path(spi).read_bytes()
        ok = True
        for name, lo, hi in board["protected"]:
            same = after[lo:hi] == image[lo:hi]
            ok &= same
            print(f"{name:26s} {'unchanged' if same else 'MODIFIED'}")

        log = Path(console).read_bytes()
        if args.console:
            shutil.copyfile(console, args.console)
        guard_hits = log.count(GUARD_MESSAGE)
        print(f"flash guard assertions     {guard_hits}")
        ok &= guard_hits == 0

    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
