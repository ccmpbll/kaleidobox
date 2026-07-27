#include "canvas.h"

#include "esp_log.h"
#include "matrix.h"
#include <string.h>

static const char *TAG = "canvas";
static uint8_t g_buffer[CANVAS_WIDTH * CANVAS_HEIGHT * 3];
static bool g_dirty = false;

esp_err_t kaleidobox_canvas_init(void) {
  memset(g_buffer, 0, sizeof(g_buffer));
  ESP_LOGI(TAG, "canvas_init");
  return ESP_OK;
}

void kaleidobox_canvas_set_pixel(uint8_t x, uint8_t y, uint8_t r, uint8_t g,
                                 uint8_t b) {
  if (x >= CANVAS_WIDTH || y >= CANVAS_HEIGHT) {
    return;
  }
  size_t idx = (y * CANVAS_WIDTH + x) * 3;
  g_buffer[idx] = r;
  g_buffer[idx + 1] = g;
  g_buffer[idx + 2] = b;
  g_dirty = true;

  kaleidobox_matrix_set_pixel(x, y, r, g, b);
}

const uint8_t *kaleidobox_canvas_buffer(void) { return g_buffer; }

void kaleidobox_canvas_flip(void) { kaleidobox_matrix_flip(); }

void kaleidobox_canvas_set_all(const uint8_t *rgb888) {
  memcpy(g_buffer, rgb888, sizeof(g_buffer));
  g_dirty = true;
  kaleidobox_matrix_draw_rgb888(g_buffer, CANVAS_WIDTH, CANVAS_HEIGHT);
}

bool kaleidobox_canvas_take_dirty(void) {
  bool was_dirty = g_dirty;
  g_dirty = false;
  return was_dirty;
}
