#include "dashboard_delivery.h"
#include <esp_system.h>
#include <Arduino.h>

uint32_t deliveryBootId() {
  static const uint32_t boot = esp_random() | 1u;
  return boot;
}
void appendDeliveryInfo(String &json, const DeliveryInfo &info) {
  json += ",\"boot_id\":" + String(deliveryBootId()) +
          ",\"delivery\":{\"acknowledged\":" + String(info.acknowledged) +
          ",\"dropped\":" + String(info.dropped) +
          ",\"oldest\":" + String(info.oldest) +
          ",\"gap\":" + String(info.gap ? "true" : "false") + "}";
}
bool runDeliverySelfTests() {
  struct Row { uint32_t sequence = 0; };
  DashboardDelivery<Row, 2> buffer;
  DashboardDelivery<Row, 2>::Snapshot snapshot;
  buffer.copy({1, 7, 0}, 7, 0, {10}, snapshot);
  if (snapshot.count != 1 || snapshot.rows[0].sequence != 10) return false;
  buffer.copy({1, 7, 0}, 7, 1, {10}, snapshot); // Lost response is retransmitted.
  if (snapshot.count != 1) return false;
  buffer.copy({1, 7, 999}, 7, 2, {10}, snapshot); // Future ACK cannot evict.
  if (snapshot.count != 1) return false;
  buffer.copy({1, 8, 10}, 7, 3, {10}, snapshot); // Wrong boot cannot evict.
  if (snapshot.count != 1) return false;
  buffer.copy({1, 7, 10}, 7, 4, {10}, snapshot);
  if (snapshot.count) return false;
  buffer.publish({11}, 5); buffer.publish({12}, 6); buffer.publish({13}, 7);
  buffer.copy({1, 7, 10}, 7, 8, {13}, snapshot);
  if (snapshot.count != 2 || !snapshot.info.gap || snapshot.info.dropped != 1) return false;
  buffer.copy({2, 7, 0}, 7, 9, {13}, snapshot); // Refresh starts at latest only.
  if (snapshot.count != 1 || snapshot.rows[0].sequence != 13 || snapshot.info.gap) return false;
  buffer.publish({14}, 4000); // Expired subscriber retains no backlog.
  buffer.copy({2, 7, 13}, 7, 4001, {14}, snapshot);
  return snapshot.count == 1 && snapshot.rows[0].sequence == 14;
}
