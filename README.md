<p align="center">
  <img src="docs/_static/images/logo.svg" alt="PebbleOS" width="320">
</p>

# PebbleOS for the legacy Pebble watches

Unofficial ports of [Core Devices' PebbleOS](https://github.com/coredevices/PebbleOS)
to the original Pebble watches. Each release corresponds to a Core's tagged releases. These ports use Bluetooth Low Energy (NimBLE) in place of the original Bluetooth stack, and therefore do not use Bluetooth Classic or work on the legacy Pebble app.

> **Not official.** This project is not made, endorsed or supported by Core
> Devices, Google or Pebble Technology. "Pebble" is a trademark of its owner
> and is used here only to say which watches these builds run on.
>
> **AI Disclaimer.** These ports were made with the help of LLM coding tools,
> used for research, coding, review and some documentation. All changes
> were reviewed and tested on real hardware by real humans.
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

## New Features

The following is a list of new features (as of v4.44.1) that have been introduced to PebbleOS that the legacy watches can now take advantage of.

### Settings and System ###

- Settings sync with the Core app, so most watch settings can be changed from the phone.
- Set time and date by hand, with a Time Source option (automatic or manual).
- Left-handed mode (display rotated 180°).
- Motion Sensitivity setting for shake-to-light and wrist gestures.
- Battery percentage shown to the exact 1%.
- Low-power watchface shows the date as well as the time.
- Text Size moved to the Display menu; open menus re-lay out immediately when it changes.

 ### Backlight ###

- Backlight presets: Max Brightness, Standard, Battery Saver, Advanced.
- Dynamic backlight: the light sensor sets the brightness. Core added it; we enabled it and calibrated it on each watch.
- Wake on motion as its own option.
- Quiet Time can turn off motion backlight and the backlight for notifications.

### Notifications ###

- Group by Sender.
- Vibe Timing setting.
- Clock Style for the status bar, including Big & Bold.
- Banner Style (Pebble 2 only, black-and-white watches).
- Dedicated notification text size.
- 30-second timeout option.
- Timed mute: 1 hour or Today.
- Quiet Time choice between auto-dismissing notifications and keeping them.
- Images sent with notifications are shown.
- iOS notifications can use custom icons, vibrations and colours.
- Many new app icons (Signal, YouTube, Reddit, WhatsApp-era apps, banks and others).
- Emoji: Rebble emoji set, colour emoji on Time and Time Round, and an emoji grid for quick replies.

### Quick Launch ###

- A single press of Up or Down on the watchface can launch an app or action; stock had press-and-hold only.
- Hold Back and Back+Up combo shortcuts.
- Picker split into "Actions" and "Apps".
- New actions: Nothing (disable a button), Backlight toggle, Clear notification history.

### Vibrations ###

- Hourly vibration ("Hourly Notice").
- Vibration when the phone disconnects.
- New built-in patterns, including a "Gentle" alarm.
- Custom vibration patterns sent by the phone.

### Quiet Time ###

- Reorganised into Schedule and Backlight submenus.
- Option to let notifications through.
- Calendar Aware moved into this menu.

### Apps ###

- Full Timeline app in the launcher, with past next to future.
- Timeline: "Unsupported Faces" option for Quick View; notification timestamps on round screens.
- Music: album art (Time and Time Round), fast-forward/rewind icons, volume controls and progress bar settings.
- Weather: wind in m/s and knots.
- Phone calls can be answered from the watch on Android.

### Health ###

- Health Tracking on/off switch.
- Distance unit setting (km or miles).
- Shows yesterday's sleep until tonight's is recorded.
- Calories shown in kcal; more days shown per screen.
- Pebble 2 HR only: today's heart rate on the HR card, HR monitoring interval (including 5 minutes), and HR during activities.
- Pebble 2 only: deep sleep shown on its black-and-white screen.

### Languages ###

- New translations: Danish, Catalan, Japanese, Polish, Ukrainian, simplified Chinese.
- Several language packs installed at once, with a picker listing them.
- Better Unicode coverage: all of Latin Extended-A, plural forms, proper space widths.

 
## Notes

### Pebble and Pebble Steel

The original Pebble has very little flash and RAM, so these watches run a
cut-down **-lite** version.

Changes compared with stock 3.x:

- Added the 4.x launcher with app glances.
- Added right-to-left text support (Arabic/Hebrew).
- Removed The Sports app and PebbleKit Sports.
- Removed Bluetooth Classic support, so the original Pebble phone app is not supported.
- Removed The serial debug console and info/warning logs.

Due to storage constraints, `legacy-lite` includes the following changes compared to `legacy`:

- The timeline feature has been reworked into a plain list of up to 32 pins.
- Health, Weather, Smart Alarm Menu, Timeline Quick View, Reminders apps are not included.
- The new Gothic 36 and LECO 60 fonts are not included.
- Some animations have been removed.

### Pebble 2

- **Heart rate variability (HRV)** mode enabled. The Pebble 2 heart rate sensor is capable of HRV, but it was never enabled in stock. It will only turn on when an app asks for HRV. The sensor requires an app request to activate HRV mode; however, current Pebble SDK limitations prevent apps from requesting this mode. A future SDK update from Core could unlock this capability. Note that HRV data decoding for Pebble 2 is still actively under development.

## Branches

- `main`: Core's `main` branch from when this repository was made, with
  this page.
- `legacy`: the build for Pebble Time, Pebble Time Steel, Pebble Time Round and
  Pebble 2.
- `legacy-lite`: the Pebble Classic and Pebble Steel's cut-down build of
  `legacy`.
- Older branches and releases are retained for historical reference. They are less stable than the current release and are not recommended for daily use.

## Building

See the README on the [`legacy`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/legacy) or
[`legacy-lite`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/legacy-lite) branch. 

The Pebble 2 relies on two proprietary components that are not included in this release. First, the heart rate sensor runs on its factory-installed application, and is not included in any releases. Second, the Bluetooth chip requires an image built from the Renesas (Dialog) DA1468x SDK. Because legal/licensing terms prevent us from sharing the SDK here, you will need to acquire it separately to compile the Bluetooth image.

## Feedback wanted

Things need more real-world testing:

- **Dynamic backlight.** The light sensor was calibrated on one watch of
  each model. Say whether the backlight is too dim, too bright or slow to
  change, and in what light (dark room, indoors, daylight).
- **Brightness Calibration** Does the backlight turn on when the room is dark? Does it stay off when out in the sun?
- **Battery life.** How many days a full charge lasts, compared with stock
  if you remember it.

If you feel performance is not as good as stock, please [open an issue](https://github.com/bjsilvest/PebbleOS-legacy-watches/issues)
and include:

- watch model and firmware version (**Settings → System → Information**)
- phone (Android or iPhone)
- backlight preset (Standard, Battery Saver, …) and whether motion
  backlight is on
- for battery: roughly how many notifications a day, and whether Health or
  heart rate monitoring is on

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
