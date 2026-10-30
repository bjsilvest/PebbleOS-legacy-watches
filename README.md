<p align="center">
  <img src="docs/_static/images/logo.svg" alt="PebbleOS" width="320">
</p>

# PebbleOS for the legacy Pebble watches

Unofficial ports of [Core Devices' PebbleOS](https://github.com/coredevices/PebbleOS)
to the original Pebble watches. Each port follows Core's tagged releases
and uses Bluetooth Low Energy (NimBLE) in place of the original Bluetooth
stack.

Disclaimer: These ports were made with an assistance of LLM coding tools. Initial research, coding, review, and documentation was done with Claude Opus 5.5.

> **Not official.** This project is not made, endorsed or supported by Core
> Devices, Google or Pebble Technology. "Pebble" is a trademark of its owner
> and is used here only to say which watches these builds run on.
>
> **Use at your own risk.** The software comes with no warranty (see
> Apache 2.0, sections 7–8). These builds leave the watch's bootloader and
> recovery firmware (PRF) alone, so recovery mode stays available in the event of a crash or update failure.

## Watches

| Watch | Codename | Latest build | Branch |
|---|---|---|---|
| Pebble, Pebble Steel | tintin | [v4.43.0-legacy-lite.1](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/tag/v4.43.0-legacy) | [`tintin-v4.43.0`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/tintin-v4.43.0) |
| Pebble Time, Pebble Time Steel | snowy | [v4.43.0-legacy.1](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/tag/v4.43.0-legacy) | [`snowy-v4.43.0`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/snowy-v4.43.0) |
| Pebble Time Round | spalding | [v4.43.0-legacy.1](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/tag/v4.43.0-legacy) | [`spalding-v4.43.0`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/spalding-v4.43.0) |
| Pebble 2 | silk | In progress, no release yet | [`silk-port`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/silk-port) |

All watches share one release per PebbleOS version. Download the
`.pbz` that matches your watch from the latest release,
[Legacy watches 4.43.0](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/tag/v4.43.0-legacy). Its notes list, for each
watch, which hardware revisions have been tested and what changed.

### Notes per watch

- **Pebble and Pebble Steel** run a cut-down `-lite` build: as the full release does not fit the original Pebble's flash.
  The timeline is a plain list, and Health, Weather, and the
  Sports app are not included. Only the Kickstarter Pebble (ev2_4) has been
  tested; the later Pebble (v1_5) and Pebble Steel (v2_0) builds have not.
- **Pebble Time Steel** has not been tested. It uses the Pebble Time's
  light-sensor calibration.
- **Pairings from stock firmware are not carried over.** Pair your phone
  again after installing.
- **If you want to return to the -rbl firmware, it is recommended you do a factory reset first** You may not be able to delete paired Bluetooth devices made from the newer firmware.

## Installing

Install the firmware from the Core app on your phone. You need the Core
app's debug options for this.

These builds use Bluetooth LE only. On Android, the Core app expects the
original Pebble watches over Bluetooth Classic and hides them from its
Bluetooth LE scan, so you have to turn that on to pair again after the
update.

### Coming from stock firmware

1. In the Core app, go to **Settings → Debug** and turn on **Show debug
   options**.
2. Download the `.pbz` for your watch from the latest release onto your
   phone.
3. With the watch connected, open the **Watches** tab, tap the watch's
   **⋮** menu, then **Debug → Firmware Update Debug → Sideload FW**, and
   pick the `.pbz`. The watch shows the update's progress and restarts
   when it is done.
4. Once the update has finished and the watch has restarted on the new
   firmware, delete the old pairing on both sides: on the watch in
   **Settings → Bluetooth**, and on the phone in the Core app and in the
   phone's Bluetooth settings.
5. **Android:** in the Core app, go to **Settings → Connectivity** and
   turn on **Show legacy watches in BLE scan**. iPhones don't need this.
6. Pair the watch again from the Core app.

### Updating from an earlier build of these ports

The watch is already paired over Bluetooth LE, so only step 3 is needed:
sideload the new `.pbz`.

### After a factory reset

A factory reset starts the stock recovery firmware. To install again, pair
the phone with recovery from the Core app (it shows no code), then sideload
the `.pbz` as in step 3.

- **Pebble and Pebble Steel:** recovery's pairing is not carried over, so
  pair once more after the install. Open **Settings → Bluetooth** on the
  watch and keep it open while the phone pairs, then enter the code the
  watch shows. Alternatively, forget the watch on both sides and pair
  again.
- **Pebble Time, Pebble Time Steel and Pebble Time Round:** in the
  4.43.0-legacy.1 builds, the same applies. From the next release, these
  watches keep recovery's pairing and reconnect without pairing again.

## Branches

- `<watch>-v4.43.0`: the current port, based on PebbleOS v4.43.0.
- `<watch>-v4.42.0`: the previous port, based on PebbleOS v4.42.0 (release
  [Legacy watches 4.42.0](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/tag/v4.42.0-legacy)).
- `snowy-port`, `spalding-port`, `tintin-lite-fork`: the earlier ports,
  based on PebbleOS v4.41.2 (release
  [Legacy watches 4.41.2](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/tag/v4.41.2-legacy)).
- `main`: Core's `main` branch from when this repository was made.

## Building

Follow Core's [getting started guide](https://developer.repebble.com/pebbleos/docs/getting_started/)
on the branch for your watch.

## Licenses

PebbleOS and the changes in these ports are under the Apache License 2.0
(`LICENSE`). PebbleOS is by Core Devices and builds on the source released
by Google. Third-party components keep their own licenses, listed in
`third_party/sbom.yml`. The TI Bluetooth init scripts are licensed by Texas
Instruments for use only with TI devices; their license is attached to
each release.

Every port except the Pebble and Pebble Steel builds includes the
[Speex](https://github.com/xiph/speex) speech codec for voice replies.
Speex is under a BSD 3-clause license; its copyright notice and license
are attached to each release that includes it as `LICENSE-Speex.txt`.
