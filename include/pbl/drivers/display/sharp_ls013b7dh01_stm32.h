/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/drivers/display.h>

/**
 * @defgroup drivers_display_sharp_ls013b7dh01_stm32 Sharp LS013B7DH01 (STM32)
 * @ingroup drivers_display
 * @brief @ref drivers_display driver for the Pebble 2's Sharp LS013B7DH01 on an STM32F4 SPI,
 * write-only and LSB first. Updates are polled and complete before pbl_display_update()
 * returns. The pins and the SPI come from the board's BOARD_CONFIG_DISPLAY; the PMIC drives
 * VCOM. One instance only.
 *
 * @code{.c}
 * PBL_DISPLAY_SHARP_LS013B7DH01_STM32_DEFINE(s_display, "display", PBL_DISPLAY_WIDTH,
 *                                            PBL_DISPLAY_HEIGHT);
 * @endcode
 * @{
 */

/** @brief Sharp LS013B7DH01 display. */
struct pbl_display_sharp_stm32 {
  /** Display. */
  struct pbl_display disp;
};

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_display_ops pbl_display_sharp_stm32_ops;
/** @endcond */

/**
 * @brief Define the display.
 * @param sym Symbol; the display is @c sym.disp.
 * @param _name Name.
 * @param _width Width, in pixels.
 * @param _height Height, in pixels.
 */
#define PBL_DISPLAY_SHARP_LS013B7DH01_STM32_DEFINE(sym, _name, _width, _height)                  \
  PBL_DISPLAY_STATE_DEFINE(sym);                                                                  \
  const struct pbl_display_sharp_stm32 sym = {                                                    \
    .disp = PBL_DISPLAY_INIT(sym, _name, &pbl_display_sharp_stm32_ops, nullptr, _width, _height, \
                             PBL_DISPLAY_FORMAT_MONO, false),                                    \
  };                                                                                              \
  PBL_DEVICE_REGISTER(sym, &sym.disp.dev)

/** @} */
