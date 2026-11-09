#pragma once
#include <stdint.h>
#include "aggregation.h"
#include "environment_history.h"

constexpr uint32_t PUBLISH_INTERVAL_MS = 1000;
struct EnvironmentStatus {
  uint32_t missedSampleDeadlines, lateConversionServices, missedReportDeadlines;
  bool historyTestsPassed;
};

// Main-loop only. setupEnvironment() uses an already configured shared Wire
// bus; this module does not own its pins/clock or MPU6050 initialization.
void setupEnvironmentHistory(); // Destructive self-tests before acquisition.
void setupEnvironment();
void disableEnvironment();                 // Status only; no bus access.
void resetEnvironmentWindow(uint32_t now); // Establish the 200 ms sampling grid.
void discardEnvironmentWindow();           // OTA failure: discard interrupted readings.
void acquireSensor();             // Advance forced conversion without waiting for completion.
void publishWindow(uint32_t now); // Call before starting the next conversion.
// Borrowed views: consume before the next publication/reset on loop().
const PublishedSample &latestEnvironmentSample();
const EnvironmentHistory &environmentHistory();
EnvironmentStatus environmentStatus();
