#include "button_manager.hpp"
#include "common_data.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
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
  
  // Enable wake-up on button press (active low)
  gpio_wakeup_enable(BUTTON_19_GPIO, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable(BUTTON_20_GPIO, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();

  // Also ensure the interrupt type is LOW_LEVEL so it fires if held down after wake
  // But wait, if we use LOW_LEVEL for ISR, it will fire continuously while held.
  // We want EDGE for ISR, LEVEL for WAKE.
  // The issue: If the falling edge happens while asleep, the wakeup triggers, but the edge interrupt might be missed.
  // One trick is to use GPIO_INTR_LOW_LEVEL temporarily or rely on the fact that we woke up.
  // Instead of complex ISR reconfiguration, we can add a small timeout to xQueueReceive 
  // to poll the buttons occasionally OR use a separate mechanism.
  
  // However, often the wake-up cause can be checked.
  // But our task is just waiting on a queue.
  
  // Better approach for buttons in Light Sleep:
  // Use ANYEDGE for the ISR.
  // When waking up, the level is still LOW.
  // If we missed the edge, we are stuck.
  
  // Let's rely on the fact that if we wake up, the buttons are likely pressed.
  // But we don't know we woke up inside the button task. The IDLE task woke up.
  
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
    // Wait for interrupt, but also check periodically (e.g., every 1s) OR
    // just rely on interrupts. The issue is missing the edge.
    // If we change portMAX_DELAY to a finite value, we poll.
    // Polling every 100ms is OK for buttons if we want to catch a missed edge, 
    // but better is to ensure the ISR fires.
    
    // Instead of indefinite wait, let's wait with a timeout.
    // If the queue is empty, we check the buttons anyway.
    // This handles the case where we wake up but the ISR didn't fire (missed edge).
    // A 200ms timeout is reasonable for responsiveness if the edge was missed.
    // But this prevents deep sleep? No, xQueueReceive blocks, so IDLE task runs, so Light Sleep happens.
    // The Light Sleep duration will be limited to 200ms chunks, which is fine.
    // It will wake up, check, and sleep again. 
    // This is a "tickless idle" friendly polling.
    
    bool event_received = xQueueReceive(gpio_evt_queue, &io_num, pdMS_TO_TICKS(100)); // 100ms polling

    // Debounce if an event actually occurred
    if (event_received) {
         vTaskDelay(pdMS_TO_TICKS(50));
    }

    // Always check state (Polling + Interrupt hybrid)
    bool current_19 = (gpio_get_level(BUTTON_19_GPIO) == 0);
    bool current_20 = (gpio_get_level(BUTTON_20_GPIO) == 0);

    // Only update if state really changed
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
