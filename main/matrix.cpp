#include "matrix.h"

#include "esp_log.h"

// TODO: real implementation - construct a MatrixPanel_I2S_DMA instance
// from board_pins.h's MATRIX_PIN_* map, call begin(), and implement
// set_pixel/flip/clear against it. Left as a log-only stub until pins are
// verified against real hardware (see board_pins.h) and the
// arduino-esp32/Adafruit_GFX/ESP32-HUB75-MatrixPanel-I2S-DMA component
// deps are actually pulled in via idf_component.yml.

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
