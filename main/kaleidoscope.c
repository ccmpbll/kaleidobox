#include "kaleidoscope.h"

#include "canvas.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "matrix.h"
#include "settings.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "kaleidoscope";

#define FRAME_INTERVAL_MS 40 // ~25fps - see start-of-file note below on cost
#define ROTATION_STEP 0.03f  // rad/frame - full revolution in ~8.4s at 25fps
// First values (0.02 rad/frame, ±25%) were real but imperceptible next
// to the faster rotation - user reported "can't tell a difference".
// Faster (~4s cycle instead of ~12.5s) and wider swing (0.4x..1.6x
// instead of 0.75x..1.25x) so it actually reads as zooming, not just
// theoretically happening.
#define ZOOM_STEP 0.05f
#define ZOOM_AMPLITUDE 0.6f

static TaskHandle_t g_task = NULL;
static SemaphoreHandle_t g_task_exited = NULL;
static volatile bool g_should_run = false;

static kaleidobox_image_t g_source = {0}; // owns a copy - caller's buffer
                                          // may not outlive this call
// Guards g_source.{rgb888,width,height} against the animation task
// (kaleidoscope_task/render_frame) reading it while
// kaleidobox_kaleidoscope_update_source() swaps it out from another
// task (e.g. an HTTP handler, on a live canvas edit) - without this, a
// freed old buffer could still be mid-read by the animation task.
static SemaphoreHandle_t g_source_mutex = NULL;

// Per destination pixel, precomputed once per start() (not per frame):
// the wedge-folded angle and radius. Destination pixel positions never
// move, so recomputing atan2f/sqrtf/fmodf for all 4096 pixels every
// single frame would be pure waste - only the animated rotation/zoom
// offsets applied on top of these actually change frame to frame.
static float g_pixel_angle[CANVAS_WIDTH * CANVAS_HEIGHT];
static float g_pixel_radius[CANVAS_WIDTH * CANVAS_HEIGHT];

static void precompute_pixel_tables(uint8_t fold_count) {
  float cx = (CANVAS_WIDTH - 1) / 2.0f;
  float cy = (CANVAS_HEIGHT - 1) / 2.0f;
  float wedge = 2.0f * (float)M_PI / (float)fold_count;

  for (int y = 0; y < CANVAS_HEIGHT; y++) {
    for (int x = 0; x < CANVAS_WIDTH; x++) {
      float rx = x - cx, ry = y - cy;
      float radius = sqrtf(rx * rx + ry * ry);
      // Tiny fixed bias BEFORE any fold math - keeps every pixel off an
      // exact wedge boundary, not just the diagonal ones. Negligible
      // visually (worth well under a tenth of a pixel of arc at any
      // radius on this panel). A first attempt at this fix only nudged
      // the copy_index floor and left it there - didn't help at all on
      // real hardware, because `a` below was still computed via an
      // independent fmodf() call that could disagree with copy_index
      // regardless of that nudge. Fixed properly this time: bias keeps
      // pixels off exact boundaries in the first place, and `a` is
      // derived directly from copy_index (see below) instead of a
      // second, separately-rounded computation that could disagree
      // with it.
      float angle = atan2f(ry, rx) + 0.001f; // -pi..pi (approx)

      // Fold into [0, wedge) - which wedge copy we're in, then mirror
      // every other copy so adjacent wedges reflect instead of repeat
      // identically. That reflection is what makes it look like a
      // kaleidoscope instead of just a spinning pie-slice duplicate.
      long copy_index = (long)floorf(angle / wedge);
      float a = angle - wedge * (float)copy_index; // in [0,wedge) by construction, always consistent with copy_index
      if (copy_index % 2 != 0) {
        a = wedge - a;
      }

      int idx = y * CANVAS_WIDTH + x;
      g_pixel_angle[idx] = a;
      g_pixel_radius[idx] = radius;
    }
  }
}

