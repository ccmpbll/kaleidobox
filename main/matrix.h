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
// hub75.h) - see matrix.cpp. Flashed and confirmed on real hardware.

esp_err_t kaleidobox_matrix_init(void);

// x/y in panel coordinates (0..63), RGB888.
void kaleidobox_matrix_set_pixel(uint8_t x, uint8_t y, uint8_t r, uint8_t g,
                                 uint8_t b);

// Bulk-writes a full w*h RGB888 region starting at (0,0) in one driver
// call, instead of w*h individual set_pixel() calls. Use this for any
// whole-grid replacement (canvas clear/submit) - looping set_pixel()
// over all 4096 pixels was slow enough to noticeably block the caller
// (esp_http_server's shared task) and, since matrix.cpp runs
// single-buffered, raced against the DMA engine continuously scanning
// that same live buffer - visible as bright flickering pixels while
// clearing, confirmed on real hardware. One bulk call is fast enough
// that this hasn't been observed.
void kaleidobox_matrix_draw_rgb888(const uint8_t *rgb888, uint16_t w,
                                   uint16_t h);

// Swaps the back buffer to the front - only meaningful in double-buffered
// mode. Currently a no-op (logs a warning) because matrix.cpp runs
// single-buffered: draw mode's sparse, incremental edits actively broke
// double buffering (see matrix.cpp's config comment for what that looked
// like on real hardware - the whole panel flashing). set_pixel() is live
// immediately in single-buffer mode, no flip needed. Kept declared for
// kaleidoscope mode, which will redraw a full frame every tick and can
// re-enable double buffering without hitting the same bug.
void kaleidobox_matrix_flip(void);

void kaleidobox_matrix_clear(void);

// 0-255. Takes effect on the next refresh cycle - no flip/redraw call
// needed, even in this driver's single-buffered mode (confirmed
// against esp-hub75's own hub75.h doc comment on set_brightness()).
void kaleidobox_matrix_set_brightness(uint8_t brightness);

#ifdef __cplusplus
}
#endif
