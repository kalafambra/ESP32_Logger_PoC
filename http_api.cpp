#include "http_api.h"
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <math.h>
#include "config.h"
#include "environment.h"
#include "motion.h"
#include "microphone.h"
#include "microphone_spectrum.h"
#include "spectrum_analysis.h"
#include "pickup_service.h"
#include "pickup_debug.h"
#include "pickup_events.h"
#include "pickup_ble.h"
#include "web_assets.h"

namespace {
WebServer server(80);
bool httpStarted = false;
static_assert(DEFAULT_ALTITUDE_M >= -500 && DEFAULT_ALTITUDE_M <= 9000,
              "Set DEFAULT_ALTITUDE_M between -500 and 9000 meters");
void appendMotion(String &json, uint32_t now);
void appendPickup(String &json);
/*
 * Keep history values in the documented [mean, minimum, maximum] order.
 * JSON nulls make invalid windows explicit and avoid non-standard NaN output.
 */
void appendChannel(String &json, const PublishedChannel &channel, bool ok) {
  if (!ok) {
    json += "[null,null,null]";
    return;
  }
  json += "[" + String(channel.mean, 3) + "," + String(channel.minimum, 3) + "," +
          String(channel.maximum, 3) + "]";
}

/*
 * Serve the latest report and incremental history for browser recovery.
 * Pressure stays local/raw here so each browser can apply its chosen altitude
 * consistently to both current readings and historical extrema.
 */
void sendReadings() {
  const PublishedSample &latest = latestEnvironmentSample();
  const EnvironmentHistory &history = environmentHistory();
  const EnvironmentStatus status = environmentStatus();
  const uint32_t now = millis();
  const uint32_t age = now - latest.sampleMs;
  // Allow a full reporting interval plus scheduling margin before marking stale.
  const bool fresh = latest.sequence && latest.ok && age <= PUBLISH_INTERVAL_MS + 1500;
  const char *error = latest.ok && !fresh ? "One-second report stale" : latest.error;
  // Browser requests records newer than its last sequence. This repairs missed
  // HTTP polls instead of drawing breaks for measurements still held in RAM.
  uint32_t since = server.hasArg("since") ? strtoul(server.arg("since").c_str(), nullptr, 10) : 0;
  if (since > latest.sequence)
    since = 0; // Device restarted.
  String json;
  json.reserve(2600 + history.selected(since, now) * 272); // Includes the second temperature trace.
  json = "{\"sequence\":" + String(latest.sequence);
  json += ",\"uptime_ms\":" + String(now);
  json += ",\"i2c_enabled\":" + String(I2C_SENSORS_ENABLED ? "true" : "false");
  json +=
      ",\"history_mode\":\"tiered_30s\",\"history_recent_ms\":30000,\"history_coarse_ms\":30000";
  json += ",\"history_self_tests_passed\":" + String(status.historyTestsPassed ? "true" : "false");
  json += ",\"default_altitude_m\":" + String(DEFAULT_ALTITUDE_M, 2);
  json += ",\"sample_ms\":" + String(latest.sampleMs);
  json += ",\"sample_age_ms\":" + (latest.sequence ? String(age) : String("null"));
  json += ",\"sample_interval_ms\":200,\"publish_interval_ms\":1000,\"sample_count\":" +
          String(latest.count);
  json += ",\"missed_sample_deadlines\":" + String(status.missedSampleDeadlines);
  json += ",\"missed_report_deadlines\":" + String(status.missedReportDeadlines);
  json += ",\"late_conversion_services\":" + String(status.lateConversionServices);
  json += ",\"sensor_ok\":";
  json += fresh ? "true" : "false";
  json += ",\"error\":\"";
  json += error;
  json += "\"";
  json += ",\"wifi_connected\":";
  json += WiFi.status() == WL_CONNECTED ? "true" : "false";
  json += ",\"rssi_dbm\":" + String(WiFi.RSSI());
  json += ",\"temperature_c\":" + (fresh ? String(latest.temperature.mean, 3) : String("null"));
  json += ",\"humidity_pct\":" + (fresh ? String(latest.humidity.mean, 3) : String("null"));
  json += ",\"pressure_hpa\":" + (fresh ? String(latest.pressure.mean, 3) : String("null"));
  appendMotion(json, now);
  json += ",\"history\":[";
  bool first = true;
  // Updated coarse rows bypass the source-sequence cursor through their promotion revision.
  history.visit(since, now, [&](const EnvironmentHistory::Point &point, bool coarse) {
    if (!first)
      json += ',';
    first = false;
    json += "{\"first_sequence\":" + String(point.firstSequence) +
            ",\"sequence\":" + String(point.sequence);
    json +=
        ",\"age_ms\":" + String(now - point.midpoint()) + ",\"revision\":" + String(point.revision);
    json += ",\"resolution_ms\":" + String(coarse ? 30000 : 1000) +
            ",\"report_count\":" + String(point.reports);
    json += ",\"sample_count\":" + String(point.samples) + ",\"ok\":";
    json += point.ok ? "true" : "false";
    json += ",\"temperature_c\":";
    appendChannel(json, point.temperature, point.ok);
    json += ",\"humidity_pct\":";
    appendChannel(json, point.humidity, point.ok);
    json += ",\"pressure_hpa\":";
    appendChannel(json, point.pressure, point.ok);
    json += ",\"mpu_temperature_c\":" +
            (point.mpuSamples == point.reports ? String(point.mpuTemperatureC, 3) : String("null"));
    json += '}';
  });
  json += "]}";
  // Live API responses must not be replaced by a browser's cached reading.
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

/* Serialize channel arrays with nulls on failure; min/max remain available without sending raw
 * samples. */
void motionArray(String &json, const float *values, bool valid) {
  json += '[';
  for (unsigned i = 0; i < 3; ++i) {
    if (i)
      json += ',';
    json += valid ? String(values[i], 3) : String("null");
  }
  json += ']';
}

/* Expose measured sample counts and IRQ/overflow diagnostics rather than claiming the target rate
 * was achieved. */
void appendMotion(String &json, uint32_t now) {
  const MotionReport &motionLatest = latestMotionReport();
  const MotionStatus status = motionStatus();
  const uint32_t interrupts = status.interrupts;
  const bool valid = motionLatest.sequence && motionLatest.ok &&
                     now - motionLatest.sampleMs <= MOTION_REPORT_MS + 1500;
  const bool temperatureValid = status.temperature.ok && now - status.temperature.sampleMs <= 2500;
  json += ",\"motion\":{\"sequence\":" + String(motionLatest.sequence) +
          ",\"sample_age_ms\":" + String(now - motionLatest.sampleMs);
  json += ",\"ok\":";
  json += valid ? "true" : "false";
  json += ",\"error\":\"";
  json += motionLatest.ok && !valid ? "Motion report stale"
          : motionLatest.sequence   ? motionLatest.error
                                    : status.error;
  json += "\",\"warning\":\"";
  if (status.ready && !interrupts && now - status.startedAt > 1000)
    json += "No INT edges; check GPIO17 wiring (FIFO polling fallback active)";
  json += "\",\"sample_count\":" + String(motionLatest.samples) +
          ",\"target_hz\":100,\"publish_interval_ms\":1000";
  json += ",\"interrupts_seen\":" + String(interrupts) +
          ",\"fifo_overflows\":" + String(status.overflows);
  json += ",\"temperature_ok\":" + String(temperatureValid ? "true" : "false") +
          ",\"temperature_c\":" +
          (temperatureValid ? String(status.temperature.celsius, 3) : String("null"));
  json += ",\"temperature_error\":\"";
  json += temperatureValid        ? ""
          : status.temperature.ok ? "MPU6050 temperature stale"
                                  : status.temperature.error;
  json += '"';
  json += ",\"acceleration_m_s2\":";
  motionArray(json, motionLatest.mean, valid);
  json += ",\"gyroscope_rad_s\":";
  motionArray(json, status.gyroMean, valid);
  float degrees[3], rawDegrees[3], biasDegrees[3];
  for (unsigned i = 0; i < 3; ++i) {
    degrees[i] = status.gyroMean[i] * 57.2957795f;
    biasDegrees[i] = status.gyroBias[i] * 57.2957795f;
    rawDegrees[i] = degrees[i] + biasDegrees[i];
  }
  json += ",\"gyroscope_deg_s\":";
  motionArray(json, degrees, valid);
  json += ",\"gyroscope_raw_deg_s\":";
  motionArray(json, rawDegrees, valid);
  json += ",\"gyro_bias_deg_s\":";
  motionArray(json, biasDegrees, true);
  json += ",\"calibration_kind\":\"";
  json += status.gyroCalibrating ? "gyro" : "accel";
  json += '"';
  json += ",\"minimum_m_s2\":";
  motionArray(json, motionLatest.minimum, valid);
  json += ",\"maximum_m_s2\":";
  motionArray(json, motionLatest.maximum, valid);
  json += ",\"bias_m_s2\":";
  motionArray(json, status.bias, true);
  json += ",\"calibrating\":";
  json += status.calibrating ? "true" : "false";
  json += ",\"calibration_samples\":" + String(status.calibrationSamples);
  json += ",\"calibration_error\":\"";
  json += status.calibrationError;
  json += '"';
  json += '}';
}

/* Serve small incremental batches separately from the large, slow environmental-history endpoint.
 */
void sendMotionReadings() {
  const MotionReport &motionLatest = latestMotionReport();
  const uint32_t now = millis();
  uint32_t since = server.hasArg("since") ? strtoul(server.arg("since").c_str(), nullptr, 10) : 0;
  if (since > motionLatest.sequence)
    since = 0;
  String json;
  json.reserve(5000);
  json = "{\"uptime_ms\":" + String(now);
  appendMotion(json, now);
  appendPickup(json);
  json += ",\"history\":[";
  unsigned sent = 0;
  for (unsigned i = 0; i < motionHistorySize() && sent < 20; ++i) {
    const MotionReport &report = motionHistoryAt(i);
    if (report.sequence <= since)
      continue;
    if (sent++)
      json += ',';
    json += "{\"sequence\":" + String(report.sequence) +
            ",\"age_ms\":" + String(now - report.sampleMs) + ",\"ok\":";
    json += report.ok ? "true" : "false";
    // Counts and the original error survive backfill, so missed live polls
    // do not prevent independent acquisition-completeness checks.
    json += ",\"sample_count\":" + String(report.samples);
    json += ",\"error\":\"";
    json += report.error;
    json += '"';
    json += ",\"a\":";
    motionArray(json, report.mean, report.ok);
    json += '}';
  }
  json += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

/* Compare diagnostics.direct sensor registers with FIFO output without changing scale, offsets or
 * gravity. */
void sendMotionDiagnostics() {
  if (!I2C_SENSORS_ENABLED) {
    server.sendHeader("Cache-Control", "no-store");
    server.send(503, "application/json",
                "{\"ok\":false,\"error\":\"I2C sensors disabled in microphone build\"}");
    return;
  }
  const MotionDiagnostics diagnostics = readMotionDiagnostics();
  const bool ok = diagnostics.ok;
  String json = "{\"ok\":";
  json += ok ? "true" : "false";
  json += ",\"accel_config\":" + String(diagnostics.range);
  json += ",\"counts_per_g\":" + String(diagnostics.countsPerG, 0) + ",\"raw_xyz\":[";
  for (unsigned i = 0; i < 3; ++i) {
    if (i)
      json += ',';
    if (!ok)
      json += "null";
    else {
      const int32_t value = diagnostics.raw[i];
      json += String(value);
    }
  }
  json += "],\"direct_m_s2\":";
  motionArray(json, diagnostics.direct, ok);
  json += ",\"direct_magnitude_m_s2\":";
  json += ok ? String(sqrtf(diagnostics.direct[0] * diagnostics.direct[0] +
                            diagnostics.direct[1] * diagnostics.direct[1] +
                            diagnostics.direct[2] * diagnostics.direct[2]),
                      3)
             : String("null");
  json += ",\"corrected_direct_m_s2\":";
  motionArray(json, diagnostics.corrected, ok);
  json += ",\"registers\":{";
  const auto &regs = MOTION_DIAGNOSTIC_REGISTERS;
  for (unsigned i = 0; i < sizeof(regs); ++i) {
    if (i)
      json += ',';
    json += "\"0x" + String(regs[i], HEX) + "\":";
    json += diagnostics.registers[i] >= 0 ? String(diagnostics.registers[i]) : String("null");
  }
  json += "}}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void startMotionCalibration() {
  if (!beginMotionCalibration()) {
    server.send(409, "application/json",
                "{\"error\":\"Sensor unavailable or calibration already running\"}");
    return;
  }
  server.send(202, "application/json",
              "{\"status\":\"Keep module flat, stationary, positive Z up for five seconds\"}");
}
void startGyroCalibration() {
  if (!beginGyroCalibration()) {
    server.send(409, "application/json",
                "{\"error\":\"Sensor unavailable or calibration already running\"}");
    return;
  }
  server.send(202, "application/json",
              "{\"status\":\"Keep module completely stationary for five seconds\"}");
}

void sendMicrophoneSpectrum() {
  const MicrophoneStatus microphone = microphoneStatus();
  const MicrophoneSpectrum &report = microphoneSpectrum();
  const uint32_t age = millis() - report.sampleMs;
  const bool ok = microphone.ready && !microphone.paused && report.valid &&
                  age < AUDIO_SPECTRUM_STALE_MS &&
                  (microphone.error == ESP_OK || microphone.error == ESP_ERR_TIMEOUT);
  String json;
  json.reserve(600 + SpectrumAnalysis::BINS * 8);
  json = "{\"ok\":" + String(ok ? "true" : "false") + ",\"error\":\"";
  json += !microphone.ready                ? "Microphone initialization failed"
          : microphone.paused              ? "Microphone paused for OTA"
          : !report.valid                  ? "Waiting for audio spectrum"
          : age >= AUDIO_SPECTRUM_STALE_MS ? "Audio spectrum stale"
          : !ok                            ? esp_err_to_name(microphone.error)
                                           : "";
  json += "\",\"sequence\":" + String(report.sequence) + ",\"age_ms\":" + String(age) +
          ",\"sample_rate_hz\":" + String(MICROPHONE_SAMPLE_RATE_HZ) +
          ",\"fft_size\":" + String(SpectrumAnalysis::SIZE) +
          ",\"bin_hz\":" + String(SpectrumAnalysis::BIN_HZ, 9) +
          ",\"frames\":" + String(report.frames) +
          ",\"dma_overflows\":" + String(microphone.overflows) +
          ",\"clipped_samples\":" + String(report.clipped);
  json += ",\"audio_samples\":" + String(microphone.samples) +
          ",\"dma_capacity_ms\":" + String(MICROPHONE_DMA_CAPACITY_MS);
  json += ",\"average_target_ms\":" + String(AUDIO_SPECTRUM_AVERAGE_MS) +
          ",\"stale_after_ms\":" + String(AUDIO_SPECTRUM_STALE_MS);
  json += ",\"averaged_audio_ms\":" +
          String(1000.0f * report.frames * SpectrumAnalysis::SIZE / SpectrumAnalysis::RATE, 1);
  json += ",\"i2c_enabled\":" + String(I2C_SENSORS_ENABLED ? "true" : "false");
  json += ",\"rms_dbfs\":" + (ok ? String(report.rmsDb, 1) : String("null"));
  json += ",\"peak_hz\":" + (ok && report.rmsDb > -119 ? String(report.peakHz, 2) : String("null"));
  json += ",\"bands_dbfs\":[";
  for (unsigned i = 0; i < 3; ++i) {
    if (i)
      json += ',';
    json += ok ? String(report.bandsDb[i], 1) : String("null");
  }
  json += "],\"bins_dbfs\":[";
  if (ok)
    for (unsigned i = 0; i < SpectrumAnalysis::BINS; ++i) {
      if (i)
        json += ',';
      json += String(report.binsDb[i], 1);
    }
  json += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

/* Lightweight, leased debug stream: bounded batches rather than resending every graph's history. */
void sendPickupDebug() {
  const PickupStatus status = pickupStatus();
  const uint32_t now = millis();
  if (!subscribePickupDebug(now)) {
    server.send(503, "application/json", "{\"error\":\"Not enough memory for debug history\"}");
    return;
  }
  uint32_t since = server.hasArg("since") ? strtoul(server.arg("since").c_str(), nullptr, 10) : 0;
  if (since > pickupDebugHistory().latestSequence())
    since = 0;
  String json;
  json.reserve(9000);
  json =
      "{\"uptime_ms\":" + String(now) + ",\"settings_revision\":" + String(status.settingsRevision);
  json += ",\"free_heap\":" + String(ESP.getFreeHeap()) +
          ",\"largest_free_block\":" + String(ESP.getMaxAllocHeap());
  appendPickup(json);
  pickupDebugHistory().appendJson(json, since);
  json += '}';
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

/* Serialize status separately from graphs so faults cannot be mistaken for a quiet, safe detector.
 */
void appendPickup(String &json) {
  const PickupStatus status = pickupStatus();
  const PickupDetector &pickup = pickupDetector();
  json += ",\"pickup\":{\"active\":";
  json += pickup.active ? "true" : "false";
  json += ",\"state\":\"";
  json += !status.selfTestsPassed ? "self_test_failed"
          : !status.dataValid     ? "sensor_unavailable"
          : pickup.active         ? "picked_up"
          : pickup.armed          ? "armed"
                                  : "learning_baseline";
  json += "\",\"error\":\"";
  json += status.error;
  json += '"';
  json += ",\"quiet_ms\":" + String(pickup.quietMs()) +
          ",\"stationary_ms\":" + String(pickup.settings.stationary_ms, 0);
  json += ",\"events\":" + String(pickup.events) + ",\"self_tests_passed\":";
  json += status.selfTestsPassed ? "true" : "false";
  json += ",\"led_available\":";
  json += status.ledAvailable ? "true" : "false";
  json += ",\"led_on\":";
  json += status.ledOn ? "true" : "false";
  json += ",\"accel_deviation\":" + String(pickup.accelDeviation, 4) +
          ",\"tilt_deg\":" + String(pickup.tiltDegrees, 2);
  json += ",\"rotation_rad_s\":" + String(pickup.rotationRate, 4) +
          ",\"accel_rms\":" + String(pickup.accelRms, 4);
  json += ",\"gyro_rms\":" + String(pickup.gyroRms, 4) +
          ",\"baseline_noise\":" + String(pickup.baselineNoise, 4) + '}';
}

/* Provide lightweight diagnostics without transferring graph history. */
void sendPickupStatus() {
  String json = "{\"uptime_ms\":" + String(millis());
  appendPickup(json);
  json += '}';
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

/* Include validation metadata so the dashboard uses the same limits and units as the firmware. */
void sendPickupSettings() {
  const PickupStatus status = pickupStatus();
  const PickupDetector &pickup = pickupDetector();
  const auto &pickupFields = pickupSettingFields();
  String json = "{\"persisted\":";
  json += status.persisted ? "true" : "false";
  json += ",\"settings_revision\":" + String(status.settingsRevision);
  json += ",\"fields\":[";
  const PickupSettings defaults;
  for (unsigned i = 0; i < PICKUP_FIELD_COUNT; ++i) {
    const auto &f = pickupFields[i];
    if (i)
      json += ',';
    json += "{\"key\":\"" + String(f.key) + "\",\"label\":\"" + String(f.label) + "\",\"unit\":\"" +
            String(f.unit) + "\"";
    json += ",\"min\":" + String(f.min, 4) + ",\"max\":" + String(f.max, 4) +
            ",\"step\":" + String(f.step, 4);
    json += ",\"value\":" + String(pickup.settings.*(f.member), 4) +
            ",\"default\":" + String(defaults.*(f.member), 4);
    json += ",\"display_unit\":\"";
    json += String(f.unit) == "rad/s" ? "deg/s" : f.unit;
    json +=
        "\",\"display_scale\":" + String(String(f.unit) == "rad/s" ? 57.29577951308232 : 1.0, 10) +
        '}';
  }
  json += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

/* Accept only a complete, finite, validated form; unknown/missing fields leave saved settings
 * untouched. */
void updatePickupSettings() {
  const auto &pickupFields = pickupSettingFields();
  if (server.args() != PICKUP_FIELD_COUNT) {
    server.send(400, "application/json", "{\"error\":\"Supply every setting exactly once\"}");
    return;
  }
  PickupSettings next;
  for (const auto &f : pickupFields) {
    if (!server.hasArg(f.key)) {
      server.send(400, "application/json", "{\"error\":\"Missing setting\"}");
      return;
    }
    String value = server.arg(f.key);
    value.trim();
    char *end = nullptr;
    const float number = strtof(value.c_str(), &end);
    if (value.isEmpty() || *end) {
      server.send(400, "application/json", "{\"error\":\"Invalid numeric setting\"}");
      return;
    }
    next.*(f.member) = number;
  }
  const char *error = validatePickupSettings(next);
  if (error) {
    server.send(400, "application/json", String("{\"error\":\"") + error + "\"}");
    return;
  }
  if (!savePickupSettings(next)) {
    server.send(500, "application/json", "{\"error\":\"Could not save settings\"}");
    return;
  }
  servicePickup(millis());
  sendPickupSettings();
}

/* Restore AND persist defaults while preserving any active alert until confirmed stillness. */
void restorePickupDefaults() {
  if (!savePickupSettings(PickupSettings{})) {
    server.send(500, "application/json", "{\"error\":\"Could not save defaults\"}");
    return;
  }
  servicePickup(millis());
  sendPickupSettings();
}

// WebServer's default five-second wait for an idle accepted socket can block
// all other clients after an aborted poll or speculative browser connection.
// Request parsing/body delivery remain owned by WebServer; only sockets that
// have supplied no bytes to its cooperative handler are timed out here.
void serviceHttpClients(WebServer &http) {
  static bool waiting = false;
  static uint32_t waitingSince = 0;
  http.handleClient();
  auto client = http.client();
  if (!client || !client.connected() || client.available()) {
    waiting = false;
    return;
  }
  const uint32_t now = millis();
  if (!waiting) {
    waiting = true;
    waitingSince = now;
  } else if (now - waitingSince >= 1000) {
    client.stop();
    waiting = false;
  }
}

} // namespace

void setupHttp() {
  /* Serve the embedded page from flash so the dashboard needs no filesystem/internet. */
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", DASHBOARD_HTML);
  });
  server.on("/api/readings", HTTP_GET, sendReadings);
  server.on("/api/microphone", HTTP_GET, sendMicrophoneSpectrum);
  server.on("/api/motion", HTTP_GET, sendMotionReadings);
  server.on("/api/motion/diagnostics", HTTP_GET, sendMotionDiagnostics);
  server.on("/api/motion/calibrate", HTTP_POST, startMotionCalibration);
  server.on("/api/motion/calibrate-gyro", HTTP_POST, startGyroCalibration);
  server.on("/api/pickup/settings", HTTP_GET, sendPickupSettings);
  server.on("/api/pickup/debug", HTTP_GET, sendPickupDebug);
  server.on("/pickup", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", PICKUP_TUNING_HTML);
  });
  server.on("/api/pickup/status", HTTP_GET, sendPickupStatus);
  server.on("/api/pickup/events", HTTP_GET, []() {
    String json = "{\"ble_ready\":" + String(isPickupBleReady() ? "true" : "false") +
                  ",\"free_heap\":" + String(ESP.getFreeHeap()) +
                  ",\"largest_free_block\":" + String(ESP.getMaxAllocHeap()) +
                  ",\"history_capacity\":" + String(EnvironmentHistory::MAX_POINTS) +
                  ",\"metadata\":" + pickupEventLog().metadataJson() + ",\"events\":[";
    for (unsigned i = 0; i < 10; ++i) {
      if (i)
        json += ',';
      json += pickupEventLog().eventJson(i);
    }
    json += "]}";
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", json);
  });
  server.on("/api/pickup/events/text", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/plain; charset=utf-8", pickupEventLog().listText());
  });
  server.on("/pickup-uuids.json", HTTP_GET, []() {
    server.sendHeader("Content-Disposition", "attachment; filename=pickup-uuids.json");
    server.send_P(200, "application/json", PICKUP_UUID_DEFINITIONS);
  });
  server.on("/api/pickup/settings", HTTP_POST, updatePickupSettings);
  server.on("/api/pickup/defaults", HTTP_POST, restorePickupDefaults);
  /* Return an actual missing-resource status instead of misleading dashboard content. */
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
}

void startHttp() {
  if (httpStarted)
    return;
  server.begin();
  httpStarted = true;
}
void serviceHttp() {
  if (httpStarted)
    serviceHttpClients(server);
}
