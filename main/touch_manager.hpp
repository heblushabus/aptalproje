#pragma once

#include "driver/touch_sens.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


class TouchManager {
public:
  TouchManager();
  ~TouchManager();

  esp_err_t init();
  esp_err_t start();

private:
  touch_sensor_handle_t sens_handle = NULL;
  touch_channel_handle_t chan_handle_4 = NULL; // For GPIO 4

  TaskHandle_t button_task_handle = NULL;
  static void button_task(void *arg);
};
