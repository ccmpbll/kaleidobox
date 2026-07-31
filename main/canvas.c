#include "canvas.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "kaleidoscope.h"
#include "matrix.h"
#include <string.h>

static const char *TAG = "canvas";
static uint8_t g_buffer[CANVAS_WIDTH * CANVAS_HEIGHT * 3];
static bool g_dirty = false;
// 0 (never drawn) reads as "long ago" by ms_since_draw_activity() below,
// since esp_timer_get_time() starts near 0 at boot too - a device that's
// never seen a draw shouldn't block takeovers forever.
static int64_t g_last_draw_activity_us = 0;

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
  // Several callers (panel_takeover.c's end(), kaleidoscope_stop(),
  // wifi.c's boot-time IP display) pass kaleidobox_canvas_buffer()
  // right back in here purely to repaint the matrix from whatever the
  // canvas already holds - not an actual content change. Marking dirty
  // for those was a real bug: every display-rotation lap (every
  // ~rotate_secs*2, see display_rotation.c) triggered a pointless
  // autosave write to SD of data that hadn't changed, competing for
  // the same small internal DMA-capable heap TLS uses - confirmed live
  // as intermittent "not enough mem" SD read failures. Only a genuine
  // new buffer (gallery load, canvas submit, upload - anything that
  // isn't g_buffer itself) is a real change worth saving.
  if (rgb888 != g_buffer) {
    memcpy(g_buffer, rgb888, sizeof(g_buffer));
    g_dirty = true;
  }
  kaleidobox_matrix_draw_rgb888(g_buffer, CANVAS_WIDTH, CANVAS_HEIGHT);

  // Kaleidoscope samples from its own private copy of the source image
  // (see kaleidoscope.c), so a canvas change here would otherwise get
  // overwritten by kaleidoscope's very next frame instead of actually
  // taking effect - covers gallery show/next/prev, draw-then-submit,
  // upload, and clear, since they all funnel through here. No-op if
  // kaleidoscope isn't running.
  if (kaleidobox_kaleidoscope_is_running()) {
    kaleidobox_image_t source = {
        .rgb888 = g_buffer,
        .width = CANVAS_WIDTH,
        .height = CANVAS_HEIGHT,
    };
    kaleidobox_kaleidoscope_update_source(&source);
  }
}

bool kaleidobox_canvas_is_dirty(void) { return g_dirty; }

void kaleidobox_canvas_clear_dirty(void) { g_dirty = false; }

void kaleidobox_canvas_mark_draw_activity(void) {
  g_last_draw_activity_us = esp_timer_get_time();
}

int64_t kaleidobox_canvas_ms_since_draw_activity(void) {
  return (esp_timer_get_time() - g_last_draw_activity_us) / 1000;
}
