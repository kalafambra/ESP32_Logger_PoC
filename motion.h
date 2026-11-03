#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <WebServer.h>
extern WebServer server;

constexpr uint8_t MPU_ADDRESS = 0x68; // AD0 tied to GND.
constexpr uint32_t MOTION_REPORT_MS = 1000;
constexpr unsigned MOTION_HISTORY_SIZE = 128; // About 128 s of network recovery.
struct MotionReport {
  uint32_t sequence = 0, sampleMs = 0;
  unsigned samples = 0;
  bool ok = false;
  float mean[3] = {}, minimum[3] = {}, maximum[3] = {};
  const char *error = "Waiting for first acceleration report";
};
MotionReport motionLatest, motionReports[MOTION_HISTORY_SIZE];
unsigned motionHead = 0, motionHistoryCount = 0, motionSamples = 0;
float motionSum[3] = {}, motionMin[3] = {}, motionMax[3] = {};
bool motionReady = false, motionPaused = false, motionWindowFailed = false, motionSettled = false;
uint32_t motionLastReport = 0, motionLastService = 0, motionOverflowCount = 0;
uint32_t motionLastDiagnostic = 0, motionStartedAt = 0;
const char *motionError = "MPU6050 not initialized";
portMUX_TYPE motionMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool motionInterruptPending = false;
volatile uint32_t motionInterrupts = 0;

/* Signal work only: I2C, allocation and serial output must never run in the ISR. */
void ARDUINO_ISR_ATTR motionDataReadyISR() {
  portENTER_CRITICAL_ISR(&motionMux);
  motionInterruptPending = true;
  ++motionInterrupts;
  portEXIT_CRITICAL_ISR(&motionMux);
}

/* Keep register reads on the shared bus in normal task context, checking short replies. */
bool motionRead(uint8_t reg, uint8_t *data, uint8_t count) {
  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(MPU_ADDRESS, count) != count) return false;
  for (uint8_t i = 0; i < count; ++i) data[i] = Wire.read();
  return true;
}

/* Acknowledge each configuration write rather than silently using unknown sensor settings. */
bool motionWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

/* Reset FIFO framing after errors; acceleration-only FIFO packets contain exactly six bytes. */
bool resetMotionFifo() {
  return motionWrite(0x23, 0x00) && motionWrite(0x6A, 0x00) &&
         motionWrite(0x6A, 0x04) && motionWrite(0x6A, 0x40) && motionWrite(0x23, 0x08);
}

/* Configure 100 Hz acceleration acquisition, using FIFO to absorb brief loop delays. */
void setupMotion() {
  uint8_t identity = 0;
  pinMode(MPU_INTERRUPT_PIN, INPUT_PULLDOWN); // INT is configured as active-high push-pull.
  attachInterrupt(digitalPinToInterrupt(MPU_INTERRUPT_PIN), motionDataReadyISR, RISING);
  motionReady = motionRead(0x75, &identity, 1) && identity == 0x68;
  if (motionReady) {
    // DLPF=3 keeps acceleration bandwidth at 44 Hz, below the 50 Hz Nyquist limit.
    // The filtered internal clock is 1 kHz; divider=9 yields 100 Hz.
    motionReady = motionWrite(0x38, 0x00) && motionWrite(0x6B, 0x01) &&
                  motionWrite(0x1A, 0x03) && motionWrite(0x19, 0x09) &&
                  motionWrite(0x1C, 0x00) && motionWrite(0x37, 0x20) &&
                  resetMotionFifo() && motionWrite(0x38, 0x11);
    // INT_PIN_CFG: latch high until INT_STATUS is read. Enable ready + overflow IRQs.
  }
  motionError = motionReady ? "Waiting for FIFO samples" : "MPU6050 initialization failed at 0x68; check wiring and reset";
  motionStartedAt = motionLastReport = millis();
  Serial.printf("[mpu] %s; INT=GPIO%d, report=1000 ms\n", motionReady ? "FIFO acquisition configured for 100 Hz" : motionError, MPU_INTERRUPT_PIN);
}

/* Suspend FIFO during OTA, since a firmware transfer intentionally blocks acquisition. */
void pauseMotion() {
  motionPaused = true;
  if (motionReady) { motionWrite(0x38, 0); motionWrite(0x23, 0); motionWrite(0x6A, 0); }
}

