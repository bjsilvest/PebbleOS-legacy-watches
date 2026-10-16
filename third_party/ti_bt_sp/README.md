# TI Bluetooth service packs

Texas Instruments' initialization scripts for the CC256x Bluetooth
controllers. The chip needs its script at every power up
(`drivers/bluetooth/cc2564.c`). Both files are unmodified.

## CC256xB: `TIInit_6.7.16.bts`

For the CC256xB (ROM 7.16), with the BLE add-on, from
https://git.ti.com/cgit/ti-bt/service-packs (mirrored as
https://github.com/coredevices/ti-bt-service-packs, commit 3aa1d75f3c2a).
Used by the Pebble Time (snowy). License: `LICENSE`.

## CC256xA: `bluetooth_init_cc2564_2.14.bts`

For the CC256xA (ROM 6.15, as in the original Pebble), service pack 2.14,
`TI_P6_15.93`, with the BLE add-on 1.2 built in. LMP subversion 0x1b5d once
loaded. From TI's CC2564 BT+BLE package `CC2564_BT_BLE_SP_BTS.zip`, linked
from processors.wiki.ti.com/index.php/CC256x_Downloads and retrieved from the
Internet Archive copy of
http://processors.wiki.ti.com/images/1/1e/CC2564_BT_BLE_SP_BTS.zip
(zip sha256 61a407a60a82f68f650225bf9cd960d98cbde2e5ecb2aa52c1c26ef0312726b0,
file sha256 6622aac7ac0c4f65e9f42328c8781f0103e03b7cbbbdb7cb1c06c8ca761bfc5e).
License: `LICENSE.cc2564`, the one shipped in that package.

Do not use `TIInit_6.6.15.bts` from linux-firmware instead: it carries the
same base patch but no BLE add-on, and turns LE off with two "Disable LE
Feature Bits" (0xFF26) commands.

## License

Both are distributed under the Texas Instruments Text File License, which
allows binary redistribution without modification, for use with TI devices
only. Each script keeps the license text it was published with.
