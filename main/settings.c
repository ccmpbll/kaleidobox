#include "settings.h"

#include <nvs.h>
#include <string.h>

// Named "settings.c", not "nvs.c" - shadows the system <nvs.h> header
// otherwise (bit printspy-cam during its own initial scaffold).
static const char *NVS_NAMESPACE = "kaleidobox";
static const char *NVS_ID_FOLD_COUNT = "fold_count";
static const char *NVS_ID_MOTION_ZOOM = "motion_zoom";
static const char *NVS_ID_INSTANT_DRAW = "instant_draw";
static const char *NVS_ID_GALLERY_AUTO = "gallery_auto";
static const char *NVS_ID_GALLERY_INTERVAL = "gallery_ival";
static const char *NVS_ID_KALEIDO_RUNNING = "kaleido_run";
static const char *NVS_ID_BRIGHTNESS = "brightness";
static const char *NVS_ID_CLOCK_MODE = "clock_mode";
static const char *NVS_ID_CLOCK_COLOR = "clock_color";
static const char *NVS_ID_CLOCK_SCALE = "clock_scale";
static const char *NVS_ID_CLOCK_24H = "clock_24h";
static const char *NVS_ID_NTP_SERVER = "ntp_server";
static const char *NVS_ID_CLOCK_TZ = "clock_tz";
static const char *NVS_ID_MQTT_BROKER = "mqtt_broker";
static const char *NVS_ID_MQTT_USER = "mqtt_user";
static const char *NVS_ID_MQTT_PASS = "mqtt_pass";
static const char *NVS_ID_PRINTSPY_EN = "printspy_en";
static const char *NVS_ID_PRINTSPY_TOPIC = "printspy_topic";
static const char *NVS_ID_CLOCK_SECS = "clock_secs";
static const char *NVS_ID_PRINTER_SECS = "printer_secs";
static const char *NVS_ID_WEATHER_SECS = "weather_secs";
static const char *NVS_ID_WEATHER_ENABLED = "weather_enabled";
static const char *NVS_ID_OW_API_KEY = "ow_api_key";
static const char *NVS_ID_WEATHER_ZIP = "weather_zip";
static const char *NVS_ID_WEATHER_UNITS = "weather_units";
static const char *NVS_ID_WEATHER_FIELDS = "weather_fields";
static const char *NVS_ID_BRIGHT_SCHED_EN = "bright_sched_en";
static const char *NVS_ID_DIM_HOUR = "dim_hour";
static const char *NVS_ID_DIM_MIN = "dim_min";
static const char *NVS_ID_DIM_BRIGHTNESS = "dim_brightness";
static const char *NVS_ID_BRIGHT_HOUR = "bright_hour";
static const char *NVS_ID_BRIGHT_MIN = "bright_min";

// Defaults chosen so a freshly-flashed device (before any setting has
// ever been written) behaves sensibly rather than at the extremes of
// each range - see kaleidobox_nvs_init below.
#define DEFAULT_FOLD_COUNT 8
#define DEFAULT_GALLERY_INTERVAL_SECONDS 30
#define DEFAULT_BRIGHTNESS 128 // matches esp-hub75's own Hub75Config default -
                               // a freshly-flashed device looks the same as
                               // before this setting existed, until touched.
#define DEFAULT_CLOCK_COLOR 0xFFFFFF // white
#define DEFAULT_CLOCK_SCALE 2
#define DEFAULT_NTP_SERVER "pool.ntp.org"
#define DEFAULT_PRINTSPY_TOPIC "printspy/printer/+/state"
#define DEFAULT_CLOCK_SECS 15
#define DEFAULT_PRINTER_SECS 8 // matches the old printspy-only rotate-between-printers cadence
// 8 (a plain copy of the printer default before these were split
// apart) was too fast for weather specifically - a 6-line weather
// screen needs more than 8s to actually read. Confirmed live ("why is
// it changing this fast?").
#define DEFAULT_WEATHER_SECS 15
#define DEFAULT_WEATHER_UNITS 1 // imperial (Fahrenheit)
#define DEFAULT_WEATHER_FIELDS 0x0043 // bit0=temp, bit1=condition, bit6=location - see weather.h
#define DEFAULT_DIM_HOUR 22
#define DEFAULT_DIM_MIN 0
#define DEFAULT_DIM_BRIGHTNESS 32
#define DEFAULT_BRIGHT_HOUR 7
#define DEFAULT_BRIGHT_MIN 0

