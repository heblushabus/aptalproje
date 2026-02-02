#include "scd4x_manager.hpp"
#include "common_data.hpp"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "Scd4xManager";

Scd4xManager::Scd4xManager() { memset(&dev, 0, sizeof(i2c_dev_t)); }

esp_err_t Scd4xManager::init(int sda_pin, int scl_pin) {
  // Initialize standard I2C descriptor for SCD4x
  // Using I2C Port 0
  ESP_ERROR_CHECK(scd4x_init_desc(&dev, (i2c_port_t)0, (gpio_num_t)sda_pin,
                                  (gpio_num_t)scl_pin));

  ESP_LOGI(TAG, "Initializing sensor...");
  // Attempt to stop periodic measurement first to reset state (important for
  // warm reboots) We ignore the error because it might fail if the sensor is
  // already idle, or if comms are glitchy at start
  scd4x_stop_periodic_measurement(&dev);
  vTaskDelay(pdMS_TO_TICKS(500));

  // Disable wake_up for now as it's typically for low-power mode exit, and
  // stop_periodic handles the main active state scd4x_wake_up(&dev);

  // Re-init to load settings
  scd4x_reinit(&dev);
  vTaskDelay(pdMS_TO_TICKS(20));
  ESP_LOGI(TAG, "Sensor initialized");

  uint16_t serial[3];
  ESP_ERROR_CHECK(
      scd4x_get_serial_number(&dev, serial, serial + 1, serial + 2));
  ESP_LOGI(TAG, "Sensor serial number: 0x%04x%04x%04x", serial[0], serial[1],
           serial[2]);

  return ESP_OK;
}

void Scd4xManager::start() {
  // Start periodic measurements
  ESP_ERROR_CHECK(scd4x_start_periodic_measurement(&dev));
  ESP_LOGI(TAG, "Periodic measurements started");

  xTaskCreate(task, "scd4x_task", 4096, this, 5, NULL);
}

void Scd4xManager::task(void *pvParameters) {
  Scd4xManager *self = (Scd4xManager *)pvParameters;

  uint16_t co2;
  float temperature, humidity;

  while (1) {
    // SCD4x update interval is usually 5 seconds
    vTaskDelay(pdMS_TO_TICKS(5000));

    esp_err_t res =
        scd4x_read_measurement(&self->dev, &co2, &temperature, &humidity);
    if (res != ESP_OK) {
      ESP_LOGE(TAG, "Error reading results %d (%s)", res, esp_err_to_name(res));
      continue;
    }

    if (co2 == 0) {
      ESP_LOGW(TAG, "Invalid sample detected, skipping");
      continue;
    }

    ESP_LOGI(TAG, "CO2: %u ppm, Temp: %.2f C, Hum: %.2f %%", co2, temperature,
             humidity);

    DeviceStatus status = global_data.getStatus();
    global_data.setEnvironmental(co2, temperature, humidity, status.altitude);
  }
}
