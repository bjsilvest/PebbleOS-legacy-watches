/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

//! Timeline as a plain list of pins, for watches without the flash for the animated timeline.
//! Select opens the same pin card as the full timeline.

#include "pin_window.h"
#include "timeline.h"

#include <pbl/drivers/rtc.h>
#include <pbl/services/blob_db/pin_db.h>
#include <pbl/services/clock.h>
#include <pbl/services/evented_timer.h>
#include <pbl/services/i18n/i18n.h>
#include <pbl/services/time.h>
#include <pbl/services/timeline/timeline_layout.h>
#include <pbl/services/timeline/timeline_resources.h>
#include <pbl/util/uuid.h>

#include <applib/app.h>
#include <applib/graphics/gdraw_command_image.h>
#include <applib/graphics/utf8.h>
#include <applib/ui/app_window_stack.h>
#include <applib/ui/menu_cell_layer.h>
#include <applib/ui/ui.h>
#include <kernel/pbl_malloc.h>
#include <process_management/app_install_manager.h>
#include <process_management/process_manager.h>
#include <resource/resource_ids.auto.h>
#include <resource/timeline_resource_ids.auto.h>
#include <shell/normal/watchface.h>
#include <shell/system_theme.h>
#include <syscall/syscall.h>

#define TIMELINE_LITE_MAX_PINS  32
#define TIMELINE_LITE_TITLE_LEN 32

typedef struct {
  Uuid id;
  //! Midnight of the day the pin is listed on
  time_t current_day;
  time_t pin_time;
  bool all_day;
  AppResourceInfo icon;
  char title[TIMELINE_LITE_TITLE_LEN];
} TimelineLiteEntry;

typedef struct {
  Window window;
  MenuLayer menu_layer;
  TimelinePinWindow pin_window;
  //! Pin shown in the card, owned here because the card only points to it
  TimelineItem card_item;
  EventServiceInfo blobdb_event_info;
  EventedTimerID inactive_timer_id;
  TimelineDirection direction;
  uint16_t num_entries;
  TimelineLiteEntry entries[TIMELINE_LITE_MAX_PINS];
} TimelineLiteData;

static TimelineLiteData *s_data;

/////////////////////////////////////
// Pins
/////////////////////////////////////

static void prv_fill_entry(TimelineLiteEntry *entry, TimelineIterState *state) {
  TimelineItem *pin = &state->pin;
  TimelineLayoutInfo info;
  timeline_layout_init_info(&info, pin, state->current_day);

  *entry = (TimelineLiteEntry){
    .id = pin->header.id,
    .current_day = state->current_day,
    .pin_time = info.pin_time,
    .all_day = info.all_day,
  };
  utf8_truncate_with_ellipsis(attribute_get_string(&pin->attr_list, AttributeIdTitle, ""),
                              entry->title, sizeof(entry->title));

  Uuid app_id;
  const bool has_app_id = timeline_get_originator_id(pin, &app_id);
  const TimelineResourceInfo res = {
    .res_id = timeline_layout_get_icon_resource_id(LayoutLayerModePinnedThin, &pin->attr_list,
                                                   TimelineResourceSizeTiny,
                                                   TIMELINE_RESOURCE_NOTIFICATION_FLAG),
    .app_id = has_app_id ? &app_id : nullptr,
    .fallback_id = TIMELINE_RESOURCE_NOTIFICATION_FLAG,
  };
  timeline_resources_get_id(&res, TimelineResourceSizeTiny, &entry->icon);
}

static void prv_load_entries(TimelineLiteData *data) {
  data->num_entries = 0;

  TimelineNode *timeline = nullptr;
  Iterator iter;
  TimelineIterState state = {};
  timeline_init(&timeline);
  const status_t rv = timeline_iter_init(&iter, &state, &timeline, data->direction, rtc_get_time());
  if (rv == S_SUCCESS) {
    do {
      prv_fill_entry(&data->entries[data->num_entries++], &state);
    } while (data->num_entries < TIMELINE_LITE_MAX_PINS && iter_next(&iter));
  }
  timeline_iter_deinit(&iter, &state, &timeline);
}

