#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "environment.h"
#include "motion.h"
#include "microphone.h"
#include "microphone_spectrum.h"
#include "pickup_service.h"
#include "pickup_events.h"
#include "pickup_ble.h"
#include "network_service.h"
#include "ota_service.h"
#include "http_api.h"

// Startup order matters on ESP32-WROOM: reuse live buffers for self-tests,
// initialize the shared I2C bus once, then reserve Wi-Fi memory before I2S/BLE.
void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32 BME280: 5 Hz acquisition, 1 s mean/min/max, SDA=21 SCL=22");
  setupEnvironmentHistory();
  setupPickup();
  setupPickupEvents();
  setupOta();

  if (I2C_SENSORS_ENABLED) {
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(100000); // Both sensors share standard-mode I2C.
    Wire.setTimeOut(20);   // Bound failures; I/O itself is synchronous.
    setupEnvironment();
    setupMotion();
    Wire.setClock(100000); // Reassert after sensor initialization helpers.
  } else {
    disableEnvironment();
    disableMotion();
    invalidatePickup("I2C sensors disabled in microphone build");
    Serial.println(
        "[sensors] Microphone build: I2C bus, BME280, MPU6050 and MPU interrupt disabled");
  }

  resetEnvironmentWindow(millis());
  setupNetwork();
  setupMicrophone();
  setupHttp();
  resetEnvironmentWindow(millis());
}

// Preserve this service order: DMA is drained around synchronous I2C/HTTP,
// and a BME report closes before a conversion enters the next sample window.
// An active OTA transfer temporarily owns loop(); the LED timer is independent.
void loop() {
  serviceOta();
  serviceMicrophoneAudio();
  publishWindow(millis());
  acquireSensor();
  acquireMotion();
  serviceHttp();
  serviceMicrophoneAudio();
  logMicrophoneDiagnostics();
  acquireMotion();
  const uint32_t now = millis();
  servicePickup(now);
  servicePickupBle();
  serviceNetwork(now);
}
