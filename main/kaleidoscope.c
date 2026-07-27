#include "kaleidoscope.h"

#include "esp_log.h"

static const char *TAG = "kaleidoscope";
static bool g_running = false;

esp_err_t kaleidobox_kaleidoscope_init(void) {
  ESP_LOGI(TAG, "kaleidoscope_init (stub)");
  return ESP_OK;
}

esp_err_t kaleidobox_kaleidoscope_start(const kaleidobox_image_t *source) {
  (void)source;
  // TODO: spin up a FreeRTOS timer task (~20-30fps) that, per frame,
  // computes angle/radius from center for each of the 64x64 output
  // pixels, folds into the configured wedge count, samples `source` via
  // a rotating/zooming UV offset, and pushes the result through
  // matrix.h. Not yet implemented - matrix driver isn't wired up either.
  ESP_LOGW(TAG, "kaleidoscope_start not yet implemented");
  g_running = true;
  return ESP_ERR_NOT_SUPPORTED;
}

void kaleidobox_kaleidoscope_stop(void) { g_running = false; }

bool kaleidobox_kaleidoscope_is_running(void) { return g_running; }
