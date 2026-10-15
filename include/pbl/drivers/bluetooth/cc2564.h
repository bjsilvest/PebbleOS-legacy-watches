/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>

#include <pbl/drivers/exti.h>

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

/**
 * @brief Whether the controller sent HCILL_GO_TO_SLEEP_IND during boot,
 * clearing the flag. The transport must then answer it.
 */
bool cc2564_take_sleep_ind(void);

/**
 * @brief Hold our RTS high so the controller cannot send (eHCILL sleep), or
 * hand it back to the UART's flow control.
 */
void cc2564_hold_rts(bool hold);

/** @brief Call @p cb on the controller's wake pulse on our CTS. */
void cc2564_cts_wake_enable(ExtiHandlerCallback cb);

/**
 * @brief Send a one-byte eHCILL message, even if the sleeping controller
 * holds our CTS deasserted. With the transport's TX lock held.
 */
void cc2564_send_hcill(uint8_t msg);

/** @brief Stop watching CTS for the wake pulse. */
void cc2564_cts_wake_disable(void);

/** @brief How far the last boot got, for diagnostics. */
const char *cc2564_status(void);

/** @brief HCI version, manufacturer and LMP subversion the chip reported. */
const char *cc2564_chip_id(void);

/** @} */
