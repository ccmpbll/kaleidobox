#include "canvas.h"
#include "esp_log.h"
#include "gallery.h"
#include "kaleidoscope.h"
#include "log.h"
#include "matrix.h"
#include "nvs_flash.h"
#include "sdcard.h"
#include "settings.h"
#include "version.h"
#include "wifi.h"

static const char *TAG = "kaleidobox";

// TEMPORARY - visual bring-up check only, remove once draw mode exists.
// Same idea as esp-hub75's own simple_colors example: colored squares in
// each corner + a center cross, so pin mapping/color order can be
// confirmed by eye (right color in the right corner) before building
// anything on top of the matrix driver.
static void draw_test_pattern(void) {
  const uint8_t sq = 8;

  for (uint8_t y = 0; y < sq; y++) {
    for (uint8_t x = 0; x < sq; x++) {
      kaleidobox_matrix_set_pixel(x, y, 255, 0, 0); // top-left: red
      kaleidobox_matrix_set_pixel(63 - x, y, 0, 255, 0); // top-right: green
      kaleidobox_matrix_set_pixel(x, 63 - y, 0, 0, 255); // bottom-left: blue
      kaleidobox_matrix_set_pixel(63 - x, 63 - y, 255, 255, 255); // bottom-right: white
    }
  }

  for (uint8_t i = 22; i <= 42; i++) {
    kaleidobox_matrix_set_pixel(i, 32, 0, 255, 255); // horizontal cyan line
    kaleidobox_matrix_set_pixel(32, i, 0, 255, 255); // vertical cyan line
  }

  kaleidobox_matrix_flip();
  ESP_LOGI(TAG, "test pattern drawn: red/green/blue/white corners, cyan cross");
}

void app_main(void) {
  // First, so the live log console can show everything from boot onward.
  kaleidobox_log_init();

  ESP_LOGI(TAG, "KaleidoBox %s starting", KALEIDOBOX_VERSION);

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  ESP_ERROR_CHECK(kaleidobox_nvs_init());

  ESP_ERROR_CHECK(kaleidobox_matrix_init());
  draw_test_pattern();
  ESP_ERROR_CHECK(kaleidobox_canvas_init());
  ESP_ERROR_CHECK(kaleidobox_kaleidoscope_init());
  // Not fatal if the card isn't present/mounted yet - gallery features
  // just stay unavailable (http_server.c's gallery handlers report 501)
  // until sdcard.c is actually implemented.
  kaleidobox_sdcard_init();
  ESP_ERROR_CHECK(kaleidobox_gallery_init());

  // WiFi runs as its own task - AP fallback mode blocks forever until the
  // device reboots, so it can't live on app_main's own stack/task.
  xTaskCreateStatic(wifi_task_run, "wifi_task", WIFI_STACK_SIZE, NULL,
                    tskIDLE_PRIORITY + 1, wifiTaskStack, &wifiTaskBuffer);
}
