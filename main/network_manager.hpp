#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include <stdbool.h>

/**
 * @brief Manages WiFi connection and SNTP time synchronization
 */
class NetworkManager {
public:
  NetworkManager();
  ~NetworkManager();

  /**
   * @brief Initialize NVS, WiFi and connect to the provided SSID
   * @param ssid WiFi SSID
   * @param password WiFi Password
   * @param timeout_ms Connection timeout in milliseconds
   * @return esp_err_t ESP_OK on success
   */
  esp_err_t init(const char *ssid, const char *password,
                 uint32_t timeout_ms = 15000);

  /**
   * @brief Synchronize time using SNTP
   * @param timeout_ms Timeout to wait for sync in milliseconds
   * @return esp_err_t ESP_OK on success
   */
  esp_err_t syncTime(uint32_t timeout_ms = 10000);

  /**
   * @brief Disconnect WiFi and stop the network stack to save power
   */
  void deinit();

  /**
   * @brief Check if WiFi is connected
   */
  bool isConnected() const { return connected; }

  /**
   * @brief Check if time is synchronized
   */
  bool isTimeSynced() const { return time_synced; }

private:
  bool connected = false;
  bool time_synced = false;

  static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data);
  static void ip_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
};
