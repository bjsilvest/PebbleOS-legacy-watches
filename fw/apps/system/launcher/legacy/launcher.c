/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

// The plain app list of the original Pebble, for builds without app glances.

#include "launcher.h"

#include <pbl/services/battery/battery_state.h>
#include <pbl/services/bluetooth/bluetooth_ctl.h>
#include <pbl/services/comm_session/session.h>
#include <pbl/services/notifications/alerts_private.h>
#include <pbl/services/notifications/do_not_disturb.h>

#include <applib/app.h>
#include <applib/event_service_client.h>
#include <applib/graphics/graphics.h>
#include <applib/ui/app_window_stack.h>
#include <applib/ui/kino/kino_layer.h>
#include <applib/ui/ui.h>
#include <apps/system/launcher/launcher.h>
#include <kernel/events.h>
#include <kernel/pbl_malloc.h>
#include <process_management/app_manager.h>
#include <process_management/app_menu_data_source.h>
#include <process_state/app_state/app_state.h>
#include <resource/resource_ids.auto.h>
#include <shell/normal/app_idle_timeout.h>
#include <shell/prefs.h>

typedef struct LauncherMenuData {
  Window window;
  StatusBarLayer status_bar;
  MenuLayer menu_layer;
  AppMenuDataSource data_source;

  EventServiceInfo battery_state_event_info;
  EventServiceInfo do_not_disturb_event_info;
  EventServiceInfo pebble_app_event_info;

  KinoLayer connectivity_icon;
  uint32_t connectivity_icon_id;
  Layer battery_layer;
} LauncherMenuData;

typedef struct LauncherMenuPersistedData {
  int scroll_offset_y;
  int menu_index_row;
  bool valid;
  RtcTicks leave_time;
} LauncherMenuPersistedData;

static LauncherMenuPersistedData s_launcher_menu_persisted_data;

///////////////////
// Status bar

static uint32_t prv_get_connectivity_icon_id(void) {
  const bool connected = (comm_session_get_system_session() != nullptr);
  if (bt_ctl_is_airplane_mode_on()) {
    return RESOURCE_ID_CONNECTIVITY_BLUETOOTH_AIRPLANE_MODE;
  } else if (do_not_disturb_is_active()) {
    return RESOURCE_ID_CONNECTIVITY_BLUETOOTH_DND;
  } else if (!connected) {
    return RESOURCE_ID_CONNECTIVITY_BLUETOOTH_DISCONNECTED;
  } else if (alerts_get_mask() == AlertMaskPhoneCalls) {
    return RESOURCE_ID_CONNECTIVITY_BLUETOOTH_CALLS_ONLY;
  } else if (alerts_get_mask() == AlertMaskAllOn) {
    return RESOURCE_ID_CONNECTIVITY_BLUETOOTH_CONNECTED;
  }
  return RESOURCE_ID_INVALID;
}

// Drawn rather than taken from icons, so no platform needs extra resources
static void prv_battery_layer_update(Layer *layer, GContext *ctx) {
  const BatteryChargeState state = battery_get_charge_state();
  const GRect bounds = layer->bounds;
  const GRect body = GRect(0, (bounds.size.h - 7) / 2, 14, 7);

  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_draw_rect(ctx, &body);
  graphics_fill_rect(ctx, &GRect(body.origin.x + body.size.w, body.origin.y + 2, 1, 3));
  const int16_t fill_w = (int16_t)((body.size.w - 4) * MIN(state.charge_percent, 100) / 100);
  graphics_fill_rect(ctx, &GRect(body.origin.x + 2, body.origin.y + 2, MAX(fill_w, 1), 3));
  if (state.is_charging || state.is_plugged) {
    // Plug mark left of the battery
    graphics_fill_rect(ctx, &GRect(body.origin.x - 4, body.origin.y + 1, 2, 5));
  }
}

static void prv_reload_status_bar_icons(LauncherMenuData *data) {
  const uint32_t icon_id = prv_get_connectivity_icon_id();
  if (data->connectivity_icon_id != icon_id) {
    data->connectivity_icon_id = icon_id;
    kino_layer_set_reel_with_resource(&data->connectivity_icon, icon_id);
  }
  layer_mark_dirty(&data->battery_layer);
}

