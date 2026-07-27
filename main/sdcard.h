#pragma once

#include "esp_err.h"

// Mounts the TF card as FAT (esp_vfs_fat_sdmmc_mount or sdspi, depending
// on how the board actually wires the slot - see board_pins.h's
// SDCARD_PIN_* placeholders). Gallery images live under a fixed directory
// once mounted (see gallery.h).
//
// STUB - not yet implemented.

esp_err_t kaleidobox_sdcard_init(void);
bool kaleidobox_sdcard_is_mounted(void);