static void render_frame(uint8_t *dst, float rotation_offset, float zoom_scale) {
  // Held for the whole frame, not just a pointer snapshot at the top -
  // update_source() frees the old buffer right after swapping it in,
  // so a snapshot-then-release here could let that free() race a read
  // still in progress below.
  xSemaphoreTake(g_source_mutex, portMAX_DELAY);

  float src_cx = (g_source.width - 1) / 2.0f;
  float src_cy = (g_source.height - 1) / 2.0f;

  for (int i = 0; i < CANVAS_WIDTH * CANVAS_HEIGHT; i++) {
    float sample_angle = g_pixel_angle[i] + rotation_offset;
    float sample_radius = g_pixel_radius[i] * zoom_scale;

    float sx = src_cx + sample_radius * cosf(sample_angle);
    float sy = src_cy + sample_radius * sinf(sample_angle);

    int isx = (int)lroundf(sx);
    int isy = (int)lroundf(sy);
    if (isx < 0) isx = 0;
    if (isx >= g_source.width) isx = g_source.width - 1;
    if (isy < 0) isy = 0;
    if (isy >= g_source.height) isy = g_source.height - 1;

    size_t src_idx = ((size_t)isy * g_source.width + isx) * 3;
    size_t dst_idx = (size_t)i * 3;
    dst[dst_idx] = g_source.rgb888[src_idx];
    dst[dst_idx + 1] = g_source.rgb888[src_idx + 1];
    dst[dst_idx + 2] = g_source.rgb888[src_idx + 2];
  }

  xSemaphoreGive(g_source_mutex);
}

static void kaleidoscope_task(void *arg) {
  (void)arg;
  uint8_t *frame = malloc(CANVAS_WIDTH * CANVAS_HEIGHT * 3);
  if (!frame) {
    ESP_LOGE(TAG, "no memory for frame buffer, aborting animation");
    g_should_run = false;
    xSemaphoreGive(g_task_exited);
    vTaskDelete(NULL);
    return;
  }

  bool motion_zoom = kaleidobox_nvs_get_motion_zoom();
  float rotation_offset = 0.0f;
  float zoom_phase = 0.0f;

  while (g_should_run) {
    float zoom_scale = motion_zoom ? (1.0f + ZOOM_AMPLITUDE * sinf(zoom_phase)) : 1.0f;
    render_frame(frame, rotation_offset, zoom_scale);
    kaleidobox_matrix_draw_rgb888(frame, CANVAS_WIDTH, CANVAS_HEIGHT);

    // Wrapped, not left to grow forever - sinf/cosf(zoom_phase) is the
    // only place zoom_phase is used, rotation_offset only ever feeds
    // sample_angle before being passed to cosf/sinf too, and both are
    // periodic in 2*PI, so wrapping changes nothing about the output.
    // Real suspect for the reported "gets slow after running a while,
    // fixed by stop/start" - these grew completely unbounded before,
    // and stop/start resets a fresh kaleidoscope_task with both back
    // at 0.0f, which fits that exact symptom.
    rotation_offset += ROTATION_STEP;
    if (rotation_offset >= 2.0f * (float)M_PI) {
      rotation_offset -= 2.0f * (float)M_PI;
    }
    zoom_phase += ZOOM_STEP;
    if (zoom_phase >= 2.0f * (float)M_PI) {
      zoom_phase -= 2.0f * (float)M_PI;
    }

    vTaskDelay(pdMS_TO_TICKS(FRAME_INTERVAL_MS));
  }

  free(frame);
  xSemaphoreGive(g_task_exited);
  vTaskDelete(NULL);
}

esp_err_t kaleidobox_kaleidoscope_init(void) {
  g_task_exited = xSemaphoreCreateBinary();
  if (!g_task_exited) {
    return ESP_ERR_NO_MEM;
  }
  xSemaphoreGive(g_task_exited); // available - stop() on a never-started
                                 // animation shouldn't block

  g_source_mutex = xSemaphoreCreateMutex();
  if (!g_source_mutex) {
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "kaleidoscope_init");
  return ESP_OK;
}

