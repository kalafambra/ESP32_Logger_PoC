#pragma once
#include <cmath>
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
  static constexpr unsigned SAMPLE_MS = 10; // MPU configuration: 100 Hz.
  static constexpr unsigned MAX_SAMPLES = 1000; // Maximum configurable stillness window: 10 s.
  PickupSettings settings;
  bool active = false, armed = false;
  float accelDeviation = 0, tiltDegrees = 0, rotationRate = 0;
  float accelRms = 0, gyroRms = 0, baselineNoise = 0;
  uint32_t events = 0;
  unsigned candidateMs = 0;

  /* Reset incomplete evidence on faults/configuration changes; an active alert survives. */
  void reset(bool preserveAlert = true) {
    if (!preserveAlert) { active = false; gyroKnown = false; events = 0; }
    armed = false; filtered = false; candidateMs = 0;
    accelDeviation = tiltDegrees = rotationRate = accelRms = gyroRms = 0;
    clearWindow();
  }

  /* Apply settings atomically after external validation/storage, then learn a fresh baseline. */
  void configure(const PickupSettings &next) { settings = next; reset(true); }

  /* Return observed quiet duration, capped at the configured window. */
  unsigned quietMs() const { return count * SAMPLE_MS; }

  /* Evaluate each FIFO frame, not the one-second published average. */
  void process(const float *a, const float *w) {
    for (unsigned i = 0; i < 3; ++i) if (!std::isfinite(a[i]) || !std::isfinite(w[i])) { reset(); return; }

    // BLOCK 1: low-pass event features, while retaining unfiltered data for stillness/noise.
    const float alpha = SAMPLE_MS / (settings.smoothing_ms + SAMPLE_MS);
    for (unsigned i = 0; i < 3; ++i) {
      fa[i] = filtered ? fa[i] + alpha * (a[i] - fa[i]) : a[i];
      fw[i] = filtered ? fw[i] + alpha * (w[i] - fw[i]) : w[i];
    }
    filtered = true;

    // BLOCK 2: collect a continuous low-variance window at the CURRENT orientation.
    // A rotation above the stillness limit or excessive variance restarts quiet evidence.
    float instantaneousRotation = 0;
    for (unsigned i = 0; i < 3; ++i) instantaneousRotation += square(w[i] - (gyroKnown ? gyroBias[i] : 0));
    if (std::sqrt(instantaneousRotation) > settings.stationary_gyro) clearWindow();
    else {
      addWindow(a, w);
      double av = sumA2, gv = sumW2;
      for (unsigned i = 0; i < 3; ++i) {
        const double ma = sumA[i] / count, mw = sumW[i] / count;
        av -= ma * ma * count;
        const double bias = gyroKnown ? gyroBias[i] : 0;
        gv += count * bias * bias - 2 * bias * sumW[i];
      }
      accelRms = std::sqrt(av > 0 ? av / count : 0);
      gyroRms = std::sqrt(gv > 0 ? gv / count : 0);
      if (accelRms > settings.stationary_accel_rms || gyroRms > settings.stationary_gyro) clearWindow();
    }

    // BLOCK 3: arm after stillness; an active alert clears only after a NEW full quiet window.
    const unsigned required = windowSize();
    if ((active || !armed) && count >= required) {
      learnBaseline();
      active = false; armed = true; candidateMs = 0;
      return;
    }
    if (active || !armed) return;

    // BLOCK 4: compare against a frozen stationary reference, so slow tilts are not learned away.
    float a2 = 0, b2 = 0, dot = 0, w2 = 0;
    for (unsigned i = 0; i < 3; ++i) {
      a2 += square(fa[i]); b2 += square(baseline[i]); dot += fa[i] * baseline[i];
      w2 += square(fw[i] - gyroBias[i]);
    }
    accelDeviation = std::fabs(std::sqrt(a2) - std::sqrt(b2));
    rotationRate = std::sqrt(w2);
    float cosine = (a2 > 0.01f && b2 > 0.01f) ? dot / std::sqrt(a2 * b2) : 1;
    if (cosine > 1) cosine = 1; if (cosine < -1) cosine = -1;
    tiltDegrees = std::acos(cosine) * (180.0f / 3.14159265358979323846f);
    const float adaptive = baselineNoise * settings.noise_multiplier;
    const float accelLimit = adaptive > settings.accel_trigger ? adaptive : settings.accel_trigger;
    const bool trigger = accelDeviation > accelLimit || tiltDegrees > settings.tilt_deg || rotationRate > settings.gyro_trigger;

    // BLOCK 5: reject short spikes; only continuous threshold exceedance confirms pickup.
    candidateMs = trigger ? candidateMs + SAMPLE_MS : 0;
    if (candidateMs >= settings.trigger_ms) {
      active = true; ++events; candidateMs = 0;
      clearWindow(); // A pre-pickup quiet window must never immediately clear the alert.
    }
  }

 private:
  struct Sample { float a[3], w[3]; } samples[MAX_SAMPLES];
  unsigned head = 0, count = 0;
  double sumA[3] = {}, sumW[3] = {}, sumA2 = 0, sumW2 = 0;
  float baseline[3] = {}, gyroBias[3] = {}, fa[3] = {}, fw[3] = {};
  bool filtered = false, gyroKnown = false;

  /* Squared norms avoid costly square roots while updating statistics. */
  static float square(float value) { return value * value; }

  /* Sampling cadence turns the runtime duration into a bounded number of raw frames. */
  unsigned windowSize() const {
    unsigned n = unsigned(std::ceil(settings.stationary_ms / SAMPLE_MS));
    return n > MAX_SAMPLES ? MAX_SAMPLES : n < 2 ? 2 : n;
  }

  /* Forget timing/statistics only; fixed learned reference survives while armed or alerting. */
  void clearWindow() {
    head = count = 0; sumA2 = sumW2 = 0;
    for (unsigned i = 0; i < 3; ++i) sumA[i] = sumW[i] = 0;
  }

  /* Rolling sums provide variance without rescanning a three-second buffer per frame. */
  void addWindow(const float *a, const float *w) {
    const unsigned n = windowSize();
    if (count == n) {
      for (unsigned i = 0; i < 3; ++i) {
        sumA[i] -= samples[head].a[i]; sumW[i] -= samples[head].w[i];
        sumA2 -= square(samples[head].a[i]); sumW2 -= square(samples[head].w[i]);
      }
    } else ++count;
    for (unsigned i = 0; i < 3; ++i) {
      samples[head].a[i] = a[i]; samples[head].w[i] = w[i];
      sumA[i] += a[i]; sumW[i] += w[i]; sumA2 += square(a[i]); sumW2 += square(w[i]);
    }
    head = (head + 1) % n;
  }

  /* Learn gravity/noise/gyro bias only from confirmed stillness, including a new resting pose. */
  void learnBaseline() {
    for (unsigned i = 0; i < 3; ++i) { baseline[i] = sumA[i] / count; gyroBias[i] = sumW[i] / count; }
    baselineNoise = accelRms; gyroKnown = true;
  }
};
