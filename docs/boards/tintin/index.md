# Tintin (original Pebble): aplite-lite

Port of PebbleOS to the original Pebble ("tintin", the `aplite` platform):
the Kickstarter watch (`tintin@ev2_4`), the retail watch (`tintin@v1_5`), the
Pebble Steel (`tintin@v2_0`) and the bigboard QEMU emulates (`tintin@bb2`).
Stock firmware stopped at 3.x for these watches; this branch, `aplite-lite`,
runs the current firmware on them with a cut-down feature set. The
Kickstarter watch runs it; the retail watch and the Steel build but have not
run on hardware yet.

This page lists what the port removes or changes compared with the bigger
watches, and why. Each item names the Kconfig option or commit, so it can be
revisited when room frees up.

## Limits

Everything below follows from three hard limits:

| Limit | Size | Why |
|---|---|---|
| Firmware (internal flash) | 496 KiB (507,904 B) | 512 KiB STM32F205, the first 16 KiB holds the stock bootloader |
| Kernel RAM | about 86 KiB | 128 KiB SRAM, less the aplite app region (32 KiB) and the worker region (12 KiB) |
| System resources | 256 KiB | The stock firmware and the stock recovery firmware install resource packs only into the stock 256 KiB banks |

Current sizes: ev2_4 firmware 506,112 B and bb2 506,240 B (about 1.7 KiB
left); resources 258,525 B (3.6 KiB left); kernel heap 13 KiB (about 11 KiB
free when idle, measured in QEMU with Bluetooth off).

## Features removed

| Feature | How | Reason |
|---|---|---|
| Health and activity tracking (steps, sleep, heart rate, workouts, KickStart, Health settings) | `SERVICE_ACTIVITY=n`; the health API is a stub returning no data (`health_service_stub.c`) | Never on stock aplite; about 49 KB. |
| Weather | `SERVICE_WEATHER=n`, weather images left out of resources | Never on stock aplite; about 43 KB |
| Sports app (PebbleKit Sports) | `APP_SPORTS=n`; the app is left out of the system app registry along with the workout screens only it used | About 5 KB. Phone apps that drive it, such as RunKeeper in 2013, are long gone; the Core app and the current store apps do not use it. |
| Right-to-left text (Hebrew, Arabic) | `TEXT_RTL=n`; `bidi.h` and `arabic_shaping.h` become left-to-right stand-ins | About 4.9 KB. The fonts have no Hebrew or Arabic glyphs and the board defines no extended fonts, so such text only ever drew as boxes. |
| Timeline Quick View (peek) and its settings page | `TIMELINE_PEEK=n`, `peek_stub.c` | About 4 KB. The timeline itself stays. |
| Voice: dictation, voice replies, the Reminders app | No microphone (`MIC=n`); the voice service is built only with one | The hardware has no microphone |
| Heart rate sensor | No `HRM` | The hardware has none |
| Gothic 36 (regular and bold) and LECO 60 fonts | `RESOURCES_LARGE_FONTS=n`; the tintin resource map aliases them to Gothic 28 and LECO 42 | 75 KB of resources. None is an SDK font. On 144x168 only the Extra Large text size used Gothic 36, so Extra Large now looks like Large. |
| Native analytics heartbeats | `SERVICE_ANALYTICS_NATIVE=n` | About 3 KB |
| Debug shell and the PULSE serial console | `SHELL=n`, `PULSE_EVERYWHERE=n` | About 13 KB (shell) and 9.5 KB (PULSE) of flash, and 9 KB of RAM. QEMU app installs through pypkjs need PULSE. |
| INFO- and WARNING-level logging | `DEFAULT_LOG_LEVEL_ERROR=y` | About 8 KB; only errors are logged. Core dumps are kept (2.8 KB) and make up for the lost warnings. |
| LE Secure Connections | Legacy pairing only: `NIMBLE_BLE_SM_SC=n`, `SM_LEGACY=y`, `SM_MITM=n` | The CC2564A is a Bluetooth 4.0 controller |
| Bluetooth central and observer roles; signed and reliable writes, read multiple (client and server), find included services, the whitelist | `NIMBLE_BLE_ROLE_CENTRAL=n` and the matching `NIMBLE_BLE_GATT_*=n` | A watch is a peripheral; unused |
| Bluetooth Classic (iOS over MFi, the stock Pebble app) | NimBLE is LE only | Phones connect over LE: Core app, and ANCS/AMS for iPhone notifications and music |
| LE Audio buffers | `NIMBLE_BLE_TRANSPORT_ISO_COUNT=0` | Unused; saves kernel RAM |
| Accelerometer double tap | The LIS3DH driver does not implement it (nor does the BMA255 one) | Not used by the system |

