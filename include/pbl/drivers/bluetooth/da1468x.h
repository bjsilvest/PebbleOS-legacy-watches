/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>

/**
 * @brief Boot the DA1468x Bluetooth controller from RAM.
 *
 * Resets the radio, loads stage 2 through its bootrom and the controller image
 * through stage 2, then leaves the board's HCI UARTs initialized at the HCI
 * baud rate with the RX interrupt disabled. Blocks for about a second, so call
 * it from a thread. On failure the radio is held in reset.
 *
 * @return true if the controller image is running.
 */
bool da1468x_boot(void);

/**
 * @brief Describe how far the last boot got, for diagnostics.
 *
 * @return A short static string, e.g. "Running (try 1)" or the failing step.
 */
const char *da1468x_status(void);
