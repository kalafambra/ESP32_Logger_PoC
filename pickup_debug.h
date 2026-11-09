#pragma once
#include <Arduino.h>
#include "pickup_detector.h"

/* Compact telemetry is independent of detection and exists only while a tuning client subscribes.
 */
class PickupDebug {
public:
  PickupDebug() = default;
  ~PickupDebug();
  PickupDebug(const PickupDebug &) = delete;
  PickupDebug &operator=(const PickupDebug &) = delete;
  static constexpr unsigned CAPACITY = 128, CAPTURE_MS = 50;
  enum Field {
    VALID,
    ACTIVE,
    ARMED,
    LED,
    RAW_A,
    FILTERED_A,
    REST_A,
    ACCEL_DEV,
    FIXED_ACCEL,
    NOISE,
    NOISE_MULT,
    ADAPTIVE_ACCEL,
    EFFECTIVE_ACCEL,
    TILT,
    TILT_LIMIT,
    GYRO,
    GYRO_LIMIT,
    ACCEL_RMS,
    ACCEL_QUIET,
    GYRO_RMS,
    INSTANT_GYRO,
    GYRO_QUIET,
    QUIET_MS,
    QUIET_LIMIT,
    EVIDENCE_MS,
    EVIDENCE_LIMIT,
    SMOOTHING_MS,
    DELIVERY_GAP_MS,
    SAMPLE_AGE_MS,
    GAP_LIMIT,
    LED_PERIOD,
    LED_DUTY,
    OBSERVED_PERIOD,
    OBSERVED_DUTY,
    REVISION,
    TRIGGER_MASK,
    CONFIRMED_MASK,
    QUIET_BLOCK_MASK,
    EVENTS,
    COUNT
  };
  struct Record {
    uint32_t sequence, ms;
    float values[COUNT];
  };
  /* Refresh a short subscription lease, avoiding continuous debug work when the page is closed. */
  bool subscribe(uint32_t now);

  /* Capture 20 Hz samples and immediate state/LED edges, including the confirming persistence
   * frame. */
  bool capture(const PickupDetector &p, bool valid, uint32_t now, uint32_t gap, uint32_t age,
               bool led, uint32_t revision);

  /* Serialize fixed-width rows with named columns; nulls represent unavailable signals, never fake
   * zeros. */
  void appendJson(String &json, uint32_t since) const;
  uint32_t latestSequence() const { return sequence; }

private:
  // Lease-owned storage is allocated only while a tuning client subscribes.
  Record *records = nullptr;
  uint32_t sequence = 0, lastCapture = 0, lastRequest = 0;
  unsigned head = 0, count = 0;
  bool subscribed = false, lastLed = false;
  uint32_t lastRise = 0, lastOnMs = 0;
  float observedPeriod = NAN, observedDuty = NAN;
  bool lastQuietRejected = false;
  uint8_t lastTriggerMask = 0;
};
