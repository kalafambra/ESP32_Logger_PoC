#include "motion.h"
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include "config.h"
#include "pickup_service.h"
#include "acquisition_timing.h"
#include "i2c_bus_service.h"

namespace {

constexpr uint8_t MPU_ADDRESS = 0x68; // AD0 tied to GND.
MotionReport motionLatest, motionReports[MOTION_HISTORY_SIZE];
unsigned motionHead = 0, motionHistoryCount = 0, motionSamples = 0;
float motionSum[3] = {}, motionMin[3] = {}, motionMax[3] = {};
float motionGyroSum[3] = {}, motionGyroMean[3] = {};
float motionGyroBias[3] = {};
bool motionGyroCalibrating = false;
double gyroCalibrationSum[3] = {}, gyroCalibrationSquares[3] = {};
bool motionReady = false, motionPaused = false, motionWindowFailed = false, motionSettled = false;
uint32_t motionLastReport = 0, motionLastService = 0, motionOverflowCount = 0;
uint32_t motionStartedAt = 0;
uint32_t lastProcessedMotionMs = 0;
const char *motionError = "MPU6050 not initialized";
portMUX_TYPE motionMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool motionInterruptPending = false;
volatile uint32_t motionInterrupts = 0;
float motionBias[3] = {};
bool motionCalibrating = false;
unsigned motionCalibrationSamples = 0;
uint32_t motionCalibrationStartedMs = 0;
double motionCalibrationSum[3] = {}, motionCalibrationSquares[3] = {};
const char *motionCalibrationError = "";
const char *motionProcessingError = "";
float motionTemperatureC = 0;
bool motionTemperatureOk = false;
uint32_t motionTemperatureMs = 0;
const char *motionTemperatureError = "Waiting for MPU6050 temperature";
uint32_t motionReadErrors = 0, motionWriteErrors = 0;
uint8_t motionFailedRegister = 0, motionFailedCount = 0, motionWriteStatus = 0;
unsigned motionReturnedBytes = 0;

/* Explicit sign extension keeps both FIFO and direct-register decoding identical. */
constexpr int32_t decodeMotionRaw(uint8_t high, uint8_t low) {
  return ((uint32_t(high) << 8) | low) >= 32768 ? int32_t((uint32_t(high) << 8) | low) - 65536
                                                : int32_t((uint32_t(high) << 8) | low);
}

/* TEMP_OUT is signed; use the manufacturer's factory conversion without an ambient-temperature
 * offset. */
constexpr float decodeMotionTemperature(int32_t raw) {
  return raw / 340.0f + 36.53f;
}

/* Signal work only: I2C, allocation and serial output must never run in the ISR. */
void ARDUINO_ISR_ATTR motionDataReadyISR() {
  portENTER_CRITICAL_ISR(&motionMux);
  motionInterruptPending = true;
  ++motionInterrupts;
  portEXIT_CRITICAL_ISR(&motionMux);
  notifyAcquisitionFromISR();
}

/* Keep register reads on the shared bus in normal task context, checking short replies.
 * Use STOP between register selection and read: the MPU retains its register
 * pointer. This avoids the combined transmit/receive path that panicked in the
 * installed ESP32 I2C driver during repeated FIFO read failures on this board.
 */
bool motionRead(uint8_t reg, uint8_t *data, uint8_t count) {
  if (!I2C_SENSORS_ENABLED)
    return false;
  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(reg);
  const uint8_t status = Wire.endTransmission(true);
  const unsigned received = status == 0 ? Wire.requestFrom(MPU_ADDRESS, count) : 0;
  if (status != 0 || received != count) {
    ++motionReadErrors;
    motionFailedRegister = reg;
    motionFailedCount = count;
    motionWriteStatus = status;
    motionReturnedBytes = received;
    return false;
  }
  for (uint8_t i = 0; i < count; ++i)
    data[i] = Wire.read();
  return true;
}

/* Acknowledge each configuration write rather than silently using unknown sensor settings. */
bool motionWrite(uint8_t reg, uint8_t value) {
  if (!I2C_SENSORS_ENABLED)
    return false;
  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  const uint8_t status = Wire.endTransmission();
  if (status != 0) {
    ++motionWriteErrors;
    motionFailedRegister = reg;
    motionFailedCount = 0;
    motionWriteStatus = status;
    motionReturnedBytes = 0;
  }
  return status == 0;
}

/* Reset FIFO framing: accel XYZ then gyro XYZ, twelve bytes with temperature excluded. */
bool resetMotionFifo() {
  return motionWrite(0x23, 0x00) && motionWrite(0x6A, 0x00) && motionWrite(0x6A, 0x04) &&
         motionWrite(0x6A, 0x40) && motionWrite(0x23, 0x78);
}

/* A CPU/OTA restart does not power-cycle the sensor: reset its registers and signal paths
 * explicitly. */
bool resetMotionDevice() {
  if (!motionWrite(0x6B, 0x80))
    return false;
  // These bounded waits run only in setup, following the vendor/library reset sequence.
  delay(100);
  uint8_t power = 0x80;
  for (unsigned attempt = 0; attempt < 20; ++attempt) {
    if (!motionRead(0x6B, &power, 1))
      return false;
    if (!(power & 0x80))
      break;
    delay(10);
  }
  if (power & 0x80)
    return false;
  if (!motionWrite(0x68, 0x07))
    return false; // Reset accel, gyro and temperature signal paths.
  delay(100);
  if (!motionWrite(0x6B, 0x01) || !motionWrite(0x6C, 0x00))
    return false;
  delay(100); // Let the PLL and enabled sensor axes settle before configuration.
  return true;
}

/* Verify scale, packet layout and timing rather than trusting acknowledged writes alone. */
bool verifyMotionConfiguration() {
  const uint8_t regs[] = {0x19, 0x1A, 0x1B, 0x1C, 0x23, 0x37, 0x38, 0x6A, 0x6B, 0x6C};
  const uint8_t expected[] = {9, 3, 0, 0, 0x78, 0x20, 0x11, 0x40, 1, 0};
  for (unsigned i = 0; i < sizeof(regs); ++i) {
    uint8_t actual;
    if (!motionRead(regs[i], &actual, 1))
      return false;
    if (actual != expected[i]) {
      Serial.printf("[mpu] Configuration mismatch reg=0x%02X actual=0x%02X expected=0x%02X\n",
                    regs[i], actual, expected[i]);
      return false;
    }
  }
  return true;
}

/* Reset framing on bus/overflow faults and mark this report as incomplete rather than averaging bad
 * data. */
void motionFault(const char *message) {
  SensorPacket packet;
  packet.kind = SensorPacketKind::MotionFault;
  packet.error = message;
  packet.sampleMs = millis();
  enqueueSensorPacket(packet);
  motionError = message;
  if (!resetMotionFifo())
    motionError = "MPU6050 I2C/FIFO recovery failed";
}

/* Measure zero-rate bias in any stationary pose; acceleration variance rejects visible movement. */
void finishGyroCalibration() {
  motionCalibrating = false;
  float proposed[3];
  for (unsigned i = 0; i < 3; ++i) {
    const double a = motionCalibrationSum[i] / motionCalibrationSamples;
    const double g = gyroCalibrationSum[i] / motionCalibrationSamples;
    if (motionCalibrationSquares[i] / motionCalibrationSamples - a * a > 0.0225 ||
        gyroCalibrationSquares[i] / motionCalibrationSamples - g * g > 0.0001) {
      motionCalibrationError = "Module moved; keep still during gyro calibration";
      return;
    }
    proposed[i] = g;
    if (fabsf(proposed[i]) > 0.35f) {
      motionCalibrationError = "Gyro bias too large; confirm stationary module";
      return;
    }
  }
  Preferences prefs;
  if (!prefs.begin("mpu6050", false)) {
    motionCalibrationError = "Cannot open calibration storage";
    return;
  }
  const bool saved = prefs.putBytes("gyro-v1", proposed, sizeof(proposed)) == sizeof(proposed);
  prefs.end();
  if (!saved) {
    motionCalibrationError = "Cannot save gyro calibration";
    return;
  }
  for (unsigned i = 0; i < 3; ++i) {
    motionGyroBias[i] = proposed[i];
    motionGyroSum[i] = motionSum[i] = 0;
  }
  motionSamples = motionHistoryCount = motionHead = 0;
  motionLatest = MotionReport{};
  motionLastReport = millis();
  motionCalibrationError = "";
  invalidatePickup("Gyro calibration changed; learning fresh residual bias");
  Serial.printf("[mpu] Saved gyro zero-rate bias: %.4f %.4f %.4f deg/s\n",
                proposed[0] * 57.2957795f, proposed[1] * 57.2957795f, proposed[2] * 57.2957795f);
}

/* Measure constant bias only in the explicitly requested stationary, flat, Z-up pose. */
void finishMotionCalibration() {
  if (motionGyroCalibrating) {
    finishGyroCalibration();
    return;
  }
  motionCalibrating = false;
  float mean[3], proposed[3];
  for (unsigned i = 0; i < 3; ++i) {
    mean[i] = motionCalibrationSum[i] / motionCalibrationSamples;
    const double variance =
        motionCalibrationSquares[i] / motionCalibrationSamples - double(mean[i]) * mean[i];
    if (variance > 0.0225) {
      motionCalibrationError = "Module moved; keep stationary for calibration";
      return;
    }
    proposed[i] = mean[i] - (i == 2 ? 9.80665f : 0.0f);
    if (fabsf(proposed[i]) > 4.0f) {
      motionCalibrationError = "Bias too large; check flat Z-up pose or sensor hardware";
      return;
    }
  }
  if (fabsf(mean[0]) > 2 || fabsf(mean[1]) > 2 || mean[2] < 4.9f) {
    motionCalibrationError = "Place module flat with positive Z pointing up";
    return;
  }
  Preferences calibration;
  if (!calibration.begin("mpu6050", false)) {
    motionCalibrationError = "Cannot open calibration storage";
    return;
  }
  const bool stored =
      calibration.putBytes("bias-v1", proposed, sizeof(proposed)) == sizeof(proposed);
  calibration.end();
  if (!stored) {
    motionCalibrationError = "Cannot save calibration";
    return;
  }
  for (unsigned i = 0; i < 3; ++i) {
    motionBias[i] = proposed[i];
    motionSum[i] = motionGyroSum[i] = 0;
  }
  // Clear old uncorrected motion history and the window straddling the change.
  motionSamples = 0;
  motionHistoryCount = 0;
  motionHead = 0;
  motionLatest = MotionReport{};
  motionLastReport = millis();
  motionCalibrationError = "";
  invalidatePickup("Calibration changed; waiting for fresh stationary baseline");
  Serial.printf("[mpu] Flat Z-up calibration saved: bias %.4f %.4f %.4f m/s^2\n", motionBias[0],
                motionBias[1], motionBias[2]);
}

/* Publish a one-second aggregate instead of flooding serial and Wi-Fi with 100 raw frames/s. */
void publishMotion(uint32_t now) {
  if (int32_t(now - motionLastReport) < int32_t(MOTION_REPORT_MS))
    return;
  motionLatest = MotionReport{};
  motionLatest.sequence =
      motionHistoryCount
          ? motionReports[(motionHead + MOTION_HISTORY_SIZE - 1) % MOTION_HISTORY_SIZE].sequence + 1
          : 1;
  motionLatest.sampleMs = motionLastReport +
                         ((now - motionLastReport) / MOTION_REPORT_MS) * MOTION_REPORT_MS;
  motionLatest.samples = motionSamples;
  bool ready;
  const char *hardwareError;
  { SensorStateLock lock; ready = motionReady; hardwareError = motionError; }
  motionLatest.ok = ready && !motionWindowFailed && motionSamples > 0 &&
                    now - motionLastReport <= MOTION_REPORT_MS + 200;
  motionLatest.error = motionLatest.ok ? ""
                       : motionWindowFailed
                           ? motionProcessingError
                       : !ready ? hardwareError
                           : "No samples or delayed one-second report";
  for (unsigned i = 0; i < 3; ++i) {
    motionLatest.mean[i] = motionSamples ? motionSum[i] / motionSamples : 0;
    // Display sensor-frame gyro means separately from the detector's learned zero-rate bias.
    motionGyroMean[i] = motionSamples ? motionGyroSum[i] / motionSamples : 0;
    motionGyroSum[i] = 0;
    motionLatest.minimum[i] = motionMin[i];
    motionLatest.maximum[i] = motionMax[i];
    motionSum[i] = 0;
  }
  motionReports[motionHead] = motionLatest;
  motionHead = (motionHead + 1) % MOTION_HISTORY_SIZE;
  if (motionHistoryCount < MOTION_HISTORY_SIZE)
    ++motionHistoryCount;
  const MotionTemperature chip = motionTemperature();
  const String temperatureText = chip.ok ? String(chip.celsius, 2) : String("unavailable");
  if (motionLatest.ok)
    Serial.printf("[mpu %lu, n=%u] mean X=%.3f Y=%.3f Z=%.3f m/s^2 | chip T=%s C\n",
                  (unsigned long)motionLatest.sequence, motionSamples, motionLatest.mean[0],
                  motionLatest.mean[1], motionLatest.mean[2], temperatureText.c_str());
  else
    Serial.printf("[mpu %lu, n=%u] %s\n", (unsigned long)motionLatest.sequence, motionSamples,
                  motionLatest.error);
  motionSamples = 0;
  motionWindowFailed = false;
  motionLastReport += ((now - motionLastReport) / MOTION_REPORT_MS) * MOTION_REPORT_MS;
}

static_assert(decodeMotionRaw(0x40, 0x00) == 16384, "+1 g decoding");
static_assert(decodeMotionRaw(0xC0, 0x00) == -16384, "-1 g decoding");
static_assert(decodeMotionRaw(0xFF, 0xFF) == -1, "negative sign extension");
static_assert(decodeMotionRaw(0x80, 0x00) == -32768, "negative full scale");
static_assert(decodeMotionTemperature(-3400) > 26.529f && decodeMotionTemperature(-3400) < 26.531f,
              "negative temperature register decoding");
static_assert(decodeMotionTemperature(3400) > 46.529f && decodeMotionTemperature(3400) < 46.531f,
              "positive temperature register decoding");

} // namespace

