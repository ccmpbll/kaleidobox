#include "canvas.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
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
// Guards g_last_draw_activity_us - written from the httpd task
// (mark_draw_activity, on every real draw) and read from
// display_rotation.c's rotation_task (ms_since_draw_activity, via
// panel_takeover.c's begin()). On 32-bit Xtensa a plain int64_t store/
// load is two 32-bit instructions, not atomic - an unsynchronized read
// during a concurrent write could observe a torn (part-old/part-new)
// value, wrongly reporting "no recent draw activity" and letting a
// takeover yank the panel mid-draw.
static portMUX_TYPE g_draw_activity_mux = portMUX_INITIALIZER_UNLOCKED;

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

// Shared tail of set_all()/repaint() below: push g_buffer to the matrix
// and, if kaleidoscope is running, feed it the same content as its
// source - kaleidoscope samples from its own private copy (see
// kaleidoscope.c), so skipping this would mean a canvas change gets
// silently overwritten by kaleidoscope's very next frame instead of
// actually taking effect.
static void repaint_matrix_and_resume_kaleidoscope(void) {
  kaleidobox_matrix_draw_rgb888(g_buffer, CANVAS_WIDTH, CANVAS_HEIGHT);
  if (kaleidobox_kaleidoscope_is_running()) {
    kaleidobox_image_t source = {
        .rgb888 = g_buffer,
        .width = CANVAS_WIDTH,
        .height = CANVAS_HEIGHT,
    };
    kaleidobox_kaleidoscope_update_source(&source);
  }
}

void kaleidobox_canvas_set_all(const uint8_t *rgb888) {
  memcpy(g_buffer, rgb888, sizeof(g_buffer));
  g_dirty = true;
  repaint_matrix_and_resume_kaleidoscope();
}

// Re-pushes whatever g_buffer already holds to the matrix (and resumes
// kaleidoscope from it), without marking the canvas dirty - for callers
// that are restoring/repainting existing content (panel_takeover.c's
// end(), kaleidoscope_stop(), wifi.c's boot-time IP display), not
// applying a genuine new image. A prior version of this used
// set_all(kaleidobox_canvas_buffer()) for the same purpose, detecting
// "is this a repaint" via pointer identity (rgb888 == g_buffer) - that
// worked only by coincidence of every caller passing that exact
// pointer, and any future caller that instead passed a distinct buffer
// with byte-identical content would have been misclassified as a real
// change, silently reintroducing the SD-write/DMA-heap-exhaustion bug
// this split now closes for good. Every repaint-only caller should use
// this function; set_all() always means "this is new content."
void kaleidobox_canvas_repaint(void) { repaint_matrix_and_resume_kaleidoscope(); }

bool kaleidobox_canvas_is_dirty(void) { return g_dirty; }

void kaleidobox_canvas_clear_dirty(void) { g_dirty = false; }

void kaleidobox_canvas_mark_draw_activity(void) {
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&g_draw_activity_mux);
  g_last_draw_activity_us = now;
  portEXIT_CRITICAL(&g_draw_activity_mux);
}

int64_t kaleidobox_canvas_ms_since_draw_activity(void) {
  portENTER_CRITICAL(&g_draw_activity_mux);
  int64_t last = g_last_draw_activity_us;
  portEXIT_CRITICAL(&g_draw_activity_mux);
  return (esp_timer_get_time() - last) / 1000;
}
