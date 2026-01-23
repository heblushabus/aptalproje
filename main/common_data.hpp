#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>

#include <time.h>

/**
 * @brief Thread-safe storage for shared application data
 */
struct DeviceStatus {
  // Environmental data
  int co2_ppm;
  float temperature;
  float humidity;
  float altitude;

  // Battery/Network
  float battery_voltage;
  bool wifi_connected;

  // Touch inputs
  bool touch_4;
  bool touch_5;
};

class CommonData {
public:
  CommonData();

  // Thread-safe setters
  void setStatus(const DeviceStatus &new_status);
  void setEnvironmental(int co2, float temp, float hum, float alt);

  // Thread-safe getter
  DeviceStatus getStatus();

private:
  DeviceStatus status;
  SemaphoreHandle_t mutex;
};

// Global instance
extern CommonData global_data;