static uint8_t fold_count_val = DEFAULT_FOLD_COUNT;
static uint8_t motion_zoom_val = 0;
static uint8_t instant_draw_val = 0;
static uint8_t gallery_auto_val = 1;
static uint16_t gallery_interval_val = DEFAULT_GALLERY_INTERVAL_SECONDS;
static uint8_t kaleido_running_val = 0;
static uint8_t brightness_val = DEFAULT_BRIGHTNESS;
static uint8_t clock_mode_val = 0; // off
static uint32_t clock_color_val = DEFAULT_CLOCK_COLOR;
static uint8_t clock_scale_val = DEFAULT_CLOCK_SCALE;
static uint8_t clock_24h_val = 1; // 24h, matches the original hardcoded "%02d:%02d" behavior
static char ntp_server_val[64] = DEFAULT_NTP_SERVER;
static char clock_tz_val[64] = ""; // empty = UTC
static char mqtt_broker_val[80] = ""; // e.g. "mqtt://host:1883"
static char mqtt_user_val[32] = "";
static char mqtt_pass_val[32] = "";
static uint8_t printspy_en_val = 0;
static char printspy_topic_val[80] = DEFAULT_PRINTSPY_TOPIC;
// Independent dwell time per display-rotation slot - see main/display_rotation.c.
static uint16_t clock_secs_val = DEFAULT_CLOCK_SECS;
static uint16_t printer_secs_val = DEFAULT_PRINTER_SECS;
static uint16_t weather_secs_val = DEFAULT_WEATHER_SECS;
static uint8_t weather_enabled_val = 0;
static char ow_api_key_val[48] = "";
static char weather_zip_val[16] = "";
static uint8_t weather_units_val = DEFAULT_WEATHER_UNITS;
static uint16_t weather_fields_val = DEFAULT_WEATHER_FIELDS;
static uint8_t bright_sched_en_val = 0;
static uint8_t dim_hour_val = DEFAULT_DIM_HOUR;
static uint8_t dim_min_val = DEFAULT_DIM_MIN;
static uint8_t dim_brightness_val = DEFAULT_DIM_BRIGHTNESS;
static uint8_t bright_hour_val = DEFAULT_BRIGHT_HOUR;
static uint8_t bright_min_val = DEFAULT_BRIGHT_MIN;

#define LOAD_NVS_SCALAR(nvs_get_fn, key, dest)                              \
  do {                                                                       \
    err = nvs_get_fn(handle, key, &(dest));                                \
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {                    \
      return err;                                                           \
    }                                                                        \
  } while (0)

#define SCALAR_SETTER(nvs_set_fn, key, dest, val)                          \
  nvs_handle_t handle;                                                      \
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);         \
  if (err != ESP_OK)                                                        \
    return err;                                                             \
  err = nvs_set_fn(handle, key, val);                                      \
  nvs_close(handle);                                                        \
  if (err == ESP_OK)                                                        \
    dest = val;                                                             \
  return err;

// String counterparts to the two macros above - strings don't fit
// LOAD_NVS_SCALAR's &(dest) pattern (nvs_get_str needs a separate in/out
// length param) or SCALAR_SETTER's plain assignment (needs a bounds
// check + strncpy instead of `dest = val`).
#define LOAD_NVS_STRING(key, dest)                                          \
  do {                                                                       \
    size_t len = sizeof(dest);                                              \
    err = nvs_get_str(handle, key, dest, &len);                            \
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {                   \
      return err;                                                           \
    }                                                                        \
  } while (0)

#define STRING_SETTER(key, dest, val)                                       \
  if (!(val) || strlen(val) >= sizeof(dest))                               \
    return ESP_ERR_INVALID_ARG;                                            \
  nvs_handle_t handle;                                                      \
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);         \
  if (err != ESP_OK)                                                        \
    return err;                                                             \
  err = nvs_set_str(handle, key, val);                                     \
  nvs_close(handle);                                                        \
  if (err == ESP_OK) {                                                      \
    strncpy(dest, val, sizeof(dest) - 1);                                  \
    dest[sizeof(dest) - 1] = '\0';                                         \
  }                                                                          \
  return err;

