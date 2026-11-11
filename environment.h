#pragma once
#include <stdint.h>
#include "aggregation.h"
#include "environment_history.h"
struct SensorPacket;
void processEnvironmentPacket(const SensorPacket &packet);
void invalidateEnvironmentProcessing(const char *message);

constexpr uint32_t PUBLISH_INTERVAL_MS = 1000;
struct EnvironmentStatus {
  uint32_t missedSampleDeadlines, lateConversionServices, missedReportDeadlines;
  bool historyTestsPassed;
};
struct EnvironmentDashboardSnapshot {
  PublishedSample latest;
  EnvironmentStatus status;
  EnvironmentHistory history;
};
void copyEnvironmentDashboard(EnvironmentDashboardSnapshot &snapshot);

// Acquisition owner only for hardware calls. setupEnvironment() uses shared Wire
// bus; this module does not own its pins/clock or MPU6050 initialization.
void setupEnvironmentHistory(); // Destructive self-tests before acquisition.
void setupEnvironment();
void disableEnvironment();                 // Status only; no bus access.
void resetEnvironmentWindow(uint32_t now); // Startup only, before task creation.
void discardEnvironmentWindow();          // Owner command: restart after OTA failure.
void acquireSensor();                     // Owner: read previous result, then trigger.
void publishWindow(uint32_t now);          // Processing owner: close after the fifth result.
// Borrowed views: processing owner only. HTTP uses copyEnvironmentDashboard().
const PublishedSample &latestEnvironmentSample();
const EnvironmentHistory &environmentHistory();
EnvironmentStatus environmentStatus();
