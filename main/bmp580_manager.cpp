#include "bmp580_manager.hpp"
#include "common_data.hpp"
#include "esp_log.h"
#include <string.h>
#include <esp_timer.h>
#include <rom/ets_sys.h>
#include <math.h>

static const char *TAG = "Bmp580Manager";

// External reference to common data store if available global, 
// otherwise we might need to include a header that declares it extern.
// Usually in these projects there is a global or a singleton for common data.
// Let's assume common_data.hpp declares a global variable or we need to access it differently.
// Checking common_data.cpp might clarify, but I'll assume we write to it later.
// Wait, I didn't see where `common_data` variable is defined.
// Let's check common_data.cpp.

Bmp580Manager::Bmp580Manager() {
    memset(&dev, 0, sizeof(i2c_dev_t));
    memset(&bmp5_dev, 0, sizeof(struct bmp5_dev));
}

int8_t Bmp580Manager::i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr) {
    i2c_dev_t *dev = (i2c_dev_t *)intf_ptr;
    if (!dev) return BMP5_E_NULL_PTR;
    
    esp_err_t err = i2c_dev_read_reg(dev, reg_addr, reg_data, len);
    return (err == ESP_OK) ? BMP5_OK : BMP5_E_COM_FAIL;
}

int8_t Bmp580Manager::i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr) {
    i2c_dev_t *dev = (i2c_dev_t *)intf_ptr;
    if (!dev) return BMP5_E_NULL_PTR;
    
    esp_err_t err = i2c_dev_write_reg(dev, reg_addr, reg_data, len);
    return (err == ESP_OK) ? BMP5_OK : BMP5_E_COM_FAIL;
}

void Bmp580Manager::delay_us(uint32_t period, void *intf_ptr) {
    uint32_t msec = period / 1000;
    if (msec > 0 && msec >= portTICK_PERIOD_MS) {
         vTaskDelay(pdMS_TO_TICKS(msec));
    } else {
         ets_delay_us(period);
    }
}

esp_err_t Bmp580Manager::init(int sda_pin, int scl_pin) {
    ESP_ERROR_CHECK(i2cdev_init()); 
    
    memset(&dev, 0, sizeof(i2c_dev_t));
    dev.port = I2C_NUM_0;
    dev.addr = BMP5_I2C_ADDR_PRIM;
    dev.cfg.sda_io_num = (gpio_num_t)sda_pin;
    dev.cfg.scl_io_num = (gpio_num_t)scl_pin;
    dev.cfg.master.clk_speed = 100000; // Match SCD4x speed (100kHz) to avoid switching
    dev.cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
    dev.cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;

    ESP_ERROR_CHECK(i2c_dev_create_mutex(&dev));

    bmp5_dev.read = i2c_read;
    bmp5_dev.write = i2c_write;
    bmp5_dev.delay_us = delay_us;
    bmp5_dev.intf = BMP5_I2C_INTF;
    bmp5_dev.intf_ptr = &dev;
    
    // Give more time for power-up
    vTaskDelay(pdMS_TO_TICKS(100));

    // Try a soft reset blindly before init to clear any bad state
    // But we need to use a temporary dummy dev with just read/write if we were to do that. 
    // Instead, let's just proceed to init. 
    // BMP580 takes time to load NVM.
    
    ESP_LOGI(TAG, "Initializing BMP580...");
    int8_t rslt = bmp5_init(&bmp5_dev);
    
    // Retry init if power-up check failed
    if (rslt == BMP5_E_POWER_UP) {
        ESP_LOGW(TAG, "BMP580 Power Up check failed, retrying...");
        vTaskDelay(pdMS_TO_TICKS(100));
        rslt = bmp5_init(&bmp5_dev);
    }
    
    // If still fails with POWER_UP, we might have already cleared the POR interrupt flag
    // so let's try to proceed if we can read Chip ID.
    if (rslt == BMP5_E_POWER_UP) {
         uint8_t chip_id = 0;
         bmp5_get_regs(BMP5_REG_CHIP_ID, &chip_id, 1, &bmp5_dev);
         if (chip_id == BMP5_CHIP_ID_PRIM || chip_id == BMP5_CHIP_ID_SEC) {
             ESP_LOGW(TAG, "Ignoring Power Up Error because Chip ID is valid: 0x%x", chip_id);
             bmp5_dev.chip_id = chip_id;
             rslt = BMP5_OK; // Force OK
         }
    }

    if (rslt != BMP5_OK) {
        ESP_LOGW(TAG, "BMP580 init failed at 0x%02x, trying 0x%02x", dev.addr, BMP5_I2C_ADDR_SEC);
        dev.addr = BMP5_I2C_ADDR_SEC; 
        rslt = bmp5_init(&bmp5_dev);
    }
    
    if (rslt != BMP5_OK) {
        ESP_LOGE(TAG, "BMP580 init failed with error %d", rslt);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "BMP580 initialized. Chip ID: 0x%x", bmp5_dev.chip_id);
    
    struct bmp5_osr_odr_press_config osr_odr_press_cfg = {0};
    osr_odr_press_cfg.osr_t = BMP5_OVERSAMPLING_8X;
    osr_odr_press_cfg.osr_p = BMP5_OVERSAMPLING_128X;
    osr_odr_press_cfg.press_en = BMP5_ENABLE;
    osr_odr_press_cfg.odr = BMP5_ODR_01_HZ;
    
    int8_t err = bmp5_set_osr_odr_press_config(&osr_odr_press_cfg, &bmp5_dev);
    if(err != BMP5_OK) {
        ESP_LOGE(TAG, "Failed to set config");
        return ESP_FAIL;
    }

    err = bmp5_set_power_mode(BMP5_POWERMODE_NORMAL, &bmp5_dev);
    if(err != BMP5_OK) {
        ESP_LOGE(TAG, "Failed to set power mode");
        return ESP_FAIL;
    }
     
    return ESP_OK;
}

void Bmp580Manager::task(void *pvParameters) {
    Bmp580Manager *manager = (Bmp580Manager *)pvParameters;
    
    struct bmp5_sensor_data data;
    struct bmp5_osr_odr_press_config osr_odr_press_cfg = {0};
    // Ensure we tell the driver that pressure is enabled so it converts it
    osr_odr_press_cfg.press_en = BMP5_ENABLE; 

    // Static flag for taring at boot
    static bool first_reading = true;
    
    while (1) {
        int8_t rslt = bmp5_get_sensor_data(&data, &osr_odr_press_cfg, &manager->bmp5_dev);
        
        if (rslt == BMP5_OK) {
            float pressure = data.pressure;
            // BMP580 returns pressure in Pa.
            // Standard formula: h = 44330 * (1 - (p / p0)^(1/5.255))
            float altitude = 44330.0f * (1.0f - pow(pressure / 101325.0f, 0.1903f));
            
            if (first_reading) {
                // Tare at boot: set offset so displayed altitude is 0
                float offset = -altitude;
                global_data.setAltitudeOffset(offset);
                ESP_LOGI(TAG, "Boot tare: Raw Alt: %.2f, Offset: %.2f", altitude, offset);
                first_reading = false;
            }

            ESP_LOGI(TAG, "Pressure: %f Pa, Temp: %f C, Alt: %f m", pressure, data.temperature, altitude);
            
            global_data.setBmpData(pressure, data.temperature, altitude);
            global_data.notifyUI();
        } else {
            ESP_LOGE(TAG, "Failed to read data");
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void Bmp580Manager::start() {
    xTaskCreate(task, "bmp580_task", 4096, this, 5, NULL);
}
