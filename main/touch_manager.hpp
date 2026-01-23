#pragma once

#include "driver/touch_sens.h"
#include "esp_err.h"

class TouchManager {
public:
  TouchManager();
  ~TouchManager();

  esp_err_t init();
  esp_err_t start();

private:
  touch_sensor_handle_t sens_handle = NULL;
  touch_channel_handle_t chan_handle_4 = NULL; // For GPIO 4
  touch_channel_handle_t chan_handle_5 = NULL; // For GPIO 5
};
