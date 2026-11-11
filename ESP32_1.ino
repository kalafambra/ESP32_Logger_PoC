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
#include "acquisition_timing.h"
#include "i2c_bus_service.h"

// Startup order matters on ESP32-WROOM: reuse live buffers for self-tests,
// initialize the shared I2C bus once, then reserve Wi-Fi memory before I2S/BLE.
void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32 BME280: 5 Hz acquisition, 1 s mean/min/max, SDA=21 SCL=22");
  setupDashboardDelivery();
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
  if (!startAcquisition()) {
    Serial.println("[sensors] Acquisition task allocation failed; acquisition stopped");
    invalidateMotionProcessing("Acquisition task allocation failed");
    invalidateEnvironmentProcessing("Acquisition task allocation failed");
  }
}

// Acquisition and processing run independently. OTA pauses both owners explicitly.
void loop() {
  ScopedServiceTiming loopTiming(acquisitionTiming().loop);
  serviceOta();
  serviceMicrophoneAudio();
  serviceHttp();
  serviceMicrophoneAudio();
  logMicrophoneDiagnostics();
  const uint32_t now = millis();
  serviceProcessingFallback(now);
  servicePickupBle();
  serviceNetwork(now);
}
