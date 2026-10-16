# Snowy (Pebble Time)

Port of PebbleOS to the Pebble Time (`snowy_dvt`, board `snowy@dvt`) and the
Pebble Time Steel (`snowy_s3`, codename "bobby", board `snowy@s3`). The stock
board file says all snowy boards are electrically identical apart from minor
quirks. The hardware is an
STM32F439 main MCU, a TI CC2564B Bluetooth controller, a MAX14690 PMIC, 16 MiB
of parallel NOR flash, and a 144×168 64-color display driven through an
iCE40LP FPGA. The port runs the `basalt` platform.

Status: runs on a retail Pebble Time. It boots through the stock bootloader,
connects to the Core app over Bluetooth LE (pairing, notifications with
pictures, calls, music with album art, app and watchface installs, firmware
updates from the app), with the CC2564 deep sleeping between transfers. The
Steel builds but has not run on hardware.

## Sources

- **Core's tree before removal.** Core supported snowy until `10649ea0f`
  ("boards: drop snowy boards", 2026-04-30). The parent commit has the board
  file, the `snowy_dvt` defconfig, and drivers for the display, flash, PMIC,
  accelerometer and compass. Its Bluetooth was the `stub` controller.
  Spalding (`f42b41294`) and the STM32 legacy drivers (`c6288702d`) were
  removed the same day.
- **google/pebble.** Has the stock bootloader source
  (`platform/snowy/boot`) and the `snowy_s3` differences.
- **pebble-dev/pebble-firmware.** A Rebble fork that runs NimBLE on the
  CC2564: `third_party/nimble/transport/chipset/cc2564.c`, the `cc2564x`
  NimBLE config, and `waftools/cc2564_service_pack_convert.py`.
  Mostly tested on `snowy_bb2`, a Pebble development board. Its CI notes that
  `snowy_dvt` is broken.

## Safety rules

As for silk, this port replaces **only the normal firmware**. The bootloader,
the recovery firmware (PRF) and the factory data are never written.

- Never build `--variant=prf` for snowy.
- Install only through the normal firmware update path (the Core app, or the
  stock firmware's or PRF's updater).
- **The stock bootloader's display bitstream lives in `MFG_INFO`**, at
  `0x0F0000` (64 KiB into the region). Core's old `fw/mfg/snowy/mfg_info.c`
  erased the whole 128 KiB sector on every setter and rewrote the bitstream,
  and `mfg_info_update_constant_data()` wrote it if missing. All of these
  must be no-ops here. A bad write would leave the bootloader unable to show
  its screens.

## External flash map (Spansion S29VS, 16 MiB, memory-mapped via FMC)

`fw/flash_region/flash_region_s29vs.h`. The first 128 KiB uses 32 KiB
"bottom boot" sectors; everything else uses 128 KiB sectors. The stock
bootloader and PRF fix where `SAFE_FIRMWARE`, `STOCK_SHARED_PRF_STORAGE` and
`MFG_INFO` are; `flash_guard` refuses writes to them, and the PRF range is
also locked in the chip (Sector Lock Range) at boot.

