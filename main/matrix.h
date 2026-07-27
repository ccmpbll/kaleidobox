#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// C-callable wrapper around the HUB75 DMA driver. The driver itself
// (esphome/esp-hub75, pulled in as an ESP-IDF component - see
// main/idf_component.yml) is C++; implementation lives in matrix.cpp.
// Every other module in this project talks to the matrix only through
// this plain-C API, so the C++ dependency stays contained to one file.
//
// Correction from this scaffold's first pass: originally planned to pull
// in arduino-esp32 + Adafruit_GFX + mrfaptastic's
// ESP32-HUB75-MatrixPanel-I2S-DMA, since that's the usual path for this
// kind of panel. Waveshare's own official ESP-IDF example for this exact
// board (github.com/waveshareteam/ESP32-S3-RGB-Matrix,
// example/idf_v5.5.2) uses esphome/esp-hub75 instead - a pure ESP-IDF
// component, no Arduino compatibility layer needed at all. Their
// hub75_bridge.cpp wraps it in almost exactly this same C-API pattern
// (Hub75Driver class -> extern "C" functions), which is a good sign this
// approach is sound for this board specifically.
//
// Implemented against esp-hub75's real API (Hub75Driver/Hub75Config from
// hub75.h) - see matrix.cpp. Not yet flashed/tested on real hardware.

esp_err_t kaleidobox_matrix_init(void);

// x/y in panel coordinates (0..63), 8-bit color depth per RGB565... - stub
// only, real signature/color depth may change once the driver is wired up.
void kaleidobox_matrix_set_pixel(uint8_t x, uint8_t y, uint8_t r, uint8_t g,
                                 uint8_t b);

// Swaps the back buffer to the front (double-buffered, tear-free update).
void kaleidobox_matrix_flip(void);

void kaleidobox_matrix_clear(void);

#ifdef __cplusplus
}
#endif