/* Configure 100 Hz acceleration acquisition, using FIFO to absorb brief loop delays. */
void setupMotion() {
  if (!I2C_SENSORS_ENABLED)
    return;
  uint8_t identity = 0;
  pinMode(MPU_INTERRUPT_PIN, INPUT_PULLDOWN); // INT is configured as active-high push-pull.
  attachInterrupt(digitalPinToInterrupt(MPU_INTERRUPT_PIN), motionDataReadyISR, RISING);
  motionReady = motionRead(0x75, &identity, 1) && identity == 0x68;
  if (motionReady)
    motionReady = resetMotionDevice();
  if (motionReady) {
    // DLPF=3 keeps acceleration bandwidth at 44 Hz, below the 50 Hz Nyquist limit.
    // The filtered internal clock is 1 kHz; divider=9 yields 100 Hz.
    motionReady = motionWrite(0x38, 0x00) && motionWrite(0x6B, 0x01) && motionWrite(0x1A, 0x03) &&
                  motionWrite(0x19, 0x09) && motionWrite(0x1B, 0x00) && motionWrite(0x1C, 0x00) &&
                  motionWrite(0x37, 0x20) && resetMotionFifo() && motionWrite(0x38, 0x11) &&
                  verifyMotionConfiguration();
    // INT_PIN_CFG: latch high until INT_STATUS is read. Enable ready + overflow IRQs.
  }
  motionError = motionReady ? "Waiting for FIFO samples"
                            : "MPU6050 initialization failed at 0x68; check wiring and reset";
  // Restore only explicitly measured calibration; never assume a boot-time pose.
  Preferences calibration;
  if (calibration.begin("mpu6050", true)) {
    float saved[3];
    if (calibration.getBytesLength("bias-v1") == sizeof(saved) &&
        calibration.getBytes("bias-v1", saved, sizeof(saved)) == sizeof(saved)) {
      bool valid = true;
      for (float value : saved)
        if (!isfinite(value) || fabsf(value) > 4.0f)
          valid = false;
      if (valid)
        for (unsigned i = 0; i < 3; ++i)
          motionBias[i] = saved[i];
    }
    float savedGyro[3];
    if (calibration.getBytesLength("gyro-v1") == sizeof(savedGyro) &&
        calibration.getBytes("gyro-v1", savedGyro, sizeof(savedGyro)) == sizeof(savedGyro)) {
      bool valid = true;
      for (float value : savedGyro)
        if (!isfinite(value) || fabsf(value) > 0.35f)
          valid = false;
      if (valid)
        for (unsigned i = 0; i < 3; ++i)
          motionGyroBias[i] = savedGyro[i];
    }
    calibration.end();
  }
  motionStartedAt = motionLastReport = millis();
  Serial.printf("[mpu] %s; INT=GPIO%d, report=1000 ms\n",
                motionReady ? "FIFO acquisition configured for 100 Hz" : motionError,
                MPU_INTERRUPT_PIN);
}

