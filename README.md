<p align="center">
  <img src="docs/_static/images/logo.svg" alt="PebbleOS" width="320">
</p>

# PebbleOS for the Pebble and Pebble Steel (tintin)

This branch is the unofficial port of
[Core Devices' PebbleOS](https://github.com/coredevices/PebbleOS) to the
original **Pebble** and the **Pebble Steel**. It is a cut-down build of the
[`legacy`](https://github.com/bjsilvest/PebbleOS-legacy-watches/tree/legacy) branch, because the full firmware doesn't fit
the original Pebble's flash.

**Downloads, installing, the disclaimer and what is different from stock
are on the [front page](https://github.com/bjsilvest/PebbleOS-legacy-watches).** The [latest release](https://github.com/bjsilvest/PebbleOS-legacy-watches/releases/latest)
lists what changed and what was tested.

## Building

Follow Core's [getting started guide](https://developer.repebble.com/pebbleos/docs/getting_started/)
to install the SDK, then build a bundle for your hardware revision:

```shell
pbl configure --board tintin@ev2_4   # or tintin@v1_5, tintin@v2_0
pbl bundle
```

| Watch shows | Board |
|---|---|
| V2R2 | `tintin@ev2_4` |
| V3R1, V3R2, V3R3 | `tintin@v1_5` |
| 102V1 (Pebble Steel) | `tintin@v2_0` |

The board's page in `docs/boards/tintin` has the hardware details.

## Licenses

PebbleOS and the changes in this port are under the Apache License 2.0
(`LICENSE`). Third-party components keep their own licenses, listed in
`third_party/sbom.yml`. The front page lists the licenses of the parts
shipped in the firmware files.
