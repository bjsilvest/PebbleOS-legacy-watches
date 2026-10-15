# TI Bluetooth service pack

`TIInit_6.7.16.bts` is Texas Instruments' initialization script for the
CC256xB (ROM 7.16), with the BLE add-on, unmodified from
https://git.ti.com/cgit/ti-bt/service-packs (mirrored as
https://github.com/coredevices/ti-bt-service-packs, commit 3aa1d75f3c2a).
The Pebble Time's CC2564B needs it at every power up
(`drivers/bluetooth/cc2564.c`).

It is distributed under the Texas Instruments Text File License in
`LICENSE`, which allows binary redistribution without modification, for use
with TI devices only.
