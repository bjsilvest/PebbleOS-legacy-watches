# Tintin (original Pebble): aplite-lite

Port of PebbleOS to the original Pebble ("tintin", the `aplite` platform):
the Kickstarter watch (`tintin@ev2_4`), the retail watch (`tintin@v1_5`), the
Pebble Steel (`tintin@v2_0`) and the bigboard QEMU emulates (`tintin@bb2`).
Stock firmware stopped at 3.x for these watches; this branch, `tintin-lite-fork`,
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

Current sizes: firmware ev2_4 507,411 B, v1_5 507,571 B, v2_0 507,699 B and
bb2 507,763 B (141 B left on bb2, the largest); resources 258,525 B (3.6 KiB
left); static kernel RAM 66,400 B, which leaves about 19 KiB for the kernel
heap by the linker map. No out-of-memory or other crash on hardware with a
phone connected since -312 (notifications, watchface installs, reconnects).

## Features removed

| Feature | How | Reason |
|---|---|---|
| Health and activity tracking (steps, sleep, heart rate, workouts, KickStart, Health settings) | `SERVICE_ACTIVITY=n`; the health API is a stub returning no data (`health_service_stub.c`) | Never on stock aplite; about 49 KB. |
| Weather | `SERVICE_WEATHER=n`, weather images left out of resources | Never on stock aplite; about 43 KB |
| Sports app (PebbleKit Sports) | `APP_SPORTS=n`; the app is left out of the system app registry along with the workout screens only it used | About 5 KB. Phone apps that drive it, such as RunKeeper in 2013, are long gone; the Core app and the current store apps do not use it. |
| Smart Alarm | Built only with `SERVICE_ACTIVITY`: no Basic/Smart menu (a new alarm opens the time picker), no Convert to Smart Alarm action, no first use dialog | About 0.9 KB. It needs sleep tracking, so on tintin it only ever showed "requires Pebble Health". |
| Bluetooth bring-up rows under Settings, System, Information (BT Radio, HCI, Pair, Reset, SMP, Chip, App Fetch, Last reboot) and the traces behind them | `BT_CONTROLLER_DIAGNOSTICS=n` | About 1.9 KB. Diagnostics from bringing up the ports; upstream has none of them. Information now lists what upstream lists. |
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
| Right-to-left text (Hebrew, Arabic) | Back on (`TEXT_RTL`): the fonts have no such glyphs, but a language pack brings its own extended fonts. Tested on ev2_4 with the Arabic pack. Paid for by the Smart Alarm and diagnostics cuts, a smaller BiDi classifier and text layout path, shorter app developer log messages, and no per-app launcher color on black and white boards. | `452984284` |
| Built-in fallback font | Hex digits only, as stock firmware had it (it only draws error codes). Applies to all boards. | `204422d73` |
| Launcher | The 4.x glance launcher (`SERVICE_APP_GLANCES=y`), which stock aplite never had. The legacy list launcher stays in the tree for builds without glances. `app_glance_reload` allocates its glance only while it runs, so apps keep the full runtime RAM. | `LAUNCHER_LEGACY`, `app_state_set_glance` |
| Per-app runtime RAM | 6,816 bytes instead of 6,820, so the 32 KiB app region is aligned for the MPU. AppState leaves out touch and health state the board lacks, and holds the app glance as a pointer, allocated only while an app reloads it. | `APP_RAM_APLITE_RUNTIME_SIZE`, `e9f29d8ec` |
| Task timer pool | 96 slots instead of 192 (24 in use after boot). Running out is fatal; it has not happened on hardware with a phone connected. | `TASK_TIMER_POOL_SIZE=96` |
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

## Pairing after installing from recovery

The usual install goes through the stock recovery firmware: factory reset,
pair the phone with recovery, install. Recovery's pairing cannot be carried
over: its key is not derivable from what it stores in its shared storage
(the EDIV matches, the key does not), so the phone's first encryption fails.
Then:

1. Open Settings, Bluetooth on the watch. While it is open, the watch accepts
   a pairing request from a phone it already has a bond with, deletes the old
   keys and pairs again (`2e7b31616`). Outside it, such requests are still
   ignored, so another device cannot quietly replace the phone's bond.
2. Enter the code the watch shows on the phone (legacy passkey entry; the
   CC2564A has no LE Secure Connections).
3. The watch reconnects once by itself about a second later (`d0b3111cc`).

The reconnect is needed because Android keeps recovery's attribute cache even
after pairing again, and the Core app keeps the attributes it found when the
link came up. The GATT service is registered before GAP, as stock does, so
Service Changed sits at the stock handle (0x0003). When the phone enables it
after pairing, the watch indicates that every handle changed (`4629dd1b9`),
and the next link uses ours. Without this the app connects but subscribes to
stock handles: no battery level, and stalls after a reconnect.

Recovery advertises from a different (random) address than this firmware
(the chip's public address), so Android may still list "Pebble-LE ####", the
recovery's pairing, next to "Pebble ####". It can be forgotten. Snowy and
spalding carry the same three changes.

## Temporary diagnostics

- The Bluetooth bring-up rows are off on tintin (`BT_CONTROLLER_DIAGNOSTICS=n`);
  silk, snowy and spalding still have them until they are removed for good.
- The **Light** calibration row in Settings, Information is gone on tintin
  (`e3b664261`); silk, snowy and spalding still have theirs.

## Open items

- The retail watch (v1_5) and the Steel (v2_0) have not run on hardware. On
  the Steel, also check the charging LED: dim orange while charging, dim green
  when full (IS31FL3196, `af207288d`).
