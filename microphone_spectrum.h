#pragma once
#include "microphone.h"
#include "spectrum_analysis.h"

SpectrumAnalysis microphoneFft;
static_assert(MICROPHONE_SAMPLE_RATE_HZ == unsigned(SpectrumAnalysis::RATE), "FFT sample rate must match I2S capture");
bool spectrumValid = false;
float spectrumSum[SpectrumAnalysis::BINS] = {}, spectrumDb[SpectrumAnalysis::BINS] = {};
float spectrumRmsSum = 0, spectrumRmsDb = -120, spectrumBandsDb[3] = {-120,-120,-120};
float spectrumPeakHz = 0;
unsigned spectrumFrames = 0, spectrumPublishedFrames = 0, spectrumClipped = 0, spectrumPublishedClipped = 0;
uint32_t spectrumSequence = 0, spectrumMs = 0, spectrumWindowMs = 0, spectrumLastOverflow = 0;

void resetSpectrumWindow() {
  spectrumValid = false;
  microphoneFft.reset();
  for (float &value : spectrumSum) value = 0;
  spectrumRmsSum = 0; spectrumFrames = 0; spectrumClipped = 0;
  spectrumWindowMs = millis();
}

void serviceMicrophoneSpectrum() {
  const uint32_t now = millis();
  // Never join samples across known DMA gaps or an OTA pause.
  if (!microphoneReady || microphonePaused) {
    resetSpectrumWindow();
    return;
  }
  if (spectrumLastOverflow != microphoneOverflows) {
    spectrumLastOverflow = microphoneOverflows;
    microphoneFft.reset(); // Keep complete frames already averaged; discard only the partial frame.
  }
  for (size_t i = 0; i < microphoneBlockCount; ++i) {
    if (!microphoneFft.push(microphoneBlock[i])) continue;
    for (unsigned k = 0; k < SpectrumAnalysis::BINS; ++k) spectrumSum[k] += microphoneFft.power[k];
    spectrumRmsSum += microphoneFft.rmsPower;
    spectrumClipped += microphoneFft.clipped;
    ++spectrumFrames;
  }
  if (now - spectrumWindowMs < AUDIO_SPECTRUM_AVERAGE_MS || !spectrumFrames) return;
  float bands[3] = {}, peak = 0;
  spectrumPeakHz = 0;
  for (unsigned k = 0; k < SpectrumAnalysis::BINS; ++k) {
    const float power = spectrumSum[k] / spectrumFrames, hz = k * SpectrumAnalysis::BIN_HZ;
    spectrumDb[k] = SpectrumAnalysis::db(power);
    if (hz >= 80) {
      bands[hz < 500 ? 0 : hz < 2000 ? 1 : 2] += power;
      if (power > peak) { peak = power; spectrumPeakHz = hz; }
    }
  }
  for (unsigned i = 0; i < 3; ++i) spectrumBandsDb[i] = SpectrumAnalysis::db(bands[i]);
  spectrumRmsDb = SpectrumAnalysis::db(spectrumRmsSum / spectrumFrames);
  spectrumPublishedFrames = spectrumFrames;
  spectrumPublishedClipped = spectrumClipped;
  spectrumMs = now; ++spectrumSequence;
  spectrumValid = true;
  // Keep a partial FFT frame between normal publication windows.
  for (float &value : spectrumSum) value = 0;
  spectrumRmsSum = 0; spectrumFrames = 0; spectrumClipped = 0; spectrumWindowMs = now;
}

// Catch up after synchronous I2C/HTTP work instead of reading only 16 ms of
// audio per loop. The descriptor count bounds CPU work, including FFTs.
void serviceMicrophoneAudio() {
  for (unsigned block = 0; block < MICROPHONE_DMA_BUFFERS; ++block) {
    serviceMicrophone();
    serviceMicrophoneSpectrum();
    if (!microphoneBlockCount) break;
  }
}

void logMicrophoneDiagnostics() {
  static uint32_t lastLog = 0;
  const uint32_t now = millis();
  if (now - lastLog < 5000) return;
  lastLog = now;
  Serial.printf("[audio] sequence=%lu age=%lu ms frames=%u samples=%lu DMA-overflows=%lu | WiFi=%d RSSI=%d dBm | heap=%u largest=%u\n",
                (unsigned long)spectrumSequence, (unsigned long)(now-spectrumMs), spectrumPublishedFrames,
                (unsigned long)microphoneSamples, (unsigned long)microphoneOverflows,
                int(WiFi.status()), int(WiFi.RSSI()), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

void sendMicrophoneSpectrum() {
  const uint32_t age = millis() - spectrumMs;
  const bool ok = microphoneReady && !microphonePaused && spectrumValid && age < AUDIO_SPECTRUM_STALE_MS &&
                  (microphoneError == ESP_OK || microphoneError == ESP_ERR_TIMEOUT);
  String json;
  json.reserve(600 + SpectrumAnalysis::BINS * 8);
  json = "{\"ok\":" + String(ok ? "true" : "false") + ",\"error\":\"";
  json += !microphoneReady ? "Microphone initialization failed" : microphonePaused ? "Microphone paused for OTA" :
          !spectrumValid ? "Waiting for audio spectrum" : age >= AUDIO_SPECTRUM_STALE_MS ? "Audio spectrum stale" :
          !ok ? esp_err_to_name(microphoneError) : "";
  json += "\",\"sequence\":" + String(spectrumSequence) + ",\"age_ms\":" + String(age) +
          ",\"sample_rate_hz\":" + String(MICROPHONE_SAMPLE_RATE_HZ) + ",\"fft_size\":" + String(SpectrumAnalysis::SIZE) +
          ",\"bin_hz\":" + String(SpectrumAnalysis::BIN_HZ, 9) + ",\"frames\":" + String(spectrumPublishedFrames) +
          ",\"dma_overflows\":" + String(microphoneOverflows) + ",\"clipped_samples\":" + String(spectrumPublishedClipped);
  json += ",\"audio_samples\":" + String(microphoneSamples) + ",\"dma_capacity_ms\":" + String(MICROPHONE_DMA_CAPACITY_MS);
  json += ",\"average_target_ms\":" + String(AUDIO_SPECTRUM_AVERAGE_MS) + ",\"stale_after_ms\":" + String(AUDIO_SPECTRUM_STALE_MS);
  json += ",\"averaged_audio_ms\":" + String(1000.0f * spectrumPublishedFrames * SpectrumAnalysis::SIZE / SpectrumAnalysis::RATE, 1);
  json += ",\"i2c_enabled\":" + String(I2C_SENSORS_ENABLED ? "true" : "false");
  json += ",\"rms_dbfs\":" + (ok ? String(spectrumRmsDb, 1) : String("null"));
  json += ",\"peak_hz\":" + (ok && spectrumRmsDb > -119 ? String(spectrumPeakHz, 2) : String("null"));
  json += ",\"bands_dbfs\":[";
  for (unsigned i = 0; i < 3; ++i) { if (i) json += ','; json += ok ? String(spectrumBandsDb[i], 1) : String("null"); }
  json += "],\"bins_dbfs\":[";
  if (ok) for (unsigned i = 0; i < SpectrumAnalysis::BINS; ++i) { if (i) json += ','; json += String(spectrumDb[i], 1); }
  json += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}
