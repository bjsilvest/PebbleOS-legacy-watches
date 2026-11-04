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
> **Disclaimer:** These ports were made with the help of LLM coding tools,
> used for research, coding, review and some documentation. All changes
> were reviewed and tested on real hardware by a real human.
>
> **Use at your own risk.** The software comes with no warranty (see
> Apache 2.0, sections 7–8). These builds leave the watch's bootloader and
> recovery firmware (PRF) alone, so recovery mode stays available in the
> event of a crash or update failure.

## Download

Get the `.pbz` for your watch from the
**[latest release](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/latest)**.

| Watch | Codename | Branch |
|---|---|---|
| Pebble, Pebble Steel | tintin | [`legacy-lite`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/legacy-lite) |
| Pebble Time, Pebble Time Steel | snowy | [`legacy`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/legacy) |
| Pebble Time Round | spalding | [`legacy`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/legacy) |
| Pebble 2 (HR and SE) | silk | [`legacy`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/legacy) |

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
4. Once the watch has restarted on the new firmware, delete the old
   pairing on both sides: on the watch in **Settings → Bluetooth**, and on
   the phone in the Core app and in the phone's Bluetooth settings.
5. **Android:** in the Core app, go to **Settings → Connectivity** and
   turn on **Show legacy watches in BLE scan**. iPhones don't need this.
6. Pair the watch again from the Core app. The Pebble 2 pairs like Core's
   newer watches. The Pebble, Pebble Steel, Pebble Time and Time Round show
   a code on the watch that you type on the phone, because their Bluetooth
   chips are older than the pairing method that only asks you to confirm a
   code.

On the Pebble and Pebble Steel, stock pairings are never carried over. The
other watches try to take over a stock Bluetooth LE pairing, but this has
not been confirmed yet. Android phones paired with stock over Bluetooth
Classic anyway, so plan on pairing again.

### Updating from an earlier build of these ports

The watch is already paired over Bluetooth LE, so only step 3 is needed:
sideload the new `.pbz`.

After an update that changed the watch's Bluetooth services, the watch
drops the connection once, about 10 seconds after reconnecting, so the
phone picks up the change. It waits for an app install to finish first.

### After a factory reset

A factory reset starts the stock recovery firmware. To install again, pair
the phone with recovery from the Core app (it shows no code), then sideload
the `.pbz` as in step 3.

- **Pebble Time, Time Steel, Time Round and Pebble 2:** the firmware should take
  over recovery's pairing, so the phone reconnects immediately upon reboot without pairing again.
- **Pebble and Pebble Steel:** due to storage constraints, recovery's pairing is not carried over, so
  pair once more after the install. Open **Settings → Bluetooth** on the
  watch and keep it open while the phone pairs, then enter the code the
  watch shows. Alternatively, forget the watch on both sides and pair
  again.

### Going back to stock firmware

Do a factory reset first. Otherwise you may not be able to remove the
pairings made with this firmware.

## Notes on each watch

### Pebble and Pebble Steel

The original Pebble has very little flash and RAM, so these watches run a
cut-down **-lite** version.

New compared with stock 3.x:

- The 4.x launcher with app glances.
- Support for right-to-left text (Arabic, Hebrew) with language packs.
- Up to four language packs at once with Core app 1.16 or later.
- Battery level in 1% steps.
- The hourly usage report that Core's own watches send for the Battery
  page in the Core app (needs "Send watch analytics" turned on).

Removed compared with stock 3.x:

- The Sports app and PebbleKit Sports.
- Bluetooth Classic, so the original Pebble phone app is not supported.
- The serial debug console and info/warning logs. 

The timeline feature has been reworked into a plain list of up to 32 pins, because the animated
timeline doesn't fit in flash. Health, Weather, Smart Alarm Menu, Timeline Quick View, Reminders and the Gothic 36 and LECO 60 fonts are not included.

### Pebble Time Steel

The Time Steel uses the Pebble Time's light sensor calibration, so the
backlight may come on at the wrong light levels. If you try it, please
report back.

### Pebble 2

- **Heart rate variability (HRV)** mode enabled. The Pebble 2 heart rate sensor is capable of HRV, but it was never taken enabled in stock. It will only turn on when an app asks for HRV. The sensor requires an app request to activate HRV mode; however, current Pebble SDK limitations prevent apps from requesting this mode. A future SDK update from Core could unlock this capability. Note that HRV data decoding for Pebble 2 is still actively under development.

## Branches

- `legacy`: the Pebble Time, Pebble Time Steel, Pebble Time Round and
  Pebble 2, from PebbleOS v4.44.0 on. It follows Core's releases, and each
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
- `main`: Core's `main` branch from when this repository was made, with
  this page.

## Building

See the README on the [`legacy`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/legacy) or
[`legacy-lite`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/legacy-lite) branch. 

The Pebble 2 relies on two proprietary components that are not included in this release. First, the heart rate sensor runs on its factory-installed application, and is not included in any releases. Second, the Bluetooth chip requires an image built from the Renesas (Dialog) DA1468x SDK. Because legal/licensing terms prevent us from sharing the SDK here, you will need to acquire it separately to compile the Bluetooth image.

## Support

This project is provided as-is, and may be updated over time. If you have
questions, please [open an issue](https://github.com/bjsilvest/PebbleOS-legacy-watches/issues).

## Licensing

- **PebbleOS** and the changes made for these ports are under the Apache
  License 2.0 (`LICENSE`). PebbleOS is developed by Core Devices and
  builds on source code released by Google. The display FPGA bitstreams for
  the Pebble Time and Time Round come from that source too.
- **Other third-party components** (NimBLE, mbedTLS, nanopb and others)
  keep their own licenses. They are listed in `third_party/sbom.yml`, with
  the license text in each component's directory.
- **TI Bluetooth init scripts** (Pebble, Pebble Steel, Pebble Time, Time
  Round) are shipped unmodified and licensed by Texas Instruments for use
  only with TI devices.
- **[Speex](https://github.com/xiph/speex)**, the speech codec used for
  voice replies on every watch except the Pebble and Pebble Steel, is under
  the BSD 3-Clause License.
- **The Pebble 2's Bluetooth chip image** (Dialog DA14681) inside its
  firmware is built with the Renesas (Dialog) DA1468x SDK and FreeRTOS. It
  is not under the Apache License and is distributed with Renesas's
  permission.

Each release has the matching license files, notices and the FreeRTOS
source attached next to the firmware files.
