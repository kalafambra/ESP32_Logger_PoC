#pragma once
#include <stdint.h>
#include "audio_config.h"

struct MicrophoneSpectrum {
  bool valid = false;
  float binsDb[AUDIO_FFT_SIZE / 2 + 1] = {};
  float rmsDb = -120, bandsDb[3] = {-120, -120, -120}, peakHz = 0;
  unsigned frames = 0, clipped = 0;
  uint32_t sequence = 0, sampleMs = 0;
};

// Main-loop only. A normal publication retains a partial FFT frame; OTA reset
// discards it. DMA gaps discard partial frames but retain completed averages.
void resetSpectrumWindow();
void serviceMicrophoneAudio(); // At most MICROPHONE_DMA_BUFFERS reads per visit.
void logMicrophoneDiagnostics();
const MicrophoneSpectrum &microphoneSpectrum(); // Valid until next service/reset.
