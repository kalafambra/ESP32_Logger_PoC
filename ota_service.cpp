#include "ota_service.h"
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include "config.h"
#include "environment.h"
#include "motion.h"
#include "i2c_bus_service.h"
#include "microphone.h"
#include "microphone_spectrum.h"

namespace {
bool otaConfigured = false, otaStarted = false;
}

void setupOta() {
  // Leave OTA disabled until a sufficiently long password has been configured.
  if (strlen(OTA_PASSWORD) >= 12) {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    /* Pause sensor acquisition; the pickup LED's timer remains independent. */
    ArduinoOTA.onStart([]() {
      pauseAcquisition(); // Owner acknowledges that all sensor I2C is stopped.
      pauseMicrophone();
      resetSpectrumWindow();
      Serial.println("[ota] Update starting; measurement and dashboard handling paused");
    });
    /* Log coarse progress so serial output does not dominate update processing. */
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      static int lastPercent = -1;
      const int percent = total ? int((uint64_t(progress) * 100) / total) : 0;
      if (percent / 10 != lastPercent / 10 || percent < lastPercent) {
        Serial.printf("[ota] Progress %d%%\n", percent);
      }
      lastPercent = percent;
    });
    /* Explain the expected reboot after the updater accepts the application image. */
    ArduinoOTA.onEnd(
        []() { Serial.println("[ota] Transfer complete; rebooting into new firmware"); });
    /* Diagnose upload failures and discard samples spanning an interrupted update. */
    ArduinoOTA.onError([](ota_error_t error) {
      resumeAcquisition(); // Owner resets FIFO framing and the BME schedule.
      resumeMicrophone();
      resetSpectrumWindow();
      const char *reason = error == OTA_AUTH_ERROR      ? "authentication failed"
                           : error == OTA_BEGIN_ERROR   ? "cannot begin update (partition/space)"
                           : error == OTA_CONNECT_ERROR ? "connection failed"
                           : error == OTA_RECEIVE_ERROR ? "receive failed"
                           : error == OTA_END_ERROR     ? "final verification failed"
                                                        : "unknown error";
      Serial.printf("[ota] Error %u: %s\n", unsigned(error), reason);
    });
    otaConfigured = true;
  } else {
    Serial.println("[ota] Disabled: set OTA_PASSWORD to at least 12 characters in config.h");
  }
}

void serviceOtaConnection(bool connected) {
  // Bind OTA only after an IP is available, and reinitialize after a Wi-Fi loss.
  if (connected && otaConfigured && !otaStarted) {
    ArduinoOTA.begin();
    otaStarted = true;
    Serial.printf("[ota] Ready: %s at %s (port 3232, password required)\n", OTA_HOSTNAME,
                  WiFi.localIP().toString().c_str());
  } else if (!connected && otaStarted) {
    ArduinoOTA.end();
    otaStarted = false;
  }
}

void serviceOta() {
  if (otaStarted)
    ArduinoOTA.handle();
}