esp_err_t kaleidobox_nvs_init(void) {
  nvs_handle_t handle;
  esp_err_t err;

  err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }

  LOAD_NVS_SCALAR(nvs_get_u8, NVS_ID_FOLD_COUNT, fold_count_val);
  LOAD_NVS_SCALAR(nvs_get_u8, NVS_ID_MOTION_ZOOM, motion_zoom_val);
  LOAD_NVS_SCALAR(nvs_get_u8, NVS_ID_INSTANT_DRAW, instant_draw_val);
  LOAD_NVS_SCALAR(nvs_get_u8, NVS_ID_GALLERY_AUTO, gallery_auto_val);
  LOAD_NVS_SCALAR(nvs_get_u16, NVS_ID_GALLERY_INTERVAL, gallery_interval_val);
  LOAD_NVS_SCALAR(nvs_get_u8, NVS_ID_KALEIDO_RUNNING, kaleido_running_val);
  LOAD_NVS_SCALAR(nvs_get_u8, NVS_ID_BRIGHTNESS, brightness_val);
  LOAD_NVS_SCALAR(nvs_get_u8, NVS_ID_CLOCK_MODE, clock_mode_val);
  LOAD_NVS_SCALAR(nvs_get_u32, NVS_ID_CLOCK_COLOR, clock_color_val);
  LOAD_NVS_SCALAR(nvs_get_u8, NVS_ID_CLOCK_SCALE, clock_scale_val);
  LOAD_NVS_SCALAR(nvs_get_u8, NVS_ID_CLOCK_24H, clock_24h_val);
  LOAD_NVS_SCALAR(nvs_get_u8, NVS_ID_PRINTSPY_EN, printspy_en_val);
  LOAD_NVS_SCALAR(nvs_get_u16, NVS_ID_CLOCK_SECS, clock_secs_val);
  LOAD_NVS_SCALAR(nvs_get_u16, NVS_ID_PRINTER_SECS, printer_secs_val);
  LOAD_NVS_SCALAR(nvs_get_u16, NVS_ID_WEATHER_SECS, weather_secs_val);
  LOAD_NVS_SCALAR(nvs_get_u8, NVS_ID_WEATHER_ENABLED, weather_enabled_val);
  LOAD_NVS_SCALAR(nvs_get_u8, NVS_ID_WEATHER_UNITS, weather_units_val);
  LOAD_NVS_SCALAR(nvs_get_u16, NVS_ID_WEATHER_FIELDS, weather_fields_val);
  LOAD_NVS_SCALAR(nvs_get_u8, NVS_ID_BRIGHT_SCHED_EN, bright_sched_en_val);
  LOAD_NVS_SCALAR(nvs_get_u8, NVS_ID_DIM_HOUR, dim_hour_val);
  LOAD_NVS_SCALAR(nvs_get_u8, NVS_ID_DIM_MIN, dim_min_val);
  LOAD_NVS_SCALAR(nvs_get_u8, NVS_ID_DIM_BRIGHTNESS, dim_brightness_val);
  LOAD_NVS_SCALAR(nvs_get_u8, NVS_ID_BRIGHT_HOUR, bright_hour_val);
  LOAD_NVS_SCALAR(nvs_get_u8, NVS_ID_BRIGHT_MIN, bright_min_val);

  // Left at their compiled-in defaults (already assigned above) on
  // ESP_ERR_NVS_NOT_FOUND, same as every scalar default here.
  LOAD_NVS_STRING(NVS_ID_NTP_SERVER, ntp_server_val);
  LOAD_NVS_STRING(NVS_ID_CLOCK_TZ, clock_tz_val);
  LOAD_NVS_STRING(NVS_ID_MQTT_BROKER, mqtt_broker_val);
  LOAD_NVS_STRING(NVS_ID_MQTT_USER, mqtt_user_val);
  LOAD_NVS_STRING(NVS_ID_MQTT_PASS, mqtt_pass_val);
  LOAD_NVS_STRING(NVS_ID_PRINTSPY_TOPIC, printspy_topic_val);
  LOAD_NVS_STRING(NVS_ID_OW_API_KEY, ow_api_key_val);
  LOAD_NVS_STRING(NVS_ID_WEATHER_ZIP, weather_zip_val);

  nvs_close(handle);

  return ESP_OK;
}

uint8_t kaleidobox_nvs_get_fold_count(void) { return fold_count_val; }
esp_err_t kaleidobox_nvs_set_fold_count(uint8_t count) {
  SCALAR_SETTER(nvs_set_u8, NVS_ID_FOLD_COUNT, fold_count_val, count)
}

bool kaleidobox_nvs_get_motion_zoom(void) { return motion_zoom_val != 0; }
esp_err_t kaleidobox_nvs_set_motion_zoom(bool enable) {
  uint8_t v = enable ? 1 : 0;
  SCALAR_SETTER(nvs_set_u8, NVS_ID_MOTION_ZOOM, motion_zoom_val, v)
}

bool kaleidobox_nvs_get_instant_draw(void) { return instant_draw_val != 0; }
esp_err_t kaleidobox_nvs_set_instant_draw(bool enable) {
  uint8_t v = enable ? 1 : 0;
  SCALAR_SETTER(nvs_set_u8, NVS_ID_INSTANT_DRAW, instant_draw_val, v)
}

