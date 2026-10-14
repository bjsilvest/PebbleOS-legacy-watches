/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Wait for the H4 UART transport's controller to come up.
 *
 * Controllers that the transport has to boot first (@c BT_CONTROLLER_DA1468X)
 * come up asynchronously and may fail; others are ready at once.
 *
 * Feeds the calling thread's task watchdog while it waits.
 *
 * @param timeout_ms How long to wait while the controller is still booting.
 * @return true if the controller can take HCI traffic.
 */
bool pbl_bt_hci_uart_wait_ready(uint32_t timeout_ms);

/**
 * @brief Describe the last HCI exchange, for diagnostics.
 *
 * Formats "<last answered command>:<its status> !<last failed command>:<its status>",
 * opcodes and status codes in hex.
 *
 * @param[out] buf Buffer for the string.
 * @param len Size of @p buf.
 */
void pbl_bt_hci_uart_copy_status(char *buf, size_t len);

/**
 * @brief Record the HCI state, for diagnosing a host reset.
 *
 * Call when the host resets; read back with pbl_bt_hci_uart_reset_snapshot().
 */
void pbl_bt_hci_uart_snapshot(void);

/**
 * @brief The HCI state at the last pbl_bt_hci_uart_snapshot().
 *
 * Formats "<last command>><last answered command>/<reply length> a<ACL packets sent>
 * n<ACL packets the controller completed>", opcodes in hex; empty before any snapshot.
 *
 * @return A static string.
 */
const char *pbl_bt_hci_uart_reset_snapshot(void);

/**
 * @brief How many times the controller reported more completed ACL packets
 *        than were outstanding, and the count was clamped (DA1468x only).
 */
uint32_t pbl_bt_hci_uart_acl_clamped(void);
