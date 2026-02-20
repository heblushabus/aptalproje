#pragma once

#include "common_data.hpp"
#include "display_manager.hpp"
#include "esp_timer.h"
#include <string>
#include <time.h>
#include <vector>

class StorageManager;
class Scd4xManager;

class UIManager {
public:
  UIManager(Adafruit_SSD1680 *display, StorageManager *storageManager,
            Scd4xManager *scd4xManager);

  // Start the UI task
  void start();

private:
  // Task entry point
  static void taskEntry(void *param);

  // Main loop
  void loop();

  // Input handling
  struct ButtonState {
    bool last_state;
    bool pressed;
  };
  void updateButtonState(ButtonState &btn, bool current);

  // Rendering
  void renderHome(const DeviceStatus &status, const struct tm *timeinfo);
  void renderMenu();
  void renderReader();

  // Members
  Adafruit_SSD1680 *display;
  StorageManager *storageManager;
  Scd4xManager *scd4xManager;

  enum AppState { STATE_HOME, STATE_MENU, STATE_READER };
  AppState current_state;
  int selected_menu_index;
  bool asc_enabled;

  // Reader State
  std::vector<std::string> pages;
  int current_page_index;
  void paginateContent(const std::string &content);
  void saveProgress();
  void loadProgress();

  ButtonState btn19;
  ButtonState btn20;

  // Button Hold Tracking
  int64_t btn20_press_start_time;
  bool btn20_hold_triggered;
};
