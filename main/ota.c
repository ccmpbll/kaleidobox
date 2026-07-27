#include "ota.h"

#include "esp_log.h"
#include "esp_ota_ops.h"

static const char *TAG = "kaleidobox_ota";

static esp_ota_handle_t ota_handle = 0;
static const esp_partition_t *update_partition = NULL;

esp_err_t kaleidobox_ota_begin(void) {
  update_partition = esp_ota_get_next_update_partition(NULL);
  if (!update_partition) {
    ESP_LOGE(TAG, "no OTA update partition available");
    return ESP_FAIL;
  }

  esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
  }
  return err;
}

esp_err_t kaleidobox_ota_write_chunk(const uint8_t *data, size_t len) {
  esp_err_t err = esp_ota_write(ota_handle, data, len);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
  }
  return err;
}

esp_err_t kaleidobox_ota_finish(void) {
  esp_err_t err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    // Most commonly: uploaded image failed validation (wrong chip target,
    // corrupted download, not actually a kaleidobox image).
    ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
    return err;
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
  }
  return err;
}

void kaleidobox_ota_abort(void) {
  if (ota_handle) {
    esp_ota_abort(ota_handle);
    ota_handle = 0;
  }
}
