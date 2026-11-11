#pragma once
#include <stdint.h>

struct DeliveryRequest { uint32_t client = 0, boot = 0, ack = 0; };
struct DeliveryInfo {
  uint32_t acknowledged = 0, dropped = 0, oldest = 0;
  bool gap = false;
};
uint32_t deliveryBootId();
class String;
void appendDeliveryInfo(String &json, const DeliveryInfo &info);
bool runDeliverySelfTests();

// Single debug subscriber. Only unacknowledged records are retained, for <=3 s.
// Owner publishes; HTTP takes a bounded copy under ProcessingStateLock.
template <typename Record, unsigned Capacity> class DashboardDelivery {
public:
  struct Snapshot { Record rows[Capacity]; unsigned count = 0; DeliveryInfo info; };
  void publish(const Record &record, uint32_t now) {
    if (!client || now - lastRequest > 3000) { client = 0; head = count = 0; return; }
    if (!count && !sent && !acknowledged) acknowledged = sent = record.sequence - 1;
    if (count == Capacity) { head = (head + 1) % Capacity; --count; ++dropped; }
    rows[(head + count) % Capacity] = record;
    ++count;
  }
  void copy(const DeliveryRequest &request, uint32_t boot, uint32_t now,
            const Record &latest, Snapshot &out) {
    out.count = 0;
    out.info = DeliveryInfo{};
    if (!request.client) return; // Read-only diagnostic callers never consume delivery.
    if (client != request.client || now - lastRequest > 3000) {
      client = request.client; head = count = dropped = 0;
      acknowledged = latest.sequence ? latest.sequence - 1 : 0;
      sent = acknowledged;
      lastRequest = now;
      if (latest.sequence) publish(latest, now);
    }
    lastRequest = now;
    // Ignore stale-boot and future ACKs. A send is not an acknowledgment.
    if (request.boot == boot && request.ack >= acknowledged && request.ack <= sent) {
      acknowledged = request.ack;
      while (count && rows[head].sequence <= acknowledged) {
        head = (head + 1) % Capacity; --count;
      }
    }
    for (unsigned i = 0; i < count; ++i) out.rows[out.count++] = rows[(head + i) % Capacity];
    if (count) sent = out.rows[out.count - 1].sequence;
    out.info.acknowledged = acknowledged;
    out.info.dropped = dropped;
    out.info.oldest = count ? rows[head].sequence : 0;
    out.info.gap = count && rows[head].sequence > acknowledged + 1;
  }
private:
  Record rows[Capacity];
  unsigned head = 0, count = 0;
  uint32_t client = 0, lastRequest = 0, acknowledged = 0, sent = 0, dropped = 0;
};
