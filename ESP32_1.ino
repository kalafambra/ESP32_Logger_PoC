#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <Adafruit_BME280.h>
#include <math.h>
#include "config.h"
#include "dashboard.h"
#include "fox_image.h"
#include "aggregation.h"
#include "motion.h"

Adafruit_BME280 bme;
WebServer server(80);
uint8_t sensorAddress = 0;
bool sensorReady = false, converting = false, ledActive = false;
uint32_t lastStart = 0, conversionStart = 0, windowStart = 0;
uint32_t ledStart = 0, lastReconnect = 0;
bool wifiRestartPending = false;
bool otaConfigured = false, otaStarted = false;
constexpr uint32_t WIFI_ATTEMPT_MS = 30000;
constexpr uint32_t WIFI_RESTART_PAUSE_MS = 250;
constexpr uint32_t PUBLISH_INTERVAL_MS = 1000;
constexpr unsigned SAMPLES_PER_WINDOW = 5;
static_assert(SAMPLE_INTERVAL_MS == 200, "5 Hz requires SAMPLE_INTERVAL_MS = 200");
static_assert(DEFAULT_ALTITUDE_M >= -500 && DEFAULT_ALTITUDE_M <= 9000,
              "Set DEFAULT_ALTITUDE_M between -500 and 9000 meters");
SampleWindow window;
PublishedSample latest;
constexpr unsigned HISTORY_SIZE = 602;
PublishedSample publishedHistory[HISTORY_SIZE];
unsigned historyHead = 0, historyCount = 0;
bool windowFailed = false;
const char *sensorError = "Sensor not initialized";

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

/*
 * Read consecutive BME280 registers and expose bus failures to the caller.
 * Explicit status checks avoid treating a missing device as a valid reading.
 */
bool readRegisters(uint8_t reg, uint8_t *data, uint8_t count) {
  Wire.beginTransmission(sensorAddress);
  Wire.write(reg); // Select the first register before switching to a read.
  // A repeated start keeps register selection and reading in one transaction.
  if (Wire.endTransmission(false) != 0) return false;
  // Reject partial replies rather than leaving uninitialized bytes in data.
  if (Wire.requestFrom(sensorAddress, count) != count) return false;
  for (uint8_t i = 0; i < count; ++i) data[i] = Wire.read();
  return true;
}

/*
 * Write a sensor control byte directly so forced conversions can be triggered
 * without using the library helper that waits for conversion completion.
 */
bool writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(sensorAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0; // Require the sensor to acknowledge the write.
}

/*
 * Invalidate the entire averaging window after a failed measurement.
 * Publishing a partial average would conceal missing or unreliable samples.
 */
void failSample(const char *message) {
  sensorError = message;
  windowFailed = true;
  converting = false;
  // Publish the error with the one-second report, not at raw acquisition rate.
}

/*
 * Advance a forced-conversion state machine at five readings per second.
 * Returning between trigger and read keeps the dashboard and OTA responsive.
 * Only validated measurements enter the average or generate an LED pulse.
 */
void acquireSensor() {
  if (!sensorReady) return;  // Initialization errors require a reset after repair.
  const uint32_t now = millis();
  if (!converting && now - lastStart >= SAMPLE_INTERVAL_MS) {
    lastStart = now;
    // ctrl_meas: temperature x1, pressure x1, forced mode. Humidity x1
    // was configured by setSampling(). Do not call takeForcedMeasurement():
    // that library method waits for the sensor conversion to complete.
    if (!writeRegister(0xF4, 0x25)) {
      failSample("I2C trigger failed");
      return;
    }
    conversionStart = now;
    converting = true;
  }
  // Leave time for x1 temperature, pressure and humidity conversion, without sleeping.
  if (!converting || now - conversionStart < 15) return;
  // Bound how long a conversion may occupy the acquisition state machine.
  if (now - conversionStart > 80) {
    failSample("Conversion timeout");
    return;
  }
  uint8_t status;
  if (!readRegisters(0xF3, &status, 1)) {
    failSample("I2C status read failed");
    return;
  }
  if (status & 0x09) return;  // measuring or calibration update
  uint8_t raw[8];
  // Check the complete raw frame before asking the library for compensated values.
  if (!readRegisters(0xF7, raw, sizeof(raw))) {
    failSample("I2C data read failed");
    return;
  }
  // These sentinel values mean a channel was skipped, not a real measurement.
  if ((raw[0] == 0x80 && raw[1] == 0 && raw[2] == 0) ||
      (raw[3] == 0x80 && raw[4] == 0 && raw[5] == 0) ||
      (raw[6] == 0x80 && raw[7] == 0)) {
    failSample("Sensor returned skipped-channel data");
    return;
  }
  // These compensated reads do not wait for conversion in sleep mode.
  const float temperatureC = bme.readTemperature();
  const float humidityPct = bme.readHumidity();
  const float pressureHpa = bme.readPressure() / 100.0F; // Convert Pa to dashboard units.
  // Check the bus again: the library's float API does not expose I2C errors.
  uint8_t id;
  if (!readRegisters(0xD0, &id, 1) || id != 0x60 ||
      !isfinite(temperatureC) || !isfinite(humidityPct) || !isfinite(pressureHpa) ||
      temperatureC < -40 || temperatureC > 85 || humidityPct < 0 || humidityPct > 100 ||
      pressureHpa < 300 || pressureHpa > 1100) {
    failSample("Invalid reading or I2C communication error");
    return;
  }
  converting = false;
  // Accumulate full-precision readings; rounding happens only when reporting.
  window.add(temperatureC, humidityPct, pressureHpa);
  // The timer in loop() turns this pulse off, independently of publication.
  if (STATUS_LED_PIN >= 0) {
    digitalWrite(STATUS_LED_PIN, LED_ON);
    ledStart = millis();
    ledActive = true;
  }
}

