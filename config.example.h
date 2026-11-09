#pragma once
#include <Arduino.h>

// Use a 2.4 GHz Wi-Fi network. Credentials stay on the device, not in the API.
constexpr char WIFI_SSID[] = "YOUR_WIFI_SSID";
constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;
// Carrier boards vary: use -1 to disable a missing user LED.
constexpr int STATUS_LED_PIN = 2;
constexpr uint8_t LED_ON = HIGH;
constexpr uint32_t SAMPLE_INTERVAL_MS = 200;

// Set your sensor altitude above sea level in meters before compiling (-500 to 9000).
constexpr float DEFAULT_ALTITUDE_M = 0.0f;

// Arduino IDE network-port name. Use letters, digits and hyphens.
constexpr char OTA_HOSTNAME[] = "esp32-bme280";
// Set a unique password (at least 12 characters). Empty disables OTA.
// Do not share this value in chat. This is separate from your Wi-Fi password.
constexpr char OTA_PASSWORD[] = "";

// MPU6050 INT: active-high data-ready interrupt, separate from the shared I2C bus.
constexpr uint8_t MPU_INTERRUPT_PIN = 17;

// INMP441 I2S microphone; L/R is tied to GND (left channel).
constexpr uint8_t MICROPHONE_BCLK_PIN = 18;
constexpr uint8_t MICROPHONE_WS_PIN = 19;
constexpr uint8_t MICROPHONE_DATA_PIN = 23;

// Set true to restore BME280/MPU6050 acquisition in a combined-sensor build.
constexpr bool I2C_SENSORS_ENABLED = false;
