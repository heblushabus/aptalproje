#include "touch_manager.hpp"
#include "common_data.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/touch_sens_types.h"

static const char *TAG = "TouchManager";

#define TOUCH_BUTTON_4_CHAN_ID 4
#define BUTTON_GPIO_0 GPIO_NUM_0

// Callbacks
static bool touch_on_active_callback(touch_sensor_handle_t sens_handle,
                                     const touch_active_event_data_t *event,
                                     void *user_ctx) {
  DeviceStatus status = global_data.getStatus();
  if (event->chan_id == TOUCH_BUTTON_4_CHAN_ID) {
    status.touch_4 = true;
    ESP_EARLY_LOGI(TAG, "Touch 4 Active");
  }
  global_data.setStatus(status);
  return false;
}

static bool touch_on_inactive_callback(touch_sensor_handle_t sens_handle,
                                       const touch_inactive_event_data_t *event,
                                       void *user_ctx) {
  DeviceStatus status = global_data.getStatus();
  if (event->chan_id == TOUCH_BUTTON_4_CHAN_ID) {
    status.touch_4 = false;
    ESP_EARLY_LOGI(TAG, "Touch 4 Inactive");
  }
  global_data.setStatus(status);
  return false;
}

TouchManager::TouchManager() {}

TouchManager::~TouchManager() {
  if (sens_handle) {
    touch_sensor_disable(sens_handle);
    touch_sensor_del_controller(sens_handle);
  }
  if (button_task_handle) {
    vTaskDelete(button_task_handle);
  }
}

esp_err_t TouchManager::init() {
  ESP_LOGI(TAG, "Initializing Touch Sensing (Tuned)...");

  touch_sensor_sample_config_t sample_cfg[1] = {
      TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(500, TOUCH_VOLT_LIM_L_0V5,
                                            TOUCH_VOLT_LIM_H_2V2)};

  touch_sensor_config_t sens_cfg =
      TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(1, sample_cfg);

  ESP_ERROR_CHECK(touch_sensor_new_controller(&sens_cfg, &sens_handle));

  // 2. Create channels
  touch_channel_config_t chan_cfg = {
      .active_thresh = {40000},                           //
      .charge_speed = TOUCH_CHARGE_SPEED_7,               //
      .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT, //
  };

  ESP_ERROR_CHECK(touch_sensor_new_channel(sens_handle, TOUCH_BUTTON_4_CHAN_ID,
                                           &chan_cfg, &chan_handle_4));

  touch_chan_info_t chan_info;
  touch_sensor_get_channel_info(chan_handle_4, &chan_info);
  ESP_LOGI(TAG, "Touch Chan 4 mapped to GPIO %d", chan_info.chan_gpio);

  // Initialize GPIO Button (Replaces Touch 5)
  gpio_config_t btn_cfg = {};
  btn_cfg.intr_type = GPIO_INTR_DISABLE;
  btn_cfg.mode = GPIO_MODE_INPUT;
  btn_cfg.pin_bit_mask = (1ULL << BUTTON_GPIO_0);
  btn_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  btn_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&btn_cfg);

  // Start Button Task
  xTaskCreate(button_task, "button_task", 4096, NULL, 5, &button_task_handle);

  // 3. Configure Filter
  touch_sensor_filter_config_t filter_cfg =
      TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
  ESP_ERROR_CHECK(touch_sensor_config_filter(sens_handle, &filter_cfg));

  // 4. Register callbacks
  touch_event_callbacks_t callbacks = {
      .on_active = touch_on_active_callback,
      .on_inactive = touch_on_inactive_callback,
      .on_measure_done = NULL,
      .on_scan_done = NULL,
      .on_timeout = NULL,
      .on_proximity_meas_done = NULL,
  };
  ESP_ERROR_CHECK(
      touch_sensor_register_callbacks(sens_handle, &callbacks, NULL));

  // 5. Initial Scan for Benchmark
  ESP_ERROR_CHECK(touch_sensor_enable(sens_handle));

  // Scan a few times to stabilize
  for (int i = 0; i < 3; i++) {
    // Increased timeout to 5000ms
    esp_err_t err = touch_sensor_trigger_oneshot_scanning(sens_handle, 5000);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Oneshot scanning failed: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(50)); // Short delay between scans
  }

  // Disable to reconfigure thresholds
  ESP_ERROR_CHECK(touch_sensor_disable(sens_handle));

  // 6. Set Thresholds based on Benchmark
  uint32_t benchmark[1] = {0};
  uint32_t smooth[1] = {0};
  // Channel 4 only

  // Read SMOOTH data first as it reflects current capacitance
  ESP_ERROR_CHECK(touch_channel_read_data(chan_handle_4,
                                          TOUCH_CHAN_DATA_TYPE_SMOOTH, smooth));
  ESP_ERROR_CHECK(touch_channel_read_data(
      chan_handle_4, TOUCH_CHAN_DATA_TYPE_BENCHMARK, benchmark));

  // Let's use smooth data as the "benchmark" reference for this session.
  // Use BENCHMARK data as the reference if available
  uint32_t benchmark_val = benchmark[0];

  // Reuse the channel config structure (ensure we don't zero out other
  // settings)
  touch_channel_config_t update_cfg = chan_cfg;

  // Set threshold to 2% (0.02) of reference.
  // Ensure at least some minimum threshold
  uint32_t thresh = (uint32_t)(benchmark_val * 0.1);
  if (thresh < 500)
    thresh = 500; // Minimum threshold floor

  update_cfg.active_thresh[0] = thresh;

  ESP_ERROR_CHECK(touch_sensor_reconfig_channel(chan_handle_4, &update_cfg));

  ESP_LOGI(TAG, "Touch %d Smooth: %lu, Benchmark: %lu, Set Threshold: %lu",
           TOUCH_BUTTON_4_CHAN_ID, smooth[0], benchmark[0],
           update_cfg.active_thresh[0]);

  return ESP_OK;
}

esp_err_t TouchManager::start() {
  ESP_ERROR_CHECK(touch_sensor_enable(sens_handle));
  return touch_sensor_start_continuous_scanning(sens_handle);
}

void TouchManager::button_task(void *arg) {
  bool last_pressed = false;
  // Initial state check
  int level = gpio_get_level(BUTTON_GPIO_0);
  last_pressed = (level == 0);
  // Sync initial state
  DeviceStatus status = global_data.getStatus();
  status.touch_5 = last_pressed;
  global_data.setStatus(status);

  while (1) {
    level = gpio_get_level(BUTTON_GPIO_0);
    bool pressed = (level == 0);

    if (pressed != last_pressed) {
      status = global_data.getStatus();
      status.touch_5 = pressed;
      global_data.setStatus(status);
      if (pressed) {
        ESP_LOGI(TAG, "Touch 5 (Button) Active");
      } else {
        ESP_LOGI(TAG, "Touch 5 (Button) Inactive");
      }
      last_pressed = pressed;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}