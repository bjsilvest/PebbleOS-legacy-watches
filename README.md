<p align="center">
  <img src="docs/_static/images/logo.svg" alt="PebbleOS" width="320">
</p>

# PebbleOS for the Pebble Time Round (spalding)

An unofficial port of [Core Devices' PebbleOS](https://github.com/coredevices/PebbleOS)
v4.42.0 to the **Pebble Time Round**. It uses Bluetooth Low Energy
(NimBLE) in place of the original Bluetooth stack.

> **Not official.** This project is not made, endorsed or supported by Core
> Devices, Google or Pebble Technology. "Pebble" is a trademark of its owner
> and is used here only to say which watches these builds run on.
>
> **Use at your own risk.** The software comes with no warranty (see
> Apache 2.0, sections 7–8). These builds leave the watch's bootloader and
> recovery firmware (PRF) alone, so recovery mode stays available, but
> flashing third-party firmware can still go wrong.

## Download

Latest build: v4.42.0-spalding.1, in the release [Legacy watches 4.42.0](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/tag/v4.42.0-legacy)
with the builds for the other watches.

| Watch             | Board      | Status |
|-------------------|------------|--------|
| Pebble Time Round | `spalding` | Tested |

Download the `.pbz` for your watch and sideload it from the Core app.
Coming from stock firmware, you also need to pair again over Bluetooth
LE: follow the [installing steps](https://github.com/bjsilvest/PebbleOS-legacy-watches#installing) on the front page. The release
notes list what was tested and what changed.

## Notes

- Tested on the Time Round: pairing with Android and iPhone,
  notifications, calls, voice replies, music control, app installs,
  firmware updates from the phone app, language packs, battery life and
  the dynamic backlight.
- **If you want to go back to the stock firmware,** do a factory reset
  first: pairings made with this firmware may not be removable otherwise.

## Building

Follow Core's [getting started guide](https://pebbleos.readthedocs.io/en/latest/getting_started/index.html)
to install the SDK, then build a bundle:

```shell
pbl configure --board spalding
pbl bundle
```

## Other watches

This branch is one of several ports in this repository. The
[front page](https://github.com/bjsilvest/PebbleOS-legacy-watches) lists
them all, with their latest releases. The earlier port, based on PebbleOS
v4.41.2, is on the `spalding-port` branch.

## Licenses

PebbleOS and the changes in this port are under the Apache License 2.0
(`LICENSE`). PebbleOS is by Core Devices and builds on the source released
by Google; the display FPGA bitstream also comes from that source.
Third-party components keep their own licenses, listed in
`third_party/sbom.yml`. The [Speex](https://github.com/xiph/speex) codec
used for voice replies is under a BSD 3-clause license, attached to each
release as `LICENSE-Speex.txt`. The TI Bluetooth init script
(`TIInit_6.7.16.bts`) is licensed by Texas Instruments for use only with
TI devices; its license is attached to each release.
