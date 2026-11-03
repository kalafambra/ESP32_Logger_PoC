#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <WebServer.h>
#include <Preferences.h>
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
float motionGyroSum[3] = {}, motionGyroMean[3] = {};
float motionGyroBias[3] = {};
bool motionGyroCalibrating = false;
double gyroCalibrationSum[3] = {}, gyroCalibrationSquares[3] = {};
bool motionReady = false, motionPaused = false, motionWindowFailed = false, motionSettled = false;
uint32_t motionLastReport = 0, motionLastService = 0, motionOverflowCount = 0;
uint32_t motionLastDiagnostic = 0, motionStartedAt = 0;
const char *motionError = "MPU6050 not initialized";
portMUX_TYPE motionMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool motionInterruptPending = false;
volatile uint32_t motionInterrupts = 0;
float motionBias[3] = {};
bool motionCalibrating = false;
unsigned motionCalibrationSamples = 0;
double motionCalibrationSum[3] = {}, motionCalibrationSquares[3] = {};
const char *motionCalibrationError = "";

/* Explicit sign extension keeps both FIFO and direct-register decoding identical. */
constexpr int32_t decodeMotionRaw(uint8_t high, uint8_t low) {
  return ((uint32_t(high) << 8) | low) >= 32768 ?
         int32_t((uint32_t(high) << 8) | low) - 65536 : int32_t((uint32_t(high) << 8) | low);
}
static_assert(decodeMotionRaw(0x40, 0x00) == 16384, "+1 g decoding");
static_assert(decodeMotionRaw(0xC0, 0x00) == -16384, "-1 g decoding");
static_assert(decodeMotionRaw(0xFF, 0xFF) == -1, "negative sign extension");
static_assert(decodeMotionRaw(0x80, 0x00) == -32768, "negative full scale");

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

/* Reset FIFO framing: accel XYZ then gyro XYZ, twelve bytes with temperature excluded. */
bool resetMotionFifo() {
  return motionWrite(0x23, 0x00) && motionWrite(0x6A, 0x00) &&
         motionWrite(0x6A, 0x04) && motionWrite(0x6A, 0x40) && motionWrite(0x23, 0x78);
}

/* A CPU/OTA restart does not power-cycle the sensor: reset its registers and signal paths explicitly. */
bool resetMotionDevice() {
  if (!motionWrite(0x6B, 0x80)) return false;
  // These bounded waits run only in setup, following the vendor/library reset sequence.
  delay(100);
  uint8_t power = 0x80;
  for (unsigned attempt = 0; attempt < 20; ++attempt) {
    if (!motionRead(0x6B, &power, 1)) return false;
    if (!(power & 0x80)) break;
    delay(10);
  }
  if (power & 0x80) return false;
  if (!motionWrite(0x68, 0x07)) return false; // Reset accel, gyro and temperature signal paths.
  delay(100);
  if (!motionWrite(0x6B, 0x01) || !motionWrite(0x6C, 0x00)) return false;
  delay(100); // Let the PLL and enabled sensor axes settle before configuration.
  return true;
}

/* Verify scale, packet layout and timing rather than trusting acknowledged writes alone. */
bool verifyMotionConfiguration() {
  const uint8_t regs[] = {0x19, 0x1A, 0x1B, 0x1C, 0x23, 0x37, 0x38, 0x6A, 0x6B, 0x6C};
  const uint8_t expected[] = {9, 3, 0, 0, 0x78, 0x20, 0x11, 0x40, 1, 0};
  for (unsigned i = 0; i < sizeof(regs); ++i) {
    uint8_t actual;
    if (!motionRead(regs[i], &actual, 1)) return false;
    if (actual != expected[i]) {
      Serial.printf("[mpu] Configuration mismatch reg=0x%02X actual=0x%02X expected=0x%02X\n", regs[i], actual, expected[i]);
      return false;
    }
  }
  return true;
}

