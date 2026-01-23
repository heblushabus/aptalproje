#include "battery_manager.hpp"
#include "common_data.hpp"
#include "display_manager.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_manager.hpp"
#include "secrets.hpp"
#include "touch_manager.hpp"
#include "ui_assets.hpp"
#include <stdio.h>
#include <time.h>

// Fonts
#include "Fonts/FreeSans18pt7b.h"
#include "Fonts/FreeSans9pt7b.h"
#include "Fonts/Picopixel.h"

static const char *TAG = "main";

// Compatibility defines
#define GxEPD_BLACK GFX_BLACK
#define GxEPD_WHITE GFX_WHITE

void renderUI(Adafruit_SSD1680 *display, const DeviceStatus &status,
              const struct tm *timeinfo) {
  display->clearBuffer();
  display->setRotation(1); // Landscape (296x128)
  display->setTextColor(GxEPD_BLACK);
  display->setTextWrap(false);

  // CO2 Label
  display->setFont(NULL); // Default font
  display->setCursor(196, 114);
  display->print("CO :");

  display->setFont(&Picopixel);
  display->setCursor(208, 122);
  display->print("2");

  // ppm Value
  display->setFont(&FreeSans9pt7b);
  display->setCursor(217, 122);
  char co2_buf[16];
  snprintf(co2_buf, sizeof(co2_buf), "%dppm", status.co2_ppm);
  display->print(co2_buf);

  // Environmental Labels (T, H, A)
  display->setFont(NULL);
  display->setCursor(238, 82);
  display->print("T:");
  display->setCursor(238, 91);
  display->print("H:");
  display->setCursor(238, 100);
  display->print("A:");

  // Environmental Values
  char val_buf[16];
  snprintf(val_buf, sizeof(val_buf), "%.2fC", status.temperature);
  display->printRightAligned(292, 82, val_buf);

  snprintf(val_buf, sizeof(val_buf), "%.2f%%", status.humidity);
  display->printRightAligned(292, 91, val_buf);

  snprintf(val_buf, sizeof(val_buf), "%+.2fm", status.altitude);
  display->printRightAligned(292, 100, val_buf);

  // Time
  char time_str[16];
  strftime(time_str, sizeof(time_str), "%H:%M:%S", timeinfo);
  display->setFont(&FreeSans18pt7b);
  display->printRightAligned(292, 78, time_str);

  // Bitmaps
  display->drawBitmap(40, 12, image_Layer_8_bits, 18, 19, GxEPD_BLACK);

  // Battery Voltage
  display->setFont(NULL);
  display->setCursor(235, 44);
  char bat_buf[10];
  snprintf(bat_buf, sizeof(bat_buf), "%.2fV", status.battery_voltage);
  display->print(bat_buf);

  display->drawBitmap(267, 37, image_battery_50_bits, 24, 16, GxEPD_BLACK);
  display->drawBitmap(276, 5, image_choice_bullet_on_bits, 15, 16, GxEPD_BLACK);
  display->drawBitmap(233, 1, image_ButtonUp_bits, 7, 4, GxEPD_BLACK);
  display->drawBitmap(102, 1, image_ButtonUp_bits, 7, 4, GxEPD_BLACK);
  display->drawBitmap(230, 6, image_stats_bits, 13, 11, GxEPD_BLACK);
  display->drawBitmap(98, 4, image_menu_settings_sliders_two_bits, 14, 16,
                      GxEPD_BLACK);
  display->drawBitmap(181, 108, image_check_bits, 12, 16, GxEPD_BLACK);

  // Touch Status
  display->setFont(NULL);
  display->setCursor(10, 115);
  display->print("T4: ");
  display->print(status.touch_4 ? "1" : "0");

  display->setCursor(60, 115);
  display->print("T5: ");
  display->print(status.touch_5 ? "1" : "0");
}

static void display_task(void *pvParameters) {
  Adafruit_SSD1680 *display = (Adafruit_SSD1680 *)pvParameters;
  bool first_run = true;
  TickType_t xLastWakeTime = xTaskGetTickCount();

  while (1) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    DeviceStatus current_status = global_data.getStatus();

    ESP_LOGI(TAG, "Refreshing display with shared data...");
    renderUI(display, current_status, &timeinfo);

    // Refresh Display: Full for the first run, Partial thereafter
    display->display(!first_run);
    first_run = false;

    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
  }
}

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "Starting up...");

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

  // Create Display Task
  xTaskCreate(display_task, "display_task", 4096, display, 5, NULL);

  ESP_LOGI(TAG, "Display task created, app_main exiting.");
}
