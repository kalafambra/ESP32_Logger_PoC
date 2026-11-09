#pragma once
#include <stdint.h>

// All motion access is on setup()/loop(), except the private data-ready ISR.
// Acceleration includes gravity (m/s^2); gyro vectors use rad/s.
constexpr uint32_t MOTION_REPORT_MS = 1000;
constexpr unsigned MOTION_HISTORY_SIZE = 128;

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
};

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
void acquireMotion(); // Bounded FIFO drain; forwards each raw frame to pickup.
void acquireMotionTemperature(uint32_t now);
void pauseMotion();
void resumeMotion();

// False means unavailable/paused/already calibrating. Completion is asynchronous.
bool beginMotionCalibration();
bool beginGyroCalibration();
MotionTemperature motionTemperature();
MotionStatus motionStatus();
// Borrowed reports: consume on loop() before the next acquire/calibration call.
const MotionReport &latestMotionReport();
unsigned motionHistorySize();
const MotionReport &motionHistoryAt(unsigned index); // Oldest first; index < size.
MotionDiagnostics readMotionDiagnostics();           // Synchronous, checked I2C reads.
