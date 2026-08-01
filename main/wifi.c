#include "wifi.h"

#include "canvas.h"
#include "clock.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "font_5x7.h"
#include "http_server.h"
#include "kaleidoscope.h"
#include "matrix.h"
#include "mdns.h"
#include "panel_takeover.h"
#include "printspy.h"
#include "settings.h"
#include "wifi_ap.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "wifi";

#define WIFI_MAX_BOOT_RETRIES 10

EventGroupHandle_t wifi_event_group_handle;
static StaticEventGroup_t wifi_event_group;

StaticTask_t wifiTaskBuffer;
StackType_t wifiTaskStack[WIFI_STACK_SIZE];

SemaphoreHandle_t wifi_req_semaphore;
static StaticSemaphore_t wifi_req_semaphore_mutex_buffer;

static bool wifi_ever_had_ip = false;
static bool mdns_started = false;

void kaleidobox_wifi_get_id_suffix(char *out, size_t out_size) {
  uint8_t mac[6] = {0};
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  snprintf(out, out_size, "%02x%02x", mac[4], mac[5]);
}

// Must run before esp_wifi_start()/DHCP negotiation - see printspy-cam's
// wifi.c history for why this can't wait until IP_EVENT_STA_GOT_IP.
static void set_dhcp_hostname(void) {
  char suffix[5];
  kaleidobox_wifi_get_id_suffix(suffix, sizeof(suffix));
  char hostname[32];
  snprintf(hostname, sizeof(hostname), "kaleidobox-%s", suffix);

  esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (sta_netif) {
    esp_netif_set_hostname(sta_netif, hostname);
  }
}

// Idempotent - IP_EVENT_STA_GOT_IP can refire on reconnect.
static void start_mdns(void) {
  if (mdns_started) {
    return;
  }

  char suffix[5];
  kaleidobox_wifi_get_id_suffix(suffix, sizeof(suffix));
  char hostname[32];
  snprintf(hostname, sizeof(hostname), "kaleidobox-%s", suffix);

  esp_err_t err = mdns_init();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "mdns_init failed: %s", esp_err_to_name(err));
    return;
  }
  mdns_hostname_set(hostname);
  mdns_instance_name_set("KaleidoBox");
  mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

  mdns_started = true;
  ESP_LOGI(TAG, "mDNS started - reachable at http://%s.local", hostname);
}

// Animated "connecting" indicator - 4 signal bars of increasing height,
// growing one-by-one (lit_count 1..4, repeating) while the STA connect
// retry loop runs. Same start/stop-with-rendezvous pattern as
// kaleidoscope.c's animation task: a plain "set a flag and move on"
// stop would race the task's own redraw loop, which is exactly the
// class of bug already hit twice this session (kaleidoscope
// overwriting the panel after Clear, and after its own stop) - here
// it'd mean a stray bars frame landing after the real IP had already
// been drawn. stop_wifi_connecting_anim() blocks until the task has
// actually exited before anything else touches the matrix.
static TaskHandle_t wifi_anim_task = NULL;
static SemaphoreHandle_t wifi_anim_task_exited = NULL;
static volatile bool wifi_anim_should_run = false;

#define WIFI_ANIM_FRAME_MS 350

static void draw_signal_bars(int lit_count) {
  static const int heights[4] = {6, 12, 18, 24};
  const int bar_w = 7, gap = 2, baseline_y = 38;
  int total_w = 4 * bar_w + 3 * gap;
  int x0 = (64 - total_w) / 2;
  for (int i = 0; i < 4; i++) {
    int x = x0 + i * (bar_w + gap);
    bool lit = i < lit_count;
    uint8_t r = 0, g = lit ? 200 : 30, b = lit ? 255 : 40;
    for (int dy = 0; dy < heights[i]; dy++) {
      uint8_t y = (uint8_t)(baseline_y - dy);
      for (int dx = 0; dx < bar_w; dx++) {
        kaleidobox_matrix_set_pixel((uint8_t)(x + dx), y, r, g, b);
      }
    }
  }
}

