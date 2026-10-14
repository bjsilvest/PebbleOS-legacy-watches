/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Wait for the H4 UART transport's controller to come up.
 *
 * Controllers that the transport has to boot first (@c BT_CONTROLLER_DA1468X)
 * come up asynchronously and may fail; others are ready at once.
 *
 * @param timeout_ms How long to wait while the controller is still booting.
 * @return true if the controller can take HCI traffic.
 */
bool pbl_bt_hci_uart_wait_ready(uint32_t timeout_ms);