static int prv_find_entry(TimelineLiteData *data, const Uuid *id) {
  for (int i = 0; i < data->num_entries; i++) {
    if (uuid_equal(&data->entries[i].id, id)) {
      return i;
    }
  }
  return -1;
}

/////////////////////////////////////
// Card
/////////////////////////////////////

static bool prv_card_shown(TimelineLiteData *data) {
  return app_window_stack_contains_window(&data->pin_window.window);
}

static void prv_push_card(TimelineLiteData *data, const Uuid *id, time_t current_day,
                          bool animated) {
  TimelineItem item;
  if (pin_db_get(id, &item) != S_SUCCESS) {
    return;
  }

  // Finish the previous card's pop first, it still draws the old pin
  if (data->pin_window.pop_animation) {
    animation_unschedule(data->pin_window.pop_animation);
    data->pin_window.pop_animation = nullptr;
  }
  timeline_item_free_allocated_buffer(&data->card_item);
  data->card_item = item;

  timeline_pin_window_init(&data->pin_window, &data->card_item, current_day);
  app_window_stack_push(&data->pin_window.window, animated);
}

/////////////////////////////////////
// List
/////////////////////////////////////

static void prv_inactive_timer_callback(void *context) {
  watchface_launch_default(nullptr);
}

static void prv_inactive_timer_refresh(TimelineLiteData *data) {
  static const uint32_t INACTIVITY_TIMEOUT_MS = 30 * 1000;
  data->inactive_timer_id = evented_timer_register_or_reschedule(
      data->inactive_timer_id, INACTIVITY_TIMEOUT_MS, prv_inactive_timer_callback, data);
}

static uint16_t prv_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *context) {
  TimelineLiteData *data = context;
  // A single row says there are no events
  return MAX(data->num_entries, 1);
}

static int16_t prv_get_cell_height(MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
  return menu_cell_basic_cell_height();
}

static void prv_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index,
                         void *context) {
  TimelineLiteData *data = context;
  if (data->num_entries == 0) {
    menu_cell_basic_draw(ctx, cell_layer, i18n_get("No events", data), nullptr, nullptr);
    return;
  }

  const TimelineLiteEntry *entry = &data->entries[cell_index->row];
  const GRect bounds = cell_layer->bounds;
  const int16_t icon_left_margin = menu_cell_basic_horizontal_inset();

  GDrawCommandImage *icon =
      gdraw_command_image_create_with_resource_system(entry->icon.res_app_num, entry->icon.res_id);
  if (icon) {
    GRect box = bounds;
    box.origin.x += icon_left_margin;
    GRect icon_rect = (GRect){.size = gdraw_command_image_get_bounds_size(icon)};
    grect_align(&icon_rect, &box, GAlignLeft, false /* clip */);
    gdraw_command_image_draw(ctx, icon, icon_rect.origin);
    gdraw_command_image_destroy(icon);
  }

  // "Tomorrow 9:00 AM", or the day alone for all-day pins
  char subtitle[TIME_STRING_REQUIRED_LENGTH * 2];
  size_t len = 0;
  if (entry->current_day != time_util_get_midnight_of(rtc_get_time())) {
    clock_get_friendly_date(subtitle, sizeof(subtitle), entry->current_day);
    len = strlen(subtitle);
  }
  if (entry->all_day) {
    if (len == 0) {
      i18n_get_with_buffer("All day", subtitle, sizeof(subtitle));
    }
  } else {
    if (len > 0) {
      subtitle[len++] = ' ';
    }
    clock_copy_time_string_timestamp(subtitle + len, sizeof(subtitle) - len, entry->pin_time);
  }

  // Inset the bounds so the text doesn't draw over the icon
  Layer *mutable_cell_layer = (Layer *)cell_layer;
  mutable_cell_layer->bounds =
      grect_inset(bounds, GEdgeInsets(0, 5, 0, icon_left_margin + ATTRIBUTE_ICON_TINY_SIZE_PX));
  menu_cell_basic_draw_custom(
      ctx, cell_layer, system_theme_get_font(TextStyleFont_MenuCellTitle), entry->title,
      nullptr /* value_font */, nullptr /* value */, system_theme_get_font(TextStyleFont_Caption),
      subtitle, nullptr /* icon */, false /* icon_on_right */, GTextOverflowModeTrailingEllipsis);
  mutable_cell_layer->bounds = bounds;
}

