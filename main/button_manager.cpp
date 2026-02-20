#include "button_manager.hpp"
#include "common_data.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "ButtonManager";

#define BUTTON_19_GPIO GPIO_NUM_19
#define BUTTON_20_GPIO GPIO_NUM_20

static QueueHandle_t gpio_evt_queue = NULL;

static void IRAM_ATTR gpio_isr_handler(void *arg) {
  uint32_t gpio_num = (uint32_t)arg;
  xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

ButtonManager::ButtonManager() {}

ButtonManager::~ButtonManager() {
  if (button_task_handle) {
    vTaskDelete(button_task_handle);
  }
}

esp_err_t ButtonManager::init() {
  ESP_LOGI(TAG, "Initializing Buttons on GPIO 19 and 20 (Active Low)...");

  // Create a queue to handle gpio event from isr
  gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));

  gpio_config_t btn_cfg = {};
  btn_cfg.intr_type = GPIO_INTR_ANYEDGE;
  btn_cfg.mode = GPIO_MODE_INPUT;
  btn_cfg.pin_bit_mask = (1ULL << BUTTON_19_GPIO) | (1ULL << BUTTON_20_GPIO);
  btn_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  btn_cfg.pull_up_en = GPIO_PULLUP_ENABLE;

  esp_err_t err = gpio_config(&btn_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(err));
    return err;
  }

  // INSTALL ISR SERVICE
  err = gpio_install_isr_service(0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "ISR install failed: %s", esp_err_to_name(err));
    return err;
  }

  gpio_isr_handler_add(BUTTON_19_GPIO, gpio_isr_handler,
                       (void *)BUTTON_19_GPIO);
  gpio_isr_handler_add(BUTTON_20_GPIO, gpio_isr_handler,
                       (void *)BUTTON_20_GPIO);

  return ESP_OK;
}

esp_err_t ButtonManager::start() {
  xTaskCreate(button_task, "button_task", 4096, NULL, 5, &button_task_handle);
  return ESP_OK;
}

void ButtonManager::button_task(void *arg) {
  bool last_19 = true;
  bool last_20 = true;
  uint32_t io_num;

  // Initial state check (Active Low: 0 = Pressed, 1 = Released)
  last_19 = (gpio_get_level(BUTTON_19_GPIO) == 0);
  last_20 = (gpio_get_level(BUTTON_20_GPIO) == 0);

  // Sync initial state
  DeviceStatus status = global_data.getStatus();
  status.btn_19 = last_19;
  status.btn_20 = last_20;
  global_data.setStatus(status);

  while (1) {
    if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
      // Simple debounce
      vTaskDelay(pdMS_TO_TICKS(50));

      bool current_19 = (gpio_get_level(BUTTON_19_GPIO) == 0);
      bool current_20 = (gpio_get_level(BUTTON_20_GPIO) == 0);

      // Only update if state really changed after debounce
      if (current_19 != last_19 || current_20 != last_20) {
        status = global_data.getStatus();
        status.btn_19 = current_19;
        status.btn_20 = current_20;
        global_data.setStatus(status);

        if (current_19 != last_19) {
          ESP_LOGI(TAG, "Button 19: %s", current_19 ? "Pressed" : "Released");
        }
        if (current_20 != last_20) {
          ESP_LOGI(TAG, "Button 20: %s", current_20 ? "Pressed" : "Released");
        }

        last_19 = current_19;
        last_20 = current_20;

        // Wake UP UI
        global_data.notifyUI();
      }
    }
  }
}
