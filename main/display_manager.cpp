#include "display_manager.hpp"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "DisplayManager";

// --- Adafruit_SSD1680 implementation ---

Adafruit_SSD1680::Adafruit_SSD1680(int16_t w, int16_t h,
                                   esp_lcd_panel_handle_t handle,
                                   SemaphoreHandle_t semaphore)
    : Adafruit_GFX(w, h), panel_handle(handle),
      epaper_panel_semaphore(semaphore) {

  buffer_size = (w * h) / 8;
  buffer = (uint8_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_DMA);
  if (!buffer) {
    ESP_LOGE(TAG, "Failed to allocate graphics buffer!");
  } else {
    memset(buffer, 0xFF, buffer_size); // Clear to white
  }
}

Adafruit_SSD1680::~Adafruit_SSD1680() {
  if (buffer) {
    free(buffer);
  }
}

void Adafruit_SSD1680::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if ((x < 0) || (x >= _width) || (y < 0) || (y >= _height))
    return;

  // Handle rotation
  int16_t t;
  switch (rotation) {
  case 1:
    t = x;
    x = WIDTH - 1 - y;
    y = t;
    break;
  case 2:
    x = WIDTH - 1 - x;
    y = HEIGHT - 1 - y;
    break;
  case 3:
    t = x;
    x = y;
    y = HEIGHT - 1 - t;
    break;
  }

  if ((x < 0) || (x >= EPD_WIDTH) || (y < 0) || (y >= EPD_HEIGHT))
    return;

  uint32_t idx = (y * EPD_WIDTH) + x;
  uint32_t byte_idx = idx / 8;
  uint8_t bit_idx = 7 - (idx % 8);

  if (color == GFX_BLACK) {
    buffer[byte_idx] &= ~(1 << bit_idx); // Clear bit -> Black
  } else {
    buffer[byte_idx] |= (1 << bit_idx); // Set bit -> White
  }
}

void Adafruit_SSD1680::clearBuffer() {
  if (buffer) {
    memset(buffer, 0xFF, buffer_size);
  }
}

void Adafruit_SSD1680::display(bool partial) {
  if (!buffer || !panel_handle || !epaper_panel_semaphore)
    return;

  // Wait for previous operation to complete
  xSemaphoreTake(epaper_panel_semaphore, portMAX_DELAY);

  if (partial) {
    // 1. Write to Current RAM (0x24) - partial mode
    epaper_panel_set_refresh_mode(panel_handle, false);
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, EPD_WIDTH, EPD_HEIGHT,
                              buffer);

    // 2. Refresh Display
    epaper_panel_refresh_screen(panel_handle);

    // 3. Write to Previous RAM (0x26) - full mode (writes both)
    // This ensures 0x26 matches the new state for the next comparison
    epaper_panel_set_refresh_mode(panel_handle, true);
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, EPD_WIDTH, EPD_HEIGHT,
                              buffer);
  } else {
    epaper_panel_set_refresh_mode(panel_handle, true); // Full
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, EPD_WIDTH, EPD_HEIGHT,
                              buffer);
    epaper_panel_refresh_screen(panel_handle);
  }
}

void Adafruit_SSD1680::printRightAligned(int16_t x, int16_t y,
                                         const char *str) {
  int16_t x1, y1;
  uint16_t w, h;
  // Get bounds with dummy coordinates to find the absolute width and offset
  getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  // Correct cursor position: Right Edge = CursorX + OffsetX + Width
  // So CursorX = TargetX - Width - OffsetX
  setCursor(x - w - x1, y);
  print(str);
}

// --- DisplayManager implementation ---

DisplayManager::DisplayManager() {}

DisplayManager::~DisplayManager() {
  if (display)
    delete display;
  if (epaper_panel_semaphore)
    vSemaphoreDelete(epaper_panel_semaphore);
}

bool DisplayManager::event_callback(const esp_lcd_panel_handle_t handle,
                                    const void *edata, void *user_data) {
  SemaphoreHandle_t *semaphore_ptr = (SemaphoreHandle_t *)user_data;
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(*semaphore_ptr, &xHigherPriorityTaskWoken);
  return xHigherPriorityTaskWoken == pdTRUE;
}

