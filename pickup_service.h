#pragma once
#include <stdint.h>
#include "pickup_detector.h"

class PickupDebug;
struct PickupField {
  const char *key, *label, *unit;
  float PickupSettings::*member;
  float min, max, step;
};
constexpr unsigned PICKUP_FIELD_COUNT = 12;
using PickupFields = PickupField[PICKUP_FIELD_COUNT];

struct PickupStatus {
  uint32_t settingsRevision;
  bool selfTestsPassed, persisted, dataValid, ledAvailable, ledOn;
  const char *error;
};
struct PickupView {
  PickupStatus status;
  PickupSettings settings;
  bool active, armed;
  unsigned quietDuration;
  uint32_t events;
  float accelDeviation, tiltDegrees, rotationRate, accelRms, gyroRms, baselineNoise;
  unsigned quietMs() const { return quietDuration; }
};
PickupView pickupView();

// Processing task owns detector/settings/debug buffers. Its dedicated timer
// alone writes the LED after initialization, including while OTA blocks loop().
void setupPickup();
void processPickupSample(const float *accel, const float *gyro, uint32_t now);
void servicePickup(uint32_t now);
void invalidatePickup(const char *reason); // reason must outlive the status.
// Borrowed view for the processing owner only; HTTP uses pickupView() snapshots.
const PickupDetector &pickupDetector();
PickupStatus pickupStatus();
const PickupFields &pickupSettingFields();
const char *validatePickupSettings(const PickupSettings &settings);
bool savePickupSettings(const PickupSettings &settings);
bool subscribePickupDebug(uint32_t now);
const PickupDebug &pickupDebugHistory();
