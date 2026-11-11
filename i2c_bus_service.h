#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
constexpr unsigned ACQUISITION_QUEUE_CAPACITY = 128;

// Acquisition owns Wire; the processing worker owns algorithms and published state.
enum class SensorPacketKind : uint8_t { Motion, MotionFault, Environment, EnvironmentFault,
                                       Boundary, Reset };
struct SensorPacket {
  SensorPacketKind kind = SensorPacketKind::Motion;
  uint32_t sampleMs = 0, retrievedMs = 0, sequence = 0, generation = 0;
  uint8_t raw[12] = {};
  float temperature = 0, humidity = 0, pressure = 0;
  bool chipTemperatureOk = false;
  const char *error = ""; // Static strings only; packets own no pointers to transient data.
};
struct AcquisitionQueueStatus {
  uint32_t dropped = 0, highWater = 0, depth = 0, stackFreeBytes = 0;
  bool running = false;
  uint32_t processingStackFreeBytes = 0;
  bool processingRunning = false;
};
bool startAcquisition();
void notifyAcquisitionFromISR();
bool enqueueSensorPacket(SensorPacket packet); // Acquisition producer; zero wait.
void processSensorPackets();                  // Sole consumer: processing task.
void serviceProcessingFallback(uint32_t now);  // Journal/LED maintenance if no worker exists.
void runSensorCommand(void (*command)(void *), void *argument);
void runProcessingCommand(void (*command)(void *), void *argument);
void pauseAcquisition();
void resumeAcquisition();
AcquisitionQueueStatus acquisitionQueueStatus();

// Protect only acquisition-owned state copies and owner commands, never HTTP sends.
class SensorStateLock {
public:
  SensorStateLock();
  ~SensorStateLock();
};
// Lock order: processing state, then acquisition state. Never wait for owner RPC
// or send HTTP while holding either state lock.
class ProcessingStateLock {
public:
  ProcessingStateLock();
  ~ProcessingStateLock();
  ProcessingStateLock(const ProcessingStateLock &) = delete;
  ProcessingStateLock &operator=(const ProcessingStateLock &) = delete;
};
