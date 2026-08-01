#include "printspy.h"

#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "font_5x7.h"
#include "freertos/FreeRTOS.h"
#include "matrix.h"
#include "mqtt_client.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "printspy";

#define MAX_PRINTERS 8
// A printer with no fresh message in this long is treated as no longer
// printing - printspy's publish is transition-or-poll-tick driven with
// no heartbeat/LWT on this topic, so a dead printspy process would
// otherwise leave a permanently-stale "printing" entry frozen forever.
#define STALE_US (120LL * 1000 * 1000)

typedef struct {
  bool used;
  int64_t id;
  char name[32];
  bool printing; // state == "printing"
  float progress; // 0-100
  int remaining_secs;
  int64_t last_seen_us;
} printer_entry_t;

static printer_entry_t g_printers[MAX_PRINTERS];
static esp_mqtt_client_handle_t g_client = NULL;
// Guards g_printers against its three concurrent accessors: the MQTT
// event task (handle_message, writer), display_rotation.c's
// rotation_task (printing_count/render_printing, reader), and the httpd
// task (kaleidobox_printspy_stop()'s memset, writer). A portMUX spinlock
// (not a FreeRTOS semaphore) - statically initializable with no
// separate init-ordering concern, and every critical section here is a
// few struct-field writes/reads, short enough not to need a
// blocking/task-switching lock.
static portMUX_TYPE g_printers_mux = portMUX_INITIALIZER_UNLOCKED;

// Caller must already hold g_printers_mux.
static printer_entry_t *find_or_insert(int64_t id) {
  printer_entry_t *free_slot = NULL;
  for (int i = 0; i < MAX_PRINTERS; i++) {
    if (g_printers[i].used && g_printers[i].id == id) {
      return &g_printers[i];
    }
    if (!g_printers[i].used && !free_slot) {
      free_slot = &g_printers[i];
    }
  }
  if (!free_slot) {
    return NULL;
  }
  memset(free_slot, 0, sizeof(*free_slot));
  free_slot->used = true;
  free_slot->id = id;
  return free_slot;
}

// Parses one MQTTPrinterState payload (see printspy's models.go) and
// updates the matching table entry. Only the fields this display
// actually renders are extracted - temps/power/thumbnail_url are
// deliberately ignored (thumbnail specifically out of scope for v1, see
// panel_takeover plan notes).
static void handle_message(const char *data, int len) {
  char *copy = malloc(len + 1);
  if (!copy) {
    return;
  }
  memcpy(copy, data, len);
  copy[len] = '\0';

  cJSON *json = cJSON_Parse(copy);
  free(copy);
  if (!json) {
    return;
  }

  cJSON *id_item = cJSON_GetObjectItem(json, "id");
  if (!cJSON_IsNumber(id_item)) {
    cJSON_Delete(json);
    return;
  }
  int64_t id = (int64_t)id_item->valuedouble;

  char name[32] = "";
  cJSON *name_item = cJSON_GetObjectItem(json, "name");
  if (cJSON_IsString(name_item)) {
    strncpy(name, name_item->valuestring, sizeof(name) - 1);
  }

  cJSON *state = cJSON_GetObjectItem(json, "state");
  bool printing = cJSON_IsString(state) && strcmp(state->valuestring, "printing") == 0;

  bool have_progress = false, have_remaining = false;
  float progress = 0;
  int remaining_secs = 0;
  cJSON *job = cJSON_GetObjectItem(json, "job");
  if (cJSON_IsObject(job)) {
    cJSON *progress_item = cJSON_GetObjectItem(job, "progress");
    if (cJSON_IsNumber(progress_item)) {
      progress = (float)progress_item->valuedouble;
      have_progress = true;
    }
    cJSON *remaining_item = cJSON_GetObjectItem(job, "remaining_secs");
    if (cJSON_IsNumber(remaining_item)) {
      remaining_secs = remaining_item->valueint;
      have_remaining = true;
    }
  }
  cJSON_Delete(json);

  // Parse fully into locals above, then hold g_printers_mux only for
  // the actual table mutation - never call ESP_LOG* while holding a
  // portMUX critical section (logging does real work - ring buffer
  // writes, possibly blocking - unsafe/slow with interrupts disabled).
  bool found;
  portENTER_CRITICAL(&g_printers_mux);
  printer_entry_t *entry = find_or_insert(id);
  found = entry != NULL;
  if (entry) {
    if (name[0]) {
      strncpy(entry->name, name, sizeof(entry->name) - 1);
      entry->name[sizeof(entry->name) - 1] = '\0';
    }
    entry->printing = printing;
    if (have_progress) {
      entry->progress = progress;
    }
    if (have_remaining) {
      entry->remaining_secs = remaining_secs;
    }
    entry->last_seen_us = esp_timer_get_time();
  }
  portEXIT_CRITICAL(&g_printers_mux);

  if (!found) {
    // %d, not %lld/PRId64 - this project builds with newlib-nano
    // formatting (CONFIG_LIBC_NEWLIB_NANO_FORMAT), which doesn't
    // support 64-bit format specifiers: passing a 64-bit arg where it
    // expects 32-bit desyncs the rest of the varargs, corrupting
    // whatever argument comes after (confirmed the hard way - crashed
    // a %s right after a %lld with a real Guru Meditation LoadStoreError,
    // see the identical fix a few lines down). Printer ids are small in
    // practice; a plain int cast is safe.
    ESP_LOGW(TAG, "printer id %d dropped - table full (%d slots)", (int)id,
             MAX_PRINTERS);
    return;
  }
  // %d, not %lld - see the comment above. This exact line is what
  // actually crashed on real hardware.
  ESP_LOGI(TAG, "printer %d (%s): %s", (int)id, name, printing ? "printing" : "not printing");
}

