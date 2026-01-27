#include "ui_manager.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui_assets.hpp"
#include <stdio.h>
#include <time.h>

// Fonts
#include "Fonts/FreeSans18pt7b.h"
#include "Fonts/FreeSans9pt7b.h"
#include "Fonts/Picopixel.h"

static const char *TAG = "UIManager";

// Menu Items
static const char *menu_items[] = {"Back", "Refresh", "Reboot"};
static const int menu_item_count = 3;

// Compatibility defines
#define GxEPD_BLACK GFX_BLACK
#define GxEPD_WHITE GFX_WHITE

UIManager::UIManager(Adafruit_SSD1680 *display)
    : display(display), current_state(STATE_HOME), selected_menu_index(0) {
  btn4 = {false, false};
  btn5 = {false, false};
}

void UIManager::start() {
  xTaskCreate(taskEntry, "ui_task", 4096, this, 5, NULL);
}

void UIManager::taskEntry(void *param) {
  UIManager *instance = (UIManager *)param;
  instance->loop();
}

void UIManager::updateButtonState(ButtonState &btn, bool current) {
  btn.pressed = (current && !btn.last_state);
  btn.last_state = current;
}

void UIManager::renderHome(const DeviceStatus &status,
                           const struct tm *timeinfo) {
  display->clearBuffer();
  display->setRotation(3); // Landscape (296x128)
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

void UIManager::renderMenu() {
  display->clearBuffer();
  display->setRotation(3);
  display->setTextColor(GxEPD_BLACK);
  display->setTextWrap(false);

  // Title
  display->setFont(&FreeSans9pt7b);
  display->setCursor(10, 20);
  display->print("Menu");

  // Items
  int start_y = 50;
  int line_height = 25;

  for (int i = 0; i < menu_item_count; i++) {
    display->setCursor(20, start_y + (i * line_height));

    if (i == selected_menu_index) {
      display->print("> "); // Cursor
    } else {
      display->print("  ");
    }
    display->print(menu_items[i]);
  }
}

void UIManager::loop() {
  bool first_run = true;
  bool force_full_refresh = false;
  int64_t last_ui_update = 0;

  while (1) {
    // 1. Poll Inputs (every 50ms)
    DeviceStatus current_status = global_data.getStatus();
    updateButtonState(btn4, current_status.touch_4);
    updateButtonState(btn5, current_status.touch_5);

    bool need_redraw = false;

    // 2. Handle Logic based on State
    if (current_state == STATE_HOME) {
      if (btn5.pressed) {
        ESP_LOGI(TAG, "Entering Menu");
        current_state = STATE_MENU;
        selected_menu_index = 0;
        need_redraw = true;
      }

      // Time based update for Home (every 1 sec)
      int64_t now_us = esp_timer_get_time();
      if ((now_us - last_ui_update) > 1000000) {
        need_redraw = true;
      }
    } else if (current_state == STATE_MENU) {
      if (btn4.pressed) {
        // Cycle Selection
        selected_menu_index = (selected_menu_index + 1) % menu_item_count;
        need_redraw = true;
      }
      if (btn5.pressed) {
        // Execute Action
        if (selected_menu_index == 0) { // Back
          current_state = STATE_HOME;
          need_redraw = true;
        } else if (selected_menu_index == 1) { // Refresh
          force_full_refresh = true;           // Next draw will be full
          current_state = STATE_HOME;
          need_redraw = true;
        } else if (selected_menu_index == 2) { // Reboot
          esp_restart();
        }
      }
    }

    // 3. Redraw if needed
    if (need_redraw || first_run) {
      time_t now;
      struct tm timeinfo;
      time(&now);
      localtime_r(&now, &timeinfo);

      if (current_state == STATE_HOME) {
        renderHome(current_status, &timeinfo);
      } else {
        renderMenu();
      }

      ESP_LOGI(TAG, "Updating Display (Partial: %d)",
               !(first_run || force_full_refresh));
      display->display(!(first_run || force_full_refresh));

      first_run = false;
      force_full_refresh = false;
      last_ui_update = esp_timer_get_time();
    }

    vTaskDelay(pdMS_TO_TICKS(50)); // Fast polling
  }
}
