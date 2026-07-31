#include "clock.h"

#include "canvas.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "font_5x7.h"
#include "settings.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "clock";

esp_err_t kaleidobox_clock_init(void) {
  kaleidobox_clock_apply_tz();
  return ESP_OK;
}

void kaleidobox_clock_apply_tz(void) {
  const char *tz = kaleidobox_nvs_get_clock_tz();
  setenv("TZ", (tz && tz[0]) ? tz : "UTC0", 1);
  tzset();
}

void kaleidobox_clock_start_sntp(void) {
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, kaleidobox_nvs_get_ntp_server());
  esp_sntp_init();
  ESP_LOGI(TAG, "SNTP sync started against %s", kaleidobox_nvs_get_ntp_server());
}

void kaleidobox_clock_overlay(uint8_t *frame) {
  uint8_t mode = kaleidobox_nvs_get_clock_mode();
  if (mode == 0) {
    return; // off
  }

  time_t now = time(NULL);
  struct tm tm_now;
  localtime_r(&now, &tm_now);

  bool is_24h = kaleidobox_nvs_get_clock_24h();
  int hour = tm_now.tm_hour;
  if (!is_24h) {
    hour = hour % 12;
    if (hour == 0) {
      hour = 12;
    }
  }
  char text[6]; // "HH:MM" + NUL, or "H:MM" + NUL for 12h - both fit
  snprintf(text, sizeof(text), is_24h ? "%02d:%02d" : "%d:%02d", hour,
           tm_now.tm_min);

  int scale = kaleidobox_nvs_get_clock_scale();
  int y = (CANVAS_HEIGHT - 7 * scale) / 2;

  uint32_t color = kaleidobox_nvs_get_clock_color();
  uint8_t fg_r = (uint8_t)(color >> 16);
  uint8_t fg_g = (uint8_t)(color >> 8);
  uint8_t fg_b = (uint8_t)color;

  if (mode == 1) {
    // Outline: user-chosen colored digits with a 2px black halo hugging
    // each stroke (not a bounding rectangle - user found a hard
    // rectangle competed visually with the kaleidoscope pattern), plus
    // any hole fully enclosed by the digit's own strokes (e.g. "0"'s
    // counter) flood-filled black too so the live pattern can't leak
    // through it.
    kaleidobox_font_outline_centered_to_buffer(frame, y, text, 0, 0, 0, fg_r,
                                               fg_g, fg_b, scale, 2);
  } else if (mode == 2) {
    // Default: user-chosen colored digit-shapes punched straight into
    // the live pattern, no halo/plate.
    kaleidobox_font_draw_text_centered_to_buffer(frame, y, text, fg_r, fg_g,
                                                 fg_b, scale);
  } else if (mode == 4) {
    // Rectangle: solid black plate behind user-chosen colored digits -
    // the original request before the plate got dropped in favor of
    // outline mode; kept as its own explicit option now.
    kaleidobox_font_rect_centered_to_buffer(frame, y, text, 0, 0, 0, fg_r,
                                            fg_g, fg_b, scale);
  } else {
    // See-through (mode 3): digit strokes stay fully transparent (the
    // live pattern shows through them) with a fixed black outline -
    // any other outline color was reported unreadable - scaled to
    // 1px/2px with clock_scale (a fixed 2px halo swallowed a 1x glyph
    // whole), plus the gaps between adjacent digits filled black too
    // (those aren't "enclosed" by any one glyph's strokes, so they were
    // leaking distracting scraps of the live pattern between digits).
    kaleidobox_font_seethrough_centered_to_buffer(frame, y, text, scale);
  }
}
