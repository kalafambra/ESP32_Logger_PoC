#include "environment.h"
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_BME280.h>
#include <math.h>
#include "config.h"
#include "motion.h"
#include "self_tests.h"

namespace {

Adafruit_BME280 bme;
uint8_t sensorAddress = 0;
bool sensorReady = false, converting = false;
uint32_t lastStart = 0, conversionStart = 0, windowStart = 0;
uint32_t missedSampleDeadlines = 0, lateConversionServices = 0, missedReportDeadlines = 0;
unsigned windowScheduledSlots = 0;
constexpr unsigned SAMPLES_PER_WINDOW = 5;
static_assert(SAMPLE_INTERVAL_MS == 200, "5 Hz requires SAMPLE_INTERVAL_MS = 200");
SampleWindow window;
PublishedSample latest;
EnvironmentHistory history;
bool environmentHistoryTestsPassed = false;
bool windowFailed = false;
const char *sensorError = "Sensor not initialized";
/*
 * Read consecutive BME280 registers and expose bus failures to the caller.
 * Explicit status checks avoid treating a missing device as a valid reading.
 */
bool readRegisters(uint8_t reg, uint8_t *data, uint8_t count) {
  if (!I2C_SENSORS_ENABLED)
    return false;
  Wire.beginTransmission(sensorAddress);
  Wire.write(reg); // Select the first register before switching to a read.
  // A repeated start keeps register selection and reading in one transaction.
  if (Wire.endTransmission(false) != 0)
    return false;
  // Reject partial replies rather than leaving uninitialized bytes in data.
  if (Wire.requestFrom(sensorAddress, count) != count)
    return false;
  for (uint8_t i = 0; i < count; ++i)
    data[i] = Wire.read();
  return true;
}

/*
 * Write a sensor control byte directly so forced conversions can be triggered
 * without using the library helper that waits for conversion completion.
 */
bool writeRegister(uint8_t reg, uint8_t value) {
  if (!I2C_SENSORS_ENABLED)
    return false;
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
} // namespace

/*
 * Advance a forced-conversion state machine at five readings per second.
 * Returning between trigger and read keeps the dashboard and OTA responsive.
 * Only validated measurements enter the average; the LED belongs to pickup detection.
 */
void acquireSensor() {
  if (!sensorReady)
    return; // Initialization errors require a reset after repair.
  const uint32_t now = millis();
  if (!converting && now - lastStart >= SAMPLE_INTERVAL_MS) {
    // Keep the original 200 ms grid. Skip elapsed slots instead of taking a
    // burst of replacement readings that would misrepresent the time average.
    const uint32_t slots = (now - lastStart) / SAMPLE_INTERVAL_MS;
    missedSampleDeadlines += slots - 1;
    windowScheduledSlots += slots;
    if (slots > 1) {
      windowFailed = true;
      sensorError = "Missed sampling deadline";
    }
    lastStart += slots * SAMPLE_INTERVAL_MS;
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
  if (!converting || now - conversionStart < 15)
    return;
  uint8_t status;
  if (!readRegisters(0xF3, &status, 1)) {
    failSample("I2C status read failed");
    return;
  }
  // A late loop visit does not mean the sensor timed out: first check whether
  // conversion actually finished. Only a still-busy sensor can time out.
  if (status & 0x09) {
    if (now - conversionStart > 80)
      failSample("Conversion timeout (sensor still busy)");
    return;
  }
  if (now - conversionStart > 80)
    ++lateConversionServices;
  uint8_t raw[8];
  // Check the complete raw frame before asking the library for compensated values.
  if (!readRegisters(0xF7, raw, sizeof(raw))) {
    failSample("I2C data read failed");
    return;
  }
  // These sentinel values mean a channel was skipped, not a real measurement.
  if ((raw[0] == 0x80 && raw[1] == 0 && raw[2] == 0) ||
      (raw[3] == 0x80 && raw[4] == 0 && raw[5] == 0) || (raw[6] == 0x80 && raw[7] == 0)) {
    failSample("Sensor returned skipped-channel data");
    return;
  }
  // These compensated reads do not wait for conversion in sleep mode.
  const float temperatureC = bme.readTemperature();
  const float humidityPct = bme.readHumidity();
  const float pressureHpa = bme.readPressure() / 100.0F; // Convert Pa to dashboard units.
  // Check the bus again: the library's float API does not expose I2C errors.
  uint8_t id;
  if (!readRegisters(0xD0, &id, 1) || id != 0x60 || !isfinite(temperatureC) ||
      !isfinite(humidityPct) || !isfinite(pressureHpa) || temperatureC < -40 || temperatureC > 85 ||
      humidityPct < 0 || humidityPct > 100 || pressureHpa < 300 || pressureHpa > 1100) {
    failSample("Invalid reading or I2C communication error");
    return;
  }
  converting = false;
  // Accumulate full-precision readings; rounding happens only when reporting.
  window.add(temperatureC, humidityPct, pressureHpa);
}

/*
 * Close each one-second window and publish mean/min/max for exactly five
 * valid readings. Keep a bounded history so missed browser polls can recover.
 * Include network status even in error reports to support remote diagnosis.
 */
void publishWindow(uint32_t now) {
  if (!I2C_SENSORS_ENABLED)
    return;
  if (now - windowStart < PUBLISH_INTERVAL_MS)
    return;
  const uint32_t elapsedWindows = (now - windowStart) / PUBLISH_INTERVAL_MS;
  // Slots never reached before the boundary are lost, not filled in later.
  if (windowScheduledSlots < SAMPLES_PER_WINDOW)
    missedSampleDeadlines += SAMPLES_PER_WINDOW - windowScheduledSlots;
  missedSampleDeadlines += (elapsedWindows - 1) * SAMPLES_PER_WINDOW;
  const uint32_t sequence = latest.sequence + 1;
  latest = PublishedSample{};
  latest.sequence = sequence;
  latest.sampleMs = now;
  // Synchronize the 1 Hz chip-temperature sample independently of BME validity.
  acquireMotionTemperature(now);
  const MotionTemperature temperature = motionTemperature();
  latest.mpuTemperatureC = temperature.celsius;
  latest.mpuTemperatureOk = temperature.ok;
  latest.count = window.count;
  // Reject missing readings, any recorded error, and excessively delayed windows.
  latest.ok = sensorReady && !windowFailed && window.count == SAMPLES_PER_WINDOW &&
              now - windowStart < PUBLISH_INTERVAL_MS + SAMPLE_INTERVAL_MS;
  latest.error = latest.ok      ? ""
                 : !sensorReady ? sensorError
                 : windowFailed ? sensorError
                                : "Incomplete one-second window (expected 5 readings)";
  // Calculate channel statistics separately; their extrema need not coincide.
  latest.temperature.finish(window.temperature, window.count);
  latest.humidity.finish(window.humidity, window.count);
  latest.pressure.finish(window.pressure, window.count);
  history.add(latest); // Recent one-second reports age into 30-second buckets.
  // A disconnected report must not advertise an obsolete address as usable.
  const wl_status_t wifiStatus = WiFi.status();
  const bool wifiConnected = wifiStatus == WL_CONNECTED;
  const String ip = wifiConnected ? WiFi.localIP().toString() : String("0.0.0.0");
  if (latest.ok) {
    Serial.printf("[report %lu @ %lu ms, n=%u] T mean/min/max %.3f/%.3f/%.3f C | H %.3f/%.3f/%.3f "
                  "%% | P %.3f/%.3f/%.3f hPa | WiFi=%s (status=%d) | IP=%s\n",
                  latest.sequence, latest.sampleMs, latest.count, latest.temperature.mean,
                  latest.temperature.minimum, latest.temperature.maximum, latest.humidity.mean,
                  latest.humidity.minimum, latest.humidity.maximum, latest.pressure.mean,
                  latest.pressure.minimum, latest.pressure.maximum,
                  wifiConnected ? "connected" : "disconnected", int(wifiStatus), ip.c_str());

  } else {
    Serial.printf("[report %lu, n=%u] %s | WiFi=%s (status=%d) | IP=%s\n", latest.sequence,
                  latest.count, latest.error, wifiConnected ? "connected" : "disconnected",
                  int(wifiStatus), ip.c_str());
  }
  // Start a non-overlapping window; measurements must not be counted twice.
  window = SampleWindow{};
  windowFailed = false;
  converting = false; // Do not carry a late conversion into the next window.
  // Advance on the original one-second grid, without manufacturing reports
  // for windows during which acquisition could not run.
  missedReportDeadlines += elapsedWindows - 1;
  windowStart += elapsedWindows * PUBLISH_INTERVAL_MS;
  windowScheduledSlots = 0;
  lastStart = windowStart - SAMPLE_INTERVAL_MS; // First slot stays on the fixed grid.
}

void setupEnvironment() {
  if (!I2C_SENSORS_ENABLED)
    return;
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
    bme.setSampling(Adafruit_BME280::MODE_SLEEP, Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::FILTER_OFF);
    sensorError = "Waiting for first sample";
    Serial.printf("[sensor] BME280 at 0x%02X\n", sensorAddress);
  } else {
    sensorError = "BME280 not found at 0x76 or 0x77; check wiring and reset";
    Serial.printf("[sensor] %s\n", sensorError);
  }
}

void setupEnvironmentHistory() {
  environmentHistoryTestsPassed = runEnvironmentHistorySelfTests(history);
  Serial.printf("[history] Tiered history self-tests: %s; storage=%u bytes\n",
                environmentHistoryTestsPassed ? "PASS" : "FAIL", unsigned(sizeof(history)));
}
void disableEnvironment() {
  sensorReady = false;
  sensorError = "I2C sensors disabled in microphone build";
  latest.error = sensorError;
}
void resetEnvironmentWindow(uint32_t now) {
  windowStart = now;
  lastStart = windowStart - SAMPLE_INTERVAL_MS;
}
void discardEnvironmentWindow() {
  window = SampleWindow{};
  windowScheduledSlots = 0;
  failSample("OTA interrupted acquisition; window discarded");
  resetEnvironmentWindow(millis());
}
const PublishedSample &latestEnvironmentSample() {
  return latest;
}
const EnvironmentHistory &environmentHistory() {
  return history;
}
EnvironmentStatus environmentStatus() {
  return {missedSampleDeadlines, lateConversionServices, missedReportDeadlines,
          environmentHistoryTestsPassed};
}
