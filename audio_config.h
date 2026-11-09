#pragma once
#include <stdint.h>

// Shared by I2S capture, FFT analysis, spectrum averaging and API metadata.
constexpr unsigned AUDIO_FFT_SIZE = 256;
constexpr uint32_t AUDIO_SAMPLE_RATE_HZ = 16000;
constexpr uint32_t AUDIO_SPECTRUM_AVERAGE_MS = 1000;

static_assert(AUDIO_FFT_SIZE >= 128 && (AUDIO_FFT_SIZE & (AUDIO_FFT_SIZE - 1)) == 0,
              "AUDIO_FFT_SIZE must be a power of two, at least 128");
static_assert(AUDIO_FFT_SIZE <= 2048, "Larger FFTs require a RAM redesign on ESP32-WROOM-32");
static_assert(AUDIO_SAMPLE_RATE_HZ >= 8000 && AUDIO_SAMPLE_RATE_HZ <= 48000,
              "Use an INMP441 sample rate between 8000 and 48000 Hz");
static_assert(AUDIO_SPECTRUM_AVERAGE_MS >=
                      (AUDIO_FFT_SIZE * 1000 + AUDIO_SAMPLE_RATE_HZ - 1) / AUDIO_SAMPLE_RATE_HZ &&
                  AUDIO_SPECTRUM_AVERAGE_MS <= 60000,
              "Averaging interval must fit at least one FFT frame and be at most 60 seconds");
constexpr uint32_t AUDIO_SPECTRUM_STALE_MS = AUDIO_SPECTRUM_AVERAGE_MS * 2 + 500;
