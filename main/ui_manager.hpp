#pragma once

#include "common_data.hpp"
#include "display_manager.hpp"
#include "esp_timer.h"
#include <string>
#include <time.h>
#include <vector>

class StorageManager;
class Scd4xManager;
class Bmp580Manager;

class UIManager {
public:
  UIManager(Adafruit_SSD1680 *display, StorageManager *storageManager,
            Scd4xManager *scd4xManager, Bmp580Manager *bmp580Manager);

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
  void renderTrimAltitudeMenu();
  void renderReader();

  // Members
  Adafruit_SSD1680 *display;
  StorageManager *storageManager;
  Scd4xManager *scd4xManager;
  Bmp580Manager *bmp580Manager;

  enum AppState { STATE_HOME, STATE_MENU, STATE_READER, STATE_TRIM_ALTITUDE };
  AppState current_state;
  
  enum GraphMode { GRAPH_CO2, GRAPH_ALTITUDE };
  GraphMode current_graph_mode;
  
  int selected_menu_index;
  bool asc_enabled;
  
  float trim_altitude_val = 0.0f;

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
