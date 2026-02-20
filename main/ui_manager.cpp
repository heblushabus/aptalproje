#include "ui_manager.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "scd4x_manager.hpp"
#include "storage_manager.h"
#include "ui_assets.hpp"
#include <stdio.h>
#include <time.h>

// Fonts

#include "Fonts/FreeSans18pt7b.h"
#include "Fonts/FreeSans7pt7b.h"
#include "Fonts/FreeSans9pt7b.h"
#include "Fonts/Picopixel.h"

static const char *TAG = "UIManager";

// Menu Items
static const char *menu_items[] = {
    "Back",   "Refresh", "SCD41 Toggle ASC", "SCD41 FRC 430ppm",
    "Reboot", "Reader",  "Factory Reset"};
static const int menu_item_count = 7;

// Compatibility defines
#define GxEPD_BLACK GFX_BLACK
#define GxEPD_WHITE GFX_WHITE

#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#endif
#ifndef pgm_read_word
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#endif
#ifndef pgm_read_dword
#define pgm_read_dword(addr) (*(const unsigned long *)(addr))
#endif

// Helper for GFXGlyph pointer math (from Adafruit_GFX.cpp)
#if !defined(__INT_MAX__) || (__INT_MAX__ > 0xFFFF)
#define pgm_read_pointer(addr) ((void *)pgm_read_dword(addr))
#else
#define pgm_read_pointer(addr) ((void *)pgm_read_word(addr))
#endif

inline GFXglyph *pgm_read_glyph_ptr(const GFXfont *gfxFont, uint8_t c) {
#ifdef __AVR__
  return &(((GFXglyph *)pgm_read_pointer(&gfxFont->glyph))[c]);
#else
  // expression in __AVR__ section may generate "dereferencing type-punned
  // pointer will break strict-aliasing rules" warning In fact, on other
  // platforms (such as STM32) there is no need to do this pointer magic as
  // program memory may be read just like Data memory !
  return gfxFont->glyph + c;
#endif
}

UIManager::UIManager(Adafruit_SSD1680 *display, StorageManager *storageManager,
                     Scd4xManager *scd4xManager)
    : display(display), storageManager(storageManager),
      scd4xManager(scd4xManager), current_state(STATE_HOME),
      selected_menu_index(0), asc_enabled(false), current_page_index(0) {
  btn19 = {false, false};
  btn20 = {false, false};
  btn20_press_start_time = 0;
  btn20_hold_triggered = false;
}

void UIManager::start() {
  xTaskCreate(taskEntry, "ui_task", 4096, this, 5, NULL);
}

void UIManager::taskEntry(void *param) {
  UIManager *instance = (UIManager *)param;
  global_data.registerUITask(xTaskGetCurrentTaskHandle());
  instance->loop();
}

void UIManager::updateButtonState(ButtonState &btn, bool current) {
  btn.pressed = (current && !btn.last_state);
  btn.last_state = current;
}

void UIManager::renderHome(const DeviceStatus &status,
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
  char co2_buf[16];
  snprintf(co2_buf, sizeof(co2_buf), "%dppm", status.co2_ppm);
  display->printRightAligned(292, 122, co2_buf);

  // display->setCursor(262, 122);
  // display->print("ppm");

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

  // Button Status
  display->setFont(NULL);
  display->setCursor(10, 115);
  display->print("B19: ");
  display->print(status.btn_19 ? "1" : "0");

  display->setCursor(65, 115);
  display->print("B20: ");
  display->print(status.btn_20 ? "1" : "0");
}

void UIManager::renderMenu() {
  display->clearBuffer();
  display->setRotation(1);
  display->setTextColor(GxEPD_BLACK);
  display->setTextWrap(false);

  // Title
  display->setFont(&FreeSans9pt7b);
  display->setCursor(10, 20);
  display->print("Menu");

  // Items
  display->setFont(&FreeSans7pt7b);
  int start_y = 38;
  int line_height = 15;

  for (int i = 0; i < menu_item_count; i++) {
    display->setCursor(20, start_y + (i * line_height));

    if (i == selected_menu_index) {
      display->print("> "); // Cursor
    } else {
      display->print("  ");
    }

    if (i == 2) { // ASC Item
      char asc_buf[32];
      snprintf(asc_buf, sizeof(asc_buf), "ASC: %s", asc_enabled ? "ON" : "OFF");
      display->print(asc_buf);
    } else {
      display->print(menu_items[i]);
    }
  }
}

void UIManager::saveProgress() {
  nvs_handle_t my_handle;
  esp_err_t err = nvs_open("reader", NVS_READWRITE, &my_handle);
  if (err == ESP_OK) {
    nvs_set_i32(my_handle, "page_idx", current_page_index);
    nvs_commit(my_handle);
    nvs_close(my_handle);
    ESP_LOGI(TAG, "Saved page index: %d", current_page_index);
  } else {
    ESP_LOGE(TAG, "Error (%s) opening NVS handle", esp_err_to_name(err));
  }
}

