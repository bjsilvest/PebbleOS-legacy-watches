<p align="center">
  <img src="docs/_static/images/logo.svg" alt="PebbleOS" width="320">
</p>

# PebbleOS for the Pebble 2 (silk)

An unofficial port of [Core Devices' PebbleOS](https://github.com/coredevices/PebbleOS)
v4.43.0 to the **Pebble 2** (heart rate and SE). It uses Bluetooth Low
Energy (NimBLE) in place of the original Bluetooth stack.

> **Not official.** This project is not made, endorsed or supported by Core
> Devices, Google or Pebble Technology. "Pebble" is a trademark of its owner
> and is used here only to say which watch these builds run on.
>
> **Use at your own risk.** The software comes with no warranty (see
> Apache 2.0, sections 7–8). These builds leave the watch's bootloader and
> recovery firmware (PRF) alone, so recovery mode stays available, but
> flashing third-party firmware can still go wrong.

## Download

Latest build: v4.43.0-legacy.2, in the release [Legacy watches 4.43.0](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/tag/v4.43.0-legacy)
with the builds for the other watches.

| Watch       | Board  | Status       |
|-------------|--------|--------------|
| Pebble 2 HR | `silk` | Tested       |
| Pebble 2 SE | `silk` | **Untested** |

Download `normal_silk_v4.43.0-legacy.2.pbz` and sideload it from the Core
app. Coming from stock firmware, you also need to pair again over
Bluetooth LE: follow the [installing steps](https://github.com/bjsilvest/PebbleOS-legacy-watches#installing) on the front page. The release
notes list what was tested and what changed.

## Notes

- Tested on the Pebble 2 HR: pairing with Android and iPhone, battery
  life, the dynamic backlight, heart rate, language packs, firmware
  updates and factory reset.
- **Pebble 2 SE** (no heart rate sensor) has never run this firmware. The
  driver looks for the sensor at boot and should leave it out. Please
  report back if you try it.
- **After a factory reset,** pair the phone with the recovery firmware to
  install. This firmware takes over that pairing, so the phone reconnects
  without pairing again (tested with Android).
- **Heart rate** uses the application already in the sensor. This firmware
  never updates the sensor; see `docs/boards/silk` for what is known about
  sensor updates.
- **If you want to go back to the stock firmware,** do a factory reset
  first: pairings made with this firmware may not be removable otherwise.

## Building

Follow Core's [getting started guide](https://pebbleos.readthedocs.io/en/latest/getting_started/index.html)
to install the SDK.

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
emulator. `docs/boards/silk` has the hardware details.

## Other watches

This branch is one of several ports in this repository. The
[front page](https://github.com/bjsilvest/PebbleOS-legacy-watches) lists
them all, with their latest releases.

## Licenses

PebbleOS and the changes in this port are under the Apache License 2.0
(`LICENSE`). PebbleOS is by Core Devices and builds on the source released
by Google. Third-party components keep their own licenses, listed in
`third_party/sbom.yml`. The [Speex](https://github.com/xiph/speex) codec
used for voice replies is under a BSD 3-clause license, attached to each
release as `LICENSE-Speex.txt`. The Bluetooth controller image inside the
firmware is built from the Renesas (Dialog) DA1468x SDK and FreeRTOS and is
not under the Apache License; it is distributed with Renesas's permission,
and its notices (`NOTICE-silk-Bluetooth.txt`) and the FreeRTOS source are
attached to each release.