static void prv_select_click(MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
  TimelineLiteData *data = context;
  if (data->num_entries == 0) {
    return;
  }
  const TimelineLiteEntry *entry = &data->entries[cell_index->row];
  prv_push_card(data, &entry->id, entry->current_day, true /* animated */);
}

static void prv_selection_changed(MenuLayer *menu_layer, MenuIndex new_index, MenuIndex old_index,
                                  void *context) {
  prv_inactive_timer_refresh(context);
}

static void prv_blobdb_event_handler(PebbleEvent *event, void *context) {
  TimelineLiteData *data = context;
  PebbleBlobDBEvent *blobdb_event = &event->blob_db;
  if (blobdb_event->db_id != BlobDBIdPins) {
    return;
  }

  if (blobdb_event->type == BlobDBEventTypeDelete && prv_card_shown(data) &&
      uuid_equal((Uuid *)blobdb_event->key, &data->card_item.header.id)) {
    app_window_stack_remove(&data->pin_window.window, false /* animated */);
  }
  prv_load_entries(data);
  menu_layer_reload_data(&data->menu_layer);
}

static void prv_window_load(Window *window) {
  TimelineLiteData *data = window_get_user_data(window);
  MenuLayer *menu_layer = &data->menu_layer;
  menu_layer_init(menu_layer, &window->layer.bounds);
  menu_layer_set_callbacks(menu_layer, data,
                           &(MenuLayerCallbacks){
                             .get_num_rows = prv_get_num_rows,
                             .get_cell_height = prv_get_cell_height,
                             .draw_row = prv_draw_row,
                             .select_click = prv_select_click,
                             .selection_changed = prv_selection_changed,
                           });
  menu_layer_set_click_config_onto_window(menu_layer, window);
  layer_add_child(&window->layer, menu_layer_get_layer(menu_layer));
}

static void prv_window_appear(Window *window) {
  prv_inactive_timer_refresh(window_get_user_data(window));
}

static void prv_window_disappear(Window *window) {
  TimelineLiteData *data = window_get_user_data(window);
  evented_timer_cancel(data->inactive_timer_id);
  data->inactive_timer_id = EVENTED_TIMER_INVALID_ID;
}

static void prv_window_unload(Window *window) {
  TimelineLiteData *data = window_get_user_data(window);
  menu_layer_deinit(&data->menu_layer);
}

/////////////////////////////////////
// Public API
/////////////////////////////////////

Animation *timeline_animate_back_from_card(void) {
  timeline_pin_window_pop(&s_data->pin_window);
  return nullptr;
}

// The pin view of the timeline layouts is only used by the full timeline
int16_t timeline_layer_get_icon_offset_y(void) {
  return 0;
}

uint16_t timeline_layer_get_ideal_sidebar_width(void) {
  return 0;
}

uint16_t timeline_layer_get_sidebar_arrow_width(void) {
  return 0;
}

/////////////////////////////////////
// App boilerplate
/////////////////////////////////////