/* One direct two-byte read per environmental report leaves the 12-byte motion FIFO layout intact.
 */
void acquireMotionTemperature(uint32_t now) {
  if (!I2C_SENSORS_ENABLED)
    return;
  motionTemperatureMs = now;
  motionTemperatureOk = false;
  if (!motionReady || motionPaused || !motionSettled) {
    motionTemperatureError = "MPU6050 temperature unavailable";
    return;
  }
  uint8_t raw[2];
  if (!motionRead(0x41, raw, 2)) {
    motionTemperatureError = "MPU6050 temperature I2C read failed";
    return;
  }
  motionTemperatureC = decodeMotionTemperature(decodeMotionRaw(raw[0], raw[1]));
  motionTemperatureOk = true;
  motionTemperatureError = "";
}

/* Suspend FIFO during OTA, since a firmware transfer intentionally blocks acquisition. */
void pauseMotion() {
  motionPaused = true;
  if (motionReady) {
    motionWrite(0x38, 0);
    motionWrite(0x23, 0);
    motionWrite(0x6A, 0);
  }
}

/* Recover from a failed OTA transfer without mixing pre-transfer and post-transfer samples. */
void resumeMotion() {
  if (motionReady && (!resetMotionFifo() || !motionWrite(0x38, 0x11)))
    motionReady = false;
  motionPaused = false;
}

