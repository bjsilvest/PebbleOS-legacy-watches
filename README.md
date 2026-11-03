<p align="center">
  <img src="docs/_static/images/logo.svg" alt="PebbleOS" width="320">
</p>

# PebbleOS for the Pebble Time, Time Round and Pebble 2

This branch is the unofficial port of
[Core Devices' PebbleOS](https://github.com/coredevices/PebbleOS) to the
**Pebble Time**, **Pebble Time Steel**, **Pebble Time Round** and
**Pebble 2** (HR and SE). It follows Core's releases, and each release is a
tag on it.

**Downloads, installing, the disclaimer and notes on each watch are on the
[front page](https://github.com/bjsilvest/PebbleOS-legacy-watches).** The [latest release](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/latest) lists what
changed and what was tested.

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
that is built from the Dialog DA1468x SDK 1.0.14.1081. The SDK can't be
shared here, so get it yourself and build the images with `tools/silk_bt`:

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

For what is known about updating the heart rate sensor, see
`docs/boards/silk`.

## Other branches

The original Pebble and Pebble Steel have their own cut-down build on the
[`legacy-lite`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/legacy-lite) branch, which follows this one. The
front page lists all branches, including the earlier per-watch ports.

## Licenses

PebbleOS and the changes in this port are under the Apache License 2.0
(`LICENSE`). Third-party components keep their own licenses, listed in
`third_party/sbom.yml`. The front page lists the licenses of the parts
shipped in the firmware files.