| Region | Range | OS access |
|---|---|---|
| `DEBUG_DB` (flash logs) | `0x000000–0x020000` | read/write |
| `SHARED_PRF_STORAGE` (the OS's own) | `0x020000–0x040000` | read/write |
| reserved | `0x040000–0x0C0000` | unused (512 KiB) |
| `STOCK_SHARED_PRF_STORAGE` | `0x0C0000–0x0E0000` | read only |
| `MFG_INFO` + boot FPGA bitstream | `0x0E0000–0x100000` | read only |
| `FIRMWARE_SLOT_1` (update scratch) | `0x100000–0x200000` | read/write |
| `SAFE_FIRMWARE` (PRF) | `0x200000–0x300000` | read only |
| `SYSTEM_RESOURCES_BANK_0` | `0x300000–0x380000` | read/write |
| `SYSTEM_RESOURCES_BANK_1` | `0x380000–0x400000` | read/write |
| `FILESYSTEM` | `0x400000–0x1000000` | read/write |

The chip has eight 2 MiB banks and can read one while it programs or erases
another; a read of a busy bank returns status, not data. Bank 0 holds the
flash logs, both shared PRF storages, `MFG_INFO` and the update scratch;
bank 1 holds the PRF and both resource banks; the filesystem takes the rest.

## Bootloader contract

Same contract as silk (`platform/snowy/boot/src/main.c`):

- The bootloader is 16 KiB, so the firmware starts at `0x08004000`.
- Three-strike fallback to PRF. `BOOT_BIT_FW_STABLE` and
  `BOOT_BIT_SOFTWARE_FAILURE_OCCURRED` work as on silk.
- **Forced PRF is Up + Back for 5 seconds** (silk is Up + Back + Select).
  `BOOT_BIT_FORCE_PRF` also forces it.
- The bootloader checks the staged image's CRC in `FIRMWARE_SLOT_1` before
  erasing anything, and only erases from `FIRMWARE_BASE`. Stock snowy
  (`CAPABILITY_HAS_DEFECTIVE_FW_CRC`) uses the legacy STM32 CRC there and in
  the PRF's description, so `FIRMWARE_LEGACY_CRC` makes the OS check the PRF
  and write staged updates with it. With CRC32 the PRF looked missing (the
  Core app then shows "Connected (Factory)") and the bootloader would have
  rejected updates sent through the app.
- Still to do: diff the snowy boot bits and backup registers against
  `fw/system/bootbits.h`, as was done for silk.

## Pin map (from `board_snowy.c` / `board_snowy.h`)

| Function | Pins / peripheral |
|---|---|
| Debug UART | USART3, TX PC10, RX PC11 (QEMU: USART2) |
| Buttons (active low) | Back PG4 (no pull), Up PG3, Select PG1, Down PG2 (pull-up) |
| Bluetooth UART | USART1, TX PA9, RX PA10, CTS PA11, RTS PA12, hardware flow control |
| Bluetooth control | nSHUTD PB12 (active low), host wake PA11 (EXTI11, shared with CTS) |
| BT slow clock | MCO1 on PA8 (32.768 kHz to the CC2564) |
| LSE | bypass mode (`RCC_LSE_Bypass`) |
| Display (iCE40LP) | SPI6, CLK PG13, MISO PG12, MOSI PG14, CS PG8; CRESET PG15, CDONE PG9 (EXTI9), BUSY PG10 (EXTI10); 16 MHz, 32 MHz fast; TX DMA2 stream 5 ch1 |
| Display rail | 6V6 enable PF3 (open drain); 4V5 enable PF2 |
| I2C1 (PMIC, compass) | SCL PB6, SDA PB9, 400 kHz; MAX14690 0x28 (0x50 8-bit), MAG3110 0x0E (0x1C 8-bit) |
| I2C2 (Apple MFi chip) | SCL PF1, SDA PF0, 0x20. Not needed for BLE only |
| PMIC interrupt | PG7 (EXTI7) |
| Accel (BMI160) | SPI1, SCK PA5, MISO PA6, MOSI PA7, CS PA4, 5 MHz; INT PG5, PG6 |
| Compass interrupt | PF14 (EXTI14) |
| Vibe | enable PF4; PWM TIM10 CH1 on PB8; H-bridge |
| Backlight | PWM TIM12 CH1 on PB14; 45% "on" level |
| ADC2 | ALS ch2 (PC2, enable PA3), battery ch1 (PA1, divided 3:1 by the PMIC) |
| Mic | I2S on SPI2, CK PB10, SD PB15; RX DMA1 stream 3 |
| Smartstrap | power PF13, interrupt PE0. Core removed accessory support |

Steel (`snowy_s3`) differences: ALS dark threshold 3220 (3130 otherwise),
`low_power_threshold` 2, a 204 h battery (144 h otherwise), its own battery
curve, and platform ID `PebbleBobbyDVT` (`PebbleSnowyDVT` for the Time). All
are in the `s3` revision (`CONFIG_BOARD_SNOWY_S3`). Stock booted the Steel with
the `snowy_dvt` bootloader, so the canary covers both.

## Port status

"Hardware" means seen working on a retail Pebble Time.

| Area | Status |
|---|---|
| Platform `basalt` (8-bit color 144x168, basalt + aplite apps) | hardware |
| SoC: STM32F439 IRQs, 192K SRAM + 64K CCM, 96 MHz (4 WS), STOP | hardware |
| GPIO (ports A-H), EXTI, RTC (LSE bypass), IWDG, debug UART (USART3), buttons | hardware |
| S29VS NOR (FMC), flash guard, PRF hardware lock | hardware |
| Memory-mapped system resources (`MMAP_RESOURCES`) | hardware |
| iCE40LP display (SPI6 TX DMA2 stream 5, BUSY on EXTI10, FPGA bitstream, MCO1 32 kHz) | hardware |
| MAX14690 PMIC: charger, LDO2/LDO3, battery and Vsys via MON | hardware (battery shown in 1% steps, `BATTERY_EXACT_PERCENT`) |
| Battery curves: stock snowy (Time) and bobby (Steel) | Time: hardware |
| BMI160 accel (SPI1) | hardware (left-handed rotation) |
| MAG3110 compass (I2C1, data ready PF14) | hardware (looks right; not calibrated against a reference) |
| Vibe (TIM10 H-bridge, PF4 enable), backlight (TIM12), ALS | not checked separately |
| I2S PDM mic (SPI2, DMA1 stream 3, OpenPDMFilter) | not checked on hardware |
| Factory data (stock OTP slots and MFG struct, read-only) | hardware |
| Bluetooth (CC2564B, NimBLE over H4, TI init script) | hardware |
| CC2564 deep sleep (eHCILL) with STOP | hardware (`BT_CC2564_SLEEP`, test bundles only so far) |
| Album art and notification pictures (`SERVICE_IMAGING_PICTURES`) | hardware |
| Firmware updates from the Core app | hardware |
| Resources: silk's plus color-only ones, 105 color emoji | done (513,755 of 524,288 bytes) |
| Pebble Time Steel (`snowy@s3`) | builds; QEMU same as the Time; not on hardware |
| Motion sensitivity setting (`ACCEL_SENSITIVITY`; default "Medium" keeps the stock threshold) | on, not checked on hardware |
| Left-handed wear (`ORIENTATION_MANAGER`: display, buttons, BMI160, MAG3110 rotate 180°) | hardware |

QEMU emulates neither the PMIC nor the BMI160 (both log that and carry on),
nor the IWDG, nor the CC2564 (Bluetooth stays off there).

### Building and QEMU

```shell
pbl configure -b build-snowy --board snowy@dvt   # snowy@s3 for the Steel
pbl build -b build-snowy
python3 tools/qemu_silk_canary.py --board snowy --build-dir build-snowy \
    --machine pebble-snowy-bb --qemu $PEBBLEOS_SDK_ROOT/qemu/bin/qemu-pebble
```

Deep sleep builds add `-DCONFIG_BT_CC2564_SLEEP=y` to `pbl configure`.

The canary boots through the **stock snowy bootloader** (from git history)
with PRF, stock shared PRF storage and MFG info filled with random data, and
fails if any of them change. With that MFG data the bootloader finds no boot
FPGA bitstream and falls back to the FPGA's NVCM, as expected.

### RAM

The F439 has only 192K of contiguous SRAM. Basalt apps take 96K and the
worker 12K, which leaves 84K for the kernel. Like stock, the kernel stacks
and heap live in the 64K CCM (`CONFIG_CCM_SIZE`), which DMA cannot reach.
NimBLE's pools (not its UART DMA buffer) and the task timer pool are also
placed there (`.ccm_bss`), and NimBLE's ISO pool is off (no LE Audio).

The kernel heap is about 33K (about 56K on silk), and kernel SRAM is 99%
used, so new static buffers there may need something moved to CCM, at the
heap's expense. Without memory-mapped resources the heap was too small: the
incoming call screen copies its 13.6K ringing animation into it, and with
about 17.6K free but no block over 11-13K the watch rebooted (OOM) on every
call.

### Memory-mapped resources

As the stock firmware did (`HAS_MAPPABLE_FLASH`), system bitmaps and vector
icons and animations are drawn straight from the NOR instead of being copied
into the heap (`MMAP_RESOURCES`). Core's version was written for the
SF32LB52, whose flash is always mapped; on snowy:

- pointers are `FLASH_MEMORY_MAPPABLE_ADDRESS` (0x60000000, the FMC window)
  plus the flash address;
- the S29VS is only readable while in use (idle, its FMC clock is off and its
  pins are analog), so `fw/resource/resource_mapped.c` counts mapped bytes
  per task, holds a flash use while any are mapped, and releases a task's on
  process cleanup;
- both resource banks share flash bank 1, so while put_bytes writes system
  resources (a firmware update), callers get copies instead of pointers.
  Other runtime writes (flash logs, settings, apps) are in other banks.

Only privileged callers (the kernel and system apps) get pointers; third
party apps still load copies into their own heap. Fonts are read in ranges,
not mapped.

### To research

- **`DYNAMIC_BACKLIGHT`** (backlight brightness follows ambient light: from
  30/20/10% in the dark, for the Bright/Standard/Dim modes, up to the user's
  maximum at 125/250/500 lux). It works in lux, and snowy's ALS is a raw
  12-bit ADC level with no calibration (`ambient_light_lux_den` is 0, so
  `ambient_light_level_to_lux()` returns the raw level). Readings in the
  thousands are past every ramp point, so as is it would always give full
  brightness.

  Stock's dark threshold (3130 of 4096, with a ±96 band between "very
  dark" and "very light") suggests the sensor packs most of its range near
  the top, maybe saturating indoors. Only measurements will tell whether a
  linear fit (`lux = (level - offset) * num / den`, as obelix and getafix
  use) works over 0-500 lux.

  Steps:
  1. Measure: next to a phone lux meter, at about 6 levels (dark room, dim
     lamp, room light, bright office, window, outdoor shade), read Settings >
     Information > Light (Select reads it with the backlight off). Time and
     Steel separately (their stock dark thresholds differ: 3130, 3220).
  2. Fit offset/num/den per revision, or leave the option off if the range
     is too compressed (or ramp in raw counts instead).
  3. With a calibration, thresholds become lux: convert the board dark
     threshold, `ambient_k_delta_threshold`, and the saved
     `lightAmbientThreshold` pref (the current builds store 3130 counts,
     which as lux would make nearly everything "dark"). Sleep tracking
     records the same lux value each minute (`activity_algorithm_kraepelin.c`),
     so check it still sees dark and light the same way.
  4. Check the backlight still turns on at the same darkness as stock.

## Bluetooth

The CC2564B is a standard HCI controller in ROM (Bluetooth 4.1). At boot
`drivers/bluetooth/cc2564.c`:

1. Blocks STOP mode for the whole start (it is released only if the chip
   fails). STOP may stop MCO1, the chip's 32 kHz slow clock: while STOP was
   allowed during the start, the chip came up ready (CTS) but never answered,
   seemingly at random.
2. Starts the slow clock (LSE on MCO1) and holds nSHUTDOWN (PB12) low, 3 s
   on the first attempt and 5 s on the two retries: after one of our resets
   the chip did not answer after 100 ms or 1 s off.
3. Waits for the chip to assert our CTS, then checks it answers Read Local
   Version (shown as "BT Chip").
4. Replays TI's `TIInit_6.7.16.bts`, embedded unmodified
   (`third_party/ti_bt_sp`), one HCI command at a time, waiting for each
   Command Complete. The script's baud rate change is sent with
   `BT_CC2564_HCI_BAUD` (921600) and both sides switch; its sleep mode
   command is sent with deep sleep off unless `BT_CC2564_SLEEP` is set.

Every wait is bounded; after three failed attempts the chip is left off and
Bluetooth reported unavailable, rather than crashing (a crash loop before
the firmware is marked stable ends in PRF). The host waits up to 30 s for the
chip. Every reset (`system_hard_reset()`) holds the chip in shutdown first,
as stock tore Bluetooth down. The NimBLE H4 transport then takes USART1 over,
with hardware RTS/CTS and interrupt-driven RX. Without deep sleep, STOP stays
blocked while the controller runs, since STOP would stop the UART while RTS
still lets the controller send.

Transport and host settings that hardware forced:

- **No host flow control** (the pebble-dev firmware had it). The CC2564 sent
  more ACL packets than the host had buffers for, so NimBLE reset itself
  (reason 12) the moment an app download started. UART flow control holds the
  chip off instead while the host is busy, as on silk.
- **16 event buffers** (4 before, as silk has 16). With 4 the HCI task could
  wait for a free one while the host waited on a write, and the watch froze
  until the watchdog reset it.
- **Bounded writes.** A write the chip has not taken (CTS) within 2 s ends
  Bluetooth until the next boot instead of hanging; BT Radio then shows
  `Stalled cts<level> rxp<rx paused> evw<waiting for an event buffer>`.

Bluetooth 4.1 has no resolving list or privacy modes. The transport answers
those commands itself and resolves a peer's private address against the
bonded IRKs on connection, reporting it as an enhanced LE Connection
Complete (the pebble-dev firmware patched NimBLE's host for the same). The
identity address is the CC2564's factory public address (TI prefix
`B0:B4:48`), as stock and the stock PRF use (`BT_ID_ADDR_PREFER_PUBLIC`); one
derived from the serial number, silk's scheme, is only the fallback for a
chip that reports none. Advertising from the serial-derived address instead
(a random static one) made the watch a different device to phones paired
with stock.

### Pairing

Snowy keeps the pebble-dev firmware's settings for this chip: legacy pairing
allowed, LE Secure Connections offered, no MITM required, IO capability
DisplayYesNo. Android offers LE Secure Connections only to devices reporting
Bluetooth 4.2 or later (it reads the version before pairing), so with the
CC2564 phones pair with legacy passkey entry: the watch shows a random
passkey and the phone asks for it to be typed in. Numeric comparison, as on
silk (4.2), is not possible over LE here; the stock PRF gets it because it
pairs over BR/EDR, which NimBLE does not do. Asking the phone to pair as it
connects (a Security Request) does not change this.

Settings > System > Information shows "BT Radio" (boot progress, e.g.
`Running (try 1) c00`, `No CTS (try 3)`, `No version t3 rx a,b,c cXY` with
BT Chip showing the first bytes, the line levels and the slow clock state,
or `Init <n> <opcode>:<status>`), "BT Chip", "BT HCI" (last reply
`<opcode>:<status>` and last failure), "BT Pair" (`r` requests, `k`
passkeys, `a` last passkey action: 3 display, 4 numeric comparison, then
pairing, encryption and disconnect statuses), "BT Reset" (state at the first
host reset), "BT SMP" (SMP opcodes in and out, with each side's IO capability
and AuthReq for Pairing Request and Response, e.g. `<1405 >2109`), "BT Rx"
(deep sleep) and "Last reboot".

### Core app

On Android the Core app hides aplite, basalt and chalk watches from its BLE
scan, expecting them to use Bluetooth Classic. To find snowy, turn on
Settings > Debug > "Show debug options", then Connectivity > "Show legacy
watches in BLE scan". Pairing from the app then works (with a typed
passkey).