/* Recover from a failed OTA transfer without mixing pre-transfer and post-transfer samples. */
void resumeMotion() {
  motionSamples = 0;
  for (unsigned i = 0; i < 3; ++i) motionSum[i] = 0;
  motionWindowFailed = true;
  motionError = "Acquisition interrupted by OTA";
  motionLastReport = millis();
  if (motionReady && (!resetMotionFifo() || !motionWrite(0x38, 0x11))) motionReady = false;
  motionPaused = false;
}

/* Reset framing on bus/overflow faults and mark this report as incomplete rather than averaging bad data. */
void motionFault(const char *message) {
  motionWindowFailed = true;
  motionError = message;
  if (!resetMotionFifo()) motionError = "MPU6050 I2C/FIFO recovery failed";
}

/* Publish a one-second aggregate instead of flooding serial and Wi-Fi with 100 raw frames/s. */
void publishMotion(uint32_t now) {
  if (now - motionLastReport < MOTION_REPORT_MS) return;
  motionLatest = MotionReport{};
  motionLatest.sequence = motionHistoryCount ? motionReports[(motionHead + MOTION_HISTORY_SIZE - 1) % MOTION_HISTORY_SIZE].sequence + 1 : 1;
  motionLatest.sampleMs = now;
  motionLatest.samples = motionSamples;
  motionLatest.ok = motionReady && !motionWindowFailed && motionSamples > 0 && now - motionLastReport <= MOTION_REPORT_MS + 200;
  motionLatest.error = motionLatest.ok ? "" : motionWindowFailed || !motionReady ? motionError : "No samples or delayed one-second report";
  for (unsigned i = 0; i < 3; ++i) {
    motionLatest.mean[i] = motionSamples ? motionSum[i] / motionSamples : 0;
    motionLatest.minimum[i] = motionMin[i]; motionLatest.maximum[i] = motionMax[i];
    motionSum[i] = 0;
  }
  motionReports[motionHead] = motionLatest;
  motionHead = (motionHead + 1) % MOTION_HISTORY_SIZE;
  if (motionHistoryCount < MOTION_HISTORY_SIZE) ++motionHistoryCount;
  if (motionLatest.ok) Serial.printf("[mpu %lu, n=%u] mean X=%.3f Y=%.3f Z=%.3f m/s^2\n",
    (unsigned long)motionLatest.sequence, motionSamples, motionLatest.mean[0], motionLatest.mean[1], motionLatest.mean[2]);
  else Serial.printf("[mpu %lu, n=%u] %s\n", (unsigned long)motionLatest.sequence, motionSamples, motionLatest.error);
  motionSamples = 0; motionWindowFailed = false; motionLastReport = now;
}

/* Drain bounded FIFO batches on notification, with a 5 ms safety poll for missed IRQ wiring. */
void acquireMotion() {
  if (motionPaused) return;
  const uint32_t now = millis();
  // Discard sensor startup samples rather than mixing the settling period into a valid mean.
  if (motionReady && !motionSettled) {
    if (now - motionStartedAt < 100) return;
    motionSettled = true;
    if (!resetMotionFifo()) motionFault("MPU6050 startup FIFO reset failed");
    motionLastReport = now;
  }
  bool pending;
  portENTER_CRITICAL(&motionMux);
  pending = motionInterruptPending; motionInterruptPending = false;
  portEXIT_CRITICAL(&motionMux);
  if (motionReady && (pending || now - motionLastService >= 5)) {
    motionLastService = now;
    uint8_t status, countBytes[2];
    if (!motionRead(0x3A, &status, 1) || !motionRead(0x72, countBytes, 2)) {
      motionFault("MPU6050 I2C status/count read failed");
    } else {
      const unsigned bytes = (unsigned(countBytes[0]) << 8) | countBytes[1];
      if ((status & 0x10) || bytes >= 1024) {
        ++motionOverflowCount;
        motionFault("FIFO overflow; samples lost and FIFO reset");
      } else {
        // At 100 kHz, this worst-case batch takes about 12 ms; normal batches are smaller.
        const unsigned frames = min(bytes / 6, 20u);
        if (frames) {
          uint8_t data[120];
          if (!motionRead(0x74, data, frames * 6)) motionFault("MPU6050 FIFO read failed");
          else for (unsigned frame = 0; frame < frames; ++frame) {
            for (unsigned axis = 0; axis < 3; ++axis) {
              const unsigned offset = frame * 6 + axis * 2;
              const int16_t raw = int16_t((uint16_t(data[offset]) << 8) | data[offset + 1]);
              const float value = raw * (9.80665f / 16384.0f); // +/-2 g, includes gravity.
              motionSum[axis] += value;
              if (!motionSamples || value < motionMin[axis]) motionMin[axis] = value;
              if (!motionSamples || value > motionMax[axis]) motionMax[axis] = value;
            }
            ++motionSamples;
          }
        }
        // Keep draining a backlog on the next loop pass even if the IRQ was coalesced.
        if (bytes / 6 > frames) {
          portENTER_CRITICAL(&motionMux); motionInterruptPending = true; portEXIT_CRITICAL(&motionMux);
        }
      }
    }
  }
  publishMotion(millis());
}

