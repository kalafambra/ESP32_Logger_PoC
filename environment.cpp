#include "environment.h"
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_BME280.h>
#include <math.h>
#include "config.h"
#include "motion.h"
#include "self_tests.h"
#include "acquisition_timing.h"
#include "i2c_bus_service.h"

namespace {

Adafruit_BME280 bme;
uint8_t sensorAddress = 0;
bool sensorReady = false, converting = false;
uint32_t lastStart = 0, conversionStart = 0, windowStart = 0;
uint32_t acquisitionWindowStart = 0, environmentSequence = 0;
bool firstTrigger = true;
uint32_t missedSampleDeadlines = 0, lateConversionServices = 0, missedReportDeadlines = 0;
constexpr unsigned SAMPLES_PER_WINDOW = 5;
static_assert(SAMPLE_INTERVAL_MS == 200, "5 Hz requires SAMPLE_INTERVAL_MS = 200");
SampleWindow window;
PublishedSample latest;
DashboardDelivery<PublishedSample, 4> delivery;
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
  converting = false;
  SensorPacket packet;
  packet.kind = SensorPacketKind::EnvironmentFault;
  packet.error = message;
  packet.sampleMs = conversionStart;
  enqueueSensorPacket(packet);
}
} // namespace

/*
 * Read the completed forced conversion once, then queue validated values for loop processing.
 */
// Read once per 200 ms deadline, before starting another forced conversion.
// Results are attributed to their trigger slot, never their retrieval time.
void readPreviousEnvironment(uint32_t now) {
  if (!converting) return;
  uint8_t status;
  if (!readRegisters(0xF3, &status, 1)) {
    failSample("I2C status read failed");
    return;
  }
  if (status & 0x09) {
    failSample("Conversion still busy at next 200 ms deadline");
    return;
  }
  if (now - conversionStart > SAMPLE_INTERVAL_MS + 50)
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
  SensorPacket packet;
  packet.kind = SensorPacketKind::Environment;
  packet.sampleMs = conversionStart;
  packet.retrievedMs = now;
  packet.sequence = ++environmentSequence;
  packet.temperature = temperatureC;
  packet.humidity = humidityPct;
  packet.pressure = pressureHpa;
  enqueueSensorPacket(packet);
  countEnvironmentSample();
}

void acquireSensor() {
  ScopedServiceTiming timing(acquisitionTiming().environment);
  const uint32_t now = millis();
  if (!firstTrigger && now - lastStart < SAMPLE_INTERVAL_MS) return;
  const uint32_t elapsedSlots = (now - lastStart) / SAMPLE_INTERVAL_MS;
  const uint32_t slots = firstTrigger ? elapsedSlots + 1 : elapsedSlots;
  if (!firstTrigger && sensorReady) readPreviousEnvironment(now);
  if (slots > 1) {
    missedSampleDeadlines += slots - 1;
    failSample("Missed sampling deadline");
  }
  lastStart += (firstTrigger ? slots - 1 : slots) * SAMPLE_INTERVAL_MS;
  firstTrigger = false;
  if (lastStart - acquisitionWindowStart >= PUBLISH_INTERVAL_MS) {
    const uint32_t windows = (lastStart - acquisitionWindowStart) / PUBLISH_INTERVAL_MS;
    acquisitionWindowStart += windows * PUBLISH_INTERVAL_MS;
    acquireMotionTemperature(now);
    SensorPacket boundary;
    boundary.kind = SensorPacketKind::Boundary;
    boundary.sampleMs = acquisitionWindowStart;
    const MotionTemperature chip = motionTemperature();
    boundary.temperature = chip.celsius;
    boundary.chipTemperatureOk = chip.ok;
    enqueueSensorPacket(boundary);
  }
  if (!sensorReady) return; // Still deliver failed report boundaries for a missing BME.
  conversionStart = millis();
  if (!writeRegister(0xF4, 0x25)) {
    failSample("I2C trigger failed");
    return;
  }
  converting = true;
}

void invalidateEnvironmentProcessing(const char *message) {
  windowFailed = true;
  sensorError = message;
}

void processEnvironmentPacket(const SensorPacket &packet) {
  if (packet.kind == SensorPacketKind::Reset) {
    window = SampleWindow{};
    windowStart = packet.sampleMs;
    windowFailed = false;
    sensorError = "Waiting for fresh samples after acquisition restart";
  } else if (packet.kind == SensorPacketKind::EnvironmentFault) {
    invalidateEnvironmentProcessing(packet.error);
  } else if (packet.kind == SensorPacketKind::Environment) {
    if (packet.sampleMs - windowStart >= PUBLISH_INTERVAL_MS) {
      invalidateEnvironmentProcessing("Missing environmental window boundary");
      publishWindow(packet.sampleMs);
    }
    window.add(packet.temperature, packet.humidity, packet.pressure);
  } else if (packet.kind == SensorPacketKind::Boundary) {
    latest.mpuTemperatureC = packet.temperature;
    latest.mpuTemperatureOk = packet.chipTemperatureOk;
    publishWindow(packet.sampleMs);
  }
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
  missedReportDeadlines += elapsedWindows - 1;
  const uint32_t sequence = latest.sequence + 1;
  const float chipC = latest.mpuTemperatureC;
  const bool chipOk = latest.mpuTemperatureOk;
  latest = PublishedSample{};
  latest.sequence = sequence;
  latest.sampleMs = now;
  latest.mpuTemperatureC = chipC;
  latest.mpuTemperatureOk = chipOk;
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
  delivery.publish(latest, millis());
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
  // Advance on the original one-second grid, without manufacturing reports
  // for windows during which acquisition could not run.
  windowStart += elapsedWindows * PUBLISH_INTERVAL_MS;
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
  environmentHistoryTestsPassed = runDeliverySelfTests();
  Serial.printf("[delivery] ACK buffer self-tests: %s\n",
                environmentHistoryTestsPassed ? "PASS" : "FAIL");
}
void disableEnvironment() {
  sensorReady = false;
  sensorError = "I2C sensors disabled in microphone build";
  latest.error = sensorError;
}
void resetEnvironmentWindow(uint32_t now) {
  windowStart = acquisitionWindowStart = lastStart = now;
  converting = false;
  firstTrigger = true;
}
void discardEnvironmentWindow() {
  acquisitionWindowStart = lastStart = millis();
  converting = false;
  firstTrigger = true;
  SensorPacket packet;
  packet.kind = SensorPacketKind::Reset;
  packet.sampleMs = acquisitionWindowStart;
  enqueueSensorPacket(packet);
}
const PublishedSample &latestEnvironmentSample() {
  return latest;
}
EnvironmentStatus environmentStatus() {
  ProcessingStateLock processingLock;
  SensorStateLock lock;
  return {missedSampleDeadlines, lateConversionServices, missedReportDeadlines,
          environmentHistoryTestsPassed};
}
void copyEnvironmentDashboard(EnvironmentDashboardSnapshot &snapshot, const DeliveryRequest &request) {
  ProcessingStateLock lock;
  snapshot.latest = latest;
  snapshot.status = environmentStatus();
  delivery.copy(request, deliveryBootId(), millis(), latest, snapshot.delivery);
}
