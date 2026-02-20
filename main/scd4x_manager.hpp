#pragma once

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <i2cdev.h>
#include <scd4x.h>

// Forward declaration
class Bmp580Manager;

class Scd4xManager {
public:
  Scd4xManager();
  esp_err_t init(int sda_pin, int scl_pin);
  void start();
  void setBmp580Manager(Bmp580Manager* manager) { bmp580Manager = manager; }

  esp_err_t toggleASC();
  esp_err_t getASCStatus(bool *enabled);
  esp_err_t performFRC(uint16_t target_ppm);

  esp_err_t getSerialNumber(uint16_t &w0, uint16_t &w1, uint16_t &w2);
  esp_err_t performSelfTest(bool &malfunction);
  esp_err_t performFactoryReset();
  esp_err_t reinit();
  void forceMeasurement();
  bool isASCEnabled() const { return asc_enabled_cache; }

private:
  static void task(void *pvParameters);

  i2c_dev_t dev;
  TaskHandle_t taskHandle = nullptr;
  Bmp580Manager* bmp580Manager = nullptr;
  bool asc_enabled_cache = false;
};
