#include "battery_manager.hpp"
#include "common_data.hpp"
#include "display_manager.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_manager.hpp"
#include "nvs_flash.h"
#include "scd4x_manager.hpp"
#include "secrets.hpp"
#include "storage_manager.h"
#include "touch_manager.hpp"
#include "ui_manager.hpp"
#include <i2cdev.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "main";

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "Starting up...");

  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Initialize Display
  static DisplayManager displayManager;
  if (displayManager.init() != ESP_OK) {
    ESP_LOGE(TAG, "Display initialization failed!");
    return;
  }

  Adafruit_SSD1680 *display = displayManager.getDisplay();
  if (!display) {
    ESP_LOGE(TAG, "Failed to get display handle!");
    return;
  }

  // Initialize Touch
  static TouchManager touchManager;
  if (touchManager.init() == ESP_OK) {
    touchManager.start();
  } else {
    ESP_LOGE(TAG, "Touch initialization failed!");
  }

  // Initialize Battery
  static BatteryManager batteryManager;
  if (batteryManager.init() == ESP_OK) {
    batteryManager.start();
  } else {
    ESP_LOGE(TAG, "Battery initialization failed!");
  }

  // Initialize Storage
  static StorageManager storageManager;
  storageManager.mount();

  // Initialize I2C Library
  ESP_ERROR_CHECK(i2cdev_init());

  // Initialize SCD4x (CO2 Sensor)
  static Scd4xManager scd4xManager;
  // SDA: 47, SCL: 21
  if (scd4xManager.init(47, 21) == ESP_OK) {
    scd4xManager.start();
  } else {
    ESP_LOGE(TAG, "SCD4x initialization failed!");
  }

  // --- Network Setup ---
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  // If year is before 2026, assume time is not set
  if (timeinfo.tm_year < (2026 - 1900)) {
    NetworkManager network;
    ESP_LOGI(TAG, "Time not set. Connecting to WiFi...");
    if (network.init(WIFI_SSID, WIFI_PASS) == ESP_OK) {
      ESP_LOGI(TAG, "Syncing time...");
      network.syncTime();
      network.deinit(); // Power off WiFi

      // Set timezone to UTC+3 (Istanbul/Moscow)
      setenv("TZ", "TRT-3", 1);
      tzset();
    } else {
      ESP_LOGW(TAG, "WiFi connection failed, using default time.");
    }
  } else {
    ESP_LOGI(TAG, "System time already set, skipping network.");
  }

  // Create Display Task via UIManager
  static UIManager uiManager(display, &storageManager);
  uiManager.start();

  ESP_LOGI(TAG, "UI Manager started, app_main exiting.");
}
