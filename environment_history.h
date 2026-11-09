#pragma once
#include <cstdint>
#include "aggregation.h"

/* Keep fine detail for 30 s; older data uses bounded 30 s buckets across a ten-minute graph. */
class EnvironmentHistory {
public:
  static constexpr unsigned RECENT_CAPACITY = 30, OLD_CAPACITY = 20, MAX_POINTS = 51;
  static constexpr uint32_t FINE_MS = 30000, BUCKET_MS = 30000, RETENTION_MS = 600000;
  struct Point {
    uint32_t firstSequence = 0, sequence = 0, startMs = 0, endMs = 0, revision = 0;
    uint16_t samples = 0, reports = 0;
    bool ok = false;
    PublishedChannel temperature, humidity, pressure;
    float mpuTemperatureC = 0;
    uint16_t mpuSamples = 0;
    /* Use the bucket centre so an average is plotted over the period it represents. */
    uint32_t midpoint() const { return startMs + (endMs - startMs) / 2; }
  };

  /* Clear only volatile graph storage; sensor reporting and persistent pickup events are
   * independent. */
  void clear();

  /* Promote expired fine reports before inserting; a delayed loop never retains old fine detail. */
  void add(const PublishedSample &sample);

  /* A revision cursor resends changed old buckets even though their source sequences are older. */
  template <typename Visitor> void visit(uint32_t since, uint32_t now, Visitor visitor) const {
    for (unsigned i = 0; i < oldCount; ++i)
      send(old[(oldHead + OLD_CAPACITY - oldCount + i) % OLD_CAPACITY], true, since, now, visitor);
    if (pending.reports)
      send(pending, true, since, now, visitor);
    for (unsigned i = 0; i < recentCount; ++i)
      send(recent[(recentHead + RECENT_CAPACITY - recentCount + i) % RECENT_CAPACITY], false, since,
           now, visitor);
  }

  /* Estimate the response allocation from selected rows rather than reserving 16 KB on every poll.
   */
  unsigned selected(uint32_t since, uint32_t now) const;

private:
  Point recent[RECENT_CAPACITY], old[OLD_CAPACITY], pending;
  unsigned recentHead = 0, recentCount = 0, oldHead = 0, oldCount = 0;

  /* Ring position depends on retained count, allowing several old reports to expire at once. */
  const Point &oldestRecent() const;

  /* Invalid constituent reports invalidate a whole coarse bucket; no sensor gaps are hidden. */
  static void mergeChannel(PublishedChannel &to, const PublishedChannel &from, unsigned n,
                           unsigned added);

  /* Time-aligned buckets remain 30-second windows even when source reports are delayed or missing.
   */
  void promote(const Point &p, uint32_t revision);

  /* Expire by age and send only changed records; the browser owns its already-received history. */
  template <typename Visitor>
  static void send(const Point &p, bool coarse, uint32_t since, uint32_t now, Visitor &visitor) {
    if (p.reports && p.revision > since && now - p.midpoint() <= RETENTION_MS)
      visitor(p, coarse);
  }
};
