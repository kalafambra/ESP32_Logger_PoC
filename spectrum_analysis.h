#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include "audio_config.h"

// Hardware-independent streaming DSP. Feed contiguous signed 24-bit PCM samples.
// A completed frame removes DC, applies a periodic Hann window and computes
// one-sided RMS power per bin. Power is digital full scale, not calibrated SPL.
struct SpectrumAnalysis {
  static constexpr unsigned SIZE = AUDIO_FFT_SIZE, BINS = SIZE / 2 + 1;
  static constexpr float RATE = AUDIO_SAMPLE_RATE_HZ, BIN_HZ = RATE / SIZE;
  float real[SIZE] = {}, imag[SIZE] = {}, window[SIZE / 2 + 1] = {};
  // power aliases imaginary FFT scratch after the transform. Only BINS entries
  // are valid, and the next push() that completes a frame overwrites them.
  float *power = imag;
  float windowEnergy = 0, rmsPower = 0;
  unsigned count = 0, clipped = 0;

  SpectrumAnalysis();
  // The power pointer refers to this object's scratch; copies would alias it.
  SpectrumAnalysis(const SpectrumAnalysis &) = delete;
  SpectrumAnalysis &operator=(const SpectrumAnalysis &) = delete;
  static float db(float power) { return 10.0f * log10f(fmaxf(power, 1e-12f)); }
  // Discard only an unfinished frame. Existing output remains until overwritten.
  void reset() { count = 0; }
  // True only when a full frame has been analyzed; consume output before feeding more.
  bool push(int32_t sample);

private:
  void analyze();
};
