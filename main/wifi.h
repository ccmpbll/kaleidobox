#pragma once

#include "freertos/FreeRTOS.h"
#include <stdbool.h>
#include <stddef.h>

// STA-mode WiFi connection lifecycle, ported from printspy-cam (itself
// ported from bitclock-redux's main/tasks/wifi.c). Runs as its own
// FreeRTOS task since AP fallback blocks forever until the device
// reboots - see wifi_task_run.
//
// State machine:
//   No STA credentials stored -> AP provisioning mode (wifi_ap.h), blocks
//     until the user submits WiFi creds via the setup page, then reboots.
//   Credentials stored -> STA connect. Every disconnect event re-triggers
//     a connect attempt. Before the first successful IP, attempts are
//     capped at WIFI_MAX_BOOT_RETRIES - exhausting them falls back to AP
//     mode (credentials are probably wrong). After the first successful
//     IP, disconnects retry silently and unlimited (assume transient
//     network blip, never kick the user back into reconfiguring).

#define WIFI_STACK_SIZE 1024 * 4

#define WIFI_READY_TO_CONNECT_EVENT BIT1
#define WIFI_AP_MODE_ACTIVE_EVENT BIT2
#define WIFI_AP_FALLBACK_EVENT BIT3
extern EventGroupHandle_t wifi_event_group_handle;

extern SemaphoreHandle_t wifi_req_semaphore;

extern StaticTask_t wifiTaskBuffer;
extern StackType_t wifiTaskStack[WIFI_STACK_SIZE];

void wifi_task_run(void *pvParameters);

bool kaleidobox_wifi_has_credentials(void);

// Writes a 4-hex-char device ID (last 2 bytes of the base MAC, e.g.
// "a1b2") into out. out_size must be >= 5. Shared by the hostname and the
// AP setup SSID so both default to a stable, unique-enough identifier.
void kaleidobox_wifi_get_id_suffix(char *out, size_t out_size);