///////////////////
// Events

static void prv_event_handler(PebbleEvent *e, void *context) {
  prv_reload_status_bar_icons(context);
}

static void prv_subscribe_to_event(EventServiceInfo *result, PebbleEventType type,
                                   void *callback_context) {
  *result = (EventServiceInfo){
    .type = type,
    .handler = prv_event_handler,
    .context = callback_context,
  };
  event_service_client_subscribe(result);
}

///////////////////
// AppMenuDataSource callbacks

static bool prv_app_filter_callback(struct AppMenuDataSource *const source,
                                    AppInstallEntry *entry) {
  // Skip watchfaces and hidden apps
  return !app_install_entry_is_watchface(entry) && !app_install_entry_is_hidden(entry);
}

static void prv_data_changed(void *context) {
  LauncherMenuData *data = context;
  menu_layer_reload_data(&data->menu_layer);
}

///////////////////
// MenuLayer callbacks

static void prv_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index,
                                LauncherMenuData *data) {
  AppMenuNode *node = app_menu_data_source_get_node_at_index(&data->data_source, cell_index->row);
  app_manager_put_launch_app_event(&(AppLaunchEventConfig){
    .id = node->install_id,
    .common.reason = APP_LAUNCH_USER,
    .common.button = BUTTON_ID_SELECT,
  });
}

static uint16_t prv_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index,
                                          LauncherMenuData *data) {
  return app_menu_data_source_get_count(&data->data_source);
}

static void prv_draw_row_callback(GContext *ctx, Layer *cell_layer, MenuIndex *cell_index,
                                  LauncherMenuData *data) {
  app_menu_data_source_draw_row(&data->data_source, ctx, cell_layer, cell_index);
}

///////////////////
// Window callbacks

static void prv_window_load(Window *window) {
  LauncherMenuData *data = window_get_user_data(window);
  GRect bounds = window->layer.bounds;

  status_bar_layer_init(&data->status_bar);
  status_bar_layer_set_colors(&data->status_bar, GColorBlack, GColorWhite);
  layer_add_child(&window->layer, status_bar_layer_get_layer(&data->status_bar));

  const int16_t icon_width = 20;
  const int16_t icon_padding = 6;
  kino_layer_init(&data->connectivity_icon,
                  &GRect(icon_padding, 0, icon_width, STATUS_BAR_LAYER_HEIGHT));
  kino_layer_set_alignment(&data->connectivity_icon, GAlignLeft);
  layer_add_child(&window->layer, kino_layer_get_layer(&data->connectivity_icon));

  layer_init(&data->battery_layer, &GRect(bounds.size.w - icon_width - icon_padding + 4, 0,
                                          icon_width, STATUS_BAR_LAYER_HEIGHT));
  layer_set_update_proc(&data->battery_layer, prv_battery_layer_update);
  layer_add_child(&window->layer, &data->battery_layer);

  prv_reload_status_bar_icons(data);

  bounds = grect_inset(bounds, GEdgeInsets(STATUS_BAR_LAYER_HEIGHT, 0, 0, 0));

  MenuLayer *menu_layer = &data->menu_layer;
  menu_layer_init(menu_layer, &bounds);
  app_menu_data_source_init(&data->data_source,
                            &(AppMenuDataSourceCallbacks){
                              .changed = prv_data_changed,
                              .filter = prv_app_filter_callback,
                            },
                            data);
  app_menu_data_source_enable_icons(&data->data_source,
                                    RESOURCE_ID_MENU_LAYER_GENERIC_WATCHAPP_ICON);

  menu_layer_set_callbacks(
      menu_layer, data,
      &(MenuLayerCallbacks){
        .get_num_rows = (MenuLayerGetNumberOfRowsInSectionsCallback)prv_get_num_rows_callback,
        .draw_row = (MenuLayerDrawRowCallback)prv_draw_row_callback,
        .select_click = (MenuLayerSelectCallback)prv_select_callback,
      });
  menu_layer_set_scroll_wrap_around(menu_layer, shell_prefs_get_menu_scroll_wrap_around_enable());
  menu_layer_set_scroll_vibe_on_wrap(
      menu_layer, shell_prefs_get_menu_scroll_vibe_behavior() == MenuScrollVibeOnWrapAround);
  menu_layer_set_scroll_vibe_on_blocked(
      menu_layer, shell_prefs_get_menu_scroll_vibe_behavior() == MenuScrollVibeOnLocked);
  menu_layer_set_click_config_onto_window(menu_layer, window);
  layer_add_child(&window->layer, menu_layer_get_layer(menu_layer));

  scroll_layer_set_shadow_hidden(&data->menu_layer.scroll_layer, true);

  if (s_launcher_menu_persisted_data.valid) {
    menu_layer_set_selected_index(&data->menu_layer,
                                  MenuIndex(0, s_launcher_menu_persisted_data.menu_index_row),
                                  MenuRowAlignNone, false);
    scroll_layer_set_content_offset(&data->menu_layer.scroll_layer,
                                    GPoint(0, s_launcher_menu_persisted_data.scroll_offset_y),
                                    false);
  } else {
    // Settings is at the top; start on the entry below it
    menu_layer_set_selected_index(&data->menu_layer, MenuIndex(0, 1), MenuRowAlignNone, false);
  }

  prv_subscribe_to_event(&data->battery_state_event_info, PEBBLE_BATTERY_STATE_CHANGE_EVENT, data);
  prv_subscribe_to_event(&data->do_not_disturb_event_info, PEBBLE_DO_NOT_DISTURB_EVENT, data);
  prv_subscribe_to_event(&data->pebble_app_event_info, PEBBLE_COMM_SESSION_EVENT, data);
}

