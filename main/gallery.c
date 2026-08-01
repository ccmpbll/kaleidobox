#include "gallery.h"

#include "brightness_schedule.h"
#include "canvas.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdcard.h"
#include "settings.h"
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "gallery";

#define GALLERY_DIR "/sdcard/gallery"
#define STATE_PATH "/sdcard/state.raw"
#define BUF_SIZE (CANVAS_WIDTH * CANVAS_HEIGHT * 3)
#define BG_TICK_MS 1000

static int g_current_index = -1;

// name comes straight from the HTTP API (POST body / URI tail) -
// reject anything that could escape GALLERY_DIR or collide with the
// ".raw" suffix logic below.
static bool name_is_valid(const char *name) {
  if (!name || name[0] == '\0') {
    return false;
  }
  if (strchr(name, '/') || strchr(name, '.')) {
    return false;
  }
  return strlen(name) <= 48;
}

static esp_err_t load_file_into_canvas(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    return ESP_ERR_NOT_FOUND;
  }
  // MALLOC_CAP_SPIRAM - same class of internal-RAM-starvation bug found
  // and fixed in kaleidoscope.c/http_server.c: 12KB is under
  // CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL's threshold, so a plain
  // malloc() would land here regardless of PSRAM headroom, and this
  // runs on every gallery next/prev/restore - no DMA requirement, it's
  // just handed to kaleidobox_canvas_set_all().
  uint8_t *buf = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_SPIRAM);
  if (!buf) {
    fclose(f);
    return ESP_ERR_NO_MEM;
  }
  size_t n = fread(buf, 1, BUF_SIZE, f);
  fclose(f);
  if (n != BUF_SIZE) {
    ESP_LOGW(TAG, "%s: wrong size (%u bytes, expected %u) - skipping", path,
             (unsigned)n, (unsigned)BUF_SIZE);
    free(buf);
    return ESP_ERR_INVALID_SIZE;
  }
  // kaleidobox_canvas_set_all() already updates a running kaleidoscope's
  // source (see canvas.c) - no need to duplicate that here.
  kaleidobox_canvas_set_all(buf);
  free(buf);
  return ESP_OK;
}

static esp_err_t save_bytes_to(const char *path, const uint8_t *bytes) {
  FILE *f = fopen(path, "wb");
  if (!f) {
    return ESP_FAIL;
  }
  size_t n = fwrite(bytes, 1, BUF_SIZE, f);
  fclose(f);
  return n == BUF_SIZE ? ESP_OK : ESP_FAIL;
}

static int count_entries(void) {
  DIR *d = opendir(GALLERY_DIR);
  if (!d) {
    return 0;
  }
  int n = 0;
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (ent->d_type == DT_REG) {
      n++;
    }
  }
  closedir(d);
  return n;
}

static esp_err_t load_at_index(int idx) {
  DIR *d = opendir(GALLERY_DIR);
  if (!d) {
    return ESP_ERR_NOT_FOUND;
  }
  struct dirent *ent;
  int i = 0;
  esp_err_t err = ESP_ERR_NOT_FOUND;
  while ((ent = readdir(d)) != NULL) {
    if (ent->d_type != DT_REG) {
      continue;
    }
    if (i == idx) {
      char path[sizeof(GALLERY_DIR) + 256];
      snprintf(path, sizeof(path), GALLERY_DIR "/%s", ent->d_name);
      err = load_file_into_canvas(path);
      break;
    }
    i++;
  }
  closedir(d);
  return err;
}

static void gallery_bg_task(void *arg) {
  (void)arg;
  uint32_t elapsed_s = 0;
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(BG_TICK_MS));

    // Piggybacks on this task's existing always-on 1s tick rather than
    // getting its own dedicated task - a once-a-minute schedule check
    // doesn't need one when this is already running regardless of mode.
    kaleidobox_brightness_schedule_tick();

    if (!kaleidobox_sdcard_is_mounted()) {
      continue;
    }

    if (kaleidobox_canvas_is_dirty()) {
      if (save_bytes_to(STATE_PATH, kaleidobox_canvas_buffer()) == ESP_OK) {
        kaleidobox_canvas_clear_dirty();
      }
      // Failure (e.g. a transient SD DMA allocation failure under
      // memory pressure) leaves dirty set, so this just retries next
      // tick instead of silently losing the pending autosave.
    }

    if (kaleidobox_nvs_get_gallery_auto_advance()) {
      uint16_t interval = kaleidobox_nvs_get_gallery_interval_seconds();
      if (interval > 0 && ++elapsed_s >= interval) {
        elapsed_s = 0;
        kaleidobox_gallery_next();
      }
    } else {
      elapsed_s = 0;
    }
  }
}

esp_err_t kaleidobox_gallery_init(void) {
  if (kaleidobox_sdcard_is_mounted()) {
    mkdir(GALLERY_DIR, 0777); // ignore EEXIST - already there is fine
  }
  // Pinned to core 1, opposite WiFi's own tasks (core 0, per the boot
  // log's "wifi driver task: ..., core=0") - confirmed on real hardware
  // that SD card I/O from this task (the autosave write, or an
  // auto-advance directory scan) landing on the same core as WiFi's
  // processing tanks in-flight upload throughput ~10-17x (627KB went
  // from ~500ms to 7-13s). Unpinned, the scheduler was free to put this
  // task on core 0 too.
  xTaskCreatePinnedToCore(gallery_bg_task, "gallery_bg", 4096, NULL,
                          tskIDLE_PRIORITY + 1, NULL, 1);
  ESP_LOGI(TAG, "gallery_init");
  return ESP_OK;
}