/* Drain bounded FIFO batches on notification, with a 5 ms safety poll for missed IRQ wiring. */
void acquireMotion() {
  if (!I2C_SENSORS_ENABLED)
    return;
  if (motionPaused)
    return;
  ScopedServiceTiming timing(acquisitionTiming().motion);
  const uint32_t now = millis();
  // Discard sensor startup samples rather than mixing the settling period into a valid mean.
  if (motionReady && !motionSettled) {
    if (now - motionStartedAt < 100)
      return;
    motionSettled = true;
    if (!resetMotionFifo())
      motionFault("MPU6050 startup FIFO reset failed");
  }
  bool pending;
  portENTER_CRITICAL(&motionMux);
  pending = motionInterruptPending;
  motionInterruptPending = false;
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
          if (!motionRead(0x74, data, frames * 12))
            motionFault("MPU6050 FIFO read failed");
          else
            for (unsigned frame = 0; frame < frames; ++frame) {
              SensorPacket packet;
              packet.kind = SensorPacketKind::Motion;
              // Nominal cadence estimate anchored to FIFO-count retrieval; not hardware timestamps.
              packet.retrievedMs = now;
              packet.sampleMs = now - (bytes / 12 - 1 - frame) * 10;
              static uint32_t sequence = 0;
              packet.sequence = ++sequence;
              memcpy(packet.raw, data + frame * 12, 12);
              enqueueSensorPacket(packet);
              countMotionFrame();
            }
        }
        // Keep draining a backlog on the next loop pass even if the IRQ was coalesced.
        if (bytes / 12 > frames) {
          portENTER_CRITICAL(&motionMux);
          motionInterruptPending = true;
          portEXIT_CRITICAL(&motionMux);
        }
      }
    }
  }
}



