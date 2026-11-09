#pragma once
#include <cstdint>

/* All detector thresholds and timing defaults live here, independent of hardware/HTTP. */
struct PickupSettings {
  float stationary_ms = 3000;
  float stationary_accel_rms = 0.10f;
  float stationary_gyro = 0.05f;
  float accel_trigger = 0.6f;
  float noise_multiplier = 6;
  float tilt_deg = 8;
  float gyro_trigger = 0.35f;
  float trigger_ms = 150;
  float smoothing_ms = 50;
  float led_period_ms = 1000;
  float led_duty_pct = 50;
  float max_gap_ms = 250;
};

class PickupDetector {
public:
  PickupDetector() = default;
  PickupDetector(const PickupDetector &) = delete;
  PickupDetector &operator=(const PickupDetector &) = delete;
  ~PickupDetector();
  static constexpr unsigned SAMPLE_MS = 10;     // MPU configuration: 100 Hz.
  static constexpr unsigned MAX_SAMPLES = 1000; // Maximum configurable stillness window: 10 s.
  PickupSettings settings;
  bool active = false, armed = false;
  float accelDeviation = 0, tiltDegrees = 0, rotationRate = 0;
  float accelRms = 0, gyroRms = 0, baselineNoise = 0;
  float rawMagnitude = 0, filteredMagnitude = 0, baselineMagnitude = 0, instantaneousRotation = 0;
  bool referenceValid = false, quietWindowEvaluated = false;
  unsigned triggerEvidenceMs = 0;
  uint32_t events = 0;
  unsigned candidateMs = 0;
  // Trigger bits: acceleration=1, tilt=2, filtered rotation=4.
  // Quiet blockers: acceleration RMS=1, instantaneous rotation=2, gyro RMS=4.
  uint8_t triggerMask = 0, confirmedTriggerMask = 0, quietBlockMask = 0;

  /* Reset incomplete evidence on faults/configuration changes; an active alert survives. */
  void reset(bool preserveAlert = true);

  /* Apply a complete set of validated/saved settings, then learn a fresh baseline. */
  bool configure(const PickupSettings &next);

  /* Preflight growth before persisting settings; failed allocation preserves current evidence. */
  bool reserveWindow(const PickupSettings &next);

  /* Return observed quiet duration, capped at the configured window. */
  unsigned quietMs() const { return count * SAMPLE_MS; }

  // Feed each contiguous 10 ms FIFO frame: accel[3] in m/s^2, gyro[3] in rad/s.
  // Acquisition timestamps are delivery times; they do not set algorithm cadence.
  void process(const float *a, const float *w);

private:
  struct Sample {
    float a[3], w[3];
  };
  Sample *samples = nullptr;
  unsigned sampleCapacity = 0;
  unsigned head = 0, count = 0;
  double sumA[3] = {}, sumW[3] = {}, sumA2 = 0, sumW2 = 0;
  float baseline[3] = {}, gyroBias[3] = {}, fa[3] = {}, fw[3] = {};
  bool filtered = false, gyroKnown = false;

  /* Squared norms avoid costly square roots while updating statistics. */
  static float square(float value) { return value * value; }

  /* Refresh live features even while learning/alerting; reference-dependent plots use
   * referenceValid. */
  void updateFeatures(const float *a, const float *w);

  /* Sampling cadence turns the runtime duration into a bounded number of raw frames. */
  static unsigned windowSize(const PickupSettings &s);
  unsigned windowSize() const { return windowSize(settings); }

  /* Forget timing/statistics only; fixed learned reference survives while armed or alerting. */
  void clearWindow();

  /* Rolling sums provide variance without rescanning a three-second buffer per frame. */
  void addWindow(const float *a, const float *w);

  /* Learn gravity/noise/gyro bias only from confirmed stillness, including a new resting pose. */
  void learnBaseline();
};
