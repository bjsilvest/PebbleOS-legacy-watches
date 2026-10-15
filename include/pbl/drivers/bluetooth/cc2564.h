/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>

/**
 * @defgroup drivers_bluetooth_cc2564 CC2564
 * @ingroup drivers_bluetooth
 * @brief TI CC2564B HCI controller (snowy).
 * @{
 */

/**
 * @brief Power the controller up and load TI's init script.
 *
 * Leaves the board's BT_HCI_UART at the HCI baud rate with its receive
 * interrupt off, for the H4 transport to take over.
 *
 * @return false if the controller did not start; it is then left off.
 */
bool cc2564_boot(void);

/** @brief How far the last boot got, for diagnostics. */
const char *cc2564_status(void);

/** @brief HCI version, manufacturer and LMP subversion the chip reported. */
const char *cc2564_chip_id(void);

/** @} */