static void wifi_connecting_anim_task(void *arg) {
  (void)arg;
  int lit = 1;
  while (wifi_anim_should_run) {
    kaleidobox_matrix_clear();
    draw_signal_bars(lit);
    kaleidobox_font_draw_text_centered(44, "Connecting", 0, 200, 255);
    lit = (lit % 4) + 1;
    vTaskDelay(pdMS_TO_TICKS(WIFI_ANIM_FRAME_MS));
  }
  xSemaphoreGive(wifi_anim_task_exited);
  vTaskDelete(NULL);
}

// Boot-time only, same reasoning as show_ip_on_matrix() - panel's blank,
// nothing to clobber.
static void start_wifi_connecting_anim(void) {
  wifi_anim_task_exited = xSemaphoreCreateBinary();
  if (!wifi_anim_task_exited) {
    return;
  }
  wifi_anim_should_run = true;
  if (xTaskCreate(wifi_connecting_anim_task, "wifi_anim", 2048, NULL,
                  tskIDLE_PRIORITY + 1, &wifi_anim_task) != pdPASS) {
    wifi_anim_should_run = false;
    wifi_anim_task = NULL;
  }
}

// Must be called (and must finish) before anything else draws to the
// matrix - see the block comment above. A no-op if the animation was
// never started (e.g. AP mode never runs it).
static void stop_wifi_connecting_anim(void) {
  if (!wifi_anim_task) {
    return;
  }
  wifi_anim_should_run = false;
  xSemaphoreTake(wifi_anim_task_exited, portMAX_DELAY);
  vSemaphoreDelete(wifi_anim_task_exited);
  wifi_anim_task_exited = NULL;
  wifi_anim_task = NULL;
}

#define IP_DISPLAY_TIMEOUT_US (10 * 1000 * 1000) // 10s

static esp_timer_handle_t ip_display_timeout_timer = NULL;

// Same trick as kaleidobox_kaleidoscope_stop(): canvas.c's own buffer is
// untouched by anything the boot-time status display draws (that writes
// straight to the matrix, bypassing canvas.c entirely), so re-pushing it
// restores whatever's actually real - blank if nothing's happened since
// boot, or the user's own drawing/upload if they started using the
// device during those 10s. Never just blindly clears; that would
// clobber real content the same way the Clear-button bug did.
static void ip_display_timeout_cb(void *arg) {
  (void)arg;

  // This 10s timer is boot-sequence-only and predates panel_takeover.c
  // (added later for PrintSpy/weather) - it has no idea a takeover
  // might already be running, and was unconditionally reclaiming the
  // panel + resuming kaleidoscope regardless. Confirmed live: a real
  // print already running meant PrintSpy's takeover began around ~7s
  // post-boot (right after MQTT connects and the retained message
  // arrives), then THIS timer fired at the unrelated 10s-after-IP mark
  // and stomped it - repushed the canvas and restarted kaleidoscope
  // over whatever PrintSpy had just drawn, with PrintSpy's own state
  // never told any of that happened. If something already owns the
  // panel, skip entirely - it'll do its own canvas-restore/kaleidoscope-
  // resume when its own takeover ends (see panel_takeover.c).
  if (kaleidobox_panel_takeover_active()) {
    return;
  }

  kaleidobox_canvas_repaint();

  // Resume kaleidoscope here, not at boot in main.c - both this whole
  // status sequence (connecting bars, then the IP itself) and
  // kaleidoscope's animation task write straight to the matrix,
  // bypassing canvas.c. Starting kaleidoscope any earlier would have
  // it fighting the status display for the panel the entire time,
  // meaning a reboot with kaleidoscope already running would never
  // actually show WiFi status or the IP. is_running() guard covers the
  // unlikely case something already started it during the 10s window.
  if (kaleidobox_nvs_get_kaleido_running() && !kaleidobox_kaleidoscope_is_running()) {
    kaleidobox_image_t source = {
        .rgb888 = (uint8_t *)kaleidobox_canvas_buffer(),
        .width = CANVAS_WIDTH,
        .height = CANVAS_HEIGHT,
    };
    kaleidobox_kaleidoscope_start(&source);
  }
}

