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

// MQTT broker connection (shared by whatever MQTT features exist - just
// PrintSpy today). Broker is a full URI e.g. "mqtt://host:1883". User
// and pass may be empty (anonymous broker auth).
const char *kaleidobox_nvs_get_mqtt_broker(void);
esp_err_t kaleidobox_nvs_set_mqtt_broker(const char *broker);
const char *kaleidobox_nvs_get_mqtt_user(void);
esp_err_t kaleidobox_nvs_set_mqtt_user(const char *user);
const char *kaleidobox_nvs_get_mqtt_pass(void);
esp_err_t kaleidobox_nvs_set_mqtt_pass(const char *pass);

// PrintSpy print-status takeover - see main/printspy.h. Subscribes to
// printspy_topic (default wildcard "printspy/printer/+/state", one
// message per printer) on the broker above and tracks every printer's
// state; main/display_rotation.c decides when to actually show it.
bool kaleidobox_nvs_get_printspy_enabled(void);
esp_err_t kaleidobox_nvs_set_printspy_enabled(bool enable);
const char *kaleidobox_nvs_get_printspy_topic(void);
esp_err_t kaleidobox_nvs_set_printspy_topic(const char *topic);

// Weather takeover - see main/weather.h. While enabled, the display
// rotation (see below) fetches a fresh OpenWeatherMap reading and shows
// it for one rotate_secs slot. units: 0=metric(C), 1=imperial(F).
// fields is a bitmask - see main/weather.h for the bit layout. Location
// is just a ZIP/postal code - OWM's zip= param defaults to US with no
// country code needed (confirmed live).
bool kaleidobox_nvs_get_weather_enabled(void);
esp_err_t kaleidobox_nvs_set_weather_enabled(bool enable);
const char *kaleidobox_nvs_get_ow_api_key(void);
esp_err_t kaleidobox_nvs_set_ow_api_key(const char *key);
const char *kaleidobox_nvs_get_weather_zip(void);
esp_err_t kaleidobox_nvs_set_weather_zip(const char *zip);
uint8_t kaleidobox_nvs_get_weather_units(void);
esp_err_t kaleidobox_nvs_set_weather_units(uint8_t units);
uint16_t kaleidobox_nvs_get_weather_fields(void);
esp_err_t kaleidobox_nvs_set_weather_fields(uint16_t fields);

// Display rotation - see main/display_rotation.c. The panel cycles
// clock (kaleidoscope/idle) -> each currently-printing printer, in
// turn -> weather (if enabled) -> clock ..., spending rotate_secs on
// each applicable slot and skipping any that don't currently apply
// (e.g. no printer printing, or weather disabled).
uint16_t kaleidobox_nvs_get_rotate_secs(void);
esp_err_t kaleidobox_nvs_set_rotate_secs(uint16_t seconds);

// Brightness schedule - see main/brightness_schedule.h. While enabled,
// brightness is set to dim_brightness at dim_hour:dim_min and restored
// to whatever the manual brightness setting already holds at
// bright_hour:bright_min. Edge-triggered (only acts exactly at each
// crossing), so a manual brightness change mid-window sticks until the
// next real crossing rather than getting overwritten.
bool kaleidobox_nvs_get_brightness_schedule_enabled(void);
esp_err_t kaleidobox_nvs_set_brightness_schedule_enabled(bool enable);
uint8_t kaleidobox_nvs_get_dim_hour(void);
esp_err_t kaleidobox_nvs_set_dim_hour(uint8_t hour);
uint8_t kaleidobox_nvs_get_dim_min(void);
esp_err_t kaleidobox_nvs_set_dim_min(uint8_t min);
uint8_t kaleidobox_nvs_get_dim_brightness(void);
esp_err_t kaleidobox_nvs_set_dim_brightness(uint8_t brightness);
uint8_t kaleidobox_nvs_get_bright_hour(void);
esp_err_t kaleidobox_nvs_set_bright_hour(uint8_t hour);
uint8_t kaleidobox_nvs_get_bright_min(void);
esp_err_t kaleidobox_nvs_set_bright_min(uint8_t min);