/* Configure 100 Hz acceleration acquisition, using FIFO to absorb brief loop delays. */
void setupMotion() {
  uint8_t identity = 0;
  pinMode(MPU_INTERRUPT_PIN, INPUT_PULLDOWN); // INT is configured as active-high push-pull.
  attachInterrupt(digitalPinToInterrupt(MPU_INTERRUPT_PIN), motionDataReadyISR, RISING);
  motionReady = motionRead(0x75, &identity, 1) && identity == 0x68;
  if (motionReady) motionReady = resetMotionDevice();
  if (motionReady) {
    // DLPF=3 keeps acceleration bandwidth at 44 Hz, below the 50 Hz Nyquist limit.
    // The filtered internal clock is 1 kHz; divider=9 yields 100 Hz.
    motionReady = motionWrite(0x38, 0x00) && motionWrite(0x6B, 0x01) &&
                  motionWrite(0x1A, 0x03) && motionWrite(0x19, 0x09) &&
                  motionWrite(0x1B, 0x00) && motionWrite(0x1C, 0x00) && motionWrite(0x37, 0x20) &&
                  resetMotionFifo() && motionWrite(0x38, 0x11) && verifyMotionConfiguration();
    // INT_PIN_CFG: latch high until INT_STATUS is read. Enable ready + overflow IRQs.
  }
  motionError = motionReady ? "Waiting for FIFO samples" : "MPU6050 initialization failed at 0x68; check wiring and reset";
  // Restore only explicitly measured calibration; never assume a boot-time pose.
  Preferences calibration;
  if (calibration.begin("mpu6050", true)) {
    float saved[3];
    if (calibration.getBytesLength("bias-v1") == sizeof(saved) &&
        calibration.getBytes("bias-v1", saved, sizeof(saved)) == sizeof(saved)) {
      bool valid = true;
      for (float value : saved) if (!isfinite(value) || fabsf(value) > 4.0f) valid = false;
      if (valid) for (unsigned i = 0; i < 3; ++i) motionBias[i] = saved[i];
    }
    float savedGyro[3];
    if(calibration.getBytesLength("gyro-v1")==sizeof(savedGyro)&&calibration.getBytes("gyro-v1",savedGyro,sizeof(savedGyro))==sizeof(savedGyro)){
      bool valid=true;for(float value:savedGyro)if(!isfinite(value)||fabsf(value)>0.35f)valid=false;
      if(valid)for(unsigned i=0;i<3;++i)motionGyroBias[i]=savedGyro[i];
    }
    calibration.end();
  }
  motionStartedAt = motionLastReport = millis();
  Serial.printf("[mpu] %s; INT=GPIO%d, report=1000 ms\n", motionReady ? "FIFO acquisition configured for 100 Hz" : motionError, MPU_INTERRUPT_PIN);
}

/* Suspend FIFO during OTA, since a firmware transfer intentionally blocks acquisition. */
void pauseMotion() {
  invalidatePickup("Acquisition paused for OTA; active alert retained");
  if (motionCalibrating) { motionCalibrating = false; motionCalibrationError = "Calibration cancelled by OTA"; }
  motionPaused = true;
  if (motionReady) { motionWrite(0x38, 0); motionWrite(0x23, 0); motionWrite(0x6A, 0); }
}

/* Recover from a failed OTA transfer without mixing pre-transfer and post-transfer samples. */
void resumeMotion() {
  motionSamples = 0;
  for (unsigned i = 0; i < 3; ++i) motionSum[i] = motionGyroSum[i] = 0;
  motionWindowFailed = true;
  motionError = "Acquisition interrupted by OTA";
  motionLastReport = millis();
  if (motionReady && (!resetMotionFifo() || !motionWrite(0x38, 0x11))) motionReady = false;
  motionPaused = false;
}

/* Reset framing on bus/overflow faults and mark this report as incomplete rather than averaging bad data. */
void motionFault(const char *message) {
  invalidatePickup(message);
  if (motionCalibrating) { motionCalibrating = false; motionCalibrationError = "Calibration cancelled by sensor read error"; }
  motionWindowFailed = true;
  motionError = message;
  if (!resetMotionFifo()) motionError = "MPU6050 I2C/FIFO recovery failed";
}

/* Measure zero-rate bias in any stationary pose; acceleration variance rejects visible movement. */
void finishGyroCalibration() {
  motionCalibrating=false;
  float proposed[3];
  for(unsigned i=0;i<3;++i){
    const double a=motionCalibrationSum[i]/motionCalibrationSamples;
    const double g=gyroCalibrationSum[i]/motionCalibrationSamples;
    if(motionCalibrationSquares[i]/motionCalibrationSamples-a*a>0.0225||
       gyroCalibrationSquares[i]/motionCalibrationSamples-g*g>0.0001){motionCalibrationError="Module moved; keep still during gyro calibration";return;}
    proposed[i]=g;
    if(fabsf(proposed[i])>0.35f){motionCalibrationError="Gyro bias too large; confirm stationary module";return;}
  }
  Preferences prefs;
  if(!prefs.begin("mpu6050",false)){motionCalibrationError="Cannot open calibration storage";return;}
  const bool saved=prefs.putBytes("gyro-v1",proposed,sizeof(proposed))==sizeof(proposed);prefs.end();
  if(!saved){motionCalibrationError="Cannot save gyro calibration";return;}
  for(unsigned i=0;i<3;++i){motionGyroBias[i]=proposed[i];motionGyroSum[i]=motionSum[i]=0;}
  motionSamples=motionHistoryCount=motionHead=0;motionLatest=MotionReport{};
  motionLastReport=millis();motionCalibrationError="";
  invalidatePickup("Gyro calibration changed; learning fresh residual bias");
  Serial.printf("[mpu] Saved gyro zero-rate bias: %.4f %.4f %.4f deg/s\n",proposed[0]*57.2957795f,proposed[1]*57.2957795f,proposed[2]*57.2957795f);
}

