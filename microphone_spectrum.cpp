#include "microphone_spectrum.h"
#include <Arduino.h>
#include <WiFi.h>
#include "microphone.h"
#include "spectrum_analysis.h"

namespace {
SpectrumAnalysis microphoneFft;
static_assert(MICROPHONE_SAMPLE_RATE_HZ == unsigned(SpectrumAnalysis::RATE),
              "FFT sample rate must match I2S capture");
MicrophoneSpectrum report;
float spectrumSum[SpectrumAnalysis::BINS] = {};
float spectrumRmsSum = 0;
unsigned spectrumFrames = 0, spectrumClipped = 0;
uint32_t spectrumWindowMs = 0, spectrumLastOverflow = 0;
void serviceMicrophoneSpectrum(const AudioBlock &block) {
  const uint32_t now = millis();
  const MicrophoneStatus microphone = microphoneStatus();
  // Never join samples across known DMA gaps or an OTA pause.
  if (!microphone.ready || microphone.paused) {
    resetSpectrumWindow();
    return;
  }
  if (spectrumLastOverflow != microphone.overflows) {
    spectrumLastOverflow = microphone.overflows;
    microphoneFft.reset(); // Keep complete frames already averaged; discard only the partial frame.
  }
  for (size_t i = 0; i < block.count; ++i) {
    if (!microphoneFft.push(block.samples[i]))
      continue;
    for (unsigned k = 0; k < SpectrumAnalysis::BINS; ++k)
      spectrumSum[k] += microphoneFft.power[k];
    spectrumRmsSum += microphoneFft.rmsPower;
    spectrumClipped += microphoneFft.clipped;
    ++spectrumFrames;
  }
  if (now - spectrumWindowMs < AUDIO_SPECTRUM_AVERAGE_MS || !spectrumFrames)
    return;
  float bands[3] = {}, peak = 0;
  report.peakHz = 0;
  for (unsigned k = 0; k < SpectrumAnalysis::BINS; ++k) {
    const float power = spectrumSum[k] / spectrumFrames, hz = k * SpectrumAnalysis::BIN_HZ;
    report.binsDb[k] = SpectrumAnalysis::db(power);
    if (hz >= 80) {
      bands[hz < 500 ? 0 : hz < 2000 ? 1 : 2] += power;
      if (power > peak) {
        peak = power;
        report.peakHz = hz;
      }
    }
  }
  for (unsigned i = 0; i < 3; ++i)
    report.bandsDb[i] = SpectrumAnalysis::db(bands[i]);
  report.rmsDb = SpectrumAnalysis::db(spectrumRmsSum / spectrumFrames);
  report.frames = spectrumFrames;
  report.clipped = spectrumClipped;
  report.sampleMs = now;
  ++report.sequence;
  report.valid = true;
  // Keep a partial FFT frame between normal publication windows.
  for (float &value : spectrumSum)
    value = 0;
  spectrumRmsSum = 0;
  spectrumFrames = 0;
  spectrumClipped = 0;
  spectrumWindowMs = now;
}
} // namespace

void resetSpectrumWindow() {
  report.valid = false;
  microphoneFft.reset();
  for (float &value : spectrumSum)
    value = 0;
  spectrumRmsSum = 0;
  spectrumFrames = 0;
  spectrumClipped = 0;
  spectrumWindowMs = millis();
}

// Catch up after synchronous I2C/HTTP work instead of reading only 16 ms of
// audio per loop. The descriptor count bounds CPU work, including FFTs.
void serviceMicrophoneAudio() {
  for (unsigned block = 0; block < MICROPHONE_DMA_BUFFERS; ++block) {
    const AudioBlock samples = serviceMicrophone();
    serviceMicrophoneSpectrum(samples);
    if (!samples.count)
      break;
  }
}

void logMicrophoneDiagnostics() {
  static uint32_t lastLog = 0;
  const uint32_t now = millis();
  const MicrophoneStatus microphone = microphoneStatus();
  if (now - lastLog < 5000)
    return;
  lastLog = now;
  Serial.printf("[audio] sequence=%lu age=%lu ms frames=%u samples=%lu DMA-overflows=%lu | WiFi=%d "
                "RSSI=%d dBm | heap=%u largest=%u\n",
                (unsigned long)report.sequence, (unsigned long)(now - report.sampleMs),
                report.frames, (unsigned long)microphone.samples,
                (unsigned long)microphone.overflows, int(WiFi.status()), int(WiFi.RSSI()),
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

const MicrophoneSpectrum &microphoneSpectrum() {
  return report;
}
