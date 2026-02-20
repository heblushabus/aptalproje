#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>

#include <time.h>
#include <vector>

/**
 * @brief Thread-safe storage for shared application data
 */
struct DeviceStatus {
  // Environmental data
  int co2_ppm;
  float temperature;
  float humidity;
  float altitude;
  float pressure_pa;
  float temp_bmp;

  // Battery/Network
  float battery_voltage;
  bool wifi_connected;

  // Button inputs
  bool btn_19;
  bool btn_20;

  // Measurement timestamp
  int64_t last_env_update_us;
  
  float altitude_offset = 0.0f;
  
  // SCD4x measurement in progress
  bool scd_measuring = false;
};

class CommonData {
public:
  CommonData();

  // Thread-safe setters
  void setStatus(const DeviceStatus &new_status);
  void setEnvironmental(int co2, float temp, float hum);
  void setBmpData(float pressure, float temp, float altitude);
  void setAltitudeOffset(float offset);

  // Thread-safe getter
  DeviceStatus getStatus();

  void registerUITask(TaskHandle_t handle);
  void notifyUI();
  
  void addCO2Reading(int ppm);
  std::vector<int> getCO2History();
  
  void addAltitudeReading(float alt);
  std::vector<float> getAltitudeHistory();
  void clearAltitudeHistory();

private:
  DeviceStatus status;
  SemaphoreHandle_t mutex;
  TaskHandle_t ui_task_handle = nullptr;
  std::vector<int> co2_history;
  std::vector<float> altitude_history;
};

// Global instance
extern CommonData global_data;
