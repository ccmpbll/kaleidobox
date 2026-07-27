#pragma once

// Waveshare ESP32-S3-RGB-Matrix pin map.
//
// PLACEHOLDER VALUES - NOT YET VERIFIED AGAINST REAL HARDWARE OR AN
// OFFICIAL SCHEMATIC. docs.waveshare.com/www.waveshare.com 403'd a plain
// WebFetch during initial research; only fragmentary info came back via
// search. printspy-cam was bitten once already by trusting an
// AI-summarized/scraped pinout instead of the real schematic (two D5/D7
// data lines transposed on the nulllab board) - do not wire real hardware
// or trust these numbers until they're confirmed against Waveshare's
// actual wiki/schematic or the board's own silkscreen.

// --- HUB75 header (R1,G1,B1,R2,G2,B2,A,B,C,D,E,CLK,LAT,OE) ---
#define MATRIX_PIN_R1 -1
#define MATRIX_PIN_G1 -1
#define MATRIX_PIN_B1 -1
#define MATRIX_PIN_R2 -1
#define MATRIX_PIN_G2 -1
#define MATRIX_PIN_B2 -1
#define MATRIX_PIN_A -1
#define MATRIX_PIN_B -1
#define MATRIX_PIN_C -1
#define MATRIX_PIN_D -1
#define MATRIX_PIN_E -1 // 64x64 (1/32 scan) needs the E line - not all HUB75 panels wire it
#define MATRIX_PIN_CLK -1
#define MATRIX_PIN_LAT -1
#define MATRIX_PIN_OE -1

// --- TF (SD) card slot - SDMMC vs SPI mode not yet confirmed ---
#define SDCARD_PIN_CLK -1
#define SDCARD_PIN_CMD -1
#define SDCARD_PIN_D0 -1
