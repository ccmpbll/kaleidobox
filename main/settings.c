#include "settings.h"

#include <nvs.h>

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

// Defaults chosen so a freshly-flashed device (before any setting has
// ever been written) behaves sensibly rather than at the extremes of
// each range - see kaleidobox_nvs_init below.
#define DEFAULT_FOLD_COUNT 8
#define DEFAULT_GALLERY_INTERVAL_SECONDS 30
#define DEFAULT_BRIGHTNESS 128 // matches esp-hub75's own Hub75Config default -
                               // a freshly-flashed device looks the same as
                               // before this setting existed, until touched.

static uint8_t fold_count_val = DEFAULT_FOLD_COUNT;
static uint8_t motion_zoom_val = 0;
static uint8_t instant_draw_val = 0;
static uint8_t gallery_auto_val = 1;
static uint16_t gallery_interval_val = DEFAULT_GALLERY_INTERVAL_SECONDS;
static uint8_t kaleido_running_val = 0;
static uint8_t brightness_val = DEFAULT_BRIGHTNESS;

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
