/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

#include <board/display.h>

// Pixels masked at each end of the top half's rows, from the stock spalding
// framebuffer layout. The framebuffer itself is that packed layout, which is
// also the one of legacy round apps (g_gbitmap_legacy_3x_data_row_infos).
const uint8_t g_display_round_mask[] = {
  76, 71, 66, 63, 60, 57, 55, 52, 50, 48, 46, 45, 43, 41, 40, 38, 37, 36, 34, 33, 32, 31, 29,
  28, 27, 26, 25, 24, 23, 22, 22, 21, 20, 19, 18, 18, 17, 16, 15, 15, 14, 13, 13, 12, 12, 11,
  10, 10, 9,  9,  8,  8,  7,  7,  7,  6,  6,  5,  5,  5,  4,  4,  4,  3,  3,  3,  2,  2,  2,
  2,  2,  1,  1,  1,  1,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};