void invalidateMotionProcessing(const char *message) {
  invalidatePickup(message);
  motionWindowFailed = true;
  motionProcessingError = message;
  if (motionCalibrating) {
    motionCalibrating = false;
    motionCalibrationError = "Calibration cancelled by acquisition discontinuity";
  }
}

void processMotionPacket(const SensorPacket &packet) {
  if (packet.kind == SensorPacketKind::MotionFault) {
    invalidateMotionProcessing(packet.error);
    return;
  }
  if (millis() - packet.sampleMs > pickupDetector().settings.max_gap_ms) {
    invalidateMotionProcessing("Queued motion samples too old for alert processing");
    return;
  }
  lastProcessedMotionMs = millis();
  // Publish before admitting a frame from the next nominal one-second window.
  publishMotion(packet.sampleMs);
  float detectorA[3], detectorW[3];
  const bool wasCalibrating = motionCalibrating;
  const bool collectCalibration = motionCalibrating &&
                                 int32_t(packet.sampleMs - motionCalibrationStartedMs) >= 0;
  for (unsigned axis = 0; axis < 3; ++axis) {
    const unsigned offset = axis * 2;
    const int32_t raw = decodeMotionRaw(packet.raw[offset], packet.raw[offset + 1]);
    const float value = raw * (9.80665f / 16384.0f); // +/-2 g, includes gravity.
    if (collectCalibration) {
      motionCalibrationSum[axis] += value;
      motionCalibrationSquares[axis] += double(value) * value;
    }
    const float corrected =
        value -
        motionBias[axis]; // Constant bias subtraction, never normalization/clipping.
    detectorA[axis] = corrected;
    const int32_t gyro = decodeMotionRaw(packet.raw[offset + 6], packet.raw[offset + 7]);
    detectorW[axis] =
        gyro * (3.14159265358979323846f / (180.0f * 131.0f)); // +/-250 deg/s -> rad/s.
    if (collectCalibration && motionGyroCalibrating) {
      gyroCalibrationSum[axis] += detectorW[axis];
      gyroCalibrationSquares[axis] += double(detectorW[axis]) * detectorW[axis];
    }
    detectorW[axis] -= motionGyroBias[axis]; // Persisted zero-rate correction, also
                                             // used by pickup baseline learning.
    motionGyroSum[axis] += detectorW[axis];
    motionSum[axis] += corrected;
    if (!motionSamples || corrected < motionMin[axis])
      motionMin[axis] = corrected;
    if (!motionSamples || corrected > motionMax[axis])
      motionMax[axis] = corrected;
  }
  ++motionSamples;
  if (collectCalibration && ++motionCalibrationSamples >= 500)
    finishMotionCalibration();
  if (!wasCalibrating)
    processPickupSample(detectorA, detectorW, packet.sampleMs);
}
void publishProcessedMotion(uint32_t now) {
  // Normal reports close on the next frame, never ahead of its estimated time.
  // Still publish failures if samples stop entirely (or startup failed).
  if (now - lastProcessedMotionMs > 250 && acquisitionQueueStatus().depth == 0) {
    invalidateMotionProcessing("No timely motion samples");
    publishMotion(now);
  }
}
void resetMotionProcessing(uint32_t now) {
  motionSamples = 0;
  for (unsigned axis = 0; axis < 3; ++axis)
    motionSum[axis] = motionGyroSum[axis] = 0;
  motionLastReport = now;
  motionWindowFailed = false;
  invalidatePickup("Acquisition restarted; waiting for fresh baseline");
}

