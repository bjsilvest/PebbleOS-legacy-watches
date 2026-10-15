# Snowy (Pebble Time)

Port of PebbleOS to the Pebble Time (`snowy_dvt`, board `snowy`). The Pebble
Time Steel (`snowy_s3`, codename "bobby") is not targeted yet. The stock board file says all snowy
boards are electrically identical apart from minor quirks. The hardware is an
STM32F439 main MCU, a TI CC2564B Bluetooth controller, a MAX14690 PMIC, 16 MiB
of parallel NOR flash, and a 144×168 64-color display driven through an
iCE40LP FPGA. The port runs the `basalt` platform.

Status: boots in QEMU through the stock bootloader to the watchface. Not
yet run on hardware, and no Bluetooth.

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

| Area | Status |
|---|---|
| Platform `basalt` (8-bit color 144x168, basalt + aplite apps) | done |
| SoC: STM32F439 IRQs, 192K SRAM + 64K CCM, 96 MHz (4 WS), STOP | done; STOP untested |
| GPIO (ports A-H), EXTI, RTC (LSE bypass), IWDG, debug UART (USART3), buttons | done |
| S29VS NOR (FMC), flash guard, PRF hardware lock | done (polled) |
| iCE40LP display (SPI6, FPGA bitstream, MCO1 32 kHz) | done (polled; DMA todo) |
| MAX14690 PMIC: charger, LDO2/LDO3, battery and Vsys via MON | done, untested on hardware |
| BMI160 accel (SPI1) | done, untested on hardware |
| Vibe (TIM10 H-bridge, PF4 enable), backlight (TIM12), ALS | done, untested on hardware |
| I2S PDM mic (SPI2, DMA1 stream 3, OpenPDMFilter) | done, untested on hardware |
| Factory data (stock OTP slots and MFG struct, read-only) | done |
| MAG3110 compass | todo |
| Bluetooth (CC2564B, NimBLE over H4) | todo |
| Resources: silk's plus color-only ones, 105 color emoji | done (513,755 of 524,288 bytes) |

QEMU emulates neither the PMIC nor the BMI160 (both log that and carry on),
nor the IWDG.

### Building and QEMU

```shell
pbl configure -b build-snowy --board snowy@dvt
pbl build -b build-snowy
python3 tools/qemu_silk_canary.py --board snowy --build-dir build-snowy \
    --machine pebble-snowy-bb --qemu $PEBBLEOS_SDK_ROOT/qemu/bin/qemu-pebble
```

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

The kernel heap is about 37K (about 56K on silk). In QEMU it peaks at about
8K idling on the watchface. Kernel SRAM is 97% used, so new static buffers
there (e.g. the Bluetooth UART's) may need something moved to CCM, at the
heap's expense.

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