/* Measure constant bias only in the explicitly requested stationary, flat, Z-up pose. */
void finishMotionCalibration() {
  if(motionGyroCalibrating){finishGyroCalibration();return;}
  motionCalibrating = false;
  float mean[3], proposed[3];
  for (unsigned i = 0; i < 3; ++i) {
    mean[i] = motionCalibrationSum[i] / motionCalibrationSamples;
    const double variance = motionCalibrationSquares[i] / motionCalibrationSamples - double(mean[i]) * mean[i];
    if (variance > 0.0225) { motionCalibrationError = "Module moved; keep stationary for calibration"; return; }
    proposed[i] = mean[i] - (i == 2 ? 9.80665f : 0.0f);
    if (fabsf(proposed[i]) > 4.0f) { motionCalibrationError = "Bias too large; check flat Z-up pose or sensor hardware"; return; }
  }
  if (fabsf(mean[0]) > 2 || fabsf(mean[1]) > 2 || mean[2] < 4.9f) {
    motionCalibrationError = "Place module flat with positive Z pointing up"; return;
  }
  Preferences calibration;
  if (!calibration.begin("mpu6050", false)) { motionCalibrationError = "Cannot open calibration storage"; return; }
  const bool stored = calibration.putBytes("bias-v1", proposed, sizeof(proposed)) == sizeof(proposed);
  calibration.end();
  if (!stored) { motionCalibrationError = "Cannot save calibration"; return; }
  for (unsigned i = 0; i < 3; ++i) { motionBias[i] = proposed[i]; motionSum[i] = motionGyroSum[i] = 0; }
  // Clear old uncorrected motion history and the window straddling the change.
  motionSamples = 0; motionHistoryCount = 0; motionHead = 0; motionLatest = MotionReport{};
  motionLastReport = millis(); motionCalibrationError = "";
  invalidatePickup("Calibration changed; waiting for fresh stationary baseline");
  Serial.printf("[mpu] Flat Z-up calibration saved: bias %.4f %.4f %.4f m/s^2\n", motionBias[0], motionBias[1], motionBias[2]);
}

/* Start five seconds of raw samples on explicit request; a reboot never triggers calibration. */
void startMotionCalibration() {
  if (!motionReady || motionPaused || motionCalibrating) { server.send(409, "application/json", "{\"error\":\"Sensor unavailable or calibration already running\"}"); return; }
  motionCalibrationSamples = 0;
  motionGyroCalibrating=false;
  for (unsigned i = 0; i < 3; ++i) motionCalibrationSum[i] = motionCalibrationSquares[i] = 0;
  motionCalibrationError = ""; motionCalibrating = true;
  invalidatePickup("Accelerometer calibration in progress");
  server.send(202, "application/json", "{\"status\":\"Keep module flat, stationary, positive Z up for five seconds\"}");
}

