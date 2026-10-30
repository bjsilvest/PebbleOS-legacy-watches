/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/drivers/display.h>

/**
 * @defgroup drivers_display_ice40lp iCE40 framebuffer
 * @ingroup drivers_display
 * @brief @ref drivers_display driver for the Pebble Time and Time Round panels behind an iCE40
 * FPGA framebuffer, on an STM32F4 SPI. Frames are always sent whole. The pins and the SPI come
 * from the board's BOARD_CONFIG_DISPLAY. One instance only.
 *
 * @code{.c}
 * PBL_DISPLAY_ICE40LP_DEFINE(s_display, "display", PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT);
 * @endcode
 * @{
 */

/** @brief iCE40 framebuffer display. */
struct pbl_display_ice40lp {
  /** Display. */
  struct pbl_display disp;
};

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_display_ops pbl_display_ice40lp_ops;
/** @endcond */

/**
 * @brief Define the display.
 * @param sym Symbol; the display is @c sym.disp.
 * @param _name Name.
 * @param _width Width, in pixels.
 * @param _height Height, in pixels.
 */
#define PBL_DISPLAY_ICE40LP_DEFINE(sym, _name, _width, _height)                               \
  PBL_DISPLAY_STATE_DEFINE(sym);                                                              \
  const struct pbl_display_ice40lp sym = {                                                    \
    .disp = PBL_DISPLAY_INIT(sym, _name, &pbl_display_ice40lp_ops, nullptr, _width, _height, \
                             PBL_DISPLAY_FORMAT_ARGB2222, false),                            \
  };                                                                                          \
  PBL_DEVICE_REGISTER(sym, &sym.disp.dev)

/** @} */