// esp-mqtt's default client buffer is 1024 bytes - a real
// MQTTPrinterState payload (temps+job+power array+thumbnail URL) runs
// right around that size (confirmed live: ~1030 bytes for a 3-outlet
// power array), so MQTT_EVENT_DATA fires multiple times per message,
// each carrying one fragment (event->data/data_len), with
// event->total_data_len/current_data_offset saying where it fits in
// the whole. Treating every fragment as a complete standalone JSON
// payload (the original version of this code) parses cleanly-truncated
// JSON that cJSON rejects - a real bug: it failed silently on every
// real-world payload big enough to fragment, while every hand-crafted
// mosquitto_pub test payload during development happened to fit in one
// shot and never exposed it. Reassemble into a single buffer sized by
// total_data_len before parsing.
static char *g_msg_buf = NULL;
static int g_msg_buf_len = 0;
// Allocated size of g_msg_buf, captured once from the FIRST fragment's
// total_data_len and never trusted again from later fragments - see the
// bounds check below for why.
static int g_msg_buf_cap = 0;

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id,
                               void *event_data) {
  (void)arg;
  (void)base;
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  switch (event_id) {
  case MQTT_EVENT_CONNECTED: {
    const char *topic = kaleidobox_nvs_get_printspy_topic();
    int msg_id = esp_mqtt_client_subscribe(event->client, topic, 1);
    ESP_LOGI(TAG, "connected, subscribing to %s (msg_id=%d)", topic, msg_id);
    break;
  }
  case MQTT_EVENT_SUBSCRIBED:
    ESP_LOGI(TAG, "subscribe acked, msg_id=%d", event->msg_id);
    break;
  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGW(TAG, "disconnected");
    break;
  case MQTT_EVENT_ERROR:
    if (event->error_handle) {
      ESP_LOGW(TAG, "mqtt error, type=%d", event->error_handle->error_type);
    }
    break;
  case MQTT_EVENT_DATA:
    // Only the FIRST fragment of a delivery carries a real topic
    // pointer - continuation fragments (current_data_offset != 0) have
    // topic=NULL/topic_len=0 by esp-mqtt's own convention. Confirmed
    // the hard way: logging event->topic unconditionally here crashed
    // (LoadProhibited) on exactly the 2nd fragment of a real ~1030-byte
    // payload, every time - don't touch event->topic outside the
    // offset==0 branch.
    if (event->current_data_offset == 0) {
      free(g_msg_buf);
      g_msg_buf = malloc(event->total_data_len);
      g_msg_buf_len = 0;
      g_msg_buf_cap = g_msg_buf ? event->total_data_len : 0;
    }
    if (g_msg_buf) {
      // Bounds check before every write, against the capacity captured
      // from the FIRST fragment only - not event->total_data_len fresh
      // each time. mqtt_client.h documents current_data_offset/
      // total_data_len as plain fields with no stated guarantee that
      // total_data_len stays constant across a delivery's fragments; a
      // broker bug, a dropped/duplicated MQTT_EVENT_DATA callback, or
      // any other deviation from that assumed invariant would otherwise
      // memcpy past the end of the heap allocation with zero defense.
      if (event->current_data_offset < 0 || event->data_len < 0 ||
          event->current_data_offset + event->data_len > g_msg_buf_cap) {
        ESP_LOGW(TAG, "mqtt fragment out of bounds (offset=%d len=%d cap=%d) - dropping message",
                 event->current_data_offset, event->data_len, g_msg_buf_cap);
        free(g_msg_buf);
        g_msg_buf = NULL;
        g_msg_buf_cap = 0;
      } else {
        memcpy(g_msg_buf + event->current_data_offset, event->data, event->data_len);
        g_msg_buf_len += event->data_len;
        if (g_msg_buf_len >= g_msg_buf_cap) {
          ESP_LOGI(TAG, "message complete, %d bytes", g_msg_buf_len);
          handle_message(g_msg_buf, g_msg_buf_len);
          free(g_msg_buf);
          g_msg_buf = NULL;
          g_msg_buf_cap = 0;
        }
      }
    }
    break;
  default:
    break;
  }
}

