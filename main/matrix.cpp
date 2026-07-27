#include "matrix.h"

#include "board_pins.h"
#include "esp_log.h"
#include "hub75.h"

static const char *TAG = "matrix";
static Hub75Driver *driver = nullptr;

// Panel: Waveshare RGB-Matrix-P2-64x64, 1/32 scan. STANDARD_TWO_SCAN is
// the right scan_wiring for this - confirmed via Waveshare's own working
// example (sdkconfig.defaults sets CONFIG_HUB75_WIRING_STANDARD=y, which
// hub75_bridge.cpp maps to STANDARD_TWO_SCAN), not guessed: per esp-hub75's
// own docs, num_rows = panel_height/2 under STANDARD_TWO_SCAN, i.e. 32
// rows for a 64-tall panel - exactly a 1/32 scan.
static Hub75Config make_config() {
  Hub75Config cfg{};
  cfg.panel_width = 64;
  cfg.panel_height = 64;
  cfg.scan_wiring = Hub75ScanWiring::STANDARD_TWO_SCAN;
  cfg.shift_driver = Hub75ShiftDriver::GENERIC;
  // Double-buffered to match kaleidobox_matrix_flip()'s tear-free-update
  // contract (matrix.h) - the kaleidoscope animation and instant-draw
  // mode both rely on flip() only swapping once a frame is fully drawn.
  cfg.double_buffer = true;

  cfg.pins.r1 = MATRIX_PIN_R1;
  cfg.pins.g1 = MATRIX_PIN_G1;
  cfg.pins.b1 = MATRIX_PIN_B1;
  cfg.pins.r2 = MATRIX_PIN_R2;
  cfg.pins.g2 = MATRIX_PIN_G2;
  cfg.pins.b2 = MATRIX_PIN_B2;
  cfg.pins.a = MATRIX_PIN_A;
  cfg.pins.b = MATRIX_PIN_B;
  cfg.pins.c = MATRIX_PIN_C;
  cfg.pins.d = MATRIX_PIN_D;
  cfg.pins.e = MATRIX_PIN_E;
  cfg.pins.lat = MATRIX_PIN_LAT;
  cfg.pins.oe = MATRIX_PIN_OE;
  cfg.pins.clk = MATRIX_PIN_CLK;

  return cfg;
}

extern "C" esp_err_t kaleidobox_matrix_init(void) {
  if (driver) {
    return ESP_OK; // already initialized
  }

  Hub75Config cfg = make_config();
  driver = new Hub75Driver(cfg);
  if (!driver->begin()) {
    ESP_LOGE(TAG, "Hub75Driver::begin() failed");
    delete driver;
    driver = nullptr;
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "matrix_init ok (%dx%d)", driver->get_width(), driver->get_height());
  return ESP_OK;
}

extern "C" void kaleidobox_matrix_set_pixel(uint8_t x, uint8_t y, uint8_t r,
                                            uint8_t g, uint8_t b) {
  if (!driver) {
    return;
  }
  driver->set_pixel(x, y, r, g, b);
}

extern "C" void kaleidobox_matrix_flip(void) {
  if (!driver) {
    return;
  }
  driver->flip_buffer();
}

extern "C" void kaleidobox_matrix_clear(void) {
  if (!driver) {
    return;
  }
  driver->clear();
}