bool kaleidobox_nvs_get_gallery_auto_advance(void) { return gallery_auto_val != 0; }
esp_err_t kaleidobox_nvs_set_gallery_auto_advance(bool enable) {
  uint8_t v = enable ? 1 : 0;
  SCALAR_SETTER(nvs_set_u8, NVS_ID_GALLERY_AUTO, gallery_auto_val, v)
}

uint16_t kaleidobox_nvs_get_gallery_interval_seconds(void) { return gallery_interval_val; }
esp_err_t kaleidobox_nvs_set_gallery_interval_seconds(uint16_t seconds) {
  SCALAR_SETTER(nvs_set_u16, NVS_ID_GALLERY_INTERVAL, gallery_interval_val, seconds)
}

bool kaleidobox_nvs_get_kaleido_running(void) { return kaleido_running_val != 0; }
esp_err_t kaleidobox_nvs_set_kaleido_running(bool running) {
  uint8_t v = running ? 1 : 0;
  SCALAR_SETTER(nvs_set_u8, NVS_ID_KALEIDO_RUNNING, kaleido_running_val, v)
}

uint8_t kaleidobox_nvs_get_brightness(void) { return brightness_val; }
esp_err_t kaleidobox_nvs_set_brightness(uint8_t brightness) {
  SCALAR_SETTER(nvs_set_u8, NVS_ID_BRIGHTNESS, brightness_val, brightness)
}

uint8_t kaleidobox_nvs_get_clock_mode(void) { return clock_mode_val; }
esp_err_t kaleidobox_nvs_set_clock_mode(uint8_t mode) {
  SCALAR_SETTER(nvs_set_u8, NVS_ID_CLOCK_MODE, clock_mode_val, mode)
}

uint32_t kaleidobox_nvs_get_clock_color(void) { return clock_color_val; }
esp_err_t kaleidobox_nvs_set_clock_color(uint32_t color) {
  SCALAR_SETTER(nvs_set_u32, NVS_ID_CLOCK_COLOR, clock_color_val, color)
}

uint8_t kaleidobox_nvs_get_clock_scale(void) { return clock_scale_val; }
esp_err_t kaleidobox_nvs_set_clock_scale(uint8_t scale) {
  SCALAR_SETTER(nvs_set_u8, NVS_ID_CLOCK_SCALE, clock_scale_val, scale)
}

bool kaleidobox_nvs_get_clock_24h(void) { return clock_24h_val != 0; }
esp_err_t kaleidobox_nvs_set_clock_24h(bool enable) {
  uint8_t v = enable ? 1 : 0;
  SCALAR_SETTER(nvs_set_u8, NVS_ID_CLOCK_24H, clock_24h_val, v)
}

const char *kaleidobox_nvs_get_ntp_server(void) { return ntp_server_val; }
esp_err_t kaleidobox_nvs_set_ntp_server(const char *server) {
  STRING_SETTER(NVS_ID_NTP_SERVER, ntp_server_val, server)
}

const char *kaleidobox_nvs_get_clock_tz(void) { return clock_tz_val; }
esp_err_t kaleidobox_nvs_set_clock_tz(const char *tz) {
  STRING_SETTER(NVS_ID_CLOCK_TZ, clock_tz_val, tz)
}

const char *kaleidobox_nvs_get_mqtt_broker(void) { return mqtt_broker_val; }
esp_err_t kaleidobox_nvs_set_mqtt_broker(const char *broker) {
  STRING_SETTER(NVS_ID_MQTT_BROKER, mqtt_broker_val, broker)
}

const char *kaleidobox_nvs_get_mqtt_user(void) { return mqtt_user_val; }
esp_err_t kaleidobox_nvs_set_mqtt_user(const char *user) {
  STRING_SETTER(NVS_ID_MQTT_USER, mqtt_user_val, user)
}

const char *kaleidobox_nvs_get_mqtt_pass(void) { return mqtt_pass_val; }
esp_err_t kaleidobox_nvs_set_mqtt_pass(const char *pass) {
  STRING_SETTER(NVS_ID_MQTT_PASS, mqtt_pass_val, pass)
}

bool kaleidobox_nvs_get_printspy_enabled(void) { return printspy_en_val != 0; }
esp_err_t kaleidobox_nvs_set_printspy_enabled(bool enable) {
  uint8_t v = enable ? 1 : 0;
  SCALAR_SETTER(nvs_set_u8, NVS_ID_PRINTSPY_EN, printspy_en_val, v)
}

const char *kaleidobox_nvs_get_printspy_topic(void) { return printspy_topic_val; }
esp_err_t kaleidobox_nvs_set_printspy_topic(const char *topic) {
  STRING_SETTER(NVS_ID_PRINTSPY_TOPIC, printspy_topic_val, topic)
}

