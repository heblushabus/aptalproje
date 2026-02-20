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

  // Button inputs
  bool btn_19;
  bool btn_20;

  // Measurement timestamp
  int64_t last_env_update_us;
};

class CommonData {
public:
  CommonData();

  // Thread-safe setters
  void setStatus(const DeviceStatus &new_status);
  void setEnvironmental(int co2, float temp, float hum, float alt);

  // Thread-safe getter
  DeviceStatus getStatus();

  void registerUITask(TaskHandle_t handle);
  void notifyUI();

private:
  DeviceStatus status;
  SemaphoreHandle_t mutex;
  TaskHandle_t ui_task_handle = nullptr;
};

// Global instance
extern CommonData global_data;