static void prv_init(void) {
  TimelineLiteData *data = app_zalloc_check(sizeof(TimelineLiteData));
  s_data = data;

  const TimelineArgs *args = process_manager_get_current_process_args();
  Uuid app_uuid;
  sys_get_app_uuid(&app_uuid);
  if (uuid_equal(&app_uuid, &(Uuid)TIMELINE_PAST_UUID_INIT)) {
    data->direction = TimelineIterDirectionPast;
  } else if (args) {
    data->direction = args->direction;
  } else {
    data->direction = TimelineIterDirectionFuture;
  }

  // Launching into a pin opens its card over the list, in the pin's direction
  bool push_card = false;
  if (args && args->launch_into_pin && !uuid_is_invalid(&args->pin_id)) {
    TimelineItem pin;
    if (pin_db_get(&args->pin_id, &pin) == S_SUCCESS) {
      if (!args->stay_in_list_view) {
        data->direction = timeline_direction_for_item(&pin, nullptr, rtc_get_time());
        push_card = true;
      }
      timeline_item_free_allocated_buffer(&pin);
    }
  }

  prv_load_entries(data);

  data->blobdb_event_info = (EventServiceInfo){
    .type = PEBBLE_BLOBDB_EVENT,
    .handler = prv_blobdb_event_handler,
    .context = data,
  };
  event_service_client_subscribe(&data->blobdb_event_info);

  Window *window = &data->window;
  window_init(window, WINDOW_NAME("Timeline"));
  window_set_user_data(window, data);
  window_set_window_handlers(window, &(WindowHandlers){
                                       .load = prv_window_load,
                                       .appear = prv_window_appear,
                                       .disappear = prv_window_disappear,
                                       .unload = prv_window_unload,
                                     });
  app_window_stack_push(window, true /* animated */);

  if (args && args->launch_into_pin) {
    const int idx = prv_find_entry(data, &args->pin_id);
    if (idx >= 0) {
      menu_layer_set_selected_index(&data->menu_layer, MenuIndex(0, idx), MenuRowAlignCenter,
                                    false /* animated */);
      if (push_card) {
        prv_push_card(data, &args->pin_id, data->entries[idx].current_day, false /* animated */);
      }
    } else if (push_card) {
      prv_push_card(data, &args->pin_id, time_util_get_midnight_of(rtc_get_time()),
                    false /* animated */);
    }
  }
}

static void prv_deinit(void) {
  evented_timer_cancel(s_data->inactive_timer_id);
  event_service_client_unsubscribe(&s_data->blobdb_event_info);
  timeline_item_free_allocated_buffer(&s_data->card_item);
  i18n_free_all(s_data);
  app_free(s_data);
}

static void prv_main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}

const PebbleProcessMd *timeline_get_app_info() {
  static const PebbleProcessMdSystem s_app_md = {
    .common =
        {
          .main_func = prv_main,
          // uuid: 79C76B48-6111-4E80-8DEB-3119EEBEF33E
          .uuid = TIMELINE_UUID_INIT,
          .visibility = ProcessVisibilityQuickLaunch,
        },
    .name = i18n_noop("Timeline Future"),
  };
  return &s_app_md.common;
}

const PebbleProcessMd *timeline_past_get_app_info() {
  static const PebbleProcessMdSystem s_app_md = {
    .common =
        {
          .main_func = prv_main,
          .uuid = TIMELINE_PAST_UUID_INIT,
          .visibility = ProcessVisibilityQuickLaunch,
        },
    /// The title of Timeline Past in Quick Launch. If the translation is too long, cut out
    /// Timeline and only translate "Past".
    .name = i18n_noop("Timeline Past"),
  };
  return &s_app_md.common;
}

const PebbleProcessMd *timeline_full_get_app_info() {
  static const PebbleProcessMdSystem s_app_md = {
    .common =
        {
          .main_func = prv_main,
          .uuid = TIMELINE_FULL_UUID_INIT,
          .visibility = ProcessVisibilityShown,
        },
    .name = i18n_noop("Timeline"),
    .icon_resource_id = RESOURCE_ID_DAY_SEPARATOR_TINY,
  };
  return &s_app_md.common;
}
