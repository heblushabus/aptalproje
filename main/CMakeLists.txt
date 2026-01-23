idf_component_register(SRCS "touch_manager.cpp" "main.cpp" "display_manager.cpp" "network_manager.cpp" "common_data.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_spi esp_driver_gpio esp_lcd esp_lcd_ssd1680 Adafruit-GFX-Library-ESP-IDF esp_timer esp_wifi esp_event nvs_flash esp_netif lwip esp_driver_touch_sens)
