/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#define DISPLAY_ORIENTATION_COLUMN_MAJOR_INVERTED 0
#define DISPLAY_ORIENTATION_ROTATED_180           0
#define DISPLAY_ORIENTATION_ROW_MAJOR             1
#define DISPLAY_ORIENTATION_ROW_MAJOR_INVERTED    0

#define PBL_BW    0
#define PBL_COLOR 1

#define PBL_RECT  0
#define PBL_ROUND 1

#define PBL_DISPLAY_WIDTH  180
#define PBL_DISPLAY_HEIGHT 180

// Spalding doesn't support 2x apps, but need to define these anyways so it builds
#define LEGACY_2X_DISP_COLS PBL_DISPLAY_WIDTH
#define LEGACY_2X_DISP_ROWS PBL_DISPLAY_HEIGHT
#define LEGACY_3X_DISP_COLS PBL_DISPLAY_WIDTH
#define LEGACY_3X_DISP_ROWS PBL_DISPLAY_HEIGHT

// Circular framebuffer: all visible pixels plus 76 padding bytes before the
// first and after the last row.
#define DISPLAY_FRAMEBUFFER_BYTES 25944

extern const void *const g_gbitmap_data_row_infos;
extern const void *const g_gbitmap_legacy_3x_data_row_infos;
