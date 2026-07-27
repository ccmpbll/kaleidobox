#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

// OTA update via the web app's firmware upload. Caller (http_server.c)
// streams the uploaded file in as it arrives: begin() once, write_chunk()
// per received buffer, then finish() to validate and switch the boot
// partition, or abort() if the upload fails/disconnects partway.
// Ported unchanged from printspy-cam - camera-agnostic.
esp_err_t kaleidobox_ota_begin(void);
esp_err_t kaleidobox_ota_write_chunk(const uint8_t *data, size_t len);
esp_err_t kaleidobox_ota_finish(void);
void kaleidobox_ota_abort(void);
