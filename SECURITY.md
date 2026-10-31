# Security Policy

These are unofficial ports of PebbleOS to the original Pebble watches. They
are not made or supported by Core Devices, so please do not send reports about
these builds to Core.

## Supported Versions

Only the latest release, listed in the [README](README.md#watches), is
supported. A fix ships as a new point release of that version (for example
v4.44.0-legacy.1 -> v4.44.0-legacy.2), which you sideload as described in the
README. These builds are not delivered through the Pebble mobile app.

## Reporting a Vulnerability

Please do not report security vulnerabilities in public issues or pull
requests. Instead, open this repository's
[Security tab](https://github.com/bjsilvest/PebbleOS-legacy-watches/security)
and select **Report a vulnerability**.

Include:

- the watch model and the firmware build (for example v4.44.0-legacy.1)
- a description of the issue and its impact
- steps or a minimal app to reproduce it, if you have one

This is a one-person project maintained on a best-effort basis. Reports will
be acknowledged and you will be credited in the fix's release notes unless
you ask not to be.

## Scope

This policy covers the changes made for these ports: for example the
watch-specific drivers, the Bluetooth LE integration for these watches and
the import of pairings from stock firmware.

If the issue is in Core's PebbleOS code and also affects Core's own builds,
please report it to Core Devices as described in
[their security policy](https://github.com/coredevices/PebbleOS/security/policy).
It will be fixed here once Core releases the fix.

These builds do not change the watch's bootloader or recovery firmware (PRF).
Issues in those, in the Pebble mobile app or in online services are out of
scope.

## Disclosure

Please do not disclose the issue publicly until a release containing the fix
is available. The release notes will describe what was fixed.
