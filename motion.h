#pragma once
#include <stdint.h>
#include "dashboard_delivery.h"
struct SensorPacket;
void processMotionPacket(const SensorPacket &packet);
void invalidateMotionProcessing(const char *message);
void publishProcessedMotion(uint32_t now);
void resetMotionProcessing(uint32_t now);

// Hardware calls belong to setup/acquisition; algorithms/calibration belong to processing.
// Acceleration includes gravity (m/s^2); gyro vectors use rad/s.
constexpr uint32_t MOTION_REPORT_MS = 1000;

struct MotionReport {
  uint32_t sequence = 0, sampleMs = 0;
  unsigned samples = 0;
  bool ok = false;
  float mean[3] = {}, minimum[3] = {}, maximum[3] = {};
  const char *error = "Waiting for first acceleration report";
};

struct MotionTemperature {
  float celsius;
  bool ok;
  uint32_t sampleMs;
  const char *error;
};

// Small copy for consumers; FIFO buffers, calibration sums and locks stay private.
struct MotionStatus {
  bool ready, gyroCalibrating, calibrating;
  uint32_t interrupts, overflows, startedAt;
  unsigned calibrationSamples;
  const char *error, *calibrationError;
  float gyroMean[3], gyroBias[3], bias[3];
  MotionTemperature temperature;
  uint32_t readErrors, writeErrors;
  uint8_t failedRegister, failedCount, writeStatus;
  unsigned returnedBytes;
};
struct MotionDashboardSnapshot {
  MotionReport latest;
  MotionStatus status;
  DashboardDelivery<MotionReport, 4>::Snapshot delivery;
};
void copyMotionDashboard(MotionDashboardSnapshot &snapshot, const DeliveryRequest &request, bool deliver);

constexpr uint8_t MOTION_DIAGNOSTIC_REGISTERS[] = {0x19, 0x1A, 0x1C, 0x23, 0x6A, 0x6B, 0x6C, 0x75};
struct MotionDiagnostics {
  bool enabled = false, ok = false;
  uint8_t range = 0;
  float countsPerG = 16384;
  int32_t raw[3] = {};
  float direct[3] = {}, corrected[3] = {};
  int registers[sizeof(MOTION_DIAGNOSTIC_REGISTERS)] = {}; // -1 denotes a failed read.
};

void setupMotion();   // Shared Wire bus must already be initialized.
void disableMotion(); // Status only: does not touch I2C or the interrupt pin.
void acquireMotion(); // Owner: bounded FIFO drain into the software queue.
void acquireMotionTemperature(uint32_t now); // Owner only.
void pauseMotion();  // Owner only; use pauseAcquisition() from loop.
void resumeMotion(); // Owner only; use resumeAcquisition() from loop.

// False means unavailable/paused/already calibrating. Completion is asynchronous.
bool beginMotionCalibration();
bool beginGyroCalibration();
MotionTemperature motionTemperature();
MotionStatus motionStatus();
// Borrowed reports: processing owner only. HTTP uses copyMotionDashboard().
const MotionReport &latestMotionReport();
MotionDiagnostics readMotionDiagnostics(); // Checked I2C request executed by owner.
