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

  // Strings don't fit LOAD_NVS_SCALAR's &(dest) pattern - nvs_get_str
  // needs a separate in/out length param. Left at their compiled-in
  // defaults (already assigned above) on ESP_ERR_NVS_NOT_FOUND, same as
  // every scalar default here.
  size_t ntp_server_len = sizeof(ntp_server_val);
  err = nvs_get_str(handle, NVS_ID_NTP_SERVER, ntp_server_val, &ntp_server_len);
  if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
    return err;
  }
  size_t clock_tz_len = sizeof(clock_tz_val);
  err = nvs_get_str(handle, NVS_ID_CLOCK_TZ, clock_tz_val, &clock_tz_len);
  if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
    return err;
  }

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
  if (!server || strlen(server) >= sizeof(ntp_server_val)) {
    return ESP_ERR_INVALID_ARG;
  }
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }
  err = nvs_set_str(handle, NVS_ID_NTP_SERVER, server);
  nvs_close(handle);
  if (err == ESP_OK) {
    strncpy(ntp_server_val, server, sizeof(ntp_server_val) - 1);
    ntp_server_val[sizeof(ntp_server_val) - 1] = '\0';
  }
  return err;
}

const char *kaleidobox_nvs_get_clock_tz(void) { return clock_tz_val; }
esp_err_t kaleidobox_nvs_set_clock_tz(const char *tz) {
  if (!tz || strlen(tz) >= sizeof(clock_tz_val)) {
    return ESP_ERR_INVALID_ARG;
  }
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }
  err = nvs_set_str(handle, NVS_ID_CLOCK_TZ, tz);
  nvs_close(handle);
  if (err == ESP_OK) {
    strncpy(clock_tz_val, tz, sizeof(clock_tz_val) - 1);
    clock_tz_val[sizeof(clock_tz_val) - 1] = '\0';
  }
  return err;
}
