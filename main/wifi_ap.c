#include "wifi_ap.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "wifi_ap";

#define AP_MAX_CONNECTIONS 4
static char ap_ssid[24]; // "kaleidobox-setup-" + 4 hex chars + null

static httpd_handle_t http_server = NULL;

// Minimal URI-decode: decodes %XX and converts + to space, in-place.
static void uri_decode(char *dst, const char *src, size_t dst_len) {
  size_t di = 0;
  for (size_t si = 0; src[si] && di + 1 < dst_len; si++) {
    if (src[si] == '%' && src[si + 1] && src[si + 2]) {
      char hex[3] = {src[si + 1], src[si + 2], '\0'};
      dst[di++] = (char)strtol(hex, NULL, 16);
      si += 2;
    } else if (src[si] == '+') {
      dst[di++] = ' ';
    } else {
      dst[di++] = src[si];
    }
  }
  dst[di] = '\0';
}

static const char *SETUP_PAGE_TEMPLATE =
    "<!DOCTYPE html>"
    "<html lang='en'>"
    "<head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>KaleidoBox Wi-Fi Setup</title>"
    "<style>"
    "body{margin:0 auto;padding:24px 16px;max-width:432px;"
    "box-sizing:border-box;background:rgb(18,18,20);"
    "color:rgb(230,230,232);font-family:-apple-system,BlinkMacSystemFont,"
    "'Segoe UI',Roboto,Helvetica,Arial,sans-serif;}"
    "h1{font-size:1.4rem;font-weight:700;margin:0 0 4px;}"
    "p.sub{margin:0 0 24px;color:rgb(150,150,160);font-size:.9rem;}"
    ".card{background:rgb(28,28,32);border:1px solid rgb(55,55,62);"
    "border-radius:8px;padding:20px;}"
    "label{display:block;font-size:.85rem;font-weight:600;margin-bottom:4px;}"
    "select,input{width:100%;box-sizing:border-box;padding:8px 10px;"
    "border:1px solid rgb(60,60,68);border-radius:6px;"
    "font-size:.95rem;background:rgb(18,18,20);color:rgb(230,230,232);"
    "margin-bottom:16px;}"
    "button{width:100%;padding:12px;border:none;border-radius:6px;"
    "background:linear-gradient(217deg,#4c6ef5,#22b8cf);"
    "color:#fff;font-size:1rem;font-weight:600;cursor:pointer;}"
    "button:active{opacity:.85;}"
    ".pw-wrap{position:relative;}"
    ".pw-wrap input{padding-right:56px;}"
    ".pw-toggle{position:absolute;top:0;right:0;width:auto;height:100%;"
    "padding:0 12px;border:none;background:none;"
    "color:rgb(120,150,255);font-size:.8rem;font-weight:600;}"
    ".err{color:rgb(255,130,130);font-size:.85rem;margin-bottom:12px;"
    "display:none;}"
    "</style>"
    "</head>"
    "<body>"
    "<h1>KaleidoBox Wi-Fi Setup</h1>"
    "<p class='sub'>%s</p>"
    "<div class='card'>"
    "<form method='POST' action='/connect' id='f'>"
    "<label for='ssid'>Network</label>"
    "<select name='ssid' id='ssid'>%s</select>"
    "<label for='pw'>Password</label>"
    "<div class='pw-wrap'>"
    "<input type='password' name='password' id='pw' autocomplete='current-password'>"
    "<button type='button' class='pw-toggle' id='pwToggle'>Show</button>"
    "</div>"
    "<p class='err' id='err'>Please select a network.</p>"
    "<button type='submit'>Connect</button>"
    "</form>"
    "</div>"
    "<script>"
    "document.getElementById('f').onsubmit=function(){"
    "if(!document.getElementById('ssid').value){"
    "document.getElementById('err').style.display='block';return false;}"
    "return true;};"
    "document.getElementById('pwToggle').onclick=function(){"
    "var pw=document.getElementById('pw');"
    "var showing=pw.type==='text';"
    "pw.type=showing?'password':'text';"
    "this.textContent=showing?'Show':'Hide';};"
    "</script>"
    "</body>"
    "</html>";

