/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/kernel/compiler.h>

#include <kernel/pebble_tasks.h>

void PBL_WEAK resource_mapped_use(PebbleTask task) {
}

void PBL_WEAK resource_mapped_release(PebbleTask task) {
}

void PBL_WEAK resource_mapped_release_all(PebbleTask task) {
}
