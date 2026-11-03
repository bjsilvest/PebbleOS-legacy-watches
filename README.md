<p align="center">
  <img src="docs/_static/images/logo.svg" alt="PebbleOS" width="320">
</p>

# PebbleOS for the legacy Pebble watches

Unofficial ports of [Core Devices' PebbleOS](https://github.com/coredevices/PebbleOS)
to the original Pebble watches. Each port follows Core's tagged releases
and uses Bluetooth Low Energy (NimBLE) in place of the original Bluetooth
stack.

Disclaimer: These ports were made with the assistance of LLM coding tools, used for research, coding, review and documentation. All changes were reviewed and tested on real hardware.

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
| Pebble, Pebble Steel | tintin | [v4.44.1-legacy-lite.2](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/tag/v4.44.1-legacy) | [`legacy-lite`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/legacy-lite) |
| Pebble Time, Pebble Time Steel | snowy | [v4.44.1-legacy.2](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/tag/v4.44.1-legacy) | [`legacy`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/legacy) |
| Pebble Time Round | spalding | [v4.44.1-legacy.2](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/tag/v4.44.1-legacy) | [`legacy`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/legacy) |
| Pebble 2 (HR and SE) | silk | [v4.44.1-legacy.2](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/tag/v4.44.1-legacy) | [`legacy`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/legacy) |

All watches share one release per PebbleOS version. Download the
`.pbz` that matches your watch from the latest release,
[Legacy watches 4.44.1](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/tag/v4.44.1-legacy). Its notes list, for each
watch, which hardware revisions have been tested and what changed.

### Notes per watch

- **Pebble and Pebble Steel** run a cut-down `-lite` build, as the full
  release does not fit the original Pebble's flash. The timeline is a plain
  list, and Health, Weather, Timeline Quick View, voice replies, Reminders,
  the Sports app and the large Gothic 36 / LECO 60 fonts are not included.
  Only the Kickstarter Pebble (ev2_4) has been tested; the later Pebble
  (v1_5) and Pebble Steel (v2_0) builds have not. To find your watch's
  file, look at **Settings → System → Information → Hardware**: V2R2 is
  ev2_4, V3R1, V3R2 and V3R3 are v1_5, and 102V1 is v2_0.
- **Pebble Time Steel** has not been tested. It uses the Pebble Time's
  light-sensor calibration.
- **Pebble 2** has one build for the HR and the SE. Only the Pebble 2 HR
  has been tested; the SE has not. Heart rate uses the application already
  in the sensor, and this firmware never updates the sensor.
- **Pair your phone again after installing from stock firmware.** On the
  Pebble and Pebble Steel, stock pairings are never carried over. The other
  watches try to take over a stock Bluetooth LE pairing, but this has not
  been confirmed, and Android phones paired with stock firmware over
  Bluetooth Classic, which these builds do not use.
- **If you want to return to the -rbl firmware, it is recommended you do a factory reset first.** You may not be able to delete paired Bluetooth devices made from the newer firmware.

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
6. Pair the watch again from the Core app. The Pebble 2 pairs like Core's
   newer watches. The Pebble, Pebble Steel, Pebble Time and Time Round show
   a code on the watch that you type on the phone: their Bluetooth chips
   are older than the pairing method that only asks you to confirm a code.

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
- **Pebble Time, Pebble Time Steel and Pebble Time Round, And Pebble 2:** the firmware takes over recovery's pairing, so the phone
  reconnects without pairing again.

## Branches

- `legacy`: the Pebble Time, Pebble Time Steel, Pebble Time Round and
  Pebble 2, from PebbleOS v4.44.0 on. It follows Core's releases; each
  release is a tag on it.
- `legacy-lite`: the original Pebble and Pebble Steel's cut-down build of
  `legacy`.
- `<watch>-v4.43.0`: one branch per watch, based on PebbleOS v4.43.0
  (release [Legacy watches 4.43.0](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/tag/v4.43.0-legacy)).
- `<watch>-v4.42.0`: based on PebbleOS v4.42.0 (release
  [Legacy watches 4.42.0](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/tag/v4.42.0-legacy)).
- `snowy-port`, `spalding-port`, `tintin-lite-fork`: the earlier ports,
  based on PebbleOS v4.41.2 (release
  [Legacy watches 4.41.2](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/tag/v4.41.2-legacy)).
- `main`: Core's `main` branch from when this repository was made.

## Building

Follow Core's [getting started guide](https://developer.repebble.com/pebbleos/docs/getting_started/)
on the branch for your watch: `legacy`, or `legacy-lite` for the
Pebble and Pebble Steel.

The Pebble 2 also needs an image for its Bluetooth chip, built from the
Renesas (Dialog) DA1468x SDK, which you have to get yourself: the SDK cannot
be shared here. The `legacy` branch's README explains how to build it.

## Support

This project is provided as-is, and may be updated over time. If you have
questions, please [open an issue](https://github.com/bjsilvest/PebbleOS-legacy-watches/issues).

## Licensing

PebbleOS and the modifications made for these ports are licensed under the Apache License 2.0 (`LICENSE`). PebbleOS is developed by Core Devices and builds upon source code released by Google. Third-party components retain their respective licenses, which are documented in `third_party/sbom.yml`. The TI Bluetooth initialization scripts are provided under a Texas Instruments license restricted to use with TI devices. A copy of that license is included with each release containing those scripts.

All ports except the Pebble and Pebble Steel include [Speex](https://github.com/xiph/speex), a speech codec used for voice replies. Speex is licensed under the BSD 3-Clause License. Its copyright notice and license are included with each applicable release as `LICENSE-Speex.txt`.

The Pebble 2 firmware also includes a separate image for its Dialog DA14681 Bluetooth chip, built using the Renesas (Dialog) DA1468x SDK and FreeRTOS. This image is not covered by the Apache License 2.0 and is distributed with permission from Renesas. The applicable notices (`NOTICE-silk-Bluetooth.txt`) and FreeRTOS source code are included with each release containing this image.
