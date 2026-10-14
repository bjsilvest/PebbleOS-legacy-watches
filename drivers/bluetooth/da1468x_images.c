/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

// Embeds the locally built DA1468x stage 2 and controller images.

#include "da1468x_image_paths.h"

#define DA1468X_INCBIN(name, path)          \
  __asm__(".section .rodata.da1468x_" #name \
          ", \"a\"\n"                       \
          ".balign 4\n"                     \
          ".global da1468x_" #name          \
          "_start\n"                        \
          "da1468x_" #name                  \
          "_start:\n"                       \
          ".incbin \"" path                 \
          "\"\n"                            \
          ".global da1468x_" #name          \
          "_end\n"                          \
          "da1468x_" #name                  \
          "_end:\n"                         \
          ".previous\n")

DA1468X_INCBIN(stage2, DA1468X_STAGE2_PATH);
DA1468X_INCBIN(controller, DA1468X_CONTROLLER_PATH);
