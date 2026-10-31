<p align="center">
  <img src="docs/_static/images/logo.svg" alt="PebbleOS" width="320">
</p>

# PebbleOS for the Pebble Time, Time Round and Pebble 2

An unofficial port of [Core Devices' PebbleOS](https://github.com/coredevices/PebbleOS)
v4.44.1 to the **Pebble Time**, **Pebble Time Steel**, **Pebble Time
Round** and **Pebble 2** (HR and SE). It uses Bluetooth Low Energy (NimBLE)
in place of the original Bluetooth stack.

> **Not official.** This project is not made, endorsed or supported by Core
> Devices, Google or Pebble Technology. "Pebble" is a trademark of its owner
> and is used here only to say which watches these builds run on.
>
> **Use at your own risk.** The software comes with no warranty (see
> Apache 2.0, sections 7–8). These builds leave the watch's bootloader and
> recovery firmware (PRF) alone, so recovery mode stays available in the event of a crash or update failure.

## Download

Latest build: v4.44.1-legacy.1, in the release [Legacy watches 4.44.1](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/tag/v4.44.1-legacy)
with the builds for the original Pebble.

| Watch                | Board       | Status       |
|----------------------|-------------|--------------|
| Pebble Time          | `snowy@dvt` | Tested       |
| Pebble Time Steel    | `snowy@s3`  | **Untested** |
| Pebble Time Round    | `spalding`  | Tested       |
| Pebble 2 (HR and SE) | `silk`      | Tested on HR |

Download the `.pbz` for your watch and sideload it from the Core app.
Coming from stock firmware, you also need to pair again over Bluetooth
LE: follow the [installing steps](https://github.com/bjsilvest/PebbleOS-legacy-watches#installing) on the front page. The release
notes list what was tested and what changed.

## Notes

- Tested, pairing with Android and iPhone:
  - **Pebble Time:** notifications, voice replies, music control, app and
    watchface installs, firmware updates, battery life and the dynamic
    backlight. Language packs have not been tested on this watch yet.
  - **Pebble Time Round:** notifications, calls, voice replies, music
    control, app installs, firmware updates, language packs, battery life
    and the dynamic backlight.
  - **Pebble 2 HR:** voice replies and the microphone, heart rate,
    language packs, firmware updates, factory reset, battery life and the
    dynamic backlight.
- **Pebble Time Steel** has never run this firmware. It uses the Pebble
  Time's light-sensor calibration, so the backlight may come on at the
  wrong light levels. Please report back if you try it.
- **Pebble 2 SE** (no heart rate sensor) has never run this firmware. The
  driver looks for the sensor at boot and should leave it out. Please
  report back if you try it.
- **After a factory reset,** pair the phone with the recovery firmware to
  install (it shows no code). This firmware takes over that pairing, so
  the phone reconnects without pairing again.
- **Pairings from the stock 4.x firmware:** the firmware tries to take
  over a stock Bluetooth LE pairing the same way, but this has not been
  confirmed. Android phones paired with stock firmware over Bluetooth
  Classic, which this firmware does not use, so pair again as in the
  installing steps.
- **Pairing a phone:** the Pebble 2 pairs like Core's newer watches. The
  Pebble Time and Time Round show a code on the watch that you type on the
  phone: their Bluetooth chip (Bluetooth 4.1) is older than the pairing
  method that only asks you to confirm a code.
- **After a firmware update** the watch may drop the connection once,
  about 10 seconds after it reconnects, so the phone picks up the changed
  Bluetooth services. It only does this when they changed, and waits for
  an app install to finish first.
- **Heart rate** on the Pebble 2 uses the application already in the
  sensor. This firmware never updates the sensor; see `docs/boards/silk`
  for what is known about sensor updates.
- **Heart rate variability (HRV)** is on for the Pebble 2: while an app
  asks for HRV, the sensor runs its HRV mode, which stock firmware never
  used (the Pebble Time 2 works the same way). Pebble 2 apps cannot ask for
  it yet: in the latest app SDK (4.33.1, with pebble-tool 5.1.1) the HRV
  request is still a stub for the Pebble 2, so apps need an updated SDK.
  The sensor's HRV results are not decoded yet either.
- **If you want to go back to the stock firmware,** do a factory reset
  first: pairings made with this firmware may not be removable otherwise.

## Building

Follow Core's [getting started guide](https://developer.repebble.com/pebbleos/docs/getting_started/)
to install the SDK, then build a bundle for your watch:

```shell
pbl configure --board snowy@dvt   # snowy@s3 for the Time Steel, spalding for the Time Round
pbl bundle
```

The boards' pages in `docs/boards` have the hardware details.

### Pebble 2

The Pebble 2's Bluetooth chip (Dialog DA14681) needs a controller image
that is built from the Dialog DA1468x SDK 1.0.14.1081. Dialog's license
does not allow the SDK or the images built from it to be redistributed, so
they are not in this repository. Get the SDK yourself and build the images
with `tools/silk_bt`:

```shell
make -C tools/silk_bt SDK=/path/to/DA1468x_DA15xxx_SDK_1.0.14.1081
```

Then point the firmware build at them:

```shell
pbl configure --board silk \
  -DCONFIG_BT_HCI_UART=y -DCONFIG_BT_CONTROLLER_DA1468X=y \
  -DCONFIG_BT_DA1468X_STAGE2_IMAGE=$PWD/tools/silk_bt/build/silk_bt_stage2.bin \
  -DCONFIG_BT_DA1468X_CONTROLLER_IMAGE=$PWD/tools/silk_bt/build/silk_bt.bin
pbl bundle
```

A plain `pbl configure --board silk` builds without a Bluetooth radio, so
the phone will not find the watch. That build is only useful in the
emulator.

## Other watches

The original Pebble and Pebble Steel have their own cut-down build on the
[`legacy-lite`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/legacy-lite)
branch. The [front page](https://github.com/bjsilvest/PebbleOS-legacy-watches)
lists every watch with its latest release. Up to PebbleOS v4.43.0 each
watch had its own branch (`snowy-v4.43.0`, `spalding-v4.43.0`,
`silk-v4.43.0` and earlier).

## Support

This project is provided as-is, and may be updated over time. If you have
questions, please [open an issue](https://github.com/bjsilvest/PebbleOS-legacy-watches/issues).

## Licenses

PebbleOS and the changes in this port are under the Apache License 2.0
(`LICENSE`). PebbleOS is by Core Devices and builds on the source released
by Google; the Pebble Time's and Time Round's display FPGA bitstreams also
come from that source. Third-party components keep their own licenses,
listed in `third_party/sbom.yml`. The [Speex](https://github.com/xiph/speex)
codec used for voice replies is under a BSD 3-clause license, attached to
each release as `LICENSE-Speex.txt`. The TI Bluetooth init script used on
the Pebble Time and Time Round (`TIInit_6.7.16.bts`) is licensed by Texas
Instruments for use only with TI devices; its license is attached to each
release. The Pebble 2's Bluetooth controller image inside its firmware is
built from the Renesas (Dialog) DA1468x SDK and FreeRTOS and is not under
the Apache License; it is distributed with Renesas's permission, and its
notices (`NOTICE-silk-Bluetooth.txt`) and the FreeRTOS source are attached
to each release.
