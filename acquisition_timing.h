#pragma once
#include <Arduino.h>

// Task instrumentation only; updates/copies use a short spinlock. No allocation/logging.
portMUX_TYPE &acquisitionTimingMux();
struct ServiceTiming {
  uint32_t calls = 0, lastUs = 0, maxUs = 0, maxGapUs = 0;
  uint32_t previousStartUs = 0;
  uint64_t totalUs = 0;
};
struct AcquisitionTiming {
  ServiceTiming loop, environment, motion, http, processing;
  uint32_t environmentSamples = 0, motionFrames = 0;
};
AcquisitionTiming &acquisitionTiming();
AcquisitionTiming acquisitionTimingSnapshot();
void countEnvironmentSample();
void countMotionFrame();

class ScopedServiceTiming {
public:
  explicit ScopedServiceTiming(ServiceTiming &value) : stats(value), start(micros()) {
    portENTER_CRITICAL(&acquisitionTimingMux());
    if (stats.calls) {
      const uint32_t gap = start - stats.previousStartUs;
      if (gap > stats.maxGapUs) stats.maxGapUs = gap;
    }
    stats.previousStartUs = start;
    ++stats.calls;
    portEXIT_CRITICAL(&acquisitionTimingMux());
  }
  ~ScopedServiceTiming() {
    const uint32_t duration = uint32_t(micros() - start);
    portENTER_CRITICAL(&acquisitionTimingMux());
    stats.lastUs = duration;
    stats.totalUs += stats.lastUs;
    if (stats.lastUs > stats.maxUs) stats.maxUs = stats.lastUs;
    portEXIT_CRITICAL(&acquisitionTimingMux());
  }
  ScopedServiceTiming(const ScopedServiceTiming &) = delete;
  ScopedServiceTiming &operator=(const ScopedServiceTiming &) = delete;
private:
  ServiceTiming &stats;
  uint32_t start;
};