### Stock pairing

Stock snowy kept its shared PRF storage in the older v2 layout (silk's is
v3): one struct at the start of `STOCK_SHARED_PRF_STORAGE` with the root
keys, the local name and one BLE pairing from Bluetopia's legacy pairing,
which kept only the DIV and EDIV it gave the phone. With
`SERVICE_SHARED_PRF_STORAGE_IMPORT_STOCK_V2`, boot imports them when our
identity root key differs from stock's, regenerating our LTK as
d1(ER, DIV, 0) with Rand 0. When the phone asks for that key, the transport
checks its EDIV against DIV ^ dm(DHK, Rand) and stands in Rand 0 for
NimBLE; if the check fails, the phone simply pairs again. The stock region
is only read (the canary's `--stock-sprf-v2` checks this with a synthetic
entry). Stock snowy pinned no address, so neither does this port.

### Deep sleep (eHCILL)

With `CONFIG_BT_CC2564_SLEEP=y` the controller deep sleeps using TI's eHCILL
(SWRA288), as the init script configures it (20 ms inactivity timeout,
100 ms WAKE_UP_IND retransmission, 150 us RTS pulse), and the host enters
STOP meanwhile. The transport:

- on GO_TO_SLEEP_IND (0x30): holds our RTS (PA12) high as a GPIO, arms a
  wake interrupt on CTS (PA11, EXTI11, both edges), sends GO_TO_SLEEP_ACK and
  releases its STOP block;