static void prv_window_unload(Window *window) {
  LauncherMenuData *data = window_get_user_data(window);

  event_service_client_unsubscribe(&data->battery_state_event_info);
  event_service_client_unsubscribe(&data->do_not_disturb_event_info);
  event_service_client_unsubscribe(&data->pebble_app_event_info);

  kino_layer_deinit(&data->connectivity_icon);
  layer_deinit(&data->battery_layer);
  status_bar_layer_deinit(&data->status_bar);

  // Remember the position to restore it when coming back soon
  s_launcher_menu_persisted_data = (LauncherMenuPersistedData){
    .valid = true,
    .scroll_offset_y = scroll_layer_get_content_offset(&data->menu_layer.scroll_layer).y,
    .menu_index_row = menu_layer_get_selected_index(&data->menu_layer).row,
    .leave_time = rtc_get_ticks(),
  };

  menu_layer_deinit(&data->menu_layer);
  app_menu_data_source_deinit(&data->data_source);
}

static void prv_push_window(void) {
  LauncherMenuData *data = app_zalloc_check(sizeof(LauncherMenuData));
  app_state_set_user_data(data);

  Window *window = &data->window;
  window_init(window, WINDOW_NAME("Launcher Menu"));
  window_set_user_data(window, data);
  window_set_window_handlers(window, &(WindowHandlers){
                                       .load = prv_window_load,
                                       .unload = prv_window_unload,
                                     });
  app_window_stack_push(window, false /* animated */);
}

///////////////////
// App boilerplate

static void prv_main(void) {
  const LauncherMenuArgs *args = app_manager_get_task_context()->args;
  if (args && args->reset_scroll &&
      (s_launcher_menu_persisted_data.leave_time + RETURN_TIMEOUT_TICKS) <= rtc_get_ticks()) {
    s_launcher_menu_persisted_data.valid = false;
  }

  prv_push_window();

  app_idle_timeout_start(APP_IDLE_TIMEOUT_LAUNCHER_MS);

  app_event_loop();
}

const PebbleProcessMd *launcher_menu_app_get_app_info(void) {
  static const PebbleProcessMdSystem s_launcher_menu_app_info = {
    .common =
        {
          .main_func = prv_main,
          // UUID: dec0424c-0625-4878-b1f2-147e57e83688
          .uuid =
              {0xde, 0xc0, 0x42, 0x4c, 0x06, 0x25, 0x48, 0x78, 0xb1, 0xf2, 0x14, 0x7e, 0x57, 0xe8,
               0x36, 0x88},
          .visibility = ProcessVisibilityHidden,
        },
    .name = "Launcher",
  };
  return (const PebbleProcessMd *)&s_launcher_menu_app_info;
}
