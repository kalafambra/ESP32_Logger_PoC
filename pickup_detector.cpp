#include "pickup_detector.h"
#include <cmath>
#include <new>

unsigned PickupDetector::windowSize(const PickupSettings &s) {
  unsigned n = unsigned(std::ceil(s.stationary_ms / SAMPLE_MS));
  return n > MAX_SAMPLES ? MAX_SAMPLES : n < 2 ? 2 : n;
}

PickupDetector::~PickupDetector() {
  delete[] samples;
}

void PickupDetector::reset(bool preserveAlert) {
  if (!preserveAlert) {
    active = false;
    gyroKnown = false;
    events = 0;
  }
  armed = false;
  filtered = false;
  candidateMs = 0;
  referenceValid = false;
  quietWindowEvaluated = false;
  triggerEvidenceMs = 0;
  triggerMask = confirmedTriggerMask = quietBlockMask = 0;
  accelDeviation = tiltDegrees = rotationRate = accelRms = gyroRms = 0;
  clearWindow();
}

bool PickupDetector::configure(const PickupSettings &next) {
  if (!reserveWindow(next))
    return false;
  const unsigned n = windowSize(next);
  // Shrinking is optional: retain the larger allocation if fragmentation prevents replacement.
  if (n < sampleCapacity) {
    if (auto *smaller = new (std::nothrow) Sample[n]) {
      delete[] samples;
      samples = smaller;
      sampleCapacity = n;
    }
  }
  settings = next;
  reset(true);
  return true;
}

bool PickupDetector::reserveWindow(const PickupSettings &next) {
  const unsigned n = windowSize(next);
  if (n <= sampleCapacity)
    return true;
  auto *larger = new (std::nothrow) Sample[n];
  if (!larger)
    return false;
  for (unsigned i = 0; i < count; ++i)
    larger[i] = samples[i]; // Only initialized window entries are needed.
  delete[] samples;
  samples = larger;
  sampleCapacity = n;
  return true;
}

void PickupDetector::process(const float *a, const float *w) {
  if (!samples) {
    reset();
    return;
  } // An unavailable buffer must never look like confirmed stillness.
  for (unsigned i = 0; i < 3; ++i)
    if (!std::isfinite(a[i]) || !std::isfinite(w[i])) {
      reset();
      return;
    }

  // BLOCK 1: low-pass event features, while retaining unfiltered data for stillness/noise.
  const float alpha = SAMPLE_MS / (settings.smoothing_ms + SAMPLE_MS);
  for (unsigned i = 0; i < 3; ++i) {
    fa[i] = filtered ? fa[i] + alpha * (a[i] - fa[i]) : a[i];
    fw[i] = filtered ? fw[i] + alpha * (w[i] - fw[i]) : w[i];
  }
  filtered = true;
  // Telemetry must keep updating during an alert, not freeze at the trigger frame.
  updateFeatures(a, w);
  triggerEvidenceMs = 0;
  quietWindowEvaluated = false;
  quietBlockMask = 0;
  const float accelLimit =
      std::fmax(settings.accel_trigger, baselineNoise * settings.noise_multiplier);
  triggerMask = referenceValid ? ((accelDeviation > accelLimit ? 1 : 0) |
                                  (tiltDegrees > settings.tilt_deg ? 2 : 0) |
                                  (rotationRate > settings.gyro_trigger ? 4 : 0))
                               : 0;

  // BLOCK 2: collect a continuous low-variance window at the CURRENT orientation.
  // A rotation above the stillness limit or excessive variance restarts quiet evidence.
  if (instantaneousRotation > settings.stationary_gyro) {
    quietBlockMask = 2;
    clearWindow();
  } else {
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
    quietWindowEvaluated = true;
    quietBlockMask = (accelRms > settings.stationary_accel_rms ? 1 : 0) |
                     (gyroRms > settings.stationary_gyro ? 4 : 0);
    if (quietBlockMask)
      clearWindow();
  }

  // BLOCK 3: arm after stillness; an active alert clears only after a NEW full quiet window.
  const unsigned required = windowSize();
  if ((active || !armed) && count >= required) {
    learnBaseline();
    updateFeatures(a, w);
    active = false;
    armed = true;
    candidateMs = 0;
    triggerMask = 0;
    return;
  }
  if (active || !armed)
    return;

  // BLOCK 4: compare against a frozen stationary reference, so slow tilts are not learned away.
  const bool trigger = triggerMask != 0;

  // BLOCK 5: reject short spikes; only continuous threshold exceedance confirms pickup.
  candidateMs = trigger ? candidateMs + SAMPLE_MS : 0;
  triggerEvidenceMs = candidateMs; // Preserve the confirming value before candidateMs resets.
  if (candidateMs >= settings.trigger_ms) {
    active = true;
    ++events;
    candidateMs = 0;
    confirmedTriggerMask =
        triggerMask; // Freeze the actual confirming-frame causes for diagnostics.
    clearWindow();   // A pre-pickup quiet window must never immediately clear the alert.
  }
}

void PickupDetector::updateFeatures(const float *a, const float *w) {
  float raw2 = 0, a2 = 0, b2 = 0, dot = 0, w2 = 0, rawW2 = 0;
  for (unsigned i = 0; i < 3; ++i) {
    raw2 += square(a[i]);
    a2 += square(fa[i]);
    b2 += square(baseline[i]);
    dot += fa[i] * baseline[i];
    const float bias = gyroKnown ? gyroBias[i] : 0;
    w2 += square(fw[i] - bias);
    rawW2 += square(w[i] - bias);
  }
  rawMagnitude = std::sqrt(raw2);
  filteredMagnitude = std::sqrt(a2);
  baselineMagnitude = std::sqrt(b2);
  instantaneousRotation = std::sqrt(rawW2);
  rotationRate = std::sqrt(w2);
  accelDeviation = std::fabs(filteredMagnitude - baselineMagnitude);
  float cosine = (a2 > 0.01f && b2 > 0.01f) ? dot / std::sqrt(a2 * b2) : 1;
  if (cosine > 1)
    cosine = 1;
  if (cosine < -1)
    cosine = -1;
  tiltDegrees = std::acos(cosine) * (180.0f / 3.14159265358979323846f);
}

void PickupDetector::clearWindow() {
  head = count = 0;
  sumA2 = sumW2 = 0;
  for (unsigned i = 0; i < 3; ++i)
    sumA[i] = sumW[i] = 0;
}

void PickupDetector::addWindow(const float *a, const float *w) {
  const unsigned n = windowSize();
  if (count == n) {
    for (unsigned i = 0; i < 3; ++i) {
      sumA[i] -= samples[head].a[i];
      sumW[i] -= samples[head].w[i];
      sumA2 -= square(samples[head].a[i]);
      sumW2 -= square(samples[head].w[i]);
    }
  } else
    ++count;
  for (unsigned i = 0; i < 3; ++i) {
    samples[head].a[i] = a[i];
    samples[head].w[i] = w[i];
    sumA[i] += a[i];
    sumW[i] += w[i];
    sumA2 += square(a[i]);
    sumW2 += square(w[i]);
  }
  head = (head + 1) % n;
}

void PickupDetector::learnBaseline() {
  for (unsigned i = 0; i < 3; ++i) {
    baseline[i] = sumA[i] / count;
    gyroBias[i] = sumW[i] / count;
  }
  baselineNoise = accelRms;
  gyroKnown = true;
  referenceValid = true;
}