/* Gyro calibration requires stillness, not a specific orientation, and never changes accelerometer offsets. */
void startGyroCalibration() {
  if(!motionReady||motionPaused||motionCalibrating){server.send(409,"application/json","{\"error\":\"Sensor unavailable or calibration already running\"}");return;}
  motionCalibrationSamples=0;
  for(unsigned i=0;i<3;++i)motionCalibrationSum[i]=motionCalibrationSquares[i]=gyroCalibrationSum[i]=gyroCalibrationSquares[i]=0;
  motionCalibrationError="";motionGyroCalibrating=motionCalibrating=true;
  invalidatePickup("Gyroscope calibration in progress");
  server.send(202,"application/json","{\"status\":\"Keep module completely stationary for five seconds\"}");
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
    // Display sensor-frame gyro means separately from the detector's learned zero-rate bias.
    motionGyroMean[i] = motionSamples ? motionGyroSum[i] / motionSamples : 0;
    motionGyroSum[i] = 0;
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
        const unsigned frames = min(bytes / 12, 10u);
        if (frames) {
          uint8_t data[120];
          if (!motionRead(0x74, data, frames * 12)) motionFault("MPU6050 FIFO read failed");
          else for (unsigned frame = 0; frame < frames; ++frame) {
            float detectorA[3], detectorW[3];
            const bool wasCalibrating = motionCalibrating;
            for (unsigned axis = 0; axis < 3; ++axis) {
              const unsigned offset = frame * 12 + axis * 2;
              const int32_t raw = decodeMotionRaw(data[offset], data[offset + 1]);
              const float value = raw * (9.80665f / 16384.0f); // +/-2 g, includes gravity.
              if (motionCalibrating) { motionCalibrationSum[axis] += value; motionCalibrationSquares[axis] += double(value) * value; }
              const float corrected = value - motionBias[axis]; // Constant bias subtraction, never normalization/clipping.
              detectorA[axis] = corrected;
              const int32_t gyro = decodeMotionRaw(data[offset+6],data[offset+7]);
              detectorW[axis] = gyro * (3.14159265358979323846f / (180.0f * 131.0f)); // +/-250 deg/s -> rad/s.
              if(motionCalibrating&&motionGyroCalibrating){gyroCalibrationSum[axis]+=detectorW[axis];gyroCalibrationSquares[axis]+=double(detectorW[axis])*detectorW[axis];}
              detectorW[axis]-=motionGyroBias[axis]; // Persisted zero-rate correction, also used by pickup baseline learning.
              motionGyroSum[axis] += detectorW[axis];
              motionSum[axis] += corrected;
              if (!motionSamples || corrected < motionMin[axis]) motionMin[axis] = corrected;
              if (!motionSamples || corrected > motionMax[axis]) motionMax[axis] = corrected;
            }
            ++motionSamples;
            if (motionCalibrating && ++motionCalibrationSamples >= 500) finishMotionCalibration();
            if (!wasCalibrating) processPickupSample(detectorA,detectorW,millis());
          }
        }
        // Keep draining a backlog on the next loop pass even if the IRQ was coalesced.
        if (bytes / 12 > frames) {
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
  json += ",\"gyroscope_rad_s\":"; motionArray(json, motionGyroMean, valid);
  float degrees[3],rawDegrees[3],biasDegrees[3];
  for(unsigned i=0;i<3;++i){degrees[i]=motionGyroMean[i]*57.2957795f;biasDegrees[i]=motionGyroBias[i]*57.2957795f;rawDegrees[i]=degrees[i]+biasDegrees[i];}
  json+=",\"gyroscope_deg_s\":";motionArray(json,degrees,valid);
  json+=",\"gyroscope_raw_deg_s\":";motionArray(json,rawDegrees,valid);
  json+=",\"gyro_bias_deg_s\":";motionArray(json,biasDegrees,true);
  json+=",\"calibration_kind\":\"";json+=motionGyroCalibrating?"gyro":"accel";json+='"';
  json += ",\"minimum_m_s2\":"; motionArray(json, motionLatest.minimum, valid);
  json += ",\"maximum_m_s2\":"; motionArray(json, motionLatest.maximum, valid);
  json += ",\"bias_m_s2\":"; motionArray(json, motionBias, true);
  json += ",\"calibrating\":"; json += motionCalibrating ? "true" : "false";
  json += ",\"calibration_samples\":" + String(motionCalibrationSamples);
  json += ",\"calibration_error\":\""; json += motionCalibrationError; json += '"';
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
  appendPickup(json);
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

/* Compare direct sensor registers with FIFO output without changing scale, offsets or gravity. */
void sendMotionDiagnostics() {
  uint8_t raw[6], range = 0;
  const bool ok = motionRead(0x1C, &range, 1) && motionRead(0x3B, raw, sizeof(raw));
  String json = "{\"ok\":"; json += ok ? "true" : "false";
  json += ",\"accel_config\":" + String(range);
  const float scales[] = {16384, 8192, 4096, 2048};
  const float scale = scales[(range >> 3) & 3];
  json += ",\"counts_per_g\":" + String(scale, 0) + ",\"raw_xyz\":[";
  float direct[3] = {};
  for (unsigned i = 0; i < 3; ++i) {
    if (i) json += ',';
    if (!ok) json += "null";
    else {
      const int32_t value = decodeMotionRaw(raw[2 * i], raw[2 * i + 1]);
      json += String(value);
      direct[i] = value * (9.80665f / scale);
    }
  }
  json += "],\"direct_m_s2\":"; motionArray(json, direct, ok);
  json += ",\"direct_magnitude_m_s2\":";
  json += ok ? String(sqrtf(direct[0]*direct[0] + direct[1]*direct[1] + direct[2]*direct[2]), 3) : String("null");
  float corrected[3]; for (unsigned i = 0; i < 3; ++i) corrected[i] = direct[i] - motionBias[i];
  json += ",\"corrected_direct_m_s2\":"; motionArray(json, corrected, ok);
  json += ",\"registers\":{";
  const uint8_t regs[] = {0x19, 0x1A, 0x1C, 0x23, 0x6A, 0x6B, 0x6C, 0x75};
  for (unsigned i = 0; i < sizeof(regs); ++i) {
    if (i) json += ',';
    uint8_t value;
    json += "\"0x" + String(regs[i], HEX) + "\":";
    json += motionRead(regs[i], &value, 1) ? String(value) : String("null");
  }
  json += "}}";
  server.sendHeader("Cache-Control", "no-store"); server.send(200, "application/json", json);
}


