#include "storage_manager.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "StorageManager";
static const char *BASE_PATH = "/littlefs";
static const char *PARTITION_LABEL = "storage";

StorageManager::StorageManager() {}

StorageManager::~StorageManager() {
  // unmount(); // Keep mounted
}

esp_err_t StorageManager::mount() {
  esp_vfs_littlefs_conf_t conf = {
      .base_path = BASE_PATH,
      .partition_label = PARTITION_LABEL,
      .format_if_mount_failed = true,
      .dont_mount = false,
  };

  esp_err_t ret = esp_vfs_littlefs_register(&conf);

  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE(TAG, "Failed to mount or format filesystem");
    } else if (ret == ESP_ERR_NOT_FOUND) {
      ESP_LOGE(TAG, "Failed to find LittleFS partition");
    } else {
      ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
    }
    return ret;
  }

  size_t total = 0, used = 0;
  ret = esp_littlefs_info(conf.partition_label, &total, &used);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)",
             esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "LittleFS mounted: total: %d, used: %d", total, used);
  }

  return ESP_OK;
}

void StorageManager::unmount() {
  esp_vfs_littlefs_unregister(PARTITION_LABEL);
  ESP_LOGI(TAG, "LittleFS unmounted");
}

esp_err_t StorageManager::readFile(const char *path) {
  ESP_LOGI(TAG, "Reading file: %s", path);

  FILE *f = fopen(path, "r");
  if (f == NULL) {
    ESP_LOGE(TAG, "Failed to open file for reading");
    return ESP_FAIL;
  }

  char line[128];
  while (fgets(line, sizeof(line), f) != NULL) {
    // Print content - using printf for direct output to console as requested
    // for "reading" Also logging it
    printf("%s", line);
  }
  printf("\n"); // Ensure newline at end

  fclose(f);
  return ESP_OK;
}

std::string StorageManager::readTextFile(const char *path) {
  ESP_LOGI(TAG, "Reading text file to string: %s", path);

  FILE *f = fopen(path, "r");
  if (f == NULL) {
    ESP_LOGE(TAG, "Failed to open file for reading");
    return "";
  }

  fseek(f, 0, SEEK_END);
  long length = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (length <= 0) {
    fclose(f);
    return "";
  }

  std::string content;
  content.resize(length);
  size_t read_len = fread(&content[0], 1, length, f);
  content.resize(read_len);

  fclose(f);
  return content;
}
