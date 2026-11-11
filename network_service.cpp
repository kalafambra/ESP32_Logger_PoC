#include "network_service.h"
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "config.h"
#include "firmware_features.h"
#include "http_api.h"
#include "ota_service.h"
#include "pickup_ble.h"

namespace {
uint32_t lastReconnect = 0;
bool wifiRestartPending = false, timeSyncStarted = false;
bool wifiDriverReady = false, radioServicesReady = false;
constexpr uint32_t WIFI_ATTEMPT_MS = 30000;
constexpr uint32_t WIFI_RESTART_PAUSE_MS = 250;
/*
 * Explain connection failures and DHCP progress without exposing credentials.
 * Keep retry decisions in loop() because Wi-Fi callbacks run on another task.
 */
void wifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  // The callback runs on the Wi-Fi event task. Log only; loop() owns retries.
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    const uint8_t reason = info.wifi_sta_disconnected.reason;
    Serial.printf("[wifi] Disconnected: reason=%u (%s)\n", reason,
                  WiFi.STA.disconnectReasonName(static_cast<wifi_err_reason_t>(reason)));
  } else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
    Serial.println("[wifi] Associated with access point; waiting for DHCP address");
  } else if (event == ARDUINO_EVENT_WIFI_STA_LOST_IP) {
    Serial.println("[wifi] IP address lost; waiting for DHCP/retry");
  }
}

/* Allocate optional services only after Wi-Fi gets its IP and HTTP/OTA have reserved their memory.
 */
void startOptionalServices() {
#if PICKUP_BLE_ENABLED
  Serial.printf("[memory] Before BLE: free=%u largest=%u\n", ESP.getFreeHeap(),
                ESP.getMaxAllocHeap());
  if (ESP.getFreeHeap() >= 81920 && ESP.getMaxAllocHeap() >= 32768)
    setupPickupBle();
  else
    Serial.println("[ble] Skipped: need 80 KiB free / 32 KiB contiguous; see [memory] diagnostic");
#else
  Serial.println("[ble] Disabled at build time; event logging remains available over HTTP");
#endif
  Serial.println("[delivery] Browser owns plot history; firmware retains pending ACK batches only");
  Serial.printf("[memory] Radios/routes/delivery: free=%u largest=%u minimum=%u\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap());
  radioServicesReady = true;
}
} // namespace

void setupNetwork() {
  WiFi.onEvent(wifiEvent);
  wifiDriverReady =
      WiFi.mode(WIFI_STA); // Reserve Wi-Fi task/queue memory before optional BLE/workers.
  if (wifiDriverReady)
    WiFi.setSleep(false); // Favor dashboard latency on this continuously sampling device.
  // One application-owned retry policy avoids racing automatic reconnect.
  WiFi.setAutoReconnect(false);
  if (wifiDriverReady) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD); // Start asynchronously; do not wait for an IP here.
    Serial.println("[wifi] Connecting to configured 2.4 GHz network");
  } else
    Serial.println("[wifi] Driver initialization failed; retry scheduled, HTTP not started");
  lastReconnect = millis();
}

void serviceNetwork(uint32_t now) {
  static bool wasConnected = false;
  const bool connected = WiFi.status() == WL_CONNECTED;
  // Never create a listening socket after a failed Wi-Fi driver initialization.
  if (connected)
    startHttp();
  if (connected && !timeSyncStarted) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    timeSyncStarted = true;
  }
  serviceOtaConnection(connected);
  // A disconnected network still permits BLE after the driver's initial connection attempt
  // finishes.
  if (!radioServicesReady && (connected || now >= WIFI_ATTEMPT_MS))
    startOptionalServices();
  // Log transitions once, rather than repeating the address on every loop pass.
  if (connected != wasConnected) {
    if (connected)
      Serial.printf("[wifi] Dashboard: http://%s/\n", WiFi.localIP().toString().c_str());
    else
      Serial.println("[wifi] Connection lost; scheduled retry will follow");
    wasConnected = connected;
  }
  if (connected) {
    lastReconnect = now;
    wifiRestartPending = false;
  } else if (!wifiDriverReady && now - lastReconnect >= WIFI_ATTEMPT_MS) {
    lastReconnect = now;
    wifiDriverReady = WiFi.mode(WIFI_STA);
    if (wifiDriverReady) {
      WiFi.setSleep(false);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    } else
      Serial.println("[wifi] Driver still unavailable; retry in 30 s");
  } else if (wifiDriverReady && !wifiRestartPending && now - lastReconnect >= WIFI_ATTEMPT_MS) {
    // Cancel the old association/DHCP attempt before starting another.
    // Keep the radio on and preserve credentials. No delay() in this loop.
    Serial.printf("[wifi] No IP after 30 s (status=%d); cancelling old attempt\n", WiFi.status());
    WiFi.disconnect(false, false);
    lastReconnect = now;
    wifiRestartPending = true;
    // Allow the disconnect to settle before requesting a fresh association.
  } else if (wifiRestartPending && now - lastReconnect >= WIFI_RESTART_PAUSE_MS) {
    wifiRestartPending = false;
    lastReconnect = now;
    Serial.println("[wifi] Starting fresh connection attempt");
    if (!WiFi.reconnect())
      Serial.println("[wifi] Could not start connection; will retry in 30 s");
  }
}
