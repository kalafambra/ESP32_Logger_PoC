#pragma once
#include <stdint.h>
#include "aggregation.h"
#include "dashboard_delivery.h"
struct SensorPacket;
void processEnvironmentPacket(const SensorPacket &packet);
void invalidateEnvironmentProcessing(const char *message);

constexpr uint32_t PUBLISH_INTERVAL_MS = 1000;
struct EnvironmentStatus {
  uint32_t missedSampleDeadlines, lateConversionServices, missedReportDeadlines;
  bool deliveryTestsPassed;
};
struct EnvironmentDashboardSnapshot {
  PublishedSample latest;
  EnvironmentStatus status;
  DashboardDelivery<PublishedSample, 4>::Snapshot delivery;
};
void copyEnvironmentDashboard(EnvironmentDashboardSnapshot &snapshot, const DeliveryRequest &request);

// Acquisition owner only for hardware calls. setupEnvironment() uses shared Wire
// bus; this module does not own its pins/clock or MPU6050 initialization.
void setupDashboardDelivery(); // Startup self-tests use only local ACK buffers.
void setupEnvironment();
void disableEnvironment();                 // Status only; no bus access.
void resetEnvironmentWindow(uint32_t now); // Startup only, before task creation.
void discardEnvironmentWindow();          // Owner command: restart after OTA failure.
void acquireSensor();                     // Owner: read previous result, then trigger.
void publishWindow(uint32_t now);          // Processing owner: close after the fifth result.
EnvironmentStatus environmentStatus();