void UIManager::loadProgress() {
  nvs_handle_t my_handle;
  esp_err_t err = nvs_open("reader", NVS_READONLY, &my_handle);
  if (err == ESP_OK) {
    int32_t saved_page = 0;
    err = nvs_get_i32(my_handle, "page_idx", &saved_page);
    if (err == ESP_OK) {
      current_page_index = saved_page;
      ESP_LOGI(TAG, "Loaded page index: %d", current_page_index);
    }
    nvs_close(my_handle);
  }
}

// Helper to paginate based on screen lines with word wrap, ignoring single
// Helper to get text width using FreeSans7pt7b
int getTextWidth(const std::string &text) {
  int width = 0;
  const GFXfont *gfxFont = &FreeSans7pt7b;
  uint8_t first = pgm_read_byte(&gfxFont->first);
  uint8_t last = pgm_read_byte(&gfxFont->last);

  for (char c : text) {
    if (c >= first && c <= last) {
      GFXglyph *glyph = pgm_read_glyph_ptr(gfxFont, c - first);
      width += pgm_read_byte(&glyph->xAdvance);
    }
  }
  return width;
}

void UIManager::paginateContent(const std::string &content) {
  pages.clear();
  // current_page_index = 0; // Do not reset here

  if (content.empty()) {
    return;
  }

  // Visual Parameters for FreeSans7pt7b
  // 7pt font is smaller.
  // 128px height / ~11px line height = ~11 lines. Let's try 10.
  const int MAX_LINES_PER_PAGE = 7;
  // Width: 296px. Margin ~3-3px. Safe width = 290px.
  const int MAX_LINE_WIDTH = 286;

  std::string current_page_str;
  int current_lines = 0;
  int current_line_width = 0;

  size_t idx = 0;
  while (idx < content.length()) {
    // 1. Consume Whitespace & Count Newlines
    int newlines = 0;
    while (
        idx < content.length() &&
        (content[idx] == ' ' || content[idx] == '\n' || content[idx] == '\r')) {
      if (content[idx] == '\n')
        newlines++;
      idx++;
    }

    if (idx >= content.length())
      break; // End of whitespace at end of file

    // 2. Consume Word
    size_t word_start = idx;
    while (idx < content.length() && content[idx] != ' ' &&
           content[idx] != '\n' && content[idx] != '\r') {
      idx++;
    }
    std::string word = content.substr(word_start, idx - word_start);
    int word_width = getTextWidth(word);

    // 3. Determine Separator Logic
    bool is_page_start = current_page_str.empty();
    int space_width = getTextWidth(" ");

    // Paragraph Break Detection (>1 newline)
    if (newlines >= 2 && !is_page_start) {
      // We want a blank line.
      int cost_lines = (current_line_width > 0) ? 2 : 1;

      if (current_lines + cost_lines >= MAX_LINES_PER_PAGE) {
        // Page Full
        pages.push_back(current_page_str);
        current_page_str.clear();
        current_lines = 0;
        current_line_width = 0;
        // On new page, ignore paragraph break (implicit at top)
      } else {
        // Apply Paragraph Break
        if (current_line_width > 0) {
          current_page_str += "\n\n";
          current_lines += 2;
        } else {
          current_page_str += "\n";
          current_lines += 1;
        }
        current_line_width = 0;
      }
    }
    // Normal Word Separation (Space or Single Newline -> Space)
    else if (!is_page_start) {
      // Check if " " + word fits on current line.
      if (current_line_width + space_width + word_width > MAX_LINE_WIDTH) {
        // Wrap to next line
        if (current_lines + 1 >= MAX_LINES_PER_PAGE) {
          // Page Full
          pages.push_back(current_page_str);
          current_page_str.clear();
          current_lines = 0;
          current_line_width = 0;
        } else {
          current_page_str += "\n";
          current_lines++;
          current_line_width = 0;
        }
      } else {
        // Fits on line
        current_page_str += " ";
        current_line_width += space_width;
      }
    } else {
      // Page start
    }

    // 4. Add Word
    current_page_str += word;
    current_line_width += word_width;
  }

  if (!current_page_str.empty()) {
    pages.push_back(current_page_str);
  }
}

void UIManager::renderReader() {
  display->clearBuffer();
  display->setRotation(1);
  display->setTextColor(GxEPD_BLACK);
  display->setTextWrap(true);

  // Load content if needed
  if (pages.empty()) {
    if (storageManager) {
      std::string content = storageManager->readTextFile("/littlefs/book.txt");
      if (content.empty()) {
        display->setCursor(10, 50);
        display->print("File empty or not found.");
        return;
      }
      paginateContent(content);

      // Validate loaded page index against new page count
      if (current_page_index >= pages.size()) {
        current_page_index = pages.empty() ? 0 : pages.size() - 1;
      }
    } else {
      display->setCursor(10, 50);
      display->print("Storage Error");
      return;
    }
  }

  // Content
  display->setFont(&FreeSans7pt7b); // New serif font
  display->setCursor(
      10, 11); // Start closer to top-left but account for baseline (y=15 approx)

  if (current_page_index < pages.size()) {
    display->print(pages[current_page_index].c_str());
  }

  // Footer: Page X/Y
  display->setFont(NULL);
  char footer[32];
  snprintf(footer, sizeof(footer), "%d / %d", current_page_index + 1,
           (int)pages.size());
  display->printRightAligned(296, 121, footer);
}

