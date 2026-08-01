#include "display_rotation.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "font_5x7.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "matrix.h"
#include "panel_takeover.h"
#include "printspy.h"
#include "settings.h"
#include "weather.h"

static const char *TAG = "display_rotation";

#define TICK_MS 1000

typedef enum { SLOT_CLOCK, SLOT_PRINTER, SLOT_WEATHER, SLOT_MESSAGE } slot_kind_t;

static slot_kind_t g_slot = SLOT_CLOCK;
static int g_printer_idx = 0;
static int64_t g_slot_start_us = 0;
// Tracks whether the message is currently showing as the exclusive
// static override (see enter_static_message()/rotation_task() below) -
// separate from g_slot, since static mode bypasses the normal slot
// machine entirely rather than being one more slot within it.
static bool g_static_message_active = false;

static void render_message(void) {
  uint32_t color = kaleidobox_nvs_get_message_color();
  kaleidobox_matrix_clear();
  kaleidobox_font_draw_text_wrapped(kaleidobox_nvs_get_message_text(),
                                    (uint8_t)(color >> 16), (uint8_t)(color >> 8),
                                    (uint8_t)color);
}

void kaleidobox_display_rotation_message_changed(void) {
  if (g_static_message_active) {
    render_message();
  }
}

static void enter_clock(void) {
  if (kaleidobox_panel_takeover_active()) {
    kaleidobox_panel_takeover_end();
  }
  g_slot = SLOT_CLOCK;
  g_slot_start_us = esp_timer_get_time();
}

static bool enter_printer(int idx) {
  if (!kaleidobox_panel_takeover_active() && !kaleidobox_panel_takeover_begin()) {
    return false; // recent draw activity, or lost the race - try again next tick
  }
  kaleidobox_printspy_render_printing(idx);
  g_slot = SLOT_PRINTER;
  g_printer_idx = idx;
  g_slot_start_us = esp_timer_get_time();
  return true;
}

static bool enter_weather(void) {
  // Fetch BEFORE touching the panel - a failed/unconfigured fetch (no
  // key/location set, network hiccup, ...) must never stop kaleidoscope
  // at all. Doing panel_takeover_begin() speculatively and only
  // bailing out afterward was a real bug: every failed fetch still did
  // a full takeover begin/end pair, which stops and restarts
  // kaleidoscope from its static source buffer - visibly a blink even
  // though the image itself never changes, confirmed live ("blinking
  // every 10 seconds or so but stays on the same image").
  if (!kaleidobox_weather_fetch()) {
    return false;
  }
  if (!kaleidobox_panel_takeover_active() && !kaleidobox_panel_takeover_begin()) {
    return false; // recent draw activity, or lost the race - the fetch just goes unused
  }
  kaleidobox_weather_render_last();
  g_slot = SLOT_WEATHER;
  g_slot_start_us = esp_timer_get_time();
  return true;
}

// Rotation-participant entry - only when message_static is off (static
// mode is handled entirely separately, see rotation_task() below).
// Unlike weather/printer there's nothing to fetch; just checks there's
// actually a message to show before taking over the panel.
static bool enter_message(void) {
  if (!kaleidobox_nvs_get_message_text()[0]) {
    return false;
  }
  if (!kaleidobox_panel_takeover_active() && !kaleidobox_panel_takeover_begin()) {
    return false; // recent draw activity, or lost the race - try again next tick
  }
  render_message();
  g_slot = SLOT_MESSAGE;
  g_slot_start_us = esp_timer_get_time();
  return true;
}