// "1h23m" / "23m" - matches the format already sketched for this
// feature; panel's too small for anything more precise.
static void format_remaining(int secs, char *out, size_t out_size) {
  if (secs < 0) {
    secs = 0;
  }
  int hours = secs / 3600;
  int mins = (secs % 3600) / 60;
  if (hours > 0) {
    snprintf(out, out_size, "%dh%dm", hours, mins);
  } else {
    snprintf(out, out_size, "%dm", mins);
  }
}

static void draw_progress_bar(int x0, int y0, int w, int h, float frac) {
  if (frac < 0) {
    frac = 0;
  }
  if (frac > 1) {
    frac = 1;
  }
  int fill_w = (int)(w * frac);
  for (int y = y0; y < y0 + h; y++) {
    for (int x = x0; x < x0 + w; x++) {
      bool border = (x == x0 || x == x0 + w - 1 || y == y0 || y == y0 + h - 1);
      bool filled = x < x0 + fill_w;
      if (border) {
        kaleidobox_matrix_set_pixel((uint8_t)x, (uint8_t)y, 120, 120, 120);
      } else if (filled) {
        kaleidobox_matrix_set_pixel((uint8_t)x, (uint8_t)y, 0, 220, 90);
      } else {
        kaleidobox_matrix_set_pixel((uint8_t)x, (uint8_t)y, 0, 0, 0);
      }
    }
  }
}

static void render_printer(const printer_entry_t *p) {
  kaleidobox_matrix_clear();
  if (!kaleidobox_font_draw_text_fit(2, p->name, 255, 255, 255)) {
    kaleidobox_font_draw_text_centered(2, "Printing", 255, 255, 255);
  }
  draw_progress_bar(4, 24, 56, 10, p->progress / 100.0f);

  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", (int)p->progress);
  kaleidobox_font_draw_text_centered(40, pct, 200, 200, 200);

  char remaining[16];
  format_remaining(p->remaining_secs, remaining, sizeof(remaining));
  kaleidobox_font_draw_text_centered(50, remaining, 150, 150, 150);
}

// Shared by printing_count() and render_printing() below so the
// "is this entry currently printing" definition can't drift out of sync
// between the two - they must agree on it, since display_rotation.c
// depends on both enumerating printers in the same order for a given
// table state. Caller must already hold g_printers_mux.
static bool is_printing_fresh(const printer_entry_t *e, int64_t now) {
  return e->used && e->printing && (now - e->last_seen_us) < STALE_US;
}

// Number of tracked printers currently printing and not yet stale.
// main/display_rotation.c calls this each tick to decide whether a
// printer slot is available and how many there are to rotate through.
int kaleidobox_printspy_printing_count(void) {
  int64_t now = esp_timer_get_time();
  int count = 0;
  portENTER_CRITICAL(&g_printers_mux);
  for (int i = 0; i < MAX_PRINTERS; i++) {
    if (is_printing_fresh(&g_printers[i], now)) {
      count++;
    }
  }
  portEXIT_CRITICAL(&g_printers_mux);
  return count;
}

