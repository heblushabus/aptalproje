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

  esp_err_t toggleASC();
  esp_err_t getASCStatus(bool *enabled);
  esp_err_t performFRC(uint16_t target_ppm);

  esp_err_t getSerialNumber(uint16_t &w0, uint16_t &w1, uint16_t &w2);
  esp_err_t performSelfTest(bool &malfunction);
  esp_err_t performFactoryReset();
  esp_err_t reinit();
  //esp_err_t getSensorVariant(uint16_t &variant);

private:
  static void task(void *pvParameters);

  i2c_dev_t dev;
};
