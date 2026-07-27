#include "image_decode.h"

#include "esp_log.h"
#include <stdlib.h>

// TODO: detect JPEG (FF D8) vs PNG (89 50 4E 47) magic bytes, decode via
// esp_jpeg / vendored lodepng, cap decode size (source doesn't need to be
// huge - the kaleidoscope transform and canvas display only ever sample a
// modest region of it), write result into out->rgb888/width/height.

static const char *TAG = "image_decode";

esp_err_t kaleidobox_image_decode(const uint8_t *data, size_t len,
                                  kaleidobox_image_t *out) {
  (void)data;
  (void)len;
  ESP_LOGW(TAG, "image_decode not yet implemented");
  out->rgb888 = NULL;
  out->width = 0;
  out->height = 0;
  return ESP_ERR_NOT_SUPPORTED;
}

void kaleidobox_image_free(kaleidobox_image_t *img) {
  if (img && img->rgb888) {
    free(img->rgb888);
    img->rgb888 = NULL;
  }
}
