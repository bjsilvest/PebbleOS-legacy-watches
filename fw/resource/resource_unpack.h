/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "resource_storage_file.h"

#include <stdint.h>

//! @file resource_unpack.h
//!
//! System resources a board keeps deflated in its system resource pack to
//! fit it in the bank. The build moves them into a filesystem resource file
//! (see FileResourceData) and stores that file's resource pack, deflated, as
//! one system resource:
//!
//!   | "PBLZ" | pack length (u32) | pack content CRC (u32) | raw deflate, 4 KiB window |
//!
//! At boot, resource_unpack_init() inflates it into the file unless the file
//! already holds a pack with that content CRC.

typedef struct {
  const FileResourceData *file;
  uint32_t packed_resource_id;
} UnpackedResourceFile;

extern const UnpackedResourceFile g_unpacked_resource_files[];
extern const uint32_t g_num_unpacked_resource_files;

//! Bring every unpacked resource file up to date with the system resources.
//! Call once the system resources are known to be valid.
void resource_unpack_init(void);
