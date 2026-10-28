<p align="center">
  <img src="docs/_static/images/logo.svg" alt="PebbleOS" width="320">
</p>

# PebbleOS for the legacy Pebble watches

Unofficial ports of [Core Devices' PebbleOS](https://github.com/coredevices/PebbleOS)
to the original Pebble watches. Each port follows Core's tagged releases
and uses Bluetooth Low Energy (NimBLE) in place of the original Bluetooth
stack.

> **Not official.** This project is not made, endorsed or supported by Core
> Devices, Google or Pebble Technology. "Pebble" is a trademark of its owner
> and is used here only to say which watches these builds run on.
>
> **Use at your own risk.** The software comes with no warranty (see
> Apache 2.0, sections 7–8). These builds leave the watch's bootloader and
> recovery firmware (PRF) alone, so recovery mode stays available, but
> flashing third-party firmware can still go wrong.

## Watches

| Watch | Codename | Latest release | Branch |
|---|---|---|---|
| Pebble, Pebble Steel | tintin | [v4.42.0-tintin.2](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/tag/v4.42.0-tintin.2) | [`tintin-v4.42.0`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/tintin-v4.42.0) |
| Pebble Time, Pebble Time Steel | snowy | [v4.42.0-snowy.1](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/tag/v4.42.0-snowy.1) | [`snowy-v4.42.0`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/snowy-v4.42.0) |
| Pebble Time Round | spalding | [v4.42.0-spalding.1](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/tag/v4.42.0-spalding.1) | [`spalding-v4.42.0`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/spalding-v4.42.0) |
| Pebble 2 | silk | In progress, no release yet | [`silk-port`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/silk-port) |

Download the `.pbz` file that matches your watch from its release. Each
release lists which hardware revisions have been tested and what changed
since the previous one.

### Notes per watch

- **Pebble and Pebble Steel** run a cut-down build, so their versions end
  in `-lite`: the full release does not fit the original Pebble's flash.
  The timeline is a plain list, and Health, Weather, voice replies and the
  Sports app are not included. Only the Kickstarter Pebble (ev2_4) has been
  tested; the later Pebble (v1_5) and Pebble Steel (v2_0) builds have not.
- **Pebble Time Steel** has not been tested. It uses the Pebble Time's
  light-sensor calibration.
- **Pairings from stock firmware are not carried over.** Pair your phone
  again after installing.

## Branches

- `<watch>-v4.42.0`: the current port, based on PebbleOS v4.42.0.
- `snowy-port`, `spalding-port`, `tintin-lite-fork`: the earlier ports,
  based on PebbleOS v4.41.2 (releases `v4.41.2-*`).
- `main`: Core's `main` branch from when this repository was made, plus
  this page.

## Building

Follow Core's [getting started guide](https://pebbleos.readthedocs.io/en/latest/getting_started/index.html)
on the branch for your watch.

## Licenses

PebbleOS and the changes in these ports are under the Apache License 2.0
(`LICENSE`). PebbleOS is by Core Devices and builds on the source released
by Google. Third-party components keep their own licenses, listed in
`third_party/sbom.yml`. The TI Bluetooth init scripts are licensed by Texas
Instruments for use only with TI devices; their license is attached to
each release.
