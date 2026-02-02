#pragma once

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <i2cdev.h>
#include <scd4x.h>


class Scd4xManager {
public:
  Scd4xManager();
  esp_err_t init(int sda_pin, int scl_pin);
  void start();

private:
  static void task(void *pvParameters);

  i2c_dev_t dev;
};
