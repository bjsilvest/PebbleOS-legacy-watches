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

Checked against the stock bootloader source: the boot bit values in
`fw/system/bootbits.h` are identical, and so is their register (`RTC_BKP_DR0`).
The bootloader only touches `DR0` (boot bits), `DR1` (stuck-button counter,
which it manages itself) and `DR2` (bootloader version). Core's STM32 register
map differs from stock only in OS-internal registers (`DR6`–`DR10`).

Before jumping to the firmware the bootloader calls `RCC_DeInit()`, so
`SystemInit()` (`soc/stm32/stm32f4/system_stm32f4xx.c`) sets up the clocks
itself. It runs the PLL from the HSI at 96 MHz, with an exact 48 MHz for the
RNG. Core's old version left the PLL at its reset values but reported 100 MHz,
and used 4 flash wait states, which is too few at silk's 1.8–2.0 V supply. It
now uses 6 wait states; tune this against RM0402.

## Factory data (read-only)

- `MFG_INFO` (external flash) uses the stock v3 struct (`fw/mfg/silk/mfg_info.c`)
  with the layout verified against stock. All setters are no-ops.
- Serial number, PCBA serial and hardware version come from the STM32 OTP
  area, in several slots per value; the most recent locked slot wins. Writing
  OTP is irreversible, so the OTP driver must not write on silk.

## Flashing

The silk board has **no `pbl flash`/`erase` runner**, on purpose. The OpenOCD
runner's `erase` erases internal flash sector 0, which on silk is the stock
bootloader. Install only through the stock update path.

## Hardware port status

Core's drivers have been redesigned since silk support was dropped, so the
STM32 drivers are rewritten against the current APIs, using the old drivers
(`git show 85a10e0b5^:src/fw/drivers/...`) for the hardware details.

| Area | Status |
|---|---|
| STM32 SPL (`third_party/hal_stm32`) | done |
| SoC: clocks, IRQs, memory, idle (`soc/stm32/stm32f4`) | done, with STOP mode (≤3.5 s, woken by RTC/EXTI) |
| GPIO, EXTI, delay, RNG, OTP (read-only), MCU ID | done |
| RTC (stock time format, timezone registers) | done |
| Watchdog (IWDG, started by the stock bootloader) | done |
| Debug UART (USART1) | done (RX DMA TODO) |
| Buttons (TIM4 debounce, reset combo, stuck-button counter) | done |
| QSPI + MX25U flash (hardware PRF/MFG block locks) | done (polled; DMA/mmap TODO) |
| Sharp display (SPI2) | done (polled; DMA TODO) |
| Board (`fw/board/boards/board_silk.*`) | done for the above |
| I2C3, AS3701B PMIC, battery ADC | done (stock charger settings) |
| Vibe (DRV2603 PWM), backlight PWM, BMA255 accel | done (stock values; QEMU does not emulate the BMA255) |
| Ambient light (ADC1 ch13, PC0 enable) | done |
| DFSDM mic (DMA2 stream 6) | done |
| Internal temperature sensor | done |
| Hardware platform ID (`PebbleSilk`), bundle `hwrev: silk` | done |
| DMA for display, QSPI, UART RX | todo (polled for now) |
| Bluetooth (DA14681, H4 over UART, NimBLE host) | done, untested on hardware; needs the Dialog SDK, see below |
| Heart rate (AS7000 on I2C3, Pebble 2 HR only) | done, untested on hardware: probed at boot (the SE runs the same image), runs the ams application already in the sensor flash and never enters its loader or reflashes it (the update image was not released); BPM only, no SpO2/HRV; QEMU does not emulate it |
| Motion sensitivity setting (`ACCEL_SENSITIVITY`; default "Medium" keeps the stock threshold) | on, untested on hardware |
| Left-handed wear (`ORIENTATION_MANAGER`: display, buttons, BMA255 rotate 180°) | on; display checked in QEMU |

The `silk` board boots in QEMU (`pebble-silk-bb`) through the **stock
bootloader** to the watchface, with PRF, stock shared PRF storage and MFG
info untouched:

```shell
pbl configure -b build-hw --board silk
pbl build -b build-hw
python3 tools/qemu_silk_canary.py --build-dir build-hw --machine pebble-silk-bb \
    --qemu $PEBBLEOS_SDK_ROOT/qemu/bin/qemu-pebble
```

Firmware updates are verified against the real stock bootloader binary in
QEMU: an image staged in `FIRMWARE_SLOT_1` with the 12-byte
`FirmwareDescription` and a zlib CRC-32 (what put_bytes writes) is
checksummed, installed at `0x08004000`, re-verified and booted. The
bootloader checks the staged image before erasing anything, and only erases
from `0x08004000`, so a bad image cannot touch the bootloader. If internal
flash holds no firmware at all, it boots PRF before looking for an update.