esp_err_t DisplayManager::init() {
  esp_err_t ret;

  // Set pin 42 to HIGH (Power Enable)
  gpio_reset_pin(GPIO_NUM_42);
  gpio_set_direction(GPIO_NUM_42, GPIO_MODE_OUTPUT);
  gpio_set_level(GPIO_NUM_42, 1);

  // --- Init SPI Bus
  ESP_LOGI(TAG, "Initializing SPI Bus...");
  spi_bus_config_t buscfg = {};
  buscfg.mosi_io_num = EXAMPLE_PIN_NUM_MOSI;
  buscfg.miso_io_num = -1;
  buscfg.sclk_io_num = EXAMPLE_PIN_NUM_SCLK;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = SOC_SPI_MAXIMUM_BUFFER_SIZE;
  ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK)
    return ret;

  // --- Init ESP_LCD IO
  ESP_LOGI(TAG, "Initializing panel IO...");
  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.cs_gpio_num = EXAMPLE_PIN_NUM_EPD_CS;
  io_config.dc_gpio_num = EXAMPLE_PIN_NUM_EPD_DC;
  io_config.spi_mode = EPD_PANEL_SPI_MODE;
  io_config.pclk_hz = EPD_PANEL_SPI_CLK;
  io_config.trans_queue_depth = 10;
  io_config.lcd_cmd_bits = EPD_PANEL_SPI_CMD_BITS;
  io_config.lcd_param_bits = EPD_PANEL_SPI_PARAM_BITS;

  ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                 &io_config, &io_handle);
  if (ret != ESP_OK)
    return ret;

  // --- Create esp_lcd panel
  ESP_LOGI(TAG, "Creating SSD1680 panel...");
  esp_lcd_ssd1680_config_t epaper_ssd1680_config = {
      .busy_gpio_num = EXAMPLE_PIN_NUM_EPD_BUSY,
      .non_copy_mode = false,
  };
  esp_lcd_panel_dev_config_t panel_config = {};
  panel_config.reset_gpio_num = EXAMPLE_PIN_NUM_EPD_RST;
  panel_config.flags.reset_active_high = 0;
  panel_config.vendor_config = &epaper_ssd1680_config;

  gpio_install_isr_service(0);
  ret = esp_lcd_new_panel_ssd1680(io_handle, &panel_config, &panel_handle);
  if (ret != ESP_OK)
    return ret;

  // --- Reset/Init display
  ESP_LOGI(TAG, "Resetting e-Paper display...");
  esp_lcd_panel_reset(panel_handle);
  vTaskDelay(pdMS_TO_TICKS(100));

  ESP_LOGI(TAG, "Initializing e-Paper display...");
  esp_lcd_panel_init(panel_handle);
  vTaskDelay(pdMS_TO_TICKS(100));

  ESP_LOGI(TAG, "Turning e-Paper display on...");
  esp_lcd_panel_disp_on_off(panel_handle, true);
  vTaskDelay(pdMS_TO_TICKS(100));

  // --- Create semaphore
  epaper_panel_semaphore = xSemaphoreCreateBinary();
  xSemaphoreGive(epaper_panel_semaphore);

  // --- Register callback
  epaper_panel_callbacks_t cbs = {
      .on_epaper_refresh_done = event_callback,
  };
  epaper_panel_register_event_callbacks(panel_handle, &cbs,
                                        &epaper_panel_semaphore);

  // --- Initialize GFX
  display = new Adafruit_SSD1680(EPD_WIDTH, EPD_HEIGHT, panel_handle,
                                 epaper_panel_semaphore);

  return ESP_OK;
}

void DisplayManager::setFullRefresh() {
  if (panel_handle) {
    epaper_panel_set_refresh_mode(panel_handle, true);
  }
}

void DisplayManager::powerOff() {
  if (panel_handle) {
    esp_lcd_panel_disp_on_off(panel_handle, false);
  }
}
