#pragma once

#include "common_data.hpp"
#include "display_manager.hpp"
#include "esp_timer.h"
#include <time.h>


class UIManager {
public:
  UIManager(Adafruit_SSD1680 *display);

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

  // Members
  Adafruit_SSD1680 *display;

  enum AppState { STATE_HOME, STATE_MENU };
  AppState current_state;
  int selected_menu_index;

  ButtonState btn4;
  ButtonState btn5;
};
