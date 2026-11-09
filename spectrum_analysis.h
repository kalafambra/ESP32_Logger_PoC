#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include "audio_config.h"

// No hardware dependencies: the exact DSP implementation is also tested on the host.
struct SpectrumAnalysis {
  static constexpr unsigned SIZE = AUDIO_FFT_SIZE, BINS = SIZE / 2 + 1;
  static constexpr float RATE = AUDIO_SAMPLE_RATE_HZ, BIN_HZ = RATE / SIZE;
  float real[SIZE] = {}, imag[SIZE] = {}, window[SIZE / 2 + 1] = {};
  // The imaginary FFT scratch is no longer needed once power is extracted.
  float *power = imag;
  float windowEnergy = 0, rmsPower = 0;
  unsigned count = 0, clipped = 0;

  SpectrumAnalysis() {
    for (unsigned i = 0; i <= SIZE / 2; ++i) {
      window[i] = 0.5f - 0.5f * cosf(6.28318530718f * i / SIZE);
      windowEnergy += window[i] * window[i] * (i == 0 || i == SIZE / 2 ? 1 : 2);
    }
  }
  static float db(float power) { return 10.0f * log10f(fmaxf(power, 1e-12f)); }
  void reset() { count = 0; }
  bool push(int32_t sample) {
    real[count++] = sample / 8388608.0f;
    if (count != SIZE) return false;
    count = 0;
    analyze();
    return true;
  }
  void analyze() {
    float mean = 0;
    clipped = 0;
    for (unsigned i = 0; i < SIZE; ++i) {
      mean += real[i];
      if (fabsf(real[i]) >= 0.999f) ++clipped;
    }
    mean /= SIZE;
    rmsPower = 0;
    for (unsigned i = 0; i < SIZE; ++i) {
      const float x = real[i] - mean;
      rmsPower += x * x / SIZE;
      real[i] = x * window[i <= SIZE / 2 ? i : SIZE - i];
      imag[i] = 0;
    }
    // Iterative radix-2 FFT: fixed storage, no allocations or trigonometry per butterfly.
    for (unsigned i = 1, j = 0; i < SIZE; ++i) {
      unsigned bit = SIZE >> 1;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit;
      if (i < j) {
        float t = real[i]; real[i] = real[j]; real[j] = t;
        t = imag[i]; imag[i] = imag[j]; imag[j] = t;
      }
    }
    for (unsigned length = 2; length <= SIZE; length <<= 1) {
      const float angle = -6.28318530718f / length;
      const float stepR = cosf(angle), stepI = sinf(angle);
      for (unsigned base = 0; base < SIZE; base += length) {
        float wr = 1, wi = 0;
        for (unsigned j = 0; j < length / 2; ++j) {
          const unsigned a = base + j, b = a + length / 2;
          const float tr = wr * real[b] - wi * imag[b], ti = wr * imag[b] + wi * real[b];
          real[b] = real[a] - tr; imag[b] = imag[a] - ti;
          real[a] += tr; imag[a] += ti;
          const float next = wr * stepR - wi * stepI;
          wi = wr * stepI + wi * stepR; wr = next;
        }
      }
    }
    for (unsigned k = 0; k < BINS; ++k) {
      // One-sided RMS power per bin, Hann energy corrected (not a density or SPL).
      const float factor = k == 0 || k == SIZE / 2 ? 1.0f : 2.0f;
      power[k] = factor * (real[k] * real[k] + imag[k] * imag[k]) / (SIZE * windowEnergy);
    }
  }
};