esp_err_t kaleidobox_kaleidoscope_start(const kaleidobox_image_t *source) {
  if (!source || !source->rgb888 || source->width == 0 || source->height == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  kaleidobox_kaleidoscope_stop(); // clean restart if already running

  // Deep copy - the caller's buffer (e.g. a stack kaleidobox_image_t
  // wrapping the canvas buffer) isn't guaranteed to outlive this call,
  // and the animation task needs its own stable copy for its lifetime.
  size_t size = (size_t)source->width * source->height * 3;
  uint8_t *copy = malloc(size);
  if (!copy) {
    return ESP_ERR_NO_MEM;
  }
  memcpy(copy, source->rgb888, size);

  if (g_source.rgb888) {
    free(g_source.rgb888);
  }
  g_source.rgb888 = copy;
  g_source.width = source->width;
  g_source.height = source->height;

  precompute_pixel_tables(kaleidobox_nvs_get_fold_count());

  g_should_run = true;
  xSemaphoreTake(g_task_exited, 0); // claim it - task will give it back on exit
  // Pinned to core 1, opposite WiFi's own tasks (core 0) - same fix,
  // same reasoning as gallery_bg_task's pinning earlier this session
  // (see gallery.c): an unpinned low-priority task sharing core 0 with
  // WiFi's own higher-priority processing gets starved under network
  // activity. There it tanked upload throughput; here (user-reported)
  // it showed up as stuttering/low frame rate on the panel instead.
  BaseType_t ok = xTaskCreatePinnedToCore(kaleidoscope_task, "kaleidoscope",
                                          4096, NULL, tskIDLE_PRIORITY + 1,
                                          &g_task, 1);
  if (ok != pdPASS) {
    g_should_run = false;
    g_task = NULL;
    xSemaphoreGive(g_task_exited);
    return ESP_ERR_NO_MEM;
  }

  kaleidobox_nvs_set_kaleido_running(true);

  ESP_LOGI(TAG, "kaleidoscope started (%ux%u source, fold=%u)", source->width,
          source->height, kaleidobox_nvs_get_fold_count());
  return ESP_OK;
}

esp_err_t kaleidobox_kaleidoscope_update_source(const kaleidobox_image_t *source) {
  if (!source || !source->rgb888 || source->width == 0 || source->height == 0) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!g_should_run) {
    return ESP_ERR_INVALID_STATE; // nothing running to update
  }

  // Same deep-copy reasoning as start() - decode/allocate before
  // taking the lock, so the animation task is only blocked for the
  // actual pointer swap below, not for this call's malloc/memcpy.
  size_t size = (size_t)source->width * source->height * 3;
  uint8_t *copy = malloc(size);
  if (!copy) {
    return ESP_ERR_NO_MEM;
  }
  memcpy(copy, source->rgb888, size);

  xSemaphoreTake(g_source_mutex, portMAX_DELAY);
  uint8_t *old = g_source.rgb888;
  g_source.rgb888 = copy;
  g_source.width = source->width;
  g_source.height = source->height;
  xSemaphoreGive(g_source_mutex);
  free(old);

  return ESP_OK;
}

void kaleidobox_kaleidoscope_stop(void) {
  if (!g_task) {
    return;
  }
  g_should_run = false;
  xSemaphoreTake(g_task_exited, portMAX_DELAY); // wait for the task to actually exit
  g_task = NULL;

  // The animation task writes frames straight to the matrix
  // (kaleidobox_matrix_draw_rgb888), bypassing canvas.c entirely - so
  // canvas.c's own buffer still holds whatever was on the panel before
  // the animation started, untouched. Re-push it so the panel goes back
  // to the original image instead of freezing on the last animated
  // frame.
  kaleidobox_canvas_set_all(kaleidobox_canvas_buffer());
  kaleidobox_nvs_set_kaleido_running(false);
}

bool kaleidobox_kaleidoscope_is_running(void) { return g_task != NULL; }