// Only ever called during the initial boot-time connection window (see
// call sites below) - the matrix runs single-buffered, so drawing here
// writes straight to the panel, bypassing canvas.c entirely. Fine at
// boot (panel starts blank, nothing to clobber), but must never run
// again on a later mid-session reconnect once the user has actual
// content (drawn/uploaded/kaleidoscope) on screen - same class of bug
// already caught once with kaleidoscope_stop() overwriting the panel
// unexpectedly.
static void show_ip_on_matrix(const esp_ip4_addr_t *ip) {
  char full[16], line1[16], line2[16];
  snprintf(full, sizeof(full), "%u.%u.%u.%u", esp_ip4_addr1_16(ip),
          esp_ip4_addr2_16(ip), esp_ip4_addr3_16(ip), esp_ip4_addr4_16(ip));
  kaleidobox_matrix_clear();

  // Most real LAN IPs (up to 12-13 chars, e.g. "10.42.11.140") fit on one
  // line once letter-spacing tightens - only fall back to splitting by
  // octet pairs for genuinely long ones (up to "255.255.255.255", 15
  // chars, which can't fit on one line at any legible size with this
  // font). Single line first since a 4-way octet split is uglier than
  // it needs to be for the common case.
  if (!kaleidobox_font_draw_text_fit(28, full, 0, 200, 255)) { // (64-7)/2
    // Wrap the real dotted string at its middle dot rather than
    // regrouping into two independent "a.b" pairs - keeps the trailing
    // "." on line 1 (e.g. "255.255." / "255.255"), reading as one
    // address wrapped in place instead of two disconnected halves.
    char *first_dot = strchr(full, '.');
    char *mid_dot = first_dot ? strchr(first_dot + 1, '.') : NULL;
    size_t split_at = mid_dot ? (size_t)(mid_dot - full) + 1 : strlen(full);
    snprintf(line1, sizeof(line1), "%.*s", (int)split_at, full);
    snprintf(line2, sizeof(line2), "%s", full + split_at);
    // Two 7px-tall lines with a 2px gap = 16px block, centered vertically.
    kaleidobox_font_draw_text_centered(24, line1, 0, 200, 255);
    kaleidobox_font_draw_text_centered(33, line2, 0, 200, 255);
  }

  const esp_timer_create_args_t timer_args = {
      .callback = ip_display_timeout_cb,
      .name = "ip_display_timeout",
  };
  if (esp_timer_create(&timer_args, &ip_display_timeout_timer) == ESP_OK) {
    esp_timer_start_once(ip_display_timeout_timer, IP_DISPLAY_TIMEOUT_US);
  }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  // Avoid any esp_wifi_*() calls directly here - done in the WiFi task,
  // which claims wifi_req_semaphore, to avoid races with other tasks.
  if (event_base == WIFI_EVENT) {
    switch (event_id) {
    case WIFI_EVENT_STA_START:
      xEventGroupSetBits(wifi_event_group_handle, WIFI_READY_TO_CONNECT_EVENT);
      break;
    case WIFI_EVENT_STA_DISCONNECTED:
      ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
      xEventGroupSetBits(wifi_event_group_handle, WIFI_READY_TO_CONNECT_EVENT);
      break;
    default:
      break;
    }
  } else if (event_base == IP_EVENT) {
    switch (event_id) {
    case IP_EVENT_STA_GOT_IP: {
      ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
      bool first_connect = !wifi_ever_had_ip;
      wifi_ever_had_ip = true;
      ESP_LOGI(TAG, "Connected with IP Address:" IPSTR,
               IP2STR(&event->ip_info.ip));
      // Only the very first connect, same reasoning as show_ip_on_matrix's
      // comment - a later reconnect must not clobber real panel content.
      if (first_connect) {
        stop_wifi_connecting_anim(); // must finish before show_ip_on_matrix draws
        show_ip_on_matrix(&event->ip_info.ip);
      }
      start_mdns();
      // Idempotent - safe on every reconnect, see its own comment.
      kaleidobox_clock_start_sntp();
      // Idempotent - only starts the HTTP server on first IP.
      kaleidobox_http_server_start();
      // Idempotent - no-ops if already started, or if printspy_en is
      // off / no broker is configured (see printspy.c).
      kaleidobox_printspy_start();
      break;
    }
    default:
      break;
    }
  }
}