/*
 * Close each one-second window and publish mean/min/max for exactly five
 * valid readings. Keep a bounded history so missed browser polls can recover.
 * Include network status even in error reports to support remote diagnosis.
 */
void publishWindow(uint32_t now) {
  if (now - windowStart < PUBLISH_INTERVAL_MS) return;
  latest = PublishedSample{};
  latest.sequence = historyCount ? publishedHistory[(historyHead + HISTORY_SIZE - 1) % HISTORY_SIZE].sequence + 1 : 1;
  latest.sampleMs = now;
  latest.count = window.count;
  // Reject missing readings, any recorded error, and excessively delayed windows.
  latest.ok = sensorReady && !windowFailed && window.count == SAMPLES_PER_WINDOW && now - windowStart < PUBLISH_INTERVAL_MS + SAMPLE_INTERVAL_MS;
  latest.error = latest.ok ? "" : !sensorReady ? sensorError : windowFailed ? sensorError : "Incomplete one-second window (expected 5 readings)";
  // Calculate channel statistics separately; their extrema need not coincide.
  latest.temperature.finish(window.temperature, window.count);
  latest.humidity.finish(window.humidity, window.count);
  latest.pressure.finish(window.pressure, window.count);
  // Overwrite the oldest ring-buffer entry instead of allocating growing history.
  publishedHistory[historyHead] = latest;
  historyHead = (historyHead + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) ++historyCount;
  // A disconnected report must not advertise an obsolete address as usable.
  const wl_status_t wifiStatus = WiFi.status();
  const bool wifiConnected = wifiStatus == WL_CONNECTED;
  const String ip = wifiConnected ? WiFi.localIP().toString() : String("0.0.0.0");
  if (latest.ok) {
    Serial.printf("[report %lu @ %lu ms, n=%u] T mean/min/max %.3f/%.3f/%.3f C | H %.3f/%.3f/%.3f %% | P %.3f/%.3f/%.3f hPa | WiFi=%s (status=%d) | IP=%s\n",
                  latest.sequence, latest.sampleMs, latest.count,
                  latest.temperature.mean, latest.temperature.minimum, latest.temperature.maximum,
                  latest.humidity.mean, latest.humidity.minimum, latest.humidity.maximum,
                  latest.pressure.mean, latest.pressure.minimum, latest.pressure.maximum,
                  wifiConnected ? "connected" : "disconnected", int(wifiStatus), ip.c_str());

    } else Serial.printf("[report %lu, n=%u] %s | WiFi=%s (status=%d) | IP=%s\n",
                       latest.sequence, latest.count, latest.error,
                       wifiConnected ? "connected" : "disconnected", int(wifiStatus), ip.c_str());
  // Start a non-overlapping window; measurements must not be counted twice.
  window = SampleWindow{};
  windowFailed = false;
  converting = false; // Do not carry a late conversion into the next window.
  windowStart = now;
  lastStart = now - SAMPLE_INTERVAL_MS; // First conversion starts immediately.
}

/*
 * Keep history values in the documented [mean, minimum, maximum] order.
 * JSON nulls make invalid windows explicit and avoid non-standard NaN output.
 */
void appendChannel(String &json, const PublishedChannel &channel, bool ok) {
  if (!ok) { json += "[null,null,null]"; return; }
  json += "[" + String(channel.mean, 3) + "," + String(channel.minimum, 3) + "," + String(channel.maximum, 3) + "]";
}

/*
 * Serve the latest report and incremental history for browser recovery.
 * Pressure stays local/raw here so each browser can apply its chosen altitude
 * consistently to both current readings and historical extrema.
 */
