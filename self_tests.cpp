#include "self_tests.h"
#include "environment_history.h"
#include "pickup_detector.h"
#include "pickup_events.h"
#include <cmath>

bool runEnvironmentHistorySelfTests(EnvironmentHistory &history) {
  history.clear();
  auto sample = [](unsigned i) {
    PublishedSample p;
    p.sequence = i;
    p.sampleMs = i * 1000;
    p.count = 5;
    p.ok = true;
    p.temperature.mean = float(i);
    p.temperature.minimum = -float(i);
    p.temperature.maximum = float(i) + 1;
    p.humidity = p.pressure = p.temperature;
    p.mpuTemperatureC = 30 + float(i);
    p.mpuTemperatureOk = true;
    return p;
  };
  for (unsigned i = 1; i <= 31; ++i)
    history.add(sample(i));
  unsigned fine = 0, coarse = 0;
  bool correct = true;
  history.visit(0, 31000, [&](const EnvironmentHistory::Point &p, bool old) {
    if (old) {
      ++coarse;
      correct &= p.firstSequence == 1 && p.reports == 1;
    } else
      ++fine;
  });
  if (!correct || fine != 30 || coarse != 1)
    return false;
  for (unsigned i = 32; i <= 60; ++i)
    history.add(sample(i));
  bool found = false;
  history.visit(59, 60000, [&](const EnvironmentHistory::Point &p, bool old) {
    if (old) {
      found = true;
      correct &= p.reports == 30 && p.samples == 150 && p.temperature.mean == 15.5f &&
                 p.temperature.minimum == -30 && p.temperature.maximum == 31 &&
                 p.mpuSamples == 30 && p.mpuTemperatureC == 45.5f;
    }
  });
  if (!found || !correct || history.selected(59, 60000) != 2)
    return false;
  for (unsigned i = 61; i <= 1800; ++i)
    history.add(sample(i));
  if (history.selected(0, 1800000) > EnvironmentHistory::MAX_POINTS)
    return false;
  history.clear();
  auto a = sample(1);
  a.count = 1;
  a.temperature.mean = 10;
  history.add(a);
  auto b = sample(2);
  b.count = 3;
  b.temperature.mean = 20;
  history.add(b);
  history.add(sample(32));
  found = false;
  history.visit(0, 32000, [&](const EnvironmentHistory::Point &p, bool old) {
    if (old) {
      found = true;
      correct &= p.temperature.mean == 17.5f;
    }
  });
  if (!found || !correct)
    return false;
  history.clear();
  a = sample(1);
  a.ok = false;
  history.add(a);
  history.add(sample(31));
  history.visit(0, 31000, [&](const EnvironmentHistory::Point &p, bool old) {
    if (old)
      correct &= !p.ok && p.mpuSamples == 1;
  });
  history.clear();
  a = sample(1);
  a.mpuTemperatureOk = false;
  history.add(a);
  history.add(sample(31));
  history.visit(0, 31000, [&](const EnvironmentHistory::Point &p, bool old) {
    if (old)
      correct &= p.ok && p.mpuSamples == 0;
  });
  history.clear();
  return correct;
}

