# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

set(sources
  api.c
  app_db.c
  contacts_db.c
  endpoint.c
  endpoint2.c
  endpoint_private.c
  ios_notif_pref_db.c
  notif_db.c
  pin_db.c
  prefs_db.c
  reminder_db.c
  settings_blob_db.c
  sync.c
  sync_util.c
  timeline_item_storage.c
  util.c
  watch_app_prefs_db.c
)
pbl_library()
pbl_library_sources(${sources})
pbl_library_sources_ifdef(CONFIG_SERVICE_ACTIVITY health_db.c)
pbl_library_sources_ifdef(CONFIG_SERVICE_APP_GLANCES app_glance_db.c)
pbl_library_sources_ifdef(CONFIG_SERVICE_WEATHER weather_db.c)
