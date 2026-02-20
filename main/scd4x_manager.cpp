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

  // Enable internal pullups to complement external 10k resistors and suppress
  // driver warning
  dev.cfg.sda_pullup_en = 1;
  dev.cfg.scl_pullup_en = 1;
  dev.cfg.master.clk_speed = 100000;

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
  vTaskDelay(pdMS_TO_TICKS(30));
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

esp_err_t Scd4xManager::toggleASC() {
  bool enabled;
  ESP_LOGI(TAG, "Toggling ASC...");

  // Must stop measurements to change settings
  scd4x_stop_periodic_measurement(&dev);
  vTaskDelay(pdMS_TO_TICKS(500));

  esp_err_t err = scd4x_get_automatic_self_calibration(&dev, &enabled);
  if (err == ESP_OK) {
    bool new_state = !enabled;
    err = scd4x_set_automatic_self_calibration(&dev, new_state);
    if (err == ESP_OK) {
      scd4x_persist_settings(&dev);
      ESP_LOGI(TAG, "ASC now %s", new_state ? "Enabled" : "Disabled");
    }
  }

  scd4x_start_periodic_measurement(&dev);
  return err;
}

esp_err_t Scd4xManager::getASCStatus(bool *enabled) {
  // Must stop measurements to check settings
  scd4x_stop_periodic_measurement(&dev);
  vTaskDelay(pdMS_TO_TICKS(500));

  esp_err_t err = scd4x_get_automatic_self_calibration(&dev, enabled);

  scd4x_start_periodic_measurement(&dev);
  return err;
}

esp_err_t Scd4xManager::performFRC(uint16_t target_ppm) {
  uint16_t correction;
  ESP_LOGI(TAG, "Performing FRC at %u ppm...", target_ppm);

  // Must stop measurements
  scd4x_stop_periodic_measurement(&dev);
  vTaskDelay(pdMS_TO_TICKS(500));

  esp_err_t err =
      scd4x_perform_forced_recalibration(&dev, target_ppm, &correction);
  if (err == ESP_OK) {
    if (correction == 0xFFFF) {
      ESP_LOGE(TAG, "FRC failed!");
      err = ESP_FAIL;
    } else {
      ESP_LOGI(TAG, "FRC successful, correction: %u ppm", correction);
    }
  }

  scd4x_start_periodic_measurement(&dev);
  return err;
}

esp_err_t Scd4xManager::getSerialNumber(uint16_t &w0, uint16_t &w1,
                                        uint16_t &w2) {
  // Can be read during periodic measurement? Datasheet says "reading out... can
  // be used...". Usually needs idle? The datasheet doesn't explicitly say "only
  // in idle" for serial number in the summary table, but typically safest in
  // idle. scd4x.h doc says scd4x_get_serial_number.

  // To be safe and compliant with typical Sensirion flows, we'll stop periodic.
  scd4x_stop_periodic_measurement(&dev);
  vTaskDelay(pdMS_TO_TICKS(500));

  esp_err_t err = scd4x_get_serial_number(&dev, &w0, &w1, &w2);

  scd4x_start_periodic_measurement(&dev);
  return err;
}

esp_err_t Scd4xManager::performSelfTest(bool &malfunction) {
  ESP_LOGI(TAG, "Performing self test...");
  scd4x_stop_periodic_measurement(&dev);
  vTaskDelay(pdMS_TO_TICKS(500));

  esp_err_t err = scd4x_perform_self_test(&dev, &malfunction);
  if (err == ESP_OK) {
    // Datasheet specifies a self-test execution time of ~10 000 ms; wait before
    // restarting periodic measurement to ensure the sensor has fully completed
    // the test, even if the driver already blocks internally.
    vTaskDelay(pdMS_TO_TICKS(10000));
    ESP_LOGI(TAG, "Self test result: %s", malfunction ? "Malfunction" : "OK");
  } else {
    ESP_LOGE(TAG, "Self test command failed");
  }

  scd4x_start_periodic_measurement(&dev);
  return err;
}

esp_err_t Scd4xManager::performFactoryReset() {
  ESP_LOGI(TAG, "Performing factory reset...");
  scd4x_stop_periodic_measurement(&dev);
  vTaskDelay(pdMS_TO_TICKS(500));

  esp_err_t err = scd4x_perform_factory_reset(&dev);
  if (err == ESP_OK) {
    // Wait 1200 ms
    vTaskDelay(pdMS_TO_TICKS(1200));
    ESP_LOGI(TAG, "Factory reset complete");
  }

  scd4x_start_periodic_measurement(&dev);
  return err;
}

esp_err_t Scd4xManager::reinit() {
  ESP_LOGI(TAG, "Reinitializing sensor...");
  scd4x_stop_periodic_measurement(&dev);
  vTaskDelay(pdMS_TO_TICKS(500));

  esp_err_t err = scd4x_reinit(&dev);
  if (err == ESP_OK) {
    // Datasheet: reinit command execution time t_reinit = 20 ms; use 30 ms
    // here to provide a safety margin.
    vTaskDelay(pdMS_TO_TICKS(30));
  }

  scd4x_start_periodic_measurement(&dev);
  return err;
}
/*AAAAAAAA
esp_err_t Scd4xManager::getSensorVariant(uint16_t &variant) {
  scd4x_stop_periodic_measurement(&dev);
  vTaskDelay(pdMS_TO_TICKS(500));

  esp_err_t err = scd4x_get_sensor_variant(&dev, &variant);

  scd4x_start_periodic_measurement(&dev);
  return err;
}*/

void Scd4xManager::task(void *pvParameters) {
  Scd4xManager *self = (Scd4xManager *)pvParameters;

  uint16_t co2;
  float temperature, humidity;

  while (1) {
    bool data_ready = false;
    // Poll data ready flag every 100ms
    esp_err_t res = scd4x_get_data_ready_status(&self->dev, &data_ready);

    if (res != ESP_OK) {
      // If checking status fails, just wait a bit and retry
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (!data_ready) {
      // Data not ready yet, wait 100ms
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // Give the sensor a moment to prepare the buffer after signaling ready
    vTaskDelay(pdMS_TO_TICKS(50));

    // Data is ready, read it
    res = scd4x_read_measurement(&self->dev, &co2, &temperature, &humidity);
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

    global_data.setEnvironmental(co2, temperature, humidity);
    global_data.addCO2Reading(co2);
    global_data.notifyUI();

    // Wait for the next measurement cycle
    // SCD4x update interval is ~5 seconds. We wait 4900ms to minimize polling.
    // This allows the CPU to stay in Light Sleep for almost the entire duration.
    vTaskDelay(pdMS_TO_TICKS(4900));
  }
}