bool runPickupSelfTests(PickupDetector &p) {
  const float flat[3] = {0, 0, 9.80665f}, still[3] = {0, 0, 0};
  auto fresh = [&]() {
    p.settings = PickupSettings{};
    p.reset(false);
  };
  auto feed = [&](const float *a, const float *w, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
      p.process(a, w);
  };
  fresh();
  feed(flat, still, 299);
  if (p.armed || p.active)
    return false;
  feed(flat, still, 1);
  if (!p.armed || p.active)
    return false;
  feed(flat, still, 600);
  if (p.active)
    return false;
  const float bump[3] = {0, 0, 12};
  feed(bump, still, 1);
  feed(flat, still, 100);
  if (p.active)
    return false;
  const float lift[3] = {0, 0, 11.5f};
  feed(lift, still, 80);
  if (!p.active)
    return false;
  feed(flat, still, 200);
  if (!p.active)
    return false;
  feed(flat, still, 110);
  if (p.active || !p.armed)
    return false;
  fresh();
  feed(flat, still, 300);
  const float tilt[3] = {4.903325f, 0, 8.492809f};
  feed(tilt, still, 80);
  if (!p.active)
    return false;
  feed(tilt, still, 310);
  if (p.active || !p.armed)
    return false; // New orientation can become stationary.
  fresh();
  feed(flat, still, 300);
  const float spin[3] = {0, 0, 0.5f};
  feed(flat, spin, 80);
  if (!p.active)
    return false;
  if (!(p.confirmedTriggerMask & 4) || p.quietBlockMask != 2)
    return false;
  feed(flat, spin, 400);
  if (!p.active)
    return false; // Rotation cannot clear the alert.
  if (p.quietMs() != 0 || p.quietBlockMask != 2)
    return false; // Exact gate blocking clearing.
  if (p.rotationRate < 0.4f || p.quietWindowEvaluated)
    return false; // Live alert telemetry must not freeze.
  p.reset();
  if (!p.active || p.armed)
    return false; // Sensor faults preserve alert.
  feed(flat, still, 299);
  if (!p.active)
    return false;
  feed(flat, still, 1);
  if (p.active || !p.armed)
    return false;
  feed(flat, spin, 80);
  if (!p.active)
    return false;
  p.configure(PickupSettings{});
  if (!p.active || p.armed)
    return false;
  feed(flat, still, 300);
  if (p.active || !p.armed)
    return false;
  fresh();
  feed(flat, spin, 600);
  if (p.armed || p.active)
    return false;
  // Adaptive noise and thresholds remain finite for a perfectly quiet baseline.
  fresh();
  feed(flat, still, 300);
  if (!std::isfinite(p.baselineNoise))
    return false;
  PickupSettings longer;
  longer.stationary_ms = 10000;
  if (!p.configure(longer))
    return false;
  feed(flat, still, 999);
  if (p.armed)
    return false;
  feed(flat, still, 1);
  if (!p.armed)
    return false; // Growing retains the full configurable ten-second window.
  if (!p.configure(PickupSettings{}))
    return false; // Shrinking releases unused samples.
  fresh();
  return true;
}

bool runPickupEventSelfTests() {
  PickupEventLog test;
  if (test.listText() != "No pickup events.\n")
    return false;
  if (test.eventJson(0) != "{\"slot\":0,\"event\":null}")
    return false;
  for (unsigned i = 1; i <= 12; ++i) {
    test.observe(true, 5);
    test.observe(false, 0);
  }
  if (test.metadataJson().indexOf("\"count\":10") < 0)
    return false;
  if (test.eventJson(0).indexOf("\"id\":12,") < 0 || test.eventJson(9).indexOf("\"id\":3,") < 0)
    return false;
  if (test.eventJson(0).indexOf("\"state\":\"complete\"") < 0 ||
      test.eventJson(0).indexOf("\"reason_mask\":5") < 0)
    return false;
  test.observe(true, 2);
  const String active = test.eventJson(0);
  if (active.indexOf("\"id\":13,") < 0 || active.indexOf("\"state\":\"active\"") < 0 ||
      active.indexOf("\"reasons\":[\"tilt\"]") < 0)
    return false;
  test.observe(true, 7); // Later motion must not overwrite the confirming-frame reason.
  if (test.eventJson(0).indexOf("\"reason_mask\":2") < 0)
    return false;
  // Maximum-width rows must include every event without truncation at the GATT limit.
  PickupEventLog::Event worst[10];
  for (auto &e : worst) {
    e.startedUtc = 1704067200;
    e.durationMs = 9999000;
    e.state = PickupEventLog::ACTIVE;
    e.reasons = 7;
  }
  const String full = PickupEventLog::formatList(worst, 10);
  if (full.length() != 512 || !full.endsWith("active|accel+tilt+gyro\n"))
    return false;
  worst[0].state = PickupEventLog::INTERRUPTED;
  if (PickupEventLog::formatList(worst, 1).indexOf("|?|reboot|") < 0)
    return false;
  worst[0].state = PickupEventLog::COMPLETE;
  worst[0].durationMs = UINT64_MAX;
  return PickupEventLog::formatList(worst, 1).indexOf("|>999y|done|") >= 0;
}