// Moves from the current slot to whatever comes next, skipping
// anything not currently applicable. Order is always clock -> each
// printing printer, in turn -> weather -> message -> clock ... - every
// lap starts and ends at clock, so panel_takeover only ever begins on
// the way out of clock and ends on the way back into it.
static void advance(void) {
  int printer_count = kaleidobox_printspy_printing_count();
  bool weather_on = kaleidobox_nvs_get_weather_enabled();
  bool message_on = kaleidobox_nvs_get_message_enabled();

  if (g_slot == SLOT_CLOCK) {
    if (printer_count > 0 && enter_printer(0)) {
      return;
    }
    if (weather_on && enter_weather()) {
      return;
    }
    if (message_on && enter_message()) {
      return;
    }
    // Nothing applicable - stay on clock, but reset the timer so this
    // isn't retried every single tick.
    g_slot_start_us = esp_timer_get_time();
    return;
  }

  if (g_slot == SLOT_PRINTER) {
    int next = g_printer_idx + 1;
    if (next < printer_count && enter_printer(next)) {
      return;
    }
    if (weather_on && enter_weather()) {
      return;
    }
    if (message_on && enter_message()) {
      return;
    }
    enter_clock();
    return;
  }

  if (g_slot == SLOT_WEATHER) {
    if (message_on && enter_message()) {
      return;
    }
    enter_clock();
    return;
  }

  // SLOT_MESSAGE
  enter_clock();
}

// Each slot has its own independent dwell time (see settings.h) - the
// user wanted these tunable separately, e.g. a shorter printer-progress
// refresh than a weather screen that needs time to actually read.
static uint16_t current_slot_secs(void) {
  switch (g_slot) {
  case SLOT_CLOCK:
    return kaleidobox_nvs_get_clock_secs();
  case SLOT_PRINTER:
    return kaleidobox_nvs_get_printer_secs();
  case SLOT_WEATHER:
    return kaleidobox_nvs_get_weather_secs();
  case SLOT_MESSAGE:
  default:
    return kaleidobox_nvs_get_message_secs();
  }
}

static void rotation_task(void *arg) {
  (void)arg;
  ESP_LOGI(TAG, "display_rotation running");
  g_slot_start_us = esp_timer_get_time();
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(TICK_MS));

    // Static message mode fully overrides the normal slot machine
    // rather than being one more slot within it - checked first, every
    // tick, ahead of the regular clock/printer/weather/message cycle
    // below. Reuses panel_takeover.h for the same draw-activity guard
    // and kaleidoscope stop/restore every other takeover already gets,
    // just held indefinitely instead of for a fixed dwell time.
    bool want_static = kaleidobox_nvs_get_message_static() &&
                       kaleidobox_nvs_get_message_text()[0];
    if (want_static) {
      if (!g_static_message_active) {
        if (kaleidobox_panel_takeover_active() ||
            kaleidobox_panel_takeover_begin()) {
          render_message();
          g_static_message_active = true;
        }
        // else: recent draw activity, or lost the race - try again next tick
      }
      continue;
    }
    if (g_static_message_active) {
      // Static mode just turned off - enter_clock() ends the takeover
      // this same static mode began and restores whatever was showing.
      g_static_message_active = false;
      enter_clock();
      continue;
    }

    uint16_t secs = current_slot_secs();
    if (secs == 0) {
      continue; // 0 = stay in this slot indefinitely
    }
    if (esp_timer_get_time() - g_slot_start_us >= (int64_t)secs * 1000000) {
      advance();
    }
  }
}

esp_err_t kaleidobox_display_rotation_init(void) {
  // 8192, not a smaller/typical bg-task size - this task calls straight
  // into kaleidobox_weather_fetch()'s TLS fetch (see weather.c), and
  // TLS handshakes are stack-hungry; matches the dedicated stack size
  // the old standalone weather task used for the same reason.
  //
  // Pinned to core 1, same reasoning gallery.c's own bg task already
  // documents: WiFi's own tasks (and httpd, unpinned by default) run on
  // core 0, and this task's weather TLS handshake is a multi-second
  // blocking call - left unpinned, the scheduler was free to land it on
  // core 0 too, stalling page loads for its duration every time a
  // weather slot's fetch overlapped an HTTP request (confirmed live:
  // "why are things loading super slow now when I refresh the page").
  xTaskCreatePinnedToCore(rotation_task, "display_rot", 8192, NULL,
                          tskIDLE_PRIORITY + 1, NULL, 1);
  ESP_LOGI(TAG, "display_rotation_init");
  return ESP_OK;
}
