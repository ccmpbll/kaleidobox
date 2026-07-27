#include "matrix.h"

#include "esp_log.h"

// TODO: real implementation - construct a Hub75Driver (esphome/esp-hub75)
// from board_pins.h's MATRIX_PIN_* map (pins verified against Waveshare's
// official example - see board_pins.h), call begin(), and implement
// set_pixel/flip/clear against it (see hub75_bridge.cpp in
// waveshareteam/ESP32-S3-RGB-Matrix's idf_v5.5.2 example for the
// reference shape: Hub75Config/Hub75Pins -> new Hub75Driver(cfg) ->
// draw_pixels()/flip_buffer()/set_brightness()). Left as a log-only stub
// until the esp-hub75 component dep is actually pulled in via
// idf_component.yml and build-tested.

static const char *TAG = "matrix";

extern "C" esp_err_t kaleidobox_matrix_init(void) {
  ESP_LOGI(TAG, "matrix_init (stub - no panel output yet)");
  return ESP_OK;
}

extern "C" void kaleidobox_matrix_set_pixel(uint8_t x, uint8_t y, uint8_t r,
                                            uint8_t g, uint8_t b) {
  (void)x;
  (void)y;
  (void)r;
  (void)g;
  (void)b;
}

extern "C" void kaleidobox_matrix_flip(void) {}

extern "C" void kaleidobox_matrix_clear(void) {}
