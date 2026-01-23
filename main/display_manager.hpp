#pragma once

#include "Adafruit_GFX.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern "C" {
#include "esp_lcd_panel_ssd1680.h"
}

// Display dimensions for SSD1680 2.9" display
#define EPD_WIDTH 128
#define EPD_HEIGHT 296

// SPI Bus
#define EPD_PANEL_SPI_CLK 4000000
#define EPD_PANEL_SPI_CMD_BITS 8
#define EPD_PANEL_SPI_PARAM_BITS 8
#define EPD_PANEL_SPI_MODE 0

// e-Paper GPIO
#define EXAMPLE_PIN_NUM_EPD_DC 38
#define EXAMPLE_PIN_NUM_EPD_RST 2
#define EXAMPLE_PIN_NUM_EPD_CS 48
#define EXAMPLE_PIN_NUM_EPD_BUSY 1

// e-Paper SPI
#define EXAMPLE_PIN_NUM_MOSI 39
#define EXAMPLE_PIN_NUM_SCLK 41

// Colors
#define GFX_BLACK 0
#define GFX_WHITE 1

/**
 * @brief Adafruit GFX implementation for SSD1680 e-Paper display
 */
class Adafruit_SSD1680 : public Adafruit_GFX {
public:
  Adafruit_SSD1680(int16_t w, int16_t h, esp_lcd_panel_handle_t handle,
                   SemaphoreHandle_t semaphore);
  ~Adafruit_SSD1680();

  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void clearBuffer();
  void display(bool partial = false);

  /**
   * @brief Print text aligned to the right of the specified X coordinate
   */
  void printRightAligned(int16_t x, int16_t y, const char *str);

private:
  esp_lcd_panel_handle_t panel_handle;
  SemaphoreHandle_t epaper_panel_semaphore;
  uint8_t *buffer;
  size_t buffer_size;
};

/**
 * @brief Manages the initialization and lifecycle of the e-Paper display
 */
class DisplayManager {
public:
  DisplayManager();
  ~DisplayManager();

  /**
   * @brief Initialize the SPI bus, LCD panel and GFX interface
   * @return esp_err_t ESP_OK on success
   */
  esp_err_t init();

  /**
   * @brief Get the GFX display object
   * @return Adafruit_SSD1680* pointer to display object
   */
  Adafruit_SSD1680 *getDisplay() { return display; }

  /**
   * @brief Set refresh mode to full
   */
  void setFullRefresh();

  /**
   * @brief Turn off the display (low power)
   */
  void powerOff();

private:
  esp_lcd_panel_io_handle_t io_handle = NULL;
  esp_lcd_panel_handle_t panel_handle = NULL;
  Adafruit_SSD1680 *display = nullptr;
  SemaphoreHandle_t epaper_panel_semaphore = NULL;

  static bool event_callback(const esp_lcd_panel_handle_t handle,
                             const void *edata, void *user_data);
};
