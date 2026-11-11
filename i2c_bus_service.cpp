#include "i2c_bus_service.h"
#include <freertos/task.h>
#include <freertos/queue.h>
#include "config.h"
#include "environment.h"
#include "motion.h"
#include "pickup_service.h"
#include "acquisition_timing.h"

namespace {
constexpr unsigned CAPACITY = 128;
struct Command { void (*function)(void *); void *argument; };
StaticQueue_t queueControl;
uint8_t queueStorage[CAPACITY * sizeof(SensorPacket)];
QueueHandle_t samples = nullptr;
StaticSemaphore_t stateStorage, commandStorage, completionStorage;
SemaphoreHandle_t stateMutex = nullptr, commandMutex = nullptr, completion = nullptr;
TaskHandle_t acquisitionTask = nullptr;
TaskHandle_t processingTask = nullptr;
StaticSemaphore_t processingStateStorage, processingCommandStorage, processingCompletionStorage;
SemaphoreHandle_t processingMutex = nullptr, processingCommandMutex = nullptr,
                  processingCompletion = nullptr;
StaticQueue_t processingCommandQueueControl;
uint8_t processingCommandQueueStorage[sizeof(Command)];
QueueHandle_t processingCommands = nullptr;
bool processingPaused = false;
TaskHandle_t interruptTask = nullptr;
portMUX_TYPE interruptTaskMux = portMUX_INITIALIZER_UNLOCKED;
StaticQueue_t commandQueueControl;
uint8_t commandQueueStorage[sizeof(Command)];
QueueHandle_t commands = nullptr;
uint32_t generation = 0, drops = 0, highWater = 0;
bool paused = false;

void processingMain(void *) {
  // Startup gate: producer creation must succeed before this task does any work.
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
    ScopedServiceTiming timing(acquisitionTiming().processing);
    Command command;
    if (xQueueReceive(processingCommands, &command, 0) == pdTRUE) {
      { ProcessingStateLock lock; command.function(command.argument); }
      xSemaphoreGive(processingCompletion);
    }
    if (!processingPaused) {
      processSensorPackets();
      ProcessingStateLock lock;
      servicePickup(millis());
    }
  }
}
void pauseProcessingOwner(void *) {
  processingPaused = true;
  invalidateMotionProcessing("Acquisition paused for OTA; active alert retained");
  invalidateEnvironmentProcessing("OTA interrupted acquisition; window discarded");
  servicePickup(millis());
}
void resumeProcessingOwner(void *) {
  processingPaused = false;
}

void taskMain(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
    {
      SensorStateLock lock;
      Command command;
      if (xQueueReceive(commands, &command, 0) == pdTRUE) {
        command.function(command.argument);
        xSemaphoreGive(completion);
      }
      if (!paused) {
        acquireMotion(); // Acknowledge latched INT and drain before BME bus work.
        acquireSensor();
      }
    }
  }
}
void pauseOwner(void *) {
  paused = true;
  pauseMotion();
  if (samples) xQueueReset(samples);
  ++generation;
}
void resumeOwner(void *) {
  if (samples) xQueueReset(samples);
  ++generation;
  resumeMotion();
  discardEnvironmentWindow();
  paused = false;
}
} // namespace

SensorStateLock::SensorStateLock() {
  if (stateMutex) xSemaphoreTakeRecursive(stateMutex, portMAX_DELAY);
}
SensorStateLock::~SensorStateLock() {
  if (stateMutex) xSemaphoreGiveRecursive(stateMutex);
}
ProcessingStateLock::ProcessingStateLock() {
  if (processingMutex) xSemaphoreTakeRecursive(processingMutex, portMAX_DELAY);
}
ProcessingStateLock::~ProcessingStateLock() {
  if (processingMutex) xSemaphoreGiveRecursive(processingMutex);
}

bool startAcquisition() {
  if (!I2C_SENSORS_ENABLED) return true;
  if (acquisitionTask) return true;
  stateMutex = xSemaphoreCreateRecursiveMutexStatic(&stateStorage);
  commandMutex = xSemaphoreCreateMutexStatic(&commandStorage);
  completion = xSemaphoreCreateBinaryStatic(&completionStorage);
  samples = xQueueCreateStatic(CAPACITY, sizeof(SensorPacket), queueStorage, &queueControl);
  commands = xQueueCreateStatic(1, sizeof(Command), commandQueueStorage, &commandQueueControl);
  processingMutex = xSemaphoreCreateRecursiveMutexStatic(&processingStateStorage);
  processingCommandMutex = xSemaphoreCreateMutexStatic(&processingCommandStorage);
  processingCompletion = xSemaphoreCreateBinaryStatic(&processingCompletionStorage);
  processingCommands = xQueueCreateStatic(1, sizeof(Command), processingCommandQueueStorage,
                                         &processingCommandQueueControl);
  resetEnvironmentWindow(millis());
  if (xTaskCreate(processingMain, "sensor-process", 6144, nullptr, 1, &processingTask) != pdPASS)
    return false;
  // ESP-IDF stack size is bytes. Do not pin: both cores remain available to the scheduler.
  const bool started = xTaskCreate(taskMain, "sensor-acquire", 4096, nullptr, 2,
                                 &acquisitionTask) == pdPASS;
  portENTER_CRITICAL(&interruptTaskMux);
  interruptTask = acquisitionTask;
  portEXIT_CRITICAL(&interruptTaskMux);
  if (started) xTaskNotifyGive(processingTask);
  else { vTaskDelete(processingTask); processingTask = nullptr; }
  return started;
}

