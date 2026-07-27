#include "sdcard.h"

#include "esp_log.h"
#include <stdbool.h>

static const char *TAG = "sdcard";
static bool g_mounted = false;

esp_err_t kaleidobox_sdcard_init(void) {
  // TODO: esp_vfs_fat_sdmmc_mount using board_pins.h's SDCARD_PIN_CLK/
  // CMD/D0 (1-bit SDMMC, confirmed against the official BSP), mount point
  // "/sdcard". Not yet implemented.
  ESP_LOGW(TAG, "sdcard_init not yet implemented");
  g_mounted = false;
  return ESP_ERR_NOT_SUPPORTED;
}

bool kaleidobox_sdcard_is_mounted(void) { return g_mounted; }
