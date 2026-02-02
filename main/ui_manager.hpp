#pragma once

#include "common_data.hpp"
#include "display_manager.hpp"
#include "esp_timer.h"
#include <string>
#include <time.h>
#include <vector>

class StorageManager;

class UIManager {
public:
  UIManager(Adafruit_SSD1680 *display, StorageManager *storageManager);

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

  enum AppState { STATE_HOME, STATE_MENU, STATE_READER };
  AppState current_state;
  int selected_menu_index;

  // Reader State
  std::vector<std::string> pages;
  int current_page_index;
  void paginateContent(const std::string &content);
  void saveProgress();
  void loadProgress();

  ButtonState btn4;
  ButtonState btn5;

  // Button Hold Tracking
  int64_t btn5_press_start_time;
  bool btn5_hold_triggered;
};