esp_err_t kaleidobox_gallery_save(const char *name) {
  return kaleidobox_gallery_save_bytes(name, kaleidobox_canvas_buffer());
}

esp_err_t kaleidobox_gallery_save_bytes(const char *name, const uint8_t *rgb888) {
  if (!name_is_valid(name)) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!kaleidobox_sdcard_is_mounted()) {
    return ESP_ERR_NOT_SUPPORTED;
  }
  char path[sizeof(GALLERY_DIR) + 64];
  snprintf(path, sizeof(path), GALLERY_DIR "/%s.raw", name);
  return save_bytes_to(path, rgb888);
}

esp_err_t kaleidobox_gallery_delete(const char *name) {
  if (!name_is_valid(name)) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!kaleidobox_sdcard_is_mounted()) {
    return ESP_ERR_NOT_SUPPORTED;
  }
  char path[sizeof(GALLERY_DIR) + 64];
  snprintf(path, sizeof(path), GALLERY_DIR "/%s.raw", name);
  return unlink(path) == 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t kaleidobox_gallery_read(const char *name, uint8_t *buf) {
  if (!name_is_valid(name)) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!kaleidobox_sdcard_is_mounted()) {
    return ESP_ERR_NOT_SUPPORTED;
  }
  char path[sizeof(GALLERY_DIR) + 64];
  snprintf(path, sizeof(path), GALLERY_DIR "/%s.raw", name);
  FILE *f = fopen(path, "rb");
  if (!f) {
    return ESP_ERR_NOT_FOUND;
  }
  size_t n = fread(buf, 1, BUF_SIZE, f);
  fclose(f);
  return n == BUF_SIZE ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

size_t kaleidobox_gallery_list(char *buf, size_t buf_size, size_t max_names) {
  if (buf_size > 0) {
    buf[0] = '\0';
  }
  if (!kaleidobox_sdcard_is_mounted()) {
    return 0;
  }
  DIR *d = opendir(GALLERY_DIR);
  if (!d) {
    return 0;
  }
  size_t count = 0;
  size_t used = 0;
  struct dirent *ent;
  while (count < max_names && (ent = readdir(d)) != NULL) {
    if (ent->d_type != DT_REG) {
      continue;
    }
    // Strip the ".raw" suffix saved entries always have - readdir gives
    // us the real on-disk filename, not the caller-facing name.
    char name[256];
    strncpy(name, ent->d_name, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    char *dot = strstr(name, ".raw");
    if (dot) {
      *dot = '\0';
    }

    size_t len = strlen(name);
    if (used + len + 2 > buf_size) { // +1 newline, +1 NUL
      break;
    }
    memcpy(buf + used, name, len);
    used += len;
    buf[used++] = '\n';
    buf[used] = '\0';
    count++;
  }
  closedir(d);
  return count;
}

esp_err_t kaleidobox_gallery_next(void) {
  if (!kaleidobox_sdcard_is_mounted()) {
    return ESP_ERR_NOT_SUPPORTED;
  }
  int n = count_entries();
  if (n == 0) {
    return ESP_ERR_NOT_FOUND;
  }
  g_current_index = (g_current_index + 1) % n;
  return load_at_index(g_current_index);
}

esp_err_t kaleidobox_gallery_prev(void) {
  if (!kaleidobox_sdcard_is_mounted()) {
    return ESP_ERR_NOT_SUPPORTED;
  }
  int n = count_entries();
  if (n == 0) {
    return ESP_ERR_NOT_FOUND;
  }
  g_current_index = ((g_current_index - 1) % n + n) % n;
  return load_at_index(g_current_index);
}

esp_err_t kaleidobox_gallery_show(const char *name) {
  if (!name_is_valid(name)) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!kaleidobox_sdcard_is_mounted()) {
    return ESP_ERR_NOT_SUPPORTED;
  }
  char target[64];
  snprintf(target, sizeof(target), "%s.raw", name);

  DIR *d = opendir(GALLERY_DIR);
  if (!d) {
    return ESP_ERR_NOT_FOUND;
  }
  struct dirent *ent;
  int i = 0;
  esp_err_t err = ESP_ERR_NOT_FOUND;
  while ((ent = readdir(d)) != NULL) {
    if (ent->d_type != DT_REG) {
      continue;
    }
    if (strcmp(ent->d_name, target) == 0) {
      char path[sizeof(GALLERY_DIR) + 256];
      snprintf(path, sizeof(path), GALLERY_DIR "/%s", ent->d_name);
      err = load_file_into_canvas(path);
      // Keeps next()/prev() cycling from this entry's position instead
      // of wherever the cursor happened to be before - picking one by
      // name is still "I'm looking at this one now" for cycling
      // purposes.
      if (err == ESP_OK) {
        g_current_index = i;
      }
      break;
    }
    i++;
  }
  closedir(d);
  return err;
}

esp_err_t kaleidobox_gallery_restore_state(void) {
  if (!kaleidobox_sdcard_is_mounted()) {
    return ESP_ERR_NOT_SUPPORTED;
  }
  return load_file_into_canvas(STATE_PATH);
}
