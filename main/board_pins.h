#pragma once

// Waveshare ESP32-S3-RGB-Matrix pin map.
//
// GPIO numbers verified against Waveshare's own official ESP-IDF example
// (github.com/waveshareteam/ESP32-S3-RGB-Matrix,
// example/idf_v5.5.2/sdkconfig.defaults + .../components/bsp/
// esp32_s3_matrix/include/bsp/config.h), not scraped/AI-summarized text -
// same discipline printspy-cam learned the hard way to apply after a
// hallucinated pinout bit its nulllab board once.
//
// Cross-checked against the real schematic too
// (docs/ESP32-S3-RGB-Matrix-Schematics.pdf, indexed in docs/index.md):
// confirms the HUB75 header is buffered through two SN74HC245 tri-state
// ICs (not a direct GPIO connection), the J1 screen connector's signal
// *order* (R1,G1,B1,R2,G2,B2,A,B,C,D,E,CLK,LAT,OE) matches what's used
// below, and the TF socket exposes a full 4-bit SDMMC pinout even though
// only D0/CMD/CLK are actually wired per the BSP. The schematic's own
// GPIO-to-net number table didn't extract cleanly as text (too small/
// garbled to hand-transcribe reliably) - it corroborates the *shape* of
// the wiring, but the actual numbers below come from the working
// sdkconfig.defaults/config.h source, not read off the schematic image.

// --- HUB75 header (from sdkconfig.defaults CONFIG_HUB75_PIN_*) ---
#define MATRIX_PIN_R1 4
#define MATRIX_PIN_G1 5
#define MATRIX_PIN_B1 6
#define MATRIX_PIN_R2 7
#define MATRIX_PIN_G2 15
#define MATRIX_PIN_B2 16
#define MATRIX_PIN_A 18
#define MATRIX_PIN_B 8
#define MATRIX_PIN_C 3
#define MATRIX_PIN_D 42
#define MATRIX_PIN_E 9 // 64x64 (1/32 scan) needs the E line - this board wires it
#define MATRIX_PIN_CLK 41
#define MATRIX_PIN_LAT 40
#define MATRIX_PIN_OE 2

// --- TF (SD) card slot - 1-bit SDMMC mode (from bsp/config.h) ---
// D1/D2/D3 unconnected (GPIO_NUM_NC in the BSP) - board wires 1-bit, not
// 4-bit, SDMMC. A separate BSP_SD_SPI_CS (GPIO14) exists in config.h but
// MISO/MOSI/CLK for that mode are all NC there too - the SDMMC path below
// is the one the BSP's own bsp_sdcard_mount() actually uses.
#define SDCARD_PIN_CLK 1
#define SDCARD_PIN_CMD 44
#define SDCARD_PIN_D0 17

// --- Onboard peripherals (not used by KaleidoBox yet, listed for
// completeness / future use - e.g. IMU/RTC status, if ever wanted) ---
#define BOARD_PIN_I2C_SCL 48
#define BOARD_PIN_I2C_SDA 47
#define BOARD_PIN_BOOT_BUTTON 0
