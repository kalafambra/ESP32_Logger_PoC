#pragma once

struct ChannelStats {
  float sum = 0;
  float minimum = 0;
  float maximum = 0;
  /* Preserve sums and extrema without retaining every raw reading in RAM. */
  void add(float value, bool first) {
    sum += value;
    // Seed extrema from real data so negative values are not biased by zero.
    if (first || value < minimum)
      minimum = value;
    if (first || value > maximum)
      maximum = value;
  }
};

struct SampleWindow {
  unsigned count = 0;
  ChannelStats temperature, humidity, pressure;
  /* Count one complete three-channel reading so all channel means share a window. */
  void add(float t, float h, float p) {
    temperature.add(t, count == 0);
    humidity.add(h, count == 0);
    pressure.add(p, count == 0);
    ++count; // Increment only after all three channels have been accumulated.
  }
};

struct PublishedChannel {
  float mean = 0, minimum = 0, maximum = 0;
  /* Snapshot the arithmetic mean and bounds for a report that outlives its window. */
  void finish(const ChannelStats &stats, unsigned count) {
    // Empty/error windows are serialized as null elsewhere; avoid division by zero.
    mean = count ? stats.sum / count : 0;
    minimum = stats.minimum;
    maximum = stats.maximum;
  }
};

struct PublishedSample {
  unsigned long sequence = 0, sampleMs = 0;
  unsigned count = 0;
  bool ok = false;
  const char *error = "Waiting for first one-second window";
  PublishedChannel temperature, humidity, pressure;
  float mpuTemperatureC = 0;
  bool mpuTemperatureOk = false;
};
