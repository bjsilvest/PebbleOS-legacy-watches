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
 * @brief Put a resolvable private address on the air in place of the identity address.
 *
 * NimBLE only takes a static random address as its own. The transport sends @p addr in every
 * LE Set Random Address, and reports it as the local RPA of new connections, so NimBLE keeps
 * the identity address for key distribution (DA1468x only; ignored otherwise).
 *
 * @param addr The address, or NULL to use the identity address again. Takes effect at the next
 *             LE Set Random Address.
 */
void pbl_bt_hci_uart_set_local_rpa(const uint8_t *addr);

/**
 * @brief Whether the controller has yet to accept the address chosen with
 *        pbl_bt_hci_uart_set_local_rpa() (DA1468x only; false otherwise).
 *
 * Connections are only reported with the local RPA the controller accepted.
 */
bool pbl_bt_hci_uart_local_addr_outdated(void);

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
 * Only the first reset since boot is kept.
 */
void pbl_bt_hci_uart_snapshot(void);

/**
 * @brief Report that the controller stopped answering HCI commands.
 *
 * The DA1468x is rebooted from the HCI thread (a few times per boot at most);
 * no-op for other controllers.
 */
void pbl_bt_hci_uart_controller_unresponsive(void);

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

/**
 * @brief The SMP opcodes seen on the link, for diagnosing pairing.
 *
 * Newest last: '<' received or '>' sent, the opcode's low hex digit, and for
 * Pairing Failed (5) its reason's low hex digit. A '+' follows a sent PDU for
 * each of the next few packets the controller reports completed, and 'x'
 * marks a disconnection (DA1468x only).
 *
 * @return A static string.
 */
const char *pbl_bt_hci_uart_smp_trace(void);

/**
 * @brief The LE ACL data packet length the controller reported, before the
 *        transport capped it to the LL PDU size (DA1468x only; 0 if unknown).
 */
uint16_t pbl_bt_hci_uart_le_acl_len_reported(void);

/**
 * @brief Describe flow control and what the transport skipped, for diagnostics.
 *
 * Formats "R<controller reboots> <F flow control|p pacing><RTS waits>!<sends forced past RTS>
 * z<times the host let STOP mode in> r<RX pauses> o<UART overruns> d<bytes dropped to resynchronize>
 * u<message>@<task> of the last GAPM_UNKNOWN_TASK_IND g<non-HCI DGTL packets
 * skipped>/<last GTL message ID>" (DA1468x only; empty otherwise).
 *
 * @param[out] buf Buffer for the string.
 * @param len Size of @p buf.
 */
void pbl_bt_hci_uart_copy_rx_stats(char *buf, size_t len);