## Features changed

| Area | Change | Option or commit |
|---|---|---|
| System fonts | Smaller glyph hash tables, and RLE4-encoded glyphs where that is smaller. Same glyphs. | `RESOURCES_COMPACT_FONTS=y` (`tools/font/pbf_compact.py`) |
| Resources kept in the filesystem | The timezone database, the emoji fonts, every font except the Gothic text fonts, Gothic 9 and the golf app are stored deflated in the resource pack. The first boot after an install inflates them into the filesystem file `sysres` (about 0.3 s); later boots check and skip it. If the file were lost, those resources would be missing until the next boot rebuilds it; Gothic, images and the Bluetooth init script never depend on it. | `"files"` entry with `"unpack": true` in `resources/normal/tintin/resource_map.json`, `fw/resource/resource_unpack.c` |
| Built-in fallback font | Hex digits only, as stock firmware had it (it only draws error codes). Applies to all boards. | `204422d73` |
| Launcher | The 4.x glance launcher (`SERVICE_APP_GLANCES=y`), which stock aplite never had. The legacy list launcher stays in the tree for builds without glances. `app_glance_reload` allocates its glance only while it runs, so apps keep the full runtime RAM. | `LAUNCHER_LEGACY`, `app_state_set_glance` |
| Per-app runtime RAM | 6,816 bytes instead of 6,820, so the 32 KiB app region is aligned for the MPU. AppState leaves out glance, touch and health state the board lacks. | `APP_RAM_APLITE_RUNTIME_SIZE`, `e9f29d8ec` |
| Task timer pool | 96 slots instead of 192 (24 in use after boot). Running out is fatal, so the peak with a phone connected still needs checking on hardware. | `TASK_TIMER_POOL_SIZE=96` |
| App cache | Keeps 300 KiB free instead of 4 MiB (the filesystem is 2.1 MiB; the 4 MiB buffer evicted every downloaded app). | `7bd5fb94d` |
| Bluetooth controller | TI CC2564A. Its init script stays unmodified in the system resources instead of the firmware. Uses the chip's factory address, as stock did, and deep sleep (eHCILL). | `BT_CC2564_BTS_RESOURCE`, `fb10c17cd` |
| Bluetooth GATT client | Only the seven procedures the firmware uses: discover services, characteristics and descriptors; read, read by UUID; write, write without response. | `aef42ad11` |
| Watch color | Read from the stock factory registry (`mfg_color`), which the Core app uses to name the model. The model string was never programmed on these watches. | `29857f674` |
| Link-time optimization | One partition with points-to analysis (about 0.6 KiB smaller, slower link). | `LTO_WHOLE_PROGRAM=y` |
| NimBLE asserts | No file, function or expression strings; the caller's address is kept. Applies to all boards. | `d227d8468` |

## Stock compatibility

- **Bootloader and recovery firmware** stay stock and are never written. The
  vector table carries the "NeWo" marker the tintin bootloader needs
  (`FIRMWARE_NEW_WORLD_MARKER`), and firmware descriptions use the legacy
  STM32 CRC (`FIRMWARE_LEGACY_CRC`). Holding Up and Back during boot starts
  the recovery firmware.
- **Filesystem**: the firmware mounts the stock firmware's filesystem. Stock
  pairings, stored in an older 109-byte layout, show in Settings, Bluetooth
  and can be forgotten (`abaa19341`); they are not imported, so a phone has
  to pair again.
- **Resources** stay within the stock 256 KiB banks, so the stock recovery
  firmware can still install them.

## Temporary diagnostics

- **BT Name** row in Settings, Information (ev2_4 only, `843bde541`): traces
  reading and storing the phone's name. Remove once names show correctly.
- **BT Radio, HCI, Pair, Reset, SMP, Rx, Chip** rows: Bluetooth bring-up
  diagnostics shared with silk and snowy.

## Open items

- Confirm on hardware: phone names, watchfaces starting, the model shown in
  the Core app, the task timer peak, and kernel heap use with a phone
  connected.
- No stock pairing import (snowy imports its stock pairing).
- About 1.7 KiB of flash left; the next feature needs another trade.
