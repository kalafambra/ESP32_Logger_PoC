#include "spectrum_analysis.h"

SpectrumAnalysis::SpectrumAnalysis() {
  for (unsigned i = 0; i <= SIZE / 2; ++i) {
    window[i] = 0.5f - 0.5f * cosf(6.28318530718f * i / SIZE);
    windowEnergy += window[i] * window[i] * (i == 0 || i == SIZE / 2 ? 1 : 2);
  }
}

bool SpectrumAnalysis::push(int32_t sample) {
  real[count++] = sample / 8388608.0f;
  if (count != SIZE)
    return false;
  count = 0;
  analyze();
  return true;
}

void SpectrumAnalysis::analyze() {
  float mean = 0;
  clipped = 0;
  for (unsigned i = 0; i < SIZE; ++i) {
    mean += real[i];
    if (fabsf(real[i]) >= 0.999f)
      ++clipped;
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
    for (; j & bit; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j) {
      float t = real[i];
      real[i] = real[j];
      real[j] = t;
      t = imag[i];
      imag[i] = imag[j];
      imag[j] = t;
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
        real[b] = real[a] - tr;
        imag[b] = imag[a] - ti;
        real[a] += tr;
        imag[a] += ti;
        const float next = wr * stepR - wi * stepI;
        wi = wr * stepI + wi * stepR;
        wr = next;
      }
    }
  }
  for (unsigned k = 0; k < BINS; ++k) {
    // One-sided RMS power per bin, Hann energy corrected (not a density or SPL).
    const float factor = k == 0 || k == SIZE / 2 ? 1.0f : 2.0f;
    power[k] = factor * (real[k] * real[k] + imag[k] * imag[k]) / (SIZE * windowEnergy);
  }
}