// %s is the mDNS hostname (e.g. "kaleidobox-a1b2") - known up front since
// it's derived from the device's own MAC, not from DHCP, so it can be
// shown here even though the device hasn't connected (or gotten an IP)
// yet. Simpler and more resilient than trying to display a live IP
// address discovered after reconnecting, which would need the AP-mode
// page (about to go down for the reboot) to somehow learn it after the
// fact.
static const char *SUCCESS_PAGE_TEMPLATE =
    "<!DOCTYPE html>"
    "<html lang='en'>"
    "<head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>KaleidoBox</title>"
    "<style>"
    "body{margin:0 auto;padding:24px 16px;max-width:432px;"
    "box-sizing:border-box;background:rgb(18,18,20);"
    "color:rgb(230,230,232);font-family:-apple-system,BlinkMacSystemFont,"
    "'Segoe UI',Roboto,Helvetica,Arial,sans-serif;text-align:center;}"
    "h1{font-size:1.4rem;font-weight:700;margin:48px 0 8px;}"
    "p{color:rgb(150,150,160);font-size:.95rem;}"
    "a{color:rgb(120,150,255);}"
    "</style>"
    "</head>"
    "<body>"
    "<h1>Credentials saved.</h1>"
    "<p>KaleidoBox is restarting and will connect to your network.</p>"
    "<p>Once connected, find it at <a href='http://%s.local'>http://%s.local</a></p>"
    "<p>You can close this page.</p>"
    "</body>"
    "</html>";

