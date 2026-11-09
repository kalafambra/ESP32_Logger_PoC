#pragma once
#include <stddef.h>
#include <stdint.h>
#include <esp_err.h>
#include "audio_config.h"

// INMP441: Philips I2S, 24 significant bits in each 32-bit slot.
// The ESP32 supplies both clocks; L/R=GND selects the WS-low (left) slot.
constexpr uint32_t MICROPHONE_SAMPLE_RATE_HZ = AUDIO_SAMPLE_RATE_HZ;
constexpr size_t MICROPHONE_BLOCK_SAMPLES = 256;
constexpr unsigned MICROPHONE_DMA_BUFFERS = 6;
constexpr unsigned MICROPHONE_DMA_CAPACITY_MS =
    MICROPHONE_DMA_BUFFERS * MICROPHONE_BLOCK_SAMPLES * 1000 / MICROPHONE_SAMPLE_RATE_HZ;

struct MicrophoneStatus {
  bool ready, paused;
  esp_err_t error;
  uint32_t samples, overflows;
};
struct AudioBlock {
  const int32_t *samples;
  size_t count;
};

bool setupMicrophone();
void stopMicrophone();
void pauseMicrophone();
void resumeMicrophone();
MicrophoneStatus microphoneStatus();

// One bounded nonblocking DMA read. Returned samples are signed 24-bit values
// in 32-bit containers. Consume before the next call; storage belongs here.
// Setup/loop/OTA callbacks only; the private overflow ISR updates a counter.
AudioBlock serviceMicrophone();
