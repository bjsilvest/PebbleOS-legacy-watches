# Snowy (Pebble Time)

Planned port of PebbleOS to the Pebble Time (`snowy_dvt`) and Pebble Time
Steel (`snowy_s3`, codename "bobby"). The stock board file says all snowy
boards are electrically identical apart from minor quirks. The hardware is an
STM32F439 main MCU, a TI CC2564B Bluetooth controller, a MAX14690 PMIC, 16 MiB
of parallel NOR flash, and a 144×168 64-color display driven through an
iCE40LP FPGA. The port runs the `basalt` platform.

This page is an inventory for planning. Nothing here has been ported yet.

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
- Install only through the normal firmware update path.
- **The stock bootloader's display bitstream lives in `MFG_INFO`**, at
  `0x0F0000` (64 KiB into the region). Core's old `fw/mfg/snowy/mfg_info.c`
  erased the whole 128 KiB sector on every setter and rewrote the bitstream,
  and `mfg_info_update_constant_data()` wrote it if missing. All of these
  must be no-ops here. A bad write would leave the bootloader unable to show
  its screens.

## External flash map (Spansion S29VS, 16 MiB, memory-mapped via FMC)

From `flash_region_s29vs.h` (google/pebble). The first 128 KiB uses 32 KiB
"bottom boot" sectors; everything else uses 128 KiB sectors.

| Region | Range | OS access |
|---|---|---|
| `DEBUG_DB` (flash logs) | `0x000000–0x020000` | read/write |
| unused | `0x020000–0x0C0000` | free (640 KiB) |
| `SHARED_PRF_STORAGE` (stock) | `0x0C0000–0x0E0000` | **never** |
| `MFG_INFO` + boot FPGA bitstream | `0x0E0000–0x100000` | **never** |
| `FIRMWARE_SCRATCH` | `0x100000–0x200000` | read/write |
| `SAFE_FIRMWARE` (PRF) | `0x200000–0x300000` | **never** |
| `SYSTEM_RESOURCES_BANK_0` | `0x300000–0x380000` | read/write |
| `SYSTEM_RESOURCES_BANK_1` | `0x380000–0x400000` | read/write |
| `FILESYSTEM` | `0x400000–0x1000000` | read/write |

Proposal, following silk: keep an OS-owned copy of shared PRF storage in the
unused range, and import the stock entry from it at boot. Both stock regions
are then read-only. `CONFIG_FLASH_GUARD` needs the S29VS sector geometry.

## Bootloader contract

Same contract as silk (`platform/snowy/boot/src/main.c`):

- The bootloader is 16 KiB, so the firmware starts at `0x08004000`.
- Three-strike fallback to PRF. `BOOT_BIT_FW_STABLE` and
  `BOOT_BIT_SOFTWARE_FAILURE_OCCURRED` work as on silk.
- **Forced PRF is Up + Back for 5 seconds** (silk is Up + Back + Select).
  `BOOT_BIT_FORCE_PRF` also forces it.
- The bootloader checks the staged image's CRC in `FIRMWARE_SCRATCH` before
  erasing anything, and only erases from `FIRMWARE_BASE`.
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
| I2C1 (PMIC, compass) | SCL PB6, SDA PB9, 400 kHz; MAX14690 0x50, MAG3110 0x1C |
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
curve, and platform ID `PebbleBobbyDVT` (`PebbleSnowyDVT` for the Time).

## Port status

| Area | Status | Starting point |
|---|---|---|
| SoC (STM32F439: clocks, FMC, IRQs, idle/STOP) | todo | silk's `soc/stm32/stm32f4`; F439 clock tree and FMC are new |
| GPIO, EXTI, RTC, IWDG, UART, buttons, I2C, PWM, ADC, temperature | todo | silk drivers, re-pinned |
| S29VS NOR flash (FMC) + flash guard | todo | Core `drivers/flash/spansion_s29vs.c` |
| iCE40LP display + FPGA bitstream | todo | Core `drivers/display/ice40lp/`, `Snowy_LP1K_framebuffer.fpga` |
| MAX14690 PMIC, battery | todo | Core `drivers/pmic/max14690_pmic.c` |
| BMI160 accel, MAG3110 compass | todo | Core `drivers/imu/` |
| Vibe, backlight, ALS | todo | silk's PWM/ADC drivers |
| Mic (I2S, not DFSDM) | todo | Core `drivers/mic/stm32/pdm.c` |
| Factory data (read-only) | todo | Core `fw/mfg/snowy/mfg_info.c`, setters disabled |
| Bluetooth (CC2564B, NimBLE over H4) | todo | pebble-dev `cc2564.c` + silk's `hci_uart` transport |
| Resources (512 KiB banks) | todo | silk's emoji subset |

The SDK QEMU has a `pebble-snowy-bb` machine (the basalt emulator). It is
the planned target for the hardware port, like `pebble-silk-bb` was for silk.
How much of the flash and display it emulates is not checked yet.

## Bluetooth plan

The CC2564B is a standard HCI controller in ROM. The host loads TI's patch
at every power-up, then runs NimBLE over H4. The pebble-dev driver:

1. Resets the chip through nSHUTD.
2. Replays the HCI commands from `TIInit_6.7.16.bts` (the CC256xB patch,
   stored as the `BT_PATCH` resource). It replaces the baud-rate command
   with 921600 baud and switches the UART to match.
3. **Skips `HCI_VS_SLEEP_MODE_CONFIG` (0xFD0C)**, so the controller never
   enters deep sleep.

Open items:

- **Power.** Deep sleep (TI's eHCILL protocol on the UART lines) is needed
  for usable battery life. Host wake on PA11 is already wired.
- **Strict license wording.** TI's license allows unmodified binary
  redistribution. Embed the `.bts` file unchanged and parse it on the watch,
  instead of converting it at build time.
- **Two bonds.** The stock PRF uses Bluetopia and keeps its own bonding, so
  the silk work on shared address and bonding applies here too.
- **Fixed sleeps.** The driver waits a fixed 500 ms after the patch instead
  of checking each Command Complete.

### Licensing

`TIInit_6.7.16.bts` is from TI's `ti-bt/service-packs` repository (mirrored
as `coredevices/ti-bt-service-packs`) under the Texas Instruments Text File
License. It allows royalty-free binary redistribution, including commercial
use, only with TI devices, without modification and without reverse
engineering, and the license must be reproduced. With that license included,
snowy builds can be redistributed, unlike silk builds, which contain Dialog
SDK code.