**Hardware test (2026-10-14):** `v4.41.0-68-g4ccca36cc`, sideloaded from the
Core app onto a real Pebble 2 through the stock update path, boots and runs
with no issues seen apart from the expected lack of Bluetooth.

QEMU does not emulate the AS3701B (the driver then reports a nominal battery
voltage) or the IWDG, so watchdog resets (and the bootloader's
three-strike fallback to PRF) are untested in emulation.

Pin map (from the stock board file):

| Function | Pins / peripheral |
|---|---|
| Debug UART | USART1, TX PA9, RX PB7 (QEMU: USART2) |
| Buttons (active high) | Back PC13 (no pull), Up PD2, Select PH0, Down PH1 (pull-down) |
| QSPI flash | CS PB6, CLK PB2, D0 PC9, D1 PC10, D2 PC8, D3 PA1, 50 MHz |
| Sharp display | SPI2, CLK PB10, MOSI PB15, CS PB9, ON PA0 |
| I2C3 (PMIC, HRM) | SCL PA8, SDA PB8, 400 kHz; AS3701B at 0x80, INT PC7 |
| HRM (AS7000) | I2C3 at 0x60 (8-bit), handshake PA15 (EXTI15, shared with JTDI), enable PC1 (active low), LEDs on the PB13 boost |
| Vibe (DRV2603) | PWM TIM14 CH1 on PA7, 22 kHz, scaled by Vsys (3300 mV) |
| Backlight | PWM TIM3 CH1 on PC6, 67% max duty; PB13 enables the 4.5 V LED boost shared with the HRM |
| Accel ints | PA6, PA3 |
| ADC1 | ALS ch13 (PC3, enable PC0), battery ch5 (PA5), temperature |
| Mic | DFSDM1 ch2, CK PC2, SD PB14 |

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

The DA14681 runs as an HCI controller and Core's NimBLE host talks H4 to it
over the bootrom UART pins: USART6 RX on PA12 (radio P1_3) and USART2 TX on
PA2 (radio P2_3), at 1 Mbaud (`BT_DA1468X_HCI_BAUD`, matching the controller image) with RTS/CTS flow control on the spare SPI lines. Stock silk used SPI
with a Pebble-specific protocol instead.

The radio has no flash of its own here; everything runs from its RAM and is
loaded at every boot (`drivers/bluetooth/da1468x.c`):

1. Reset the radio (PC5, active high). Its bootrom prints the chip ID and
   loads a 480-byte stage 2 at 57600 baud.
2. Stage 2 (`tools/silk_bt/stage2`) moves itself up in RAM, receives the
   ~60 KB controller image at 1 Mbaud, checks its CRC-32 and jumps to it.
3. The driver sends HCI Reset and waits for Command Complete before the host
   may use the controller.

This takes about 1.5 s and runs in the HCI thread, so boot is not delayed.
If any step fails three times, the radio stays in reset and Bluetooth is
reported unavailable; `pbl_bt_start()` returns false instead of letting the
host start time out. That matters because a crash loop before
`BOOT_BIT_FW_STABLE` (15 minutes of uptime) would end in the bootloader
starting PRF.

The controller image is the Dialog SDK's BLE stack in pass-through
(external host) mode, configured for silk (`tools/silk_bt/config`): the
DA14681 charger, USB and 1V8 rails stay off, the LP clock is the internal
RCX, and the radio never sleeps, because H4 without flow control cannot wake
it. The controller only has a placeholder public address, so the identity
address is a random static address derived from the serial number on every
boot, like the legacy firmware (`BT_ID_ADDR_SERIAL`). It needs no storage, so
it never changes, and it matches the address the legacy firmware used.

### Building

The Dialog DA1468x SDK 1.0.14.1081 (with hotfix 002) may not be
redistributed, and neither may images built from it, so neither is in this
tree, and firmware built with them is for personal use only. Without them
silk builds with the fake controller (no Bluetooth).

```shell
make -C tools/silk_bt SDK=/path/to/DA1468x_DA15xxx_SDK_1.0.14.1081 \
    CROSS=$PEBBLEOS_SDK_ROOT/arm-none-eabi/bin/arm-none-eabi-
pbl configure -b build-bt --board silk -DCONFIG_BT_HCI_UART=y \
    -DCONFIG_BT_CONTROLLER_DA1468X=y \
    -DCONFIG_BT_DA1468X_STAGE2_IMAGE=$PWD/tools/silk_bt/build/silk_bt_stage2.bin \
    -DCONFIG_BT_DA1468X_CONTROLLER_IMAGE=$PWD/tools/silk_bt/build/silk_bt.bin
pbl build -b build-bt
```

