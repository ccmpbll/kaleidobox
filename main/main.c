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

void app_main(void) {
  // Absolute first thing, before logging/NVS/anything else - the panel's
  // LED driver ICs hold their last-latched row data across an MCU reset
  // (they're separate hardware, not reset by the ESP32 resetting), and
  // without active scanning that stale data shows solid-on/full-
  // brightness instead of normally PWM-dimmed. Hub75Driver::begin()
  // zero-fills its buffers before starting the scan (confirmed in the
  // boot log), so getting here as fast as possible minimizes how long
  // leftover pixels from before the reset can show through. Doesn't
  // cover the ROM-bootloader window before app_main even starts - that's
  // outside app code's control.
  ESP_ERROR_CHECK(kaleidobox_matrix_init());

  // First of the rest, so the live log console can show everything from
  // here onward.
  kaleidobox_log_init();

  ESP_LOGI(TAG, "KaleidoBox %s starting", KALEIDOBOX_VERSION);

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  ESP_ERROR_CHECK(kaleidobox_nvs_init());

  ESP_ERROR_CHECK(kaleidobox_canvas_init());
  ESP_ERROR_CHECK(kaleidobox_kaleidoscope_init());
  // Not fatal if the card isn't present/mounted yet - gallery features
  // just stay unavailable (http_server.c's gallery handlers report 501)
  // until sdcard.c is actually implemented.
  kaleidobox_sdcard_init();
  ESP_ERROR_CHECK(kaleidobox_gallery_init());

  // Restore whatever was on the panel before the last reboot (canvas
  // content autosaved by gallery.c's background task) - a no-op if no
  // card is mounted or nothing's been saved yet, leaving the blank
  // canvas from kaleidobox_canvas_init() above untouched.
  kaleidobox_gallery_restore_state();

  // Kaleidoscope resume (if it was running when the device last
  // stopped) is NOT done here - it's deferred to wifi.c, once the
  // boot-time WiFi connecting animation / IP display sequence has
  // actually finished. Both that sequence and kaleidoscope's animation
  // task write straight to the matrix, bypassing canvas.c - starting
  // kaleidoscope here would have it fighting the WiFi status display
  // for the panel for the entire connect+10s-IP-display window,
  // meaning you'd never actually get to see the WiFi status or IP on
  // reboot if kaleidoscope had been running. See wifi.c's
  // ip_display_timeout_cb().

  // WiFi runs as its own task - AP fallback mode blocks forever until the
  // device reboots, so it can't live on app_main's own stack/task.
  xTaskCreateStatic(wifi_task_run, "wifi_task", WIFI_STACK_SIZE, NULL,
                    tskIDLE_PRIORITY + 1, wifiTaskStack, &wifiTaskBuffer);
}
