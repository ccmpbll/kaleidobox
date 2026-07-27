#include "gallery.h"

#include "esp_log.h"
#include "sdcard.h"

static const char *TAG = "gallery";

esp_err_t kaleidobox_gallery_init(void) {
  ESP_LOGI(TAG, "gallery_init (stub)");
  return ESP_OK;
}

esp_err_t kaleidobox_gallery_save(const char *name) {
  (void)name;
  // TODO: write the current canvas buffer (canvas.h) to
  // "/sdcard/gallery/<name>.raw" (or similar) once sdcard.h is
  // implemented.
  ESP_LOGW(TAG, "gallery_save not yet implemented");
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t kaleidobox_gallery_delete(const char *name) {
  (void)name;
  ESP_LOGW(TAG, "gallery_delete not yet implemented");
  return ESP_ERR_NOT_SUPPORTED;
}

size_t kaleidobox_gallery_list(char *buf, size_t buf_size, size_t max_names) {
  (void)max_names;
  if (buf_size > 0) {
    buf[0] = '\0';
  }
  ESP_LOGW(TAG, "gallery_list not yet implemented");
  return 0;
}

esp_err_t kaleidobox_gallery_next(void) {
  ESP_LOGW(TAG, "gallery_next not yet implemented");
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t kaleidobox_gallery_prev(void) {
  ESP_LOGW(TAG, "gallery_prev not yet implemented");
  return ESP_ERR_NOT_SUPPORTED;
}