The SDK's BLE library targets DA14681-01 (AE) silicon. The driver logs the
chip ID the bootrom prints; pre-production AD silicon prints none.

A radio that fails to boot or that the host cannot sync with leaves
Bluetooth off instead of crashing: a crash loop before the firmware is
marked stable ends in the bootloader starting PRF. Settings > System >
Information shows how far it got without a serial console: "BT Radio" is
the boot step (for example `Bootrom: none (0 ch)` or `Running (try 1)`),
"BT HCI" is `<last answered command>:<status> !<last failed command>:<status>`
with HCI opcodes in hex, "BT Pair" is `r<pairing requests>k<passkey prompts>
p<pairing status> e<encryption status> d<disconnect reason>` (NimBLE codes in hex), "BT Reset" is the HCI state at the last host reset:
`<last command>><last answered command>/<reply length> a<ACL sent> n<ACL completed> c<clamps>`,
where `c` counts completed-packet reports the transport clamped because the
controller over-reports them, and `b` is the LE ACL data packet length the controller
reported (the transport caps it to 27 bytes, the LL PDU size, so the host
fragments large packets itself), "BT SMP" is the newest SMP opcodes on the link
(`<` received, `>` sent, the opcode's low hex digit, and for Pairing Failed
the reason's; a `+` after a sent PDU for each of the next few packets the
controller completed, `x` for a disconnection; over two rows), "BT Rx" is
`g<non-HCI DGTL packets skipped>/<last GTL message ID> d<bytes dropped> o<UART overruns>`
(the controller's DGTL also sends its own GTL messages, which the transport
skips because NimBLE's H4 parser asserts on them), "Last reboot" is the last reboot reason code and its first two data words
(the LR of an assert or fault), and "BT Chip" is the chip ID the bootrom printed.
If the controller does not answer HCI Reset, "BT Radio" reads
`HCI [<markers>] <bytes after markers>/<bytes after reset> f<framing errors>`,
and "BT Rx boot" and "BT Rx reset" show the first of those bytes in hex: the controller
image sends `m` (main), `c` (clocks), `p` (power manager) and `b` (BLE
manager) before its HCI transport starts, then the vendor event
`04 ff 01 'H'` through it (shown as `???H`).

### Known limitations

- **Shared address and bonding with stock and PRF.** Stock and PRF connect
  from a pinned resolvable private address (e.g. `7B:CA:D6:4E:13:9B`) kept in
  the stock shared PRF storage (0x7FE000), next to their root keys and
  bonding. When our identity root key differs from stock's, boot copies that
  entry into ours (`SERVICE_SHARED_PRF_STORAGE_IMPORT_STOCK`; the stock
  region is only read), and the watch always connects from the pinned
  address (`SERVICE_BLUETOOTH_ALWAYS_PIN_ADDRESS`). NimBLE only takes static
  random addresses as its own, so the DA1468x transport puts the pinned
  address in every LE Set Random Address and reports it as the local RPA of
  each connection; the serial-derived identity address
  (e.g. `FB:F2:D1:20:55:24`, which "Pebble 5524" comes from) stays the
  identity. Phones then see one watch in every firmware. Caveats: PRF only
  knows the stock bonding, so pairing again here leaves PRF on the old one;
  a factory reset here imports the stock bonding again.

- **Power:** the host enters STOP mode with the controller running: after
  20 ms without HCI traffic it deasserts CTS so the controller holds its
  data, and the controller image raises a wake line (radio P0_4 to PA10)
  while it has data for the host, which wakes it to reassert CTS. "BT Rx"
  counts these sleeps (`z`). The controller uses extended sleep between BLE
  events: the SDK's UART adapter sleeps only while the host holds CTS
  deasserted, deasserts RTS first, and wakes when the host asserts CTS.
- Radio P2_3 is wired to both PA2 (HCI TX) and PC4 (stock's interrupt
  input), so PC4 must stay an input.
- The controller keeps running while Bluetooth is off (airplane mode).
- H4 flow control runs over the spare stock SPI lines: the controller image
  routes its UART RTS to P0_0 (PB0) and CTS to P0_1 (PB1). The host gates
  each byte on RTS from the TX interrupt, deasserts CTS while its receive
  ring is full, and only drives CTS once the image runs, since the bootrom
  probes these pins for SPI. At 115200 baud the controller's DGTL deadlocked
  against its BLE manager under load (both of its queues full), so the link
  runs at 1 Mbaud. Without RTS from the
  image the transport falls back to 8-byte bursts with 1 ms pauses, which
  lose bytes when the controller is busy. "BT Rx" ends with `F` (flow
  control) or `p` (pacing) and the number of RTS waits.
- QEMU has no DA14681, so only the failure path is tested there.
