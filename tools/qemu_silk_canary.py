#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Brian Silvestri
# SPDX-License-Identifier: Apache-2.0

"""Boot a silk build in QEMU and check the stock-owned flash regions survive.

Fills PRF, stock shared PRF storage and MFG info in a copy of the SPI flash
image with random bytes, boots the firmware for a while, then checks those
regions are byte-for-byte unchanged. Exits non-zero if any changed or the
firmware hit a flash guard assertion.

Two targets:
  qemu_silk (default): the flint emulator machine, firmware loaded as ELF.
  silk (--machine pebble-silk-bb): the emulated STM32F412, booted through
    the stock silk bootloader, which is taken from git history and only
    ever used here.
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

PROTECTED = (
    ("PRF", 0x200000, 0x280000),
    ("stock shared PRF storage", 0x7FE000, 0x7FF000),
    ("MFG info", 0x7FF000, 0x800000),
)
GUARD_MESSAGE = b"protected flash"

STOCK_BOOTLOADER = "85a10e0b5^:bin/boot/boot_silk@1478015115.hex"
SILK_FLASH_SIZE = 0x800000
SILK_RESOURCES_BEGIN = 0x100000


def build_silk_images(build_dir, tmp):
    """Stock bootloader + firmware, and an 8 MiB SPI flash with resources."""
    from intelhex import IntelHex

    boot_hex = os.path.join(tmp, "boot.hex")
    with open(boot_hex, "wb") as f:
        f.write(subprocess.check_output(["git", "show", STOCK_BOOTLOADER]))

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

    spi = bytearray(b"\xff" * SILK_FLASH_SIZE)
    res = Path(build_dir, "system_resources.pbpack").read_bytes()
    spi[SILK_RESOURCES_BEGIN : SILK_RESOURCES_BEGIN + len(res)] = res
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
    parser.add_argument("--build-dir", default="build-silk")
    parser.add_argument("--qemu", required=True, help="path to qemu-pebble")
    parser.add_argument("--machine", default="pebble-flint")
    parser.add_argument("--seconds", type=int, default=40)
    parser.add_argument("--screenshot", help="write a PNG of the screen here")
    parser.add_argument("--console", help="copy the console log here")
    args = parser.parse_args()

    silk_hw = args.machine == "pebble-silk-bb"

    with tempfile.TemporaryDirectory() as tmp:
        spi = os.path.join(tmp, "spi.bin")
        if silk_hw:
            micro_bin, image = build_silk_images(args.build_dir, tmp)
            boot_args = ["-cpu", "cortex-m4", "-kernel", micro_bin]
        else:
            shutil.copyfile(os.path.join(args.build_dir, "qemu_spi_flash.bin"), spi)
            image = bytearray(Path(spi).read_bytes())
            boot_args = ["-kernel", os.path.join(args.build_dir, "pebbleos.elf")]
        rng = random.Random(0x5111C)
        for _name, lo, hi in PROTECTED:
            image[lo:hi] = rng.randbytes(hi - lo)
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
                "-drive",
                f"if=mtd,format=raw,file={spi}",
                "-display",
                "none",
            ]
        )
        time.sleep(args.seconds)

        if args.screenshot:
            ppm = os.path.join(tmp, "screen.ppm")
            s = socket.socket(socket.AF_UNIX)
            s.connect(mon)
            s.sendall(f"screendump {ppm}\n".encode())
            time.sleep(1)
            s.close()
            if os.path.exists(ppm):
                ppm_to_png(ppm, args.screenshot)

        qemu.terminate()
        qemu.wait()

        after = Path(spi).read_bytes()
        ok = True
        for name, lo, hi in PROTECTED:
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
