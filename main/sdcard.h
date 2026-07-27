#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// Mounts the TF card as FAT via 1-bit SDMMC (esp_vfs_fat_sdmmc_mount) -
// confirmed against Waveshare's official BSP (D1/D2/D3 are unconnected on
// this board, only D0/CMD/CLK are wired - see board_pins.h's SDCARD_PIN_*
// values and their sourcing comment). Mount point "/sdcard", matching the
// official BSP's BSP_SD_MOUNT_POINT. Gallery images live under a fixed
// directory once mounted (see gallery.h). Not fatal if no card is
// present or mount fails - kaleidobox_sdcard_is_mounted() reports false
// and gallery/persistence features just stay unavailable.

esp_err_t kaleidobox_sdcard_init(void);
bool kaleidobox_sdcard_is_mounted(void);

// Total/free space on the mounted FAT filesystem (not the raw card
// capacity - FAT overhead/reserved clusters mean these are always a
// bit less than the card's advertised size). ESP_ERR_NOT_SUPPORTED if
// nothing's mounted.
esp_err_t kaleidobox_sdcard_get_space(uint64_t *total_bytes, uint64_t *free_bytes);