static void wifi_driver_init(void) {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  wifi_event_group_handle = xEventGroupCreateStatic(&wifi_event_group);

  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                             &event_handler, NULL));

  esp_netif_create_default_wifi_sta();
  esp_netif_create_default_wifi_ap();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  set_dhcp_hostname();

  wifi_req_semaphore =
      xSemaphoreCreateMutexStatic(&wifi_req_semaphore_mutex_buffer);
}

bool kaleidobox_wifi_has_credentials(void) {
  wifi_config_t cfg = {0};
  if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK) {
    return false;
  }
  return cfg.sta.ssid[0] != '\0';
}

static void enter_ap_mode(bool is_fallback) {
  ESP_LOGI(TAG, "Entering AP provisioning mode (fallback=%d)", is_fallback);
  EventBits_t bits = WIFI_AP_MODE_ACTIVE_EVENT;
  if (is_fallback) {
    bits |= WIFI_AP_FALLBACK_EVENT;
  }
  xEventGroupSetBits(wifi_event_group_handle, bits);
  kaleidobox_wifi_ap_start(is_fallback);
  // Block here - AP mode only ends via reboot (triggered inside the setup
  // page's POST handler), so this task effectively sleeps until restart.
  while (1) {
    vTaskDelay(portMAX_DELAY);
  }
}

void wifi_task_run(void *pvParameters) {
  wifi_driver_init();

  // If no credentials are saved, go straight to AP mode for first-time setup.
  if (!kaleidobox_wifi_has_credentials()) {
    ESP_LOGI(TAG, "No WiFi credentials found, starting AP setup");
    enter_ap_mode(false);
    return; // unreachable - enter_ap_mode blocks until reboot
  }

  // Credentials exist: start STA and attempt to connect. Boot-time only
  // (this function runs once) - panel's still blank at this point, same
  // reasoning as show_ip_on_matrix().
  start_wifi_connecting_anim();

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
  // Mains-powered device, not battery - no reason to trade connection
  // reliability for power savings we don't need (same reasoning printspy-cam
  // used after modem-sleep wake jitter caused real problems for it).
  esp_wifi_set_ps(WIFI_PS_NONE);

  EventBits_t wifi_event_bits = 0;
  int boot_retry_count = 0;

  while (1) {
    if (!xSemaphoreTake(wifi_req_semaphore, portMAX_DELAY)) {
      continue;
    }

    if (wifi_event_bits & WIFI_READY_TO_CONNECT_EVENT) {
      if (!wifi_ever_had_ip) {
        // Still in the boot-time connection window - enforce retry limit.
        if (boot_retry_count >= WIFI_MAX_BOOT_RETRIES) {
          xSemaphoreGive(wifi_req_semaphore);
          ESP_LOGW(TAG, "Exhausted %d boot retries, entering AP mode",
                   WIFI_MAX_BOOT_RETRIES);
          stop_wifi_connecting_anim(); // AP mode doesn't use the matrix at all yet
          enter_ap_mode(true);
          return; // unreachable
        }
        boot_retry_count++;
        ESP_LOGI(TAG, "STA connect attempt %d/%d", boot_retry_count,
                 WIFI_MAX_BOOT_RETRIES);
      }
      esp_wifi_connect();
    }

    xSemaphoreGive(wifi_req_semaphore);

    wifi_event_bits = xEventGroupWaitBits(wifi_event_group_handle,
                                          WIFI_READY_TO_CONNECT_EVENT,
                                          true,  // clear on exit
                                          false, // wait for all
                                          portMAX_DELAY);
  }
}
