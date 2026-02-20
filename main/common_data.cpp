#include "common_data.hpp"
#include "esp_timer.h"
#include <vector>

CommonData::CommonData() {
  mutex = xSemaphoreCreateMutex();

  // Initialize with default values
  status = {}; // Zero initialize
  status.co2_ppm = 1372;
  status.temperature = 12.34f;
  status.temp_bmp = 0.0f;
  status.humidity = 34.87f;
  status.altitude = 23.18f;
  status.altitude_offset = 0.0f;
  status.battery_voltage = 3.7f;
  status.wifi_connected = false;
  status.last_env_update_us = 0;
}

void CommonData::setStatus(const DeviceStatus &new_status) {
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    status = new_status;
    xSemaphoreGive(mutex);
  }
}

void CommonData::setEnvironmental(int co2, float temp, float hum) {
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    status.co2_ppm = co2;
    status.temperature = temp;
    status.humidity = hum;
    status.last_env_update_us = esp_timer_get_time() + 1000;
    status.scd_measuring = false;
  
    if (co2 > 0) {
      co2_history.push_back(co2);
      if (co2_history.size() > 200) {
        co2_history.erase(co2_history.begin());
      }
    }
    xSemaphoreGive(mutex);
  }
  notifyUI();
}

void CommonData::setBmpData(float pressure, float temp, float alt) {
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    status.pressure_pa = pressure;
    status.temp_bmp = temp;
    float final_alt = alt + status.altitude_offset;
    status.altitude = final_alt;
    status.last_env_update_us = esp_timer_get_time() + 1000;
    
    altitude_history.push_back(final_alt);
    if (altitude_history.size() > 200) {
      altitude_history.erase(altitude_history.begin());
    }
    xSemaphoreGive(mutex);
  }
  notifyUI();
}

void CommonData::setAltitudeOffset(float offset) {
    if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    status.altitude_offset = offset;
    xSemaphoreGive(mutex);
  }
}

DeviceStatus CommonData::getStatus() {
  DeviceStatus current_status;
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    current_status = status;
    xSemaphoreGive(mutex);
  }
  return current_status;
}

void CommonData::registerUITask(TaskHandle_t handle) {
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    ui_task_handle = handle;
    xSemaphoreGive(mutex);
  }
}

void CommonData::notifyUI() {
  TaskHandle_t task_to_notify = nullptr;
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    task_to_notify = ui_task_handle;
    xSemaphoreGive(mutex);
  }
  if (task_to_notify != nullptr) {
    xTaskNotifyGive(task_to_notify);
  }
}

void CommonData::addCO2Reading(int ppm) {
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    if (ppm > 0) {
      co2_history.push_back(ppm);
      if (co2_history.size() > 200) {
        co2_history.erase(co2_history.begin());
      }
    }
    xSemaphoreGive(mutex);
  }
}

std::vector<int> CommonData::getCO2History() {
  std::vector<int> history;
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    history = co2_history;
    xSemaphoreGive(mutex);
  }
  return history;
}

void CommonData::addAltitudeReading(float alt) {
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    altitude_history.push_back(alt);
    if (altitude_history.size() > 200) {
      altitude_history.erase(altitude_history.begin());
    }
    xSemaphoreGive(mutex);
  }
}

std::vector<float> CommonData::getAltitudeHistory() {
  std::vector<float> history;
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    history = altitude_history;
    xSemaphoreGive(mutex);
  }
  return history;
}

void CommonData::clearAltitudeHistory() {
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    altitude_history.clear();
    xSemaphoreGive(mutex);
  }
}

// Instantiate global object
CommonData global_data;
