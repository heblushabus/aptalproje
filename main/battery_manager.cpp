#include "battery_manager.hpp"
#include "common_data.hpp"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "BatteryManager";

#define BATTERY_GPIO 9
#define BATTERY_ATTEN ADC_ATTEN_DB_12

BatteryManager::BatteryManager() {}

BatteryManager::~BatteryManager() {
  if (adc_handle) {
    adc_oneshot_del_unit(adc_handle);
  }
}

static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel,
                                 adc_atten_t atten,
                                 adc_cali_handle_t *out_handle) {
  adc_cali_handle_t handle = NULL;
  esp_err_t ret = ESP_FAIL;
  bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  if (!calibrated) {
    ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .chan = channel,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
      calibrated = true;
    }
  }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
  if (!calibrated) {
    ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
      calibrated = true;
    }
  }
#endif

  *out_handle = handle;
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Calibration Success");
  } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
    ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
  } else {
    ESP_LOGE(TAG, "Invalid arg or no mem");
  }

  return calibrated;
}

esp_err_t BatteryManager::init() {
  ESP_LOGI(TAG, "Initializing Battery Manager");

  adc_unit_t unit_id;
  adc_channel_t channel;
  ESP_ERROR_CHECK(adc_oneshot_io_to_channel(BATTERY_GPIO, &unit_id, &channel));
  this->adc_channel = channel; // Store for task

  adc_oneshot_unit_init_cfg_t init_config = {
      .unit_id = unit_id,
      .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
      .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

  adc_oneshot_chan_cfg_t config = {
      .atten = BATTERY_ATTEN,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, channel, &config));

  calibrated =
      adc_calibration_init(unit_id, channel, BATTERY_ATTEN, &cali_handle);

  return ESP_OK;
}

void BatteryManager::start() {
  xTaskCreate(battery_task, "battery_task", 4096, this, 5, NULL);
}

void BatteryManager::battery_task(void *pvParameters) {
  BatteryManager *self = (BatteryManager *)pvParameters;

  while (1) {
    int adc_raw = 0;
    int voltage_mv = 0;

    ESP_ERROR_CHECK(
        adc_oneshot_read(self->adc_handle, self->adc_channel, &adc_raw));

    if (self->calibrated) {
      ESP_ERROR_CHECK(
          adc_cali_raw_to_voltage(self->cali_handle, adc_raw, &voltage_mv));
    } else {
      // Fallback estimation if uncalibrated: V = Raw * Vmax / RawMax
      // For 11dB (3.3V range approx) and 12-bit (4095)
      voltage_mv = adc_raw * 3300 / 4095;
    }

    // Multiply by 2 as per hardware divider
    float battery_v = (voltage_mv * 2.0f) / 1000.0f;

    // Update shared state
    DeviceStatus status = global_data.getStatus();
    status.battery_voltage = battery_v;
    global_data.setStatus(status);

    ESP_LOGD(TAG, "Battery: Raw %d, %d mV, %.2f V", adc_raw, voltage_mv,
             battery_v);

    vTaskDelay(pdMS_TO_TICKS(2000)); // Update every 2 seconds
  }
}