/* Start five seconds of raw samples on explicit request; a reboot never triggers calibration. */
bool beginMotionCalibrationOwner() {
  SensorStateLock lock;
  if (!motionReady || motionPaused || motionCalibrating)
    return false;
  motionCalibrationSamples = 0;
  motionCalibrationStartedMs = millis();
  motionGyroCalibrating = false;
  for (unsigned i = 0; i < 3; ++i)
    motionCalibrationSum[i] = motionCalibrationSquares[i] = 0;
  motionCalibrationError = "";
  motionCalibrating = true;
  invalidatePickup("Accelerometer calibration in progress");
  return true;
}

/* Gyro calibration requires stillness, not a specific orientation, and never changes accelerometer
 * offsets. */
bool beginGyroCalibrationOwner() {
  SensorStateLock lock;
  if (!motionReady || motionPaused || motionCalibrating)
    return false;
  motionCalibrationSamples = 0;
  motionCalibrationStartedMs = millis();
  for (unsigned i = 0; i < 3; ++i)
    motionCalibrationSum[i] = motionCalibrationSquares[i] = gyroCalibrationSum[i] =
        gyroCalibrationSquares[i] = 0;
  motionCalibrationError = "";
  motionGyroCalibrating = motionCalibrating = true;
  invalidatePickup("Gyroscope calibration in progress");
  return true;
}

void disableMotion() {
  motionReady = false;
  motionError = motionTemperatureError = "I2C sensors disabled in microphone build";
}