void sendReadings() {
  const uint32_t now = millis();
  const uint32_t age = now - latest.sampleMs;
  // Allow a full reporting interval plus scheduling margin before marking stale.
  const bool fresh = latest.sequence && latest.ok && age <= PUBLISH_INTERVAL_MS + 1500;
  const char *error = latest.ok && !fresh ? "One-second report stale" : latest.error;
  // Browser requests records newer than its last sequence. This repairs missed
  // HTTP polls instead of drawing breaks for measurements still held in RAM.
  uint32_t since = server.hasArg("since") ? strtoul(server.arg("since").c_str(), nullptr, 10) : 0;
  if (since > latest.sequence) since = 0; // Device restarted.
  String json;
  json.reserve(16000); // Reduce heap reallocations when backfilling ten minutes.
  json = "{\"sequence\":" + String(latest.sequence);
  json += ",\"uptime_ms\":" + String(now);
  json += ",\"default_altitude_m\":" + String(DEFAULT_ALTITUDE_M, 2);
  json += ",\"sample_ms\":" + String(latest.sampleMs);
  json += ",\"sample_age_ms\":" + (latest.sequence ? String(age) : String("null"));
  json += ",\"sample_interval_ms\":200,\"publish_interval_ms\":1000,\"sample_count\":" + String(latest.count);
  json += ",\"sensor_ok\":"; json += fresh ? "true" : "false";
  json += ",\"error\":\""; json += error; json += "\"";
  json += ",\"wifi_connected\":";
  json += WiFi.status() == WL_CONNECTED ? "true" : "false";
  json += ",\"rssi_dbm\":" + String(WiFi.RSSI());
  json += ",\"temperature_c\":" + (fresh ? String(latest.temperature.mean, 3) : String("null"));
  json += ",\"humidity_pct\":" + (fresh ? String(latest.humidity.mean, 3) : String("null"));
  json += ",\"pressure_hpa\":" + (fresh ? String(latest.pressure.mean, 3) : String("null"));
  appendMotion(json, now);
  json += ",\"history\":[";
  bool first = true;
  unsigned sent = 0; // Bound history backfill so a long response does not starve FIFO servicing.
  // Traverse oldest to newest so the browser can append reports in sequence.
  for (unsigned i = 0; i < historyCount && sent < 60; ++i) {
    const PublishedSample &point = publishedHistory[(historyHead + HISTORY_SIZE - historyCount + i) % HISTORY_SIZE];
    if (point.sequence <= since || now - point.sampleMs > 600000) continue; // Limit to requested, recent reports.
    if (!first) json += ',';
    first = false;
    ++sent;
    json += "{\"sequence\":" + String(point.sequence) + ",\"age_ms\":" + String(now - point.sampleMs);
    json += ",\"sample_count\":" + String(point.count) + ",\"ok\":";
    json += point.ok ? "true" : "false";
    json += ",\"temperature_c\":"; appendChannel(json, point.temperature, point.ok);
    json += ",\"humidity_pct\":"; appendChannel(json, point.humidity, point.ok);
    json += ",\"pressure_hpa\":"; appendChannel(json, point.pressure, point.ok);
    json += '}';
  }
  json += "]}";
  // Live API responses must not be replaced by a browser's cached reading.
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

/*
 * Initialize hardware, authenticated OTA callbacks and local HTTP routes once.
 * Sensor or Wi-Fi failures do not trap startup in a wait loop: the dashboard
 * can report sensor errors, and loop() owns connection progress and retries.
 */
void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32 BME280: 5 Hz acquisition, 1 s mean/min/max, SDA=21 SCL=22");
  // Leave OTA disabled until a sufficiently long password has been configured.
  if (strlen(OTA_PASSWORD) >= 12) {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    /* Turn off the measurement indicator while the OTA handler transfers firmware. */
    ArduinoOTA.onStart([]() {
      pauseMotion(); // Stop the FIFO while OTA temporarily owns loop execution.
      Serial.println("[ota] Update starting; measurement and dashboard handling paused");
      if (STATUS_LED_PIN >= 0) digitalWrite(STATUS_LED_PIN, !LED_ON);
      ledActive = false;
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
    ArduinoOTA.onEnd([]() {
      Serial.println("[ota] Transfer complete; rebooting into new firmware");
    });
    /* Diagnose upload failures and discard samples spanning an interrupted update. */
    ArduinoOTA.onError([](ota_error_t error) {
      resumeMotion(); // Reset FIFO framing before collecting post-update samples.
      const char *reason = error == OTA_AUTH_ERROR ? "authentication failed" :
                           error == OTA_BEGIN_ERROR ? "cannot begin update (partition/space)" :
                           error == OTA_CONNECT_ERROR ? "connection failed" :
                           error == OTA_RECEIVE_ERROR ? "receive failed" :
                           error == OTA_END_ERROR ? "final verification failed" : "unknown error";
      Serial.printf("[ota] Error %u: %s\n", unsigned(error), reason);
      // Discard the sampling window interrupted by the attempted transfer.
      window = SampleWindow{};
      failSample("OTA interrupted acquisition; window discarded");
      windowStart = millis();
      lastStart = windowStart - SAMPLE_INTERVAL_MS;
    });
    otaConfigured = true;
  } else {
    Serial.println("[ota] Disabled: set OTA_PASSWORD to at least 12 characters in config.h");
  }
  if (STATUS_LED_PIN >= 0) {
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, !LED_ON);
  }
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000); // Lower rates fit standard-mode I2C, with more wiring margin.
  Wire.setTimeOut(20);  // Bound I2C failures; I/O itself is synchronous.
  // Breakouts use either address depending on their SDO wiring.
  for (uint8_t address : {uint8_t(0x76), uint8_t(0x77)}) {
    if (bme.begin(address, &Wire)) {
      sensorAddress = address;
      sensorReady = true;
      break;
    }
  }
  if (sensorReady) {
    // Sleep between x1 conversions; acquireSensor() explicitly starts each sample.
    bme.setSampling(Adafruit_BME280::MODE_SLEEP,
                    Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::FILTER_OFF);
    sensorError = "Waiting for first sample";
    Serial.printf("[sensor] BME280 at 0x%02X\n", sensorAddress);
  } else {
    sensorError = "BME280 not found at 0x76 or 0x77; check wiring and reset";
    Serial.printf("[sensor] %s\n", sensorError);
  }
  setupMotion(); // Both sensors share GPIO21/GPIO22, with different I2C addresses.
  Wire.setClock(100000); // Reassert standard-mode speed after sensor initialization helpers.
  windowStart = millis();
  lastStart = windowStart - SAMPLE_INTERVAL_MS;
  WiFi.onEvent(wifiEvent);
  WiFi.mode(WIFI_STA);
  // One application-owned retry policy avoids racing automatic reconnect.
  WiFi.setAutoReconnect(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD); // Start asynchronously; do not wait for an IP here.
  Serial.println("[wifi] Connecting to configured 2.4 GHz network");
  lastReconnect = millis();
  /* Serve the embedded page from flash so the dashboard needs no filesystem/internet. */
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", DASHBOARD_HTML);
  });
  server.on("/api/readings", HTTP_GET, sendReadings);
  server.on("/api/motion", HTTP_GET, sendMotionReadings);
  /* Serve the original binary PNG with an explicit length, including any zero bytes. */
  server.on("/fox.png", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "public, max-age=3600");
    server.send_P(200, "image/png", reinterpret_cast<const char *>(FOX_PNG), sizeof(FOX_PNG));
  });
  /* Return an actual missing-resource status instead of misleading dashboard content. */
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
  windowStart = millis();
  lastStart = windowStart - SAMPLE_INTERVAL_MS;
}

