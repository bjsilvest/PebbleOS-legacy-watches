/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#ifdef CONFIG_LAUNCHER_LEGACY
#include <apps/system/launcher/legacy/launcher.h>
#else
#include <apps/system/launcher/default/launcher.h>
#endif
#include <process_management/pebble_process_md.h>

#define RETURN_TIMEOUT_TICKS (5 * RTC_TICKS_HZ)

const PebbleProcessMd *launcher_menu_app_get_app_info(void);
