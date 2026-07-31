#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t kaleidobox_nvs_init(void);

// Kaleidoscope mode settings - see main/kaleidoscope.h for what each
// controls. fold_count is the mirrored-wedge repeat count around the
// center (e.g. 6/8/12); motion_zoom toggles rotation-only vs
// rotation+zoom.
uint8_t kaleidobox_nvs_get_fold_count(void);
esp_err_t kaleidobox_nvs_set_fold_count(uint8_t count);

bool kaleidobox_nvs_get_motion_zoom(void);
esp_err_t kaleidobox_nvs_set_motion_zoom(bool enable);

// Draw-mode push behavior: instant (each pixel edit sent live over the
// /ws/draw WebSocket) vs draw-then-submit (client batches edits, one
// full-grid POST when done).
bool kaleidobox_nvs_get_instant_draw(void);
esp_err_t kaleidobox_nvs_set_instant_draw(bool enable);

// Gallery cycling: auto-advance (with interval_seconds) vs manual
// next/prev via the web app.
bool kaleidobox_nvs_get_gallery_auto_advance(void);
esp_err_t kaleidobox_nvs_set_gallery_auto_advance(bool enable);

uint16_t kaleidobox_nvs_get_gallery_interval_seconds(void);
esp_err_t kaleidobox_nvs_set_gallery_interval_seconds(uint16_t seconds);

// Whether kaleidoscope was running at last stop/start - kaleidoscope.c
// sets this on every start()/stop() call. Read at boot (see main.c) to
// resume the animation across a reboot instead of coming back up static.
bool kaleidobox_nvs_get_kaleido_running(void);
esp_err_t kaleidobox_nvs_set_kaleido_running(bool running);

// Panel brightness, 0-255 - see main/matrix.h. Applied at boot (main.c)
// and on every change via the HTTP API.
uint8_t kaleidobox_nvs_get_brightness(void);
esp_err_t kaleidobox_nvs_set_brightness(uint8_t brightness);

// Clock overlay (kaleidoscope mode only) - see main/clock.h. mode:
// 0=off, 1=outline (colored digits with a black halo hugging each
// stroke, enclosed holes like "0"'s counter flood-filled black too),
// 2=default (same colored digits, no halo - painted straight onto the
// live pattern), 3=seethrough (digit strokes stay fully transparent -
// the live pattern shows through them - with a fixed black outline,
// scaled 1px/2px with clock_scale, and the gaps between digits filled
// black too), 4=rectangle (solid black plate behind colored digits).
uint8_t kaleidobox_nvs_get_clock_mode(void);
esp_err_t kaleidobox_nvs_set_clock_mode(uint8_t mode);

// Packed 0x00RRGGBB - the digit color in solid/cutout modes, the halo
// outline color in see-through mode.
uint32_t kaleidobox_nvs_get_clock_color(void);
esp_err_t kaleidobox_nvs_set_clock_color(uint32_t color);

// Digit size multiplier (1-2) - see kaleidobox_font_draw_text_centered_to_buffer()'s
// scale param in font_5x7.h. Default 2. Capped at 2, not higher -
// "HH:MM"'s real (proportional) width already clips past the panel's
// 64px at 3x (user-confirmed on hardware: unreadable, not just tight).
uint8_t kaleidobox_nvs_get_clock_scale(void);
esp_err_t kaleidobox_nvs_set_clock_scale(uint8_t scale);

// true = 24h ("13:00"), false = 12h ("1:00", no AM/PM shown - the panel
// has no room for it at this text size). Default true.
bool kaleidobox_nvs_get_clock_24h(void);
esp_err_t kaleidobox_nvs_set_clock_24h(bool enable);

// NTP server hostname, default "pool.ntp.org". Getters return a pointer
// to static storage, not a copy - fine under the same single-writer
// assumption every setting here already makes (settings only change via
// HTTP handlers, which run one at a time on the httpd task).
const char *kaleidobox_nvs_get_ntp_server(void);
esp_err_t kaleidobox_nvs_set_ntp_server(const char *server);

// POSIX TZ string (e.g. "EST5EDT,M3.2.0,M11.1.0"), resolved client-side
// from an IANA-style label - see web/app.html's timezone dropdown.
// Empty string (the default) means UTC.
const char *kaleidobox_nvs_get_clock_tz(void);
esp_err_t kaleidobox_nvs_set_clock_tz(const char *tz);
