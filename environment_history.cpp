#include "environment_history.h"

void EnvironmentHistory::clear() {
  recentHead = recentCount = oldHead = oldCount = 0;
  pending = Point{};
}

void EnvironmentHistory::add(const PublishedSample &sample) {
  while (recentCount &&
         (recentCount == RECENT_CAPACITY || sample.sampleMs - oldestRecent().endMs >= FINE_MS)) {
    promote(oldestRecent(), sample.sequence);
    --recentCount;
  }
  Point p;
  p.firstSequence = p.sequence = sample.sequence;
  p.startMs = p.endMs = sample.sampleMs;
  p.revision = sample.sequence;
  p.samples = sample.count;
  p.reports = 1;
  p.ok = sample.ok;
  p.temperature = sample.temperature;
  p.humidity = sample.humidity;
  p.pressure = sample.pressure;
  p.mpuTemperatureC = sample.mpuTemperatureC;
  p.mpuSamples = sample.mpuTemperatureOk ? 1 : 0;
  recent[recentHead] = p;
  recentHead = (recentHead + 1) % RECENT_CAPACITY;
  ++recentCount;
}

unsigned EnvironmentHistory::selected(uint32_t since, uint32_t now) const {
  unsigned count = 0;
  visit(since, now, [&](const Point &, bool) { ++count; });
  return count;
}

const EnvironmentHistory::Point &EnvironmentHistory::oldestRecent() const {
  return recent[(recentHead + RECENT_CAPACITY - recentCount) % RECENT_CAPACITY];
}

void EnvironmentHistory::mergeChannel(PublishedChannel &to, const PublishedChannel &from,
                                      unsigned n, unsigned added) {
  if (!added)
    return;
  if (!n) {
    to = from;
    return;
  }
  to.mean = (double(to.mean) * n + double(from.mean) * added) / (n + added);
  if (from.minimum < to.minimum)
    to.minimum = from.minimum;
  if (from.maximum > to.maximum)
    to.maximum = from.maximum;
}

void EnvironmentHistory::promote(const Point &p, uint32_t revision) {
  if (pending.reports && (p.endMs - 1) / BUCKET_MS != (pending.endMs - 1) / BUCKET_MS) {
    old[oldHead] = pending;
    oldHead = (oldHead + 1) % OLD_CAPACITY;
    if (oldCount < OLD_CAPACITY)
      ++oldCount;
    pending = Point{};
  }
  if (!pending.reports)
    pending = p;
  else {
    mergeChannel(pending.temperature, p.temperature, pending.samples, p.samples);
    mergeChannel(pending.humidity, p.humidity, pending.samples, p.samples);
    mergeChannel(pending.pressure, p.pressure, pending.samples, p.samples);
    if (p.mpuSamples) {
      pending.mpuTemperatureC = (double(pending.mpuTemperatureC) * pending.mpuSamples +
                                 double(p.mpuTemperatureC) * p.mpuSamples) /
                                (pending.mpuSamples + p.mpuSamples);
      pending.mpuSamples += p.mpuSamples;
    }
    pending.sequence = p.sequence;
    pending.endMs = p.endMs;
    pending.samples += p.samples;
    ++pending.reports;
    pending.ok = pending.ok && p.ok;
  }
  pending.revision = revision;
}