void ARDUINO_ISR_ATTR notifyAcquisitionFromISR() {
  portENTER_CRITICAL_ISR(&interruptTaskMux);
  TaskHandle_t target = interruptTask;
  portEXIT_CRITICAL_ISR(&interruptTaskMux);
  if (!target) return;
  BaseType_t wake = pdFALSE;
  vTaskNotifyGiveFromISR(target, &wake);
  if (wake) portYIELD_FROM_ISR();
}

bool enqueueSensorPacket(SensorPacket packet) {
  if (!samples) return false;
  packet.generation = generation;
  if (xQueueSend(samples, &packet, 0) != pdTRUE) {
    ++drops;
    ++generation; // Consumer sees a discontinuity before the next accepted packet.
    return false;
  }
  highWater = max(highWater, uint32_t(uxQueueMessagesWaiting(samples)));
  if (processingTask) xTaskNotifyGive(processingTask);
  return true;
}

void processSensorPackets() {
  if (!samples) return;
  static uint32_t seenGeneration = 0;
  SensorPacket packet;
  // Bounded work per loop; acquisition never waits for this consumer.
  for (unsigned i = 0; i < CAPACITY && xQueueReceive(samples, &packet, 0) == pdTRUE; ++i) {
    ProcessingStateLock lock;
    if (packet.generation != seenGeneration) {
      seenGeneration = packet.generation;
      invalidateMotionProcessing("Software acquisition queue discontinuity");
      invalidateEnvironmentProcessing("Software acquisition queue discontinuity");
    }
    if (packet.kind == SensorPacketKind::Motion || packet.kind == SensorPacketKind::MotionFault)
      processMotionPacket(packet);
    else {
      processEnvironmentPacket(packet);
      if (packet.kind == SensorPacketKind::Reset) resetMotionProcessing(packet.sampleMs);
    }
  }
  { ProcessingStateLock lock; publishProcessedMotion(millis()); }
}

void runProcessingCommand(void (*function)(void *), void *argument) {
  if (!processingTask || xTaskGetCurrentTaskHandle() == processingTask) {
    ProcessingStateLock lock;
    function(argument);
    return;
  }
  xSemaphoreTake(processingCommandMutex, portMAX_DELAY);
  const Command command{function, argument};
  xQueueSend(processingCommands, &command, portMAX_DELAY);
  xTaskNotifyGive(processingTask);
  xSemaphoreTake(processingCompletion, portMAX_DELAY);
  xSemaphoreGive(processingCommandMutex);
}
void serviceProcessingFallback(uint32_t now) {
  if (!processingTask) servicePickup(now);
}

void runSensorCommand(void (*function)(void *), void *argument) {
  if (!acquisitionTask || xTaskGetCurrentTaskHandle() == acquisitionTask) {
    SensorStateLock lock;
    function(argument);
    return;
  }
  xSemaphoreTake(commandMutex, portMAX_DELAY);
  const Command command{function, argument};
  xQueueSend(commands, &command, portMAX_DELAY);
  xTaskNotifyGive(acquisitionTask);
  // Caller-owned arguments remain alive until the owner acknowledges completion.
  xSemaphoreTake(completion, portMAX_DELAY);
  xSemaphoreGive(commandMutex);
}
void pauseAcquisition() {
  runSensorCommand(pauseOwner, nullptr);
  runProcessingCommand(pauseProcessingOwner, nullptr);
}
void resumeAcquisition() {
  runSensorCommand(resumeOwner, nullptr);
  runProcessingCommand(resumeProcessingOwner, nullptr);
}
AcquisitionQueueStatus acquisitionQueueStatus() {
  SensorStateLock lock;
  return {drops, highWater, samples ? uint32_t(uxQueueMessagesWaiting(samples)) : 0,
          acquisitionTask ? uint32_t(uxTaskGetStackHighWaterMark(acquisitionTask)) : 0,
          acquisitionTask != nullptr,
          processingTask ? uint32_t(uxTaskGetStackHighWaterMark(processingTask)) : 0,
          processingTask != nullptr};
}
