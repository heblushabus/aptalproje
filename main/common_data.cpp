#include "common_data.hpp"
#include "esp_timer.h"

CommonData::CommonData() {
  mutex = xSemaphoreCreateMutex();

  // Initialize with default values
  status = {}; // Zero initialize
  status.co2_ppm = 1372;
  status.temperature = 12.34f;
  status.humidity = 34.87f;
  status.altitude = 23.18f;
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

void CommonData::setEnvironmental(int co2, float temp, float hum, float alt) {
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    status.co2_ppm = co2;
    status.temperature = temp;
    status.humidity = hum;
    status.altitude = alt;
    status.last_env_update_us = esp_timer_get_time();
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

// Instantiate global object
CommonData global_data;