void UIManager::loop() {
  bool first_run = true;
  bool force_full_refresh = false;
  int64_t last_ui_update = 0;

  while (1) {
    // Wait for notification (from buttons or environmental update)
    // Use a timeout (e.g., 5s) just in case, or max delay
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // 1. Poll Inputs (every 50ms)
    DeviceStatus current_status = global_data.getStatus();
    updateButtonState(btn19, current_status.btn_19);
    updateButtonState(btn20, current_status.btn_20);

    bool need_redraw = false;

    // 2. Handle Logic based on State
    if (current_state == STATE_HOME) {
      if (btn20.pressed) {
        ESP_LOGI(TAG, "Entering Menu");
        current_state = STATE_MENU;
        selected_menu_index = 0;
        // Fetch ASC status once when entering menu
        if (scd4xManager) {
          scd4xManager->getASCStatus(&asc_enabled);
        }
        need_redraw = true;
      }

      // Update only when new environmental data is available
      if (current_status.last_env_update_us > last_ui_update) {
        need_redraw = true;
      }
    } else if (current_state == STATE_MENU) {
      if (btn19.pressed) {
        // Cycle Selection
        selected_menu_index = (selected_menu_index + 1) % menu_item_count;
        need_redraw = true;
      }
      if (btn20.pressed) {
        // Execute Action
        if (selected_menu_index == 0) { // Back
          current_state = STATE_HOME;
          need_redraw = true;
        } else if (selected_menu_index == 1) { // Refresh
          force_full_refresh = true;           // Next draw will be full
          current_state = STATE_HOME;
          need_redraw = true;
        } else if (selected_menu_index == 2) { // SCD41 Toggle ASC
          if (scd4xManager)
            scd4xManager->toggleASC();
          current_state = STATE_HOME;
          need_redraw = true;
        } else if (selected_menu_index == 3) { // SCD41 FRC 430ppm
          if (scd4xManager)
            scd4xManager->performFRC(430);
          current_state = STATE_HOME;
          need_redraw = true;
        } else if (selected_menu_index == 4) { // Reboot
          esp_restart();
        } else if (selected_menu_index == 5) { // Reader
          current_state = STATE_READER;
          loadProgress(); // Load saved page
          need_redraw = true;
        } else if (selected_menu_index == 6) { // Factory Reset
          if (scd4xManager)
            scd4xManager->performFactoryReset();
          current_state = STATE_HOME;
          need_redraw = true;
        }
      }
    } else if (current_state == STATE_READER) {
      // Button 20 Logic: Hold for Exit, Click for Previous Page
      if (current_status.btn_20) { // Button is held down
        if (btn20_press_start_time == 0) {
          btn20_press_start_time = esp_timer_get_time();
          btn20_hold_triggered = false;
        } else {
          if (!btn20_hold_triggered &&
              (esp_timer_get_time() - btn20_press_start_time > 1000000)) {
            // Hold detected (> 1s) -> Exit
            ESP_LOGI(TAG, "Hold detected: Exiting Reader");
            saveProgress();
            current_state = STATE_MENU;
            need_redraw = true;
            btn20_hold_triggered = true; // Prevent click action on release
          }
        }
      } else { // Button is released
        if (btn20_press_start_time != 0) {
          // Falling edge
          if (!btn20_hold_triggered &&
              (esp_timer_get_time() - btn20_press_start_time < 1000000)) {
            // Short press -> Previous Page
            if (current_page_index > 0) {
              current_page_index--;
              saveProgress();
              force_full_refresh = true;
              need_redraw = true;
            }
          }
          btn20_press_start_time = 0;
          btn20_hold_triggered = false;
        }
      }

      if (btn19.pressed) { // Next Page
        if (!pages.empty()) {
          if (current_page_index < (int)pages.size() - 1) {
            current_page_index++;
            saveProgress();
            force_full_refresh = true;
            need_redraw = true;
          }
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
      } else if (current_state == STATE_MENU) {
        renderMenu();
      } else if (current_state == STATE_READER) {
        renderReader();
      }

      ESP_LOGI(TAG, "Updating Display (Partial: %d)",
               !(first_run || force_full_refresh));
      display->display(!(first_run || force_full_refresh));

      first_run = false;
      force_full_refresh = false;
      last_ui_update = esp_timer_get_time();
    }

    // Handled by task notification wait
    // vTaskDelay(pdMS_TO_TICKS(50)); // Fast polling
  }
}