/*
 * Cooperatively service OTA, acquisition, HTTP, the LED and Wi-Fi recovery.
 * Timers avoid deliberate sampling delays; unsigned subtraction handles the
 * millis() rollover. An active OTA transfer temporarily takes over execution.
 */
void loop() {
  if (otaStarted) ArduinoOTA.handle();
  // Close the old window before a new conversion can enter the next window.
  publishWindow(millis());
  acquireSensor();
  acquireMotion(); // Drain FIFO and publish independent one-second motion reports.
  server.handleClient();
  acquireMotion(); // Service data accumulated while a synchronous client was handled.
  const uint32_t now = millis();
  // End the measurement pulse without delaying acquisition or client handling.
  if (ledActive && now - ledStart >= LED_PULSE_MS) {
    digitalWrite(STATUS_LED_PIN, !LED_ON);
    ledActive = false;
  }
  static bool wasConnected = false;
  const bool connected = WiFi.status() == WL_CONNECTED;
  // Bind OTA only after an IP is available, and reinitialize after a Wi-Fi loss.
  if (connected && otaConfigured && !otaStarted) {
    ArduinoOTA.begin();
    otaStarted = true;
    Serial.printf("[ota] Ready: %s at %s (port 3232, password required)\n",
                  OTA_HOSTNAME, WiFi.localIP().toString().c_str());
  } else if (!connected && otaStarted) {
    ArduinoOTA.end();
    otaStarted = false;
  }
  // Log transitions once, rather than repeating the address on every loop pass.
  if (connected != wasConnected) {
    if (connected) Serial.printf("[wifi] Dashboard: http://%s/\n", WiFi.localIP().toString().c_str());
    else Serial.println("[wifi] Connection lost; scheduled retry will follow");
    wasConnected = connected;
  }
  if (connected) {
    lastReconnect = now;
    wifiRestartPending = false;
  } else if (!wifiRestartPending && now - lastReconnect >= WIFI_ATTEMPT_MS) {
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
    if (!WiFi.reconnect()) Serial.println("[wifi] Could not start connection; will retry in 30 s");
  }
}




