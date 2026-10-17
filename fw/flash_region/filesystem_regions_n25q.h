/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

//! @file filesystem_regions_n25q.h
//!
//! The original Pebble's filesystem grew over the years into several pieces of
//! its 4 MiB flash, plus a 2 MiB region past them on 8 MiB watches. The order
//! is the stock firmware's: it numbers the filesystem's pages.

#ifdef CONFIG_FLASH_LAYOUT_TINTIN_8MB
#define FILE_SYSTEM_EXTRA_REGION(MACRO_OPERATOR) \
  MACRO_OPERATOR(FLASH_REGION_EXTRA_FILESYSTEM_BEGIN, FLASH_REGION_EXTRA_FILESYSTEM_END)
#else
#define FILE_SYSTEM_EXTRA_REGION(MACRO_OPERATOR)
#endif

#define FILE_SYSTEM_REGIONS(MACRO_OPERATOR)                                      \
  MACRO_OPERATOR(FLASH_REGION_FILESYSTEM_BEGIN, FLASH_REGION_FILESYSTEM_END)     \
  MACRO_OPERATOR(FLASH_REGION_FILESYSTEM_2_BEGIN, FLASH_REGION_FILESYSTEM_2_END) \
  MACRO_OPERATOR(FLASH_REGION_FILESYSTEM_3_BEGIN, FLASH_REGION_FILESYSTEM_3_END) \
  MACRO_OPERATOR(FLASH_REGION_FILESYSTEM_4_BEGIN, FLASH_REGION_FILESYSTEM_4_END) \
  FILE_SYSTEM_EXTRA_REGION(MACRO_OPERATOR)                                       \
  MACRO_OPERATOR(FLASH_REGION_FILESYSTEM_5_BEGIN, FLASH_REGION_FILESYSTEM_5_END)