static esp_err_t get_handler(httpd_req_t *req) {
  wifi_scan_config_t scan_cfg = {
      .ssid = NULL,
      .bssid = NULL,
      .channel = 0,
      .show_hidden = false,
  };

  esp_wifi_scan_start(&scan_cfg, true); // blocking scan

  uint16_t ap_count = 0;
  esp_wifi_scan_get_ap_num(&ap_count);
  if (ap_count > 20) {
    ap_count = 20;
  }

  wifi_ap_record_t *ap_records =
      (wifi_ap_record_t *)malloc(ap_count * sizeof(wifi_ap_record_t));
  if (!ap_records) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  esp_wifi_scan_get_ap_records(&ap_count, ap_records);

  // Build <option> list - each entry: template(~27 bytes) + 2x SSID (max 32 bytes each)
  size_t opts_size = ap_count * 96 + 64;
  char *opts = (char *)malloc(opts_size);
  if (!opts) {
    free(ap_records);
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  opts[0] = '\0';
  strlcat(opts, "<option value=''>Select a network...</option>", opts_size);
  for (uint16_t i = 0; i < ap_count; i++) {
    char opt[96];
    snprintf(opt, sizeof(opt), "<option value='%s'>%s</option>",
             (char *)ap_records[i].ssid, (char *)ap_records[i].ssid);
    strlcat(opts, opt, opts_size);
  }
  free(ap_records);

  const char *subtitle = (bool)(intptr_t)req->user_ctx
                             ? "Could not connect to saved network."
                             : "Connect your KaleidoBox to Wi-Fi.";

  size_t page_size = strlen(SETUP_PAGE_TEMPLATE) + strlen(subtitle) +
                     strlen(opts) + 32;
  char *page = (char *)malloc(page_size);
  if (!page) {
    free(opts);
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  snprintf(page, page_size, SETUP_PAGE_TEMPLATE, subtitle, opts);
  free(opts);

  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
  free(page);
  return ESP_OK;
}

// Shared by both the POST body handler and the GET query-string fallback -
// both use the same key=val&key=val encoding, just from a different
// source. The GET fallback exists because some clients (notably
// iOS/macOS's captive-portal mini-browser, which auto-opens against an
// open AP with no internet) have been observed sending GET instead of
// POST for this form - without it the credentials silently 405 and the
// device never leaves AP mode.
static esp_err_t handle_connect_body(httpd_req_t *req, const char *body) {
  // Parse ssid=...&password=...
  char raw_ssid[64] = {0};
  char raw_password[128] = {0};

  char *ssid_start = strstr(body, "ssid=");
  if (ssid_start) {
    ssid_start += 5;
    char *end = strchr(ssid_start, '&');
    size_t len = end ? (size_t)(end - ssid_start) : strlen(ssid_start);
    if (len >= sizeof(raw_ssid))
      len = sizeof(raw_ssid) - 1;
    strncpy(raw_ssid, ssid_start, len);
    raw_ssid[len] = '\0';
  }

  char *pw_start = strstr(body, "password=");
  if (pw_start) {
    pw_start += 9;
    char *end = strchr(pw_start, '&');
    size_t len = end ? (size_t)(end - pw_start) : strlen(pw_start);
    if (len >= sizeof(raw_password))
      len = sizeof(raw_password) - 1;
    strncpy(raw_password, pw_start, len);
    raw_password[len] = '\0';
  }

  char ssid[32] = {0};
  char password[64] = {0};
  uri_decode(ssid, raw_ssid, sizeof(ssid));
  uri_decode(password, raw_password, sizeof(password));

  if (ssid[0] == '\0') {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_send(req, "Missing SSID", HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Received credentials for SSID: %s", ssid);

  // Save credentials via the standard wifi path so they persist across reboot
  wifi_config_t wifi_cfg = {0};
  strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid));
  strncpy((char *)wifi_cfg.sta.password, password,
          sizeof(wifi_cfg.sta.password));
  esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);

  char hostname[32];
  char id_suffix[5];
  kaleidobox_wifi_get_id_suffix(id_suffix, sizeof(id_suffix));
  snprintf(hostname, sizeof(hostname), "kaleidobox-%s", id_suffix);
  // Heap-allocated and sized from the actual runtime string, same as
  // get_handler's page/opts above - GCC's -Wformat-truncation can't
  // reason about a fixed-size stack buffer against a runtime-length %s
  // arg and flags it regardless of how generous the size is; a
  // runtime-computed size sidesteps the check entirely.
  size_t page_size = strlen(SUCCESS_PAGE_TEMPLATE) + 2 * strlen(hostname) + 16;
  char *page = malloc(page_size);
  if (!page) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  snprintf(page, page_size, SUCCESS_PAGE_TEMPLATE, hostname, hostname);

  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
  free(page);

  vTaskDelay(pdMS_TO_TICKS(500)); // Let the response flush
  esp_restart();

  return ESP_OK;
}

static esp_err_t post_handler(httpd_req_t *req) {
  char body[200] = {0};
  int received = httpd_req_recv(req, body, sizeof(body) - 1);
  if (received <= 0) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  body[received] = '\0';
  return handle_connect_body(req, body);
}

static esp_err_t connect_get_handler(httpd_req_t *req) {
  size_t query_len = httpd_req_get_url_query_len(req);
  if (query_len == 0) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_send(req, "Missing SSID", HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;
  }
  char *query = malloc(query_len + 1);
  if (!query) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  esp_err_t err = httpd_req_get_url_query_str(req, query, query_len + 1);
  if (err != ESP_OK) {
    free(query);
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  err = handle_connect_body(req, query);
  free(query);
  return err;
}

void kaleidobox_wifi_ap_start(bool is_fallback) {
  ESP_LOGI(TAG, "Starting AP mode (fallback=%d)", is_fallback);

  char id_suffix[5];
  kaleidobox_wifi_get_id_suffix(id_suffix, sizeof(id_suffix));
  snprintf(ap_ssid, sizeof(ap_ssid), "kaleidobox-setup-%s", id_suffix);

  // Switch to APSTA so we can still run a WiFi scan
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

  wifi_config_t ap_cfg = {
      .ap = {
          .channel = 1,
          .password = "",
          .max_connection = AP_MAX_CONNECTIONS,
          .authmode = WIFI_AUTH_OPEN,
      },
  };
  strncpy((char *)ap_cfg.ap.ssid, ap_ssid, sizeof(ap_cfg.ap.ssid));
  ap_cfg.ap.ssid_len = strlen(ap_ssid);
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "AP started: SSID=%s IP=192.168.4.1", ap_ssid);

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.lru_purge_enable = true;

  ESP_ERROR_CHECK(httpd_start(&http_server, &config));

  httpd_uri_t get_uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = get_handler,
      .user_ctx = (void *)(intptr_t)is_fallback,
  };
  httpd_register_uri_handler(http_server, &get_uri);

  httpd_uri_t post_uri = {
      .uri = "/connect",
      .method = HTTP_POST,
      .handler = post_handler,
      .user_ctx = NULL,
  };
  httpd_register_uri_handler(http_server, &post_uri);

  httpd_uri_t connect_get_uri = {
      .uri = "/connect",
      .method = HTTP_GET,
      .handler = connect_get_handler,
      .user_ctx = NULL,
  };
  httpd_register_uri_handler(http_server, &connect_get_uri);
}