uint16_t kaleidobox_nvs_get_clock_secs(void) { return clock_secs_val; }
esp_err_t kaleidobox_nvs_set_clock_secs(uint16_t seconds) {
  SCALAR_SETTER(nvs_set_u16, NVS_ID_CLOCK_SECS, clock_secs_val, seconds)
}

uint16_t kaleidobox_nvs_get_printer_secs(void) { return printer_secs_val; }
esp_err_t kaleidobox_nvs_set_printer_secs(uint16_t seconds) {
  SCALAR_SETTER(nvs_set_u16, NVS_ID_PRINTER_SECS, printer_secs_val, seconds)
}

uint16_t kaleidobox_nvs_get_weather_secs(void) { return weather_secs_val; }
esp_err_t kaleidobox_nvs_set_weather_secs(uint16_t seconds) {
  SCALAR_SETTER(nvs_set_u16, NVS_ID_WEATHER_SECS, weather_secs_val, seconds)
}

bool kaleidobox_nvs_get_weather_enabled(void) { return weather_enabled_val != 0; }
esp_err_t kaleidobox_nvs_set_weather_enabled(bool enable) {
  uint8_t v = enable ? 1 : 0;
  SCALAR_SETTER(nvs_set_u8, NVS_ID_WEATHER_ENABLED, weather_enabled_val, v)
}

const char *kaleidobox_nvs_get_ow_api_key(void) { return ow_api_key_val; }
esp_err_t kaleidobox_nvs_set_ow_api_key(const char *key) {
  STRING_SETTER(NVS_ID_OW_API_KEY, ow_api_key_val, key)
}

const char *kaleidobox_nvs_get_weather_zip(void) { return weather_zip_val; }
esp_err_t kaleidobox_nvs_set_weather_zip(const char *zip) {
  STRING_SETTER(NVS_ID_WEATHER_ZIP, weather_zip_val, zip)
}

uint8_t kaleidobox_nvs_get_weather_units(void) { return weather_units_val; }
esp_err_t kaleidobox_nvs_set_weather_units(uint8_t units) {
  SCALAR_SETTER(nvs_set_u8, NVS_ID_WEATHER_UNITS, weather_units_val, units)
}

uint16_t kaleidobox_nvs_get_weather_fields(void) { return weather_fields_val; }
esp_err_t kaleidobox_nvs_set_weather_fields(uint16_t fields) {
  SCALAR_SETTER(nvs_set_u16, NVS_ID_WEATHER_FIELDS, weather_fields_val, fields)
}

bool kaleidobox_nvs_get_brightness_schedule_enabled(void) { return bright_sched_en_val != 0; }
esp_err_t kaleidobox_nvs_set_brightness_schedule_enabled(bool enable) {
  uint8_t v = enable ? 1 : 0;
  SCALAR_SETTER(nvs_set_u8, NVS_ID_BRIGHT_SCHED_EN, bright_sched_en_val, v)
}

uint8_t kaleidobox_nvs_get_dim_hour(void) { return dim_hour_val; }
esp_err_t kaleidobox_nvs_set_dim_hour(uint8_t hour) {
  SCALAR_SETTER(nvs_set_u8, NVS_ID_DIM_HOUR, dim_hour_val, hour)
}

uint8_t kaleidobox_nvs_get_dim_min(void) { return dim_min_val; }
esp_err_t kaleidobox_nvs_set_dim_min(uint8_t min) {
  SCALAR_SETTER(nvs_set_u8, NVS_ID_DIM_MIN, dim_min_val, min)
}

uint8_t kaleidobox_nvs_get_dim_brightness(void) { return dim_brightness_val; }
esp_err_t kaleidobox_nvs_set_dim_brightness(uint8_t brightness) {
  SCALAR_SETTER(nvs_set_u8, NVS_ID_DIM_BRIGHTNESS, dim_brightness_val, brightness)
}

uint8_t kaleidobox_nvs_get_bright_hour(void) { return bright_hour_val; }
esp_err_t kaleidobox_nvs_set_bright_hour(uint8_t hour) {
  SCALAR_SETTER(nvs_set_u8, NVS_ID_BRIGHT_HOUR, bright_hour_val, hour)
}

uint8_t kaleidobox_nvs_get_bright_min(void) { return bright_min_val; }
esp_err_t kaleidobox_nvs_set_bright_min(uint8_t min) {
  SCALAR_SETTER(nvs_set_u8, NVS_ID_BRIGHT_MIN, bright_min_val, min)
}
