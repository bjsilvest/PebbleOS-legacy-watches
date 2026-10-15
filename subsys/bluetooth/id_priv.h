/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>

//! Puts the local address chosen with pbl_bt_set_local_address() on the controller if it does
//! not use it yet. Fails while advertising.
//! @return true if the controller uses the chosen address.
bool pbl_bt_id_sync_local_address(void);
