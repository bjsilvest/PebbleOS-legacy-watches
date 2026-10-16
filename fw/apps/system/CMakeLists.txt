# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_SHELL_SDK)
  set(source)
  file(GLOB _glob0 CONFIGURE_DEPENDS timeline/*.c)
  list(APPEND source ${_glob0})
  file(GLOB_RECURSE _glob1 CONFIGURE_DEPENDS launcher/default/*.c)
  list(APPEND source ${_glob1})
  list(APPEND source app_fetch_ui.c watchfaces.c)
else()
  file(GLOB_RECURSE source CONFIGURE_DEPENDS *.c)
endif()
if(NOT CONFIG_SERVICE_ACTIVITY)
  # The Sports app (PebbleKit Sports) stays; it shares the workout screens
  list(FILTER source EXCLUDE REGEX "/health/[^/]*\\.c$")
  list(FILTER source EXCLUDE REGEX "/workout/(countdown|data|selection|workout)\\.c$")
  list(FILTER source EXCLUDE REGEX "/(launcher/default/app_glance_workout|settings/health)\\.c$")
endif()
# Reminders are created by dictation
if(NOT CONFIG_SERVICE_VOICE)
  list(FILTER source EXCLUDE REGEX "/reminders/[^/]*\\.c$")
endif()
if(NOT CONFIG_SERVICE_WEATHER)
  list(FILTER source EXCLUDE REGEX "/(weather/[^/]*|launcher/default/app_glance_weather)\\.c$")
endif()
pbl_library()
pbl_library_sources(${source})