// Renders the idx'th currently-printing (non-stale) entry, in the same
// stable table-slot enumeration order kaleidobox_printspy_printing_count()
// above uses - index N means the same printer here as it does there for
// a given tick. No-op if idx is out of range.
void kaleidobox_printspy_render_printing(int idx) {
  int64_t now = esp_timer_get_time();
  // Snapshot the matched entry under the lock, then render from the
  // local copy after releasing it - render_printer() does real matrix
  // I/O (several draw calls), far too slow to run inside a portMUX
  // critical section, and this also closes the torn-read risk if
  // kaleidobox_printspy_stop()'s memset() runs concurrently.
  printer_entry_t snapshot;
  bool found = false;
  int seen = 0;
  portENTER_CRITICAL(&g_printers_mux);
  for (int i = 0; i < MAX_PRINTERS; i++) {
    if (is_printing_fresh(&g_printers[i], now)) {
      if (seen == idx) {
        snapshot = g_printers[i];
        found = true;
        break;
      }
      seen++;
    }
  }
  portEXIT_CRITICAL(&g_printers_mux);
  if (found) {
    render_printer(&snapshot);
  }
}

void kaleidobox_printspy_start(void) {
  if (g_client) {
    return; // already started - idempotent on WiFi reconnect
  }
  if (!kaleidobox_nvs_get_printspy_enabled()) {
    return;
  }
  const char *broker = kaleidobox_nvs_get_mqtt_broker();
  if (!broker || broker[0] == '\0') {
    ESP_LOGW(TAG, "printspy enabled but no MQTT broker configured");
    return;
  }

  // esp-mqtt's URI parser requires a scheme (mqtt://, mqtts://, ...) -
  // a bare "host:port" (the natural way to type a broker address) fails
  // to parse silently. Auto-prepend the plain scheme rather than
  // forcing the user to type it.
  static char broker_uri[96];
  if (strstr(broker, "://")) {
    strncpy(broker_uri, broker, sizeof(broker_uri) - 1);
    broker_uri[sizeof(broker_uri) - 1] = '\0';
  } else {
    snprintf(broker_uri, sizeof(broker_uri), "mqtt://%s", broker);
  }

  // A password with no username is an invalid MQTT CONNECT combination
  // (mosquitto's own client refuses to even send it) - setting both
  // fields unconditionally meant a leftover/blank username with a set
  // password silently broke the connection instead of just connecting
  // anonymously, which is what an empty username should mean. Only
  // attach credentials at all when there's a real username.
  esp_mqtt_client_config_t config = {
      .broker.address.uri = broker_uri,
  };
  const char *user = kaleidobox_nvs_get_mqtt_user();
  const char *pass = kaleidobox_nvs_get_mqtt_pass();
  if (user && user[0]) {
    config.credentials.username = user;
    if (pass && pass[0]) {
      config.credentials.authentication.password = pass;
    }
  }
  g_client = esp_mqtt_client_init(&config);
  if (!g_client) {
    ESP_LOGE(TAG, "esp_mqtt_client_init failed");
    return;
  }
  esp_mqtt_client_register_event(g_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
  esp_mqtt_client_start(g_client);

  // No background task started here anymore - main/display_rotation.c
  // owns the 1s scheduling tick for all three display slots and calls
  // kaleidobox_printspy_printing_count()/_render_printing() above.
  ESP_LOGI(TAG, "printspy started, broker=%s", broker_uri);
}

// Disconnects and tears down the MQTT client, and clears the tracked
// printer table - called from the /api/printspy POST handler whenever
// the saved settings no longer describe a running connection (enabled
// turned off, or the broker/credentials/topic changed), so unchecking
// "Enabled" actually stops the client immediately instead of only
// preventing a future connect on the next reboot (confirmed live: the
// user wants a disabled feature to genuinely not be running, not just
// "won't restart next time").
void kaleidobox_printspy_stop(void) {
  if (!g_client) {
    return;
  }
  esp_mqtt_client_stop(g_client);
  esp_mqtt_client_destroy(g_client);
  g_client = NULL;
  portENTER_CRITICAL(&g_printers_mux);
  memset(g_printers, 0, sizeof(g_printers));
  portEXIT_CRITICAL(&g_printers_mux);
  ESP_LOGI(TAG, "printspy stopped");
}
