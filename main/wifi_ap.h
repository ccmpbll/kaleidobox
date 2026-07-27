#pragma once

#include <stdbool.h>

// AP-mode WiFi provisioning, ported from printspy-cam (itself ported from
// bitclock-redux's main/tasks/wifi_ap.c). SoftAP + a setup page at
// 192.168.4.1 with a network-scan dropdown; submitting credentials writes
// them via esp_wifi_set_config (which persists to NVS on its own) and
// reboots.

void kaleidobox_wifi_ap_start(bool is_fallback);
