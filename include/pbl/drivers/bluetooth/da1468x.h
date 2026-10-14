/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>

#include <pbl/drivers/exti.h>

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

/**
 * @brief The chip ID the bootrom printed in the last boot, for diagnostics.
 *
 * @return A static string, empty if none was printed (AD silicon) or no boot got that far.
 */
const char *da1468x_chip_id(void);

/**
 * @brief The first bytes the controller sent after its init markers, for diagnostics.
 *
 * @return A static hex string, empty if nothing came.
 */
const char *da1468x_rx_after_markers(void);

/**
 * @brief The first bytes the controller sent after the last HCI Reset, if it failed.
 *
 * @return A static hex string, empty if nothing came.
 */
const char *da1468x_rx_after_reset(void);

/**
 * @brief Whether the running controller drives RTS for H4 flow control.
 *
 * Call once the controller answers HCI: its RTS then reads ready.
 */
bool da1468x_has_flow_control(void);

/**
 * @brief Whether the controller can take more bytes (its RTS is asserted).
 *
 * Always true on boards without flow control lines. ISR-safe.
 */
bool da1468x_rts_ready(void);

/**
 * @brief Allow or stop the controller sending (drive its CTS), once it runs.
 *
 * No-op on boards without flow control lines. ISR-safe.
 */
void da1468x_set_cts(bool allow);

/**
 * @brief Whether the board has the controller's wake line, so the host may
 *        enter STOP mode with the controller running.
 */
bool da1468x_has_wake(void);

/**
 * @brief Whether the controller has data for the host (its wake line is high).
 *
 * ISR-safe.
 */
bool da1468x_wake_pending(void);

/**
 * @brief Call @p cb from an interrupt when the controller raises its wake line.
 */
void da1468x_wake_irq_init(ExtiHandlerCallback cb);
