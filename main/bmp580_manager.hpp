#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <i2cdev.h>
#include "bmp5.h"

class Bmp580Manager {
public:
    Bmp580Manager();
    esp_err_t init(int sda_pin, int scl_pin);
    void start();
    void forceMeasurement();
    
private:
    static void task(void *pvParameters);
    
    // I2C interface functions for BMP5 driver
    static int8_t i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr);
    static int8_t i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr);
    static void delay_us(uint32_t period, void *intf_ptr);

    i2c_dev_t dev;
    struct bmp5_dev bmp5_dev;
    TaskHandle_t taskHandle = nullptr;
};