- on the CTS wake pulse: releases RTS, waits for WAKE_UP_IND and answers
  WAKE_UP_ACK; a CTS edge with no WAKE_UP_IND within 50 ms counts as a false
  wake and it goes back to sleep;
- to send while asleep: releases RTS, sends WAKE_UP_IND and waits up to
  100 ms (5 tries) for WAKE_UP_ACK, a crossing WAKE_UP_IND counting as one.
  The wake-up byte goes out even if the controller holds our CTS
  deasserted.

On hardware it is stable with STOP allowed while the chip sleeps (for
example "BT Rx" `S z249 c193 h55 r0 f0`: `<state A/S/W/C> z<sleeps>
c<controller wakes> h<host wakes> r<wake retries> f<false wakes>`), so STOP
does not take away the slow clock the sleeping chip runs on.
`BT_CC2564_SLEEP_NO_STOP` keeps STOP off while the chip sleeps, for telling
the two apart. The option is still off by default.

Open items:
- **Two bonds after re-pairing.** Pairing again here leaves the stock PRF
  on its old pairing (as on silk).
- Raise the baud rate towards the script's 3 Mbaud.
- Find out whether the 3 s nSHUTDOWN time is still needed now that STOP is
  blocked during the start.

### Debugging over the Core app

With the watch connected and the phone on adb, the Core app's developer
connection gives libpebble2 a WebSocket to the watch:

