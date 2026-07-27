#include "sdcard.h"

#include "board_pins.h"
#include "driver/sdmmc_default_configs.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include <stdbool.h>

static const char *TAG = "sdcard";
static bool g_mounted = false;

#define MOUNT_POINT "/sdcard"

esp_err_t kaleidobox_sdcard_init(void) {
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 8,
      .allocation_unit_size = 16 * 1024,
  };

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_1BIT; // board only wires D0/CMD/CLK - see
                                     // board_pins.h's sourcing comment

  // GPIO numbers below don't match either of ESP32-S3's fixed SDMMC slot
  // pinouts (there isn't one - S3 routes SDMMC through the GPIO matrix),
  // so every pin has to be given explicitly rather than relying on
  // SDMMC_SLOT_CONFIG_DEFAULT()'s built-in numbers.
  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.clk = SDCARD_PIN_CLK;
  slot_config.cmd = SDCARD_PIN_CMD;
  slot_config.d0 = SDCARD_PIN_D0;
  slot_config.width = 1;
  slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  sdmmc_card_t *card = NULL;
  esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config,
                                          &mount_config, &card);
  if (err != ESP_OK) {
    // Not fatal - no card inserted is a normal, expected state (see
    // main.c's comment at the call site). Gallery features just stay
    // unavailable until one's inserted and the device reboots.
    ESP_LOGW(TAG, "sdcard mount failed (%s) - gallery features unavailable",
             esp_err_to_name(err));
    g_mounted = false;
    return err;
  }

  ESP_LOGI(TAG, "sdcard mounted at " MOUNT_POINT);
  g_mounted = true;
  return ESP_OK;
}

bool kaleidobox_sdcard_is_mounted(void) { return g_mounted; }