/* Serialize channel arrays with nulls on failure; min/max remain available without sending raw samples. */
void motionArray(String &json, const float *values, bool valid) {
  json += '[';
  for (unsigned i = 0; i < 3; ++i) { if (i) json += ','; json += valid ? String(values[i], 3) : String("null"); }
  json += ']';
}

/* Expose measured sample counts and IRQ/overflow diagnostics rather than claiming the target rate was achieved. */
void appendMotion(String &json, uint32_t now) {
  uint32_t interrupts;
  portENTER_CRITICAL(&motionMux); interrupts = motionInterrupts; portEXIT_CRITICAL(&motionMux);
  const bool valid = motionLatest.sequence && motionLatest.ok && now - motionLatest.sampleMs <= MOTION_REPORT_MS + 1500;
  json += ",\"motion\":{\"sequence\":" + String(motionLatest.sequence) + ",\"sample_age_ms\":" + String(now - motionLatest.sampleMs);
  json += ",\"ok\":"; json += valid ? "true" : "false";
  json += ",\"error\":\""; json += motionLatest.ok && !valid ? "Motion report stale" : motionLatest.sequence ? motionLatest.error : motionError;
  json += "\",\"warning\":\""; if (motionReady && !interrupts && now - motionStartedAt > 1000) json += "No INT edges; check GPIO17 wiring (FIFO polling fallback active)";
  json += "\",\"sample_count\":" + String(motionLatest.samples) + ",\"target_hz\":100,\"publish_interval_ms\":1000";
  json += ",\"interrupts_seen\":" + String(interrupts) + ",\"fifo_overflows\":" + String(motionOverflowCount);
  json += ",\"acceleration_m_s2\":"; motionArray(json, motionLatest.mean, valid);
  json += ",\"minimum_m_s2\":"; motionArray(json, motionLatest.minimum, valid);
  json += ",\"maximum_m_s2\":"; motionArray(json, motionLatest.maximum, valid);
  json += '}';
}

/* Serve small incremental batches separately from the large, slow environmental-history endpoint. */
void sendMotionReadings() {
  const uint32_t now = millis();
  uint32_t since = server.hasArg("since") ? strtoul(server.arg("since").c_str(), nullptr, 10) : 0;
  if (since > motionLatest.sequence) since = 0;
  String json; json.reserve(5000);
  json = "{\"uptime_ms\":" + String(now);
  appendMotion(json, now);
  json += ",\"history\":[";
  unsigned sent = 0;
  for (unsigned i = 0; i < motionHistoryCount && sent < 20; ++i) {
    const MotionReport &report = motionReports[(motionHead + MOTION_HISTORY_SIZE - motionHistoryCount + i) % MOTION_HISTORY_SIZE];
    if (report.sequence <= since) continue;
    if (sent++) json += ',';
    json += "{\"sequence\":" + String(report.sequence) + ",\"age_ms\":" + String(now - report.sampleMs) + ",\"ok\":";
    json += report.ok ? "true" : "false";
    json += ",\"a\":"; motionArray(json, report.mean, report.ok); json += '}';
  }
  json += "]}";
  server.sendHeader("Cache-Control", "no-store"); server.send(200, "application/json", json);
}