```shell
adb shell am broadcast -a coredevices.coreapp.DEV_CONNECTION \
    -n coredevices.coreapp/coredevices.coreapp.debug.DevConnectionReceiver
adb forward tcp:9123 tcp:9000
```

Then `libpebble2`'s `GetBytesService` reads the flash logs
(`get_flash_region(0x000000, 0x20000)`) and any coredump (`get_coredump()`).
Decode the logs with `tools/dehash_flash_logs.py --dehash
<build>/fw/loghash_dict.json --output logs.txt flash_logs.bin`, and a
coredump with `tools/readcore.py crash.core crash.elfcore` and the SDK's
`arm-none-eabi-gdb -batch -ex "thread apply all bt" pebbleos.elf
crash.elfcore`. The phone's own view comes from its Bluetooth HCI snoop log
(developer options), which an `adb bugreport` includes as
`FS/data/misc/bluetooth/logs/btsnoop_hci.log`.

### Licensing

`TIInit_6.7.16.bts` is from TI's `ti-bt/service-packs` repository (mirrored
as `coredevices/ti-bt-service-packs`) under the Texas Instruments Text File
License. It allows royalty-free binary redistribution, including commercial
use, only with TI devices, without modification and without reverse
engineering, and the license must be reproduced (it is in
`third_party/ti_bt_sp/LICENSE`, and the file is embedded byte for byte).
With that license included, snowy builds can be redistributed, unlike silk
builds, which contain Dialog SDK code.
