/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <kernel/pebble_tasks.h>

//! @file resource_mapped.h
//!
//! Keeps the flash readable while a task holds memory-mapped resource bytes,
//! on boards whose flash is only readable through its memory window while in
//! use (FLASH_MEMORY_MAPPABLE_ADDRESS). Elsewhere these do nothing.

//! @p task took a pointer into the mapped resource banks.
void resource_mapped_use(PebbleTask task);

//! @p task dropped a pointer taken with resource_mapped_use().
void resource_mapped_release(PebbleTask task);

//! Drop every reference @p task still holds, when its process is cleaned up.
void resource_mapped_release_all(PebbleTask task);
