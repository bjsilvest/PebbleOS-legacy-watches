# Silk (Pebble 2)

Port of PebbleOS to the original Pebble 2 (SE / HR). The hardware is an
STM32F412 main MCU, a DA14681 Bluetooth controller, an AS3701B PMIC, and a
144×168 1-bit display, using the `diorite` SDK platform. Reference code comes
from the original google/pebble release (`platform/silk`,
`src/fw/flash_region/flash_region_mx25u.h`).

## Safety rules

This port replaces **only the normal firmware**. The bootloader, the recovery
firmware (PRF) and the factory data are never written.

- Never build `--variant=prf` for silk.
- Install only through the normal firmware update path (the phone app, or the
  watch while it is in PRF). Never do a mass erase, write a full flash image,
  or flash over SWD outside the normal firmware region.

## External flash map (MX25U64, 8 MiB)

Taken from the stock layout. It must not change.

| Region                    | Range                 | OS access     |
|---------------------------|-----------------------|---------------|
| `FIRMWARE_SCRATCH`        | `0x000000–0x100000`   | read/write    |
| `SYSTEM_RESOURCES_BANK_0` | `0x100000–0x180000`   | read/write    |
| `SYSTEM_RESOURCES_BANK_1` | `0x180000–0x200000`   | read/write    |
| `SAFE_FIRMWARE` (PRF)     | `0x200000–0x280000`   | **read-only** |
| `DEBUG_DB`                | `0x280000–0x2A0000`   | read/write    |
| `FILESYSTEM`              | `0x2A0000–0x7F0000`   | read/write    |
| `RSVD`                    | `0x7F0000–0x7FE000`   | unused        |
| `SHARED_PRF_STORAGE`      | `0x7FE000–0x7FF000`   | **read-only** |
| `MFG_INFO`                | `0x7FF000–0x800000`   | **read-only** |

The flash driver must reject any erase or write that overlaps a read-only
region. It asserts instead of issuing the command.

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

## Emulation

The PebbleOS SDK QEMU provides a `pebble-silk-bb` machine. Bring-up happens
there before any work on hardware.

## Bluetooth

NimBLE's H4 UART transport (`CONFIG_BT_HCI_UART`) is the planned host
interface to the DA14681, running as an HCI controller. Planned for after
bring-up without Bluetooth.
