#include "acquisition_timing.h"
namespace { portMUX_TYPE timingMux = portMUX_INITIALIZER_UNLOCKED; }
portMUX_TYPE &acquisitionTimingMux() { return timingMux; }

AcquisitionTiming &acquisitionTiming() {
  static AcquisitionTiming timing;
  return timing;
}
AcquisitionTiming acquisitionTimingSnapshot() {
  portENTER_CRITICAL(&timingMux);
  const AcquisitionTiming result = acquisitionTiming();
  portEXIT_CRITICAL(&timingMux);
  return result;
}
void countEnvironmentSample() {
  portENTER_CRITICAL(&timingMux);
  ++acquisitionTiming().environmentSamples;
  portEXIT_CRITICAL(&timingMux);
}
void countMotionFrame() {
  portENTER_CRITICAL(&timingMux);
  ++acquisitionTiming().motionFrames;
  portEXIT_CRITICAL(&timingMux);
}
