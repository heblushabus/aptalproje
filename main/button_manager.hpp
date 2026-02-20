#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class ButtonManager {
public:
  ButtonManager();
  ~ButtonManager();

  esp_err_t init();
  esp_err_t start();

private:
  TaskHandle_t button_task_handle = NULL;
  static void button_task(void *arg);
};
