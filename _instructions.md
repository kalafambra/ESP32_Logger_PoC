# Quick start — ESP32-WROOM-32 (4 MB)

## Wiring

Disconnect USB before wiring. Use **3.3 V-compatible modules and logic**, with a common GND.

| Module | Connections to ESP32 |
| --- | --- |
| BME280 | VCC → 3V3, GND → GND, SDA → GPIO21, SCL → GPIO22 |
| MPU6050 | VCC → 3V3, GND → GND, SDA → GPIO21, SCL → GPIO22, INT → GPIO17, AD0 → GND; leave XDA/XCL unused |
| INMP441 | VDD → 3V3, GND → GND, SCK/BCLK → GPIO18, WS/LRCLK → GPIO19, SD → GPIO23, L/R → GND |
| Status LED | Onboard GPIO2 LED, active HIGH; set `STATUS_LED_PIN = -1` if absent |

BME280 and MPU6050 share SDA/SCL. BME addresses 0x76/0x77 are detected automatically; MPU uses 0x68.

## First build and USB upload — Arduino IDE

1. Install Arduino IDE 2.x. In **Preferences → Additional Boards Manager URLs**, add:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
2. In **Boards Manager**, install **esp32 by Espressif Systems 3.3.12** (tested version).
3. In **Library Manager**, install **Adafruit BME280 Library** and accept its **Adafruit BusIO / Unified Sensor** dependencies. No MPU library is needed; BLE is disabled by default.
4. Download the complete project into a folder named **ESP32_1**; open **ESP32_1.ino**. Keep all source/header files in that folder.
5. Copy **config.example.h → config.h**. Set your **2.4 GHz Wi-Fi** credentials, `I2C_SENSORS_ENABLED = true`, and `MICROPHONE_ENABLED = true` if the microphone is connected (otherwise `false`). Keep `PICKUP_BLE_ENABLED = 0` in **firmware_features.h**.
6. Connect with a USB data cable. USB<->RS232 drivers may need to be installed. In Arduino IDE, under **Tools**, select **ESP32 Dev Module**, **Flash Size: 4MB**, **Partition Scheme: Default 4MB with spiffs**, and your USB serial **Port**. Check the board's USB-UART driver if no port appears.
7. Click **Verify**, then **Upload**. If it stays at “Connecting…”, hold **BOOT** until writing starts, then release.
8. Open **Serial Monitor at 115200 baud**, press **EN/Reset**, and open the printed IP address in a browser on the same LAN. Main dashboard: `/`; tuning: `/pickup`.

First installation uses USB. See [README.md](README.md) for calibration/OTA and [BUILD_VARIANTS.md](BUILD_VARIANTS.md) before enabling BLE or changing partitions.
