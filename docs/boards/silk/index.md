# Silk (Pebble 2)

Port of PebbleOS to the original Pebble 2 (SE / HR). The hardware is an
STM32F412 main MCU, a DA14681 Bluetooth controller, an AS3701B PMIC, and a
144×168 1-bit display. The port runs the `flint` platform, which is
diorite-compatible.

Core supported silk until commit `85a10e0b5` ("boards: drop silk boards",
2026-04-30). Its STM32F4 drivers, MX25U/QSPI flash driver, Sharp display
driver and `silk_flint` board config are the starting point for the hardware
port. They need moving to the current CMake/Kconfig layout. The original
google/pebble release has the stock bootloader source (`platform/silk/boot`).

## Safety rules

This port replaces **only the normal firmware**. The bootloader, the recovery
firmware (PRF) and the factory data are never written.

- Never build `--variant=prf` for silk.
- Install only through the normal firmware update path (the phone app, or the
  watch while it is in PRF). Never do a mass erase, write a full flash image,
  or flash over SWD outside the normal firmware region.

## External flash map (MX25U64, 8 MiB)

See `fw/flash_region/flash_region_mx25u.h`. Stock regions keep their stock
addresses (enforced by static asserts). The regions Core added since the
stock layout live in the last 64K sector of the stock filesystem range.

| Region                     | Range                 | OS access     |
|----------------------------|-----------------------|---------------|
| `FIRMWARE_SLOT_1` (scratch)| `0x000000–0x100000`   | read/write    |
| `SYSTEM_RESOURCES_BANK_0`  | `0x100000–0x180000`   | read/write    |
| `SYSTEM_RESOURCES_BANK_1`  | `0x180000–0x200000`   | read/write    |
| `SAFE_FIRMWARE` (PRF)      | `0x200000–0x280000`   | **never**     |
| `DEBUG_DB`                 | `0x280000–0x2A0000`   | read/write    |
| `FILESYSTEM`               | `0x2A0000–0x7E0000`   | read/write    |
| `TZINFO`                   | `0x7E0000–0x7E1000`   | read/write    |
| `MFG_RESULTS`              | `0x7E1000–0x7E2000`   | read/write    |
| `MFG_BATTERY_STATE`        | `0x7E2000–0x7E3000`   | read/write    |
| `SHARED_PRF_STORAGE` (OS)  | `0x7E3000–0x7E4000`   | read/write    |
| `RSVD_OS`                  | `0x7E4000–0x7F0000`   | unused        |
| `RSVD` (stock)             | `0x7F0000–0x7FE000`   | unused        |
| `STOCK_SHARED_PRF_STORAGE` | `0x7FE000–0x7FF000`   | **never**     |
| `MFG_INFO`                 | `0x7FF000–0x800000`   | **never**     |

`CONFIG_FLASH_GUARD` (`drivers/flash/flash_guard.c`) asserts instead of
issuing any erase or write that overlaps a "never" region. This applies to
the normal flash API and the core dump driver. It also refuses security
register writes. On top of that, the features that would otherwise write
these regions are disabled or redirected:

- **Shared PRF storage:** Bluetooth pairing keys and the BT identity live in
  an OS-owned copy. The stock PRF keeps its own pairing, so PRF may need
  pairing separately.
- **PRF updates:** put_bytes refuses recovery objects, and a pending
  `BOOT_BIT_NEW_PRF_AVAILABLE` is cleared without updating.

The stock PRF's factory reset erases the stock filesystem range
(`0x2A0000–0x7F0000`), including the OS-owned regions above. All of them are
regenerated or meant to be cleared on a factory reset.

## Bootloader contract

The stock bootloader (`platform/silk/boot/src/main.c`, `fw_copy.c`) stays in
place. The OS must cooperate with it through the boot bits kept in the RTC
backup registers:

- **Three-strike fallback:** a watchdog reset, or a reset with
  `BOOT_BIT_SOFTWARE_FAILURE_OCCURRED` set, counts as a strike. After three
  strikes the bootloader starts PRF. So the OS must keep the IWDG watchdog
  running, set `BOOT_BIT_SOFTWARE_FAILURE_OCCURRED` on a fatal error, and set
  `BOOT_BIT_FW_STABLE` once it has booted successfully.
- **Forced PRF:** holding Up + Back + Select for 5 seconds at boot starts PRF.
  The bootloader handles this on its own, so the OS cannot break it.
- **Updates:** the new image is staged in `FIRMWARE_SCRATCH` and
  `BOOT_BIT_NEW_FW_AVAILABLE` is set. The bootloader then copies the image to
  internal flash. If the copy damages internal flash, the bootloader falls
  back to PRF immediately. The image header format must match what the stock
  bootloader expects (`firmware.h`).

## Resources

Each stock resource bank is 512K. The full Core resource pack (about 705K)
does not fit, mostly because of the emoji fonts (about 242K). The build
fails if the pack exceeds 512K, because overflowing bank 1 would run into
PRF.

Silk keeps Core's emoji artwork but only 258 emoji
(`resources/normal/silk/emoji_subset.json`): the legacy Pebble emoji set,
then the most used emoji from the Unicode frequency ranking. The
`glyphSubset` font option (`tools/font/pbf_subset.py`) trims the base
`EMOJI_*.pbf` fonts at build time, so upstream redraws still apply. Others
show the missing-glyph box. Current pack: 509,550 of 524,288 bytes.

**TODO — revisit resource bank placement.** To ship every emoji, the A/B
resource banks could move into larger banks carved from `FILESYSTEM`. The
problem is that the stock PRF's factory reset erases the whole stock
filesystem range (`filesystem_regions_erase_all()`) and would wipe resources
stored there. The watch would then need an OS reinstall after every factory
reset. It is recoverable but poor UX. Options to evaluate: keep the subset,
move the banks anyway, or regenerate missing resources on boot.

## Emulation

`qemu_silk` runs the silk flash layout, flash guard and resources on the
flint emulator hardware (`pebble-flint` machine):

```shell
pbl configure -b build-silk --board qemu_silk
pbl build -b build-silk
cmake --build build-silk --target qemu_image_micro qemu_image_spi
python3 tools/qemu_silk_canary.py --qemu $PEBBLEOS_SDK_ROOT/qemu/bin/qemu-pebble
```

`tools/qemu_silk_canary.py` fills the "never" regions with random data,
boots the firmware, and fails if any of them changed or the guard fired.

The SDK QEMU also provides a `pebble-silk-bb` machine, which emulates the
STM32F412 itself. It is the target for the hardware port before anything
runs on a real watch.

## Bluetooth

NimBLE's H4 UART transport (`CONFIG_BT_HCI_UART`) is the planned host
interface to the DA14681, running as an HCI controller. Planned for after
bring-up without Bluetooth.
