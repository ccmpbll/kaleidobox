#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// C-callable wrapper around the HUB75 DMA driver. The driver itself
// (ESP32-HUB75-MatrixPanel-I2S-DMA, via Adafruit_GFX) is C++ and built
// against arduino-esp32 conventions - implementation lives in matrix.cpp,
// pulled in as an ESP-IDF component (see main/idf_component.yml). Every
// other module in this project talks to the matrix only through this
// plain-C API, so the Arduino-compat dependency stays contained to one
// file instead of leaking through the rest of the (otherwise pure
// ESP-IDF) codebase.
//
// STUB - matrix.cpp currently only logs; no panel output yet. Real driver
// bring-up is blocked on verifying board_pins.h against actual hardware.

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
