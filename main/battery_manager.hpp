#pragma once

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


class BatteryManager {
public:
  BatteryManager();
  ~BatteryManager();

  esp_err_t init();
  void start();

private:
  static void battery_task(void *pvParameters);

  adc_oneshot_unit_handle_t adc_handle = NULL;
  adc_cali_handle_t cali_handle = NULL;
  bool calibrated = false;
  adc_channel_t adc_channel; // Call check mapping during init
};
