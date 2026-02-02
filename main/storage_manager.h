#pragma once

#include "esp_err.h"
#include <string>

class StorageManager {
public:
  StorageManager();
  ~StorageManager();

  /**
   * @brief Mounts the LittleFS filesystem
   * @return ESP_OK on success, error code otherwise
   */
  esp_err_t mount();

  /**
   * @brief Unmounts the LittleFS filesystem
   */
  void unmount();

  /**
   * @brief Reads a file and prints its content to stdout
   * @param path Full path to the file (e.g., "/littlefs/file.txt")
   * @return ESP_OK on success, error code otherwise
   */
  esp_err_t readFile(const char *path);

  /**
   * @brief Reads a text file into a string
   * @param path Full path to the file
   * @return File content as string, or empty string on failure
   */
  std::string readTextFile(const char *path);
};
