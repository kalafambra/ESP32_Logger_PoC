#pragma once

// Use a 2.4 GHz Wi-Fi network. Credentials stay on the device, not in the API.
constexpr char WIFI_SSID[] = "YOUR_WIFI_SSID";
constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;
// Carrier boards vary: use -1 to disable a missing user LED.
constexpr int STATUS_LED_PIN = 2;
constexpr uint8_t LED_ON = HIGH;
constexpr uint32_t SAMPLE_INTERVAL_MS = 1000;
constexpr uint32_t LED_PULSE_MS = 250;



// Set your sensor altitude above sea level in meters before compiling (-500 to 9000).
constexpr float DEFAULT_ALTITUDE_M = 0.0f;


// Arduino IDE network-port name. Use letters, digits and hyphens.
constexpr char OTA_HOSTNAME[] = "esp32-bme280";
// Set a unique password (at least 12 characters). Empty disables OTA.
// Do not share this value in chat. This is separate from your Wi-Fi password.
constexpr char OTA_PASSWORD[] = "";
