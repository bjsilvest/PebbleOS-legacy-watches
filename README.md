<p align="center">
  <img src="docs/_static/images/logo.svg" alt="PebbleOS" width="320">
</p>

# PebbleOS for the Pebble and Pebble Steel (tintin)

An unofficial port of [Core Devices' PebbleOS](https://github.com/coredevices/PebbleOS)
v4.42.0 to the original **Pebble** and the **Pebble Steel**. It uses
Bluetooth Low Energy (NimBLE) in place of the original Bluetooth stack.

> **Not official.** This project is not made, endorsed or supported by Core
> Devices, Google or Pebble Technology. "Pebble" is a trademark of its owner
> and is used here only to say which watches these builds run on.
>
> **Use at your own risk.** The software comes with no warranty (see
> Apache 2.0, sections 7–8). These builds leave the watch's bootloader and
> recovery firmware (PRF) alone, so recovery mode stays available in the event of a crash or update failure.

## Download

Latest build: v4.42.0-tintin.2, in the release [Legacy watches 4.42.0](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/tag/v4.42.0-legacy)
with the builds for the other watches.

| Watch                               | Board          | Status       |
|-------------------------------------|----------------|--------------|
| Pebble, Kickstarter edition (ev2_4) | `tintin@ev2_4` | Tested       |
| Pebble, later edition (v1_5)        | `tintin@v1_5`  | **Untested** |
| Pebble Steel (v2_0)                 | `tintin@v2_0`  | **Untested** |

Download the `.pbz` for your hardware revision and sideload it from the
Core app.
Coming from stock firmware, you also need to pair again over Bluetooth
LE: follow the [installing steps](https://github.com/bjsilvest/PebbleOS-legacy-watches#installing) on the front page. The release
notes list what was tested and what changed.

## What is different on this watch

The original Pebble has very little flash and RAM, so this is a cut-down
build and its version ends in **-lite**.

- The timeline is a plain list.
- Not included: Health, Weather, Timeline Quick View, voice replies,
  Reminders, the Sports app and the large Gothic 36 / LECO 60 fonts.
- Bluetooth LE only: the original Pebble phone app (Bluetooth Classic)
  cannot connect. iPhones work over LE.
- No serial debug console, and only error logs are kept.
- **Pairings from stock firmware are not carried over.** Pair your phone
  again after installing.
- **If you want to go back to the stock firmware,** do a factory reset
  first: pairings made with this firmware may not be removable otherwise.

New compared with stock 3.x: the 4.x launcher with app glances,
right-to-left text (Arabic, Hebrew) with language packs, and battery level
in 1% steps.

## Building

Follow Core's [getting started guide](https://developer.repebble.com/pebbleos/docs/getting_started/)
to install the SDK, then build a bundle for your revision:

```shell
pbl configure --board tintin@ev2_4   # or tintin@v1_5, tintin@v2_0
pbl bundle
```

The board's page in `docs/boards/tintin` has the hardware details.

## Other watches

This branch is one of several ports in this repository. The
[front page](https://github.com/bjsilvest/PebbleOS-legacy-watches) lists
them all, with their latest releases. The earlier port, based on PebbleOS
v4.41.2, is on the `tintin-lite-fork` branch.

## Support

This project is provided as-is, and may be updated over time. If you have
questions, please [open an issue](https://github.com/bjsilvest/PebbleOS-legacy-watches/issues).

## Licenses

PebbleOS and the changes in this port are under the Apache License 2.0
(`LICENSE`). PebbleOS is by Core Devices and builds on the source released
by Google. Third-party components keep their own licenses, listed in
`third_party/sbom.yml`. The TI Bluetooth init script
(`bluetooth_init_cc2564_2.14.bts`) is licensed by Texas Instruments for use
only with TI devices; its license is attached to each release.