MotionTemperature motionTemperature() {
  SensorStateLock lock;
  return {motionTemperatureC, motionTemperatureOk, motionTemperatureMs, motionTemperatureError};
}

MotionStatus motionStatus() {
  ProcessingStateLock processingLock;
  SensorStateLock lock;
  MotionStatus status{};
  status.ready = motionReady;
  status.gyroCalibrating = motionGyroCalibrating;
  status.calibrating = motionCalibrating;
  portENTER_CRITICAL(&motionMux);
  status.interrupts = motionInterrupts;
  portEXIT_CRITICAL(&motionMux);
  status.overflows = motionOverflowCount;
  status.startedAt = motionStartedAt;
  status.calibrationSamples = motionCalibrationSamples;
  status.error = motionError;
  status.calibrationError = motionCalibrationError;
  status.temperature = motionTemperature();
  status.readErrors = motionReadErrors;
  status.writeErrors = motionWriteErrors;
  status.failedRegister = motionFailedRegister;
  status.failedCount = motionFailedCount;
  status.writeStatus = motionWriteStatus;
  status.returnedBytes = motionReturnedBytes;
  for (unsigned i = 0; i < 3; ++i) {
    status.gyroMean[i] = motionGyroMean[i];
    status.gyroBias[i] = motionGyroBias[i];
    status.bias[i] = motionBias[i];
  }
  return status;
}

const MotionReport &latestMotionReport() {
  return motionLatest;
}
unsigned motionHistorySize() {
  return motionHistoryCount;
}
const MotionReport &motionHistoryAt(unsigned index) {
  return motionReports[(motionHead + MOTION_HISTORY_SIZE - motionHistoryCount + index) %
                       MOTION_HISTORY_SIZE];
}

MotionDiagnostics readMotionDiagnosticsOwner() {
  MotionDiagnostics result;
  result.enabled = I2C_SENSORS_ENABLED;
  if (!result.enabled)
    return result;
  uint8_t raw[6];
  result.ok = motionRead(0x1C, &result.range, 1) && motionRead(0x3B, raw, sizeof(raw));
  const float scales[] = {16384, 8192, 4096, 2048};
  result.countsPerG = scales[(result.range >> 3) & 3];
  for (unsigned i = 0; i < 3; ++i) {
    if (result.ok) {
      result.raw[i] = decodeMotionRaw(raw[2 * i], raw[2 * i + 1]);
      result.direct[i] = result.raw[i] * (9.80665f / result.countsPerG);
    }
  }
  for (unsigned i = 0; i < sizeof(MOTION_DIAGNOSTIC_REGISTERS); ++i) {
    uint8_t value;
    result.registers[i] = motionRead(MOTION_DIAGNOSTIC_REGISTERS[i], &value, 1) ? value : -1;
  }
  return result;
}

MotionDiagnostics readMotionDiagnostics() {
  MotionDiagnostics result;
  runSensorCommand([](void *argument) {
    *static_cast<MotionDiagnostics *>(argument) = readMotionDiagnosticsOwner();
  }, &result);
  { ProcessingStateLock lock;
    for (unsigned i = 0; i < 3; ++i) result.corrected[i] = result.direct[i] - motionBias[i];
  }
  return result;
}
bool beginMotionCalibration() {
  bool result = false;
  runProcessingCommand([](void *argument) {
    *static_cast<bool *>(argument) = beginMotionCalibrationOwner();
  }, &result);
  return result;
}
bool beginGyroCalibration() {
  bool result = false;
  runProcessingCommand([](void *argument) {
    *static_cast<bool *>(argument) = beginGyroCalibrationOwner();
  }, &result);
  return result;
}
void copyMotionDashboard(MotionDashboardSnapshot &snapshot, uint32_t since, bool history) {
  ProcessingStateLock lock;
  snapshot.latest = motionLatest;
  snapshot.status = motionStatus();
  snapshot.count = 0;
  if (!history) return;
  if (since > motionLatest.sequence) since = 0;
  for (unsigned i = 0; i < motionHistoryCount && snapshot.count < 20; ++i) {
    const MotionReport &report = motionHistoryAt(i);
    if (report.sequence > since) snapshot.reports[snapshot.count++] = report;
  }
}
