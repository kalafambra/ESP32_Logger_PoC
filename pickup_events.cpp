#include "pickup_events.h"
#include "self_tests.h"
#include <Preferences.h>
#include <esp_timer.h>
#include <time.h>

void PickupEventLog::begin() {
  Preferences prefs;
  if (prefs.begin("pickup-events", true)) {
    Stored saved;
    if (prefs.getBytesLength("journal") == sizeof(saved) &&
        prefs.getBytes("journal", &saved, sizeof(saved)) == sizeof(saved)) {
      bool valid = saved.version == 1 && saved.count <= CAPACITY && saved.head < CAPACITY &&
                   saved.nextId > 0;
      for (const auto &e : saved.events)
        if (e.state > INTERRUPTED || (e.reasons & ~7))
          valid = false;
      if (valid)
        data = saved;
      else
        Serial.println("[events] Invalid journal; starting empty");
    }
    prefs.end();
  }
  for (auto &e : data.events)
    if (e.state == ACTIVE) {
      e.state = INTERRUPTED;
      dirty = true;
    }
  Serial.printf("[events] Restored %u records; capacity %u\n", data.count, CAPACITY);
  flush();
}

void PickupEventLog::observe(bool active, uint8_t reasons) {
  const uint64_t now = esp_timer_get_time() / 1000;
  const int64_t utc = utcNow();
  portENTER_CRITICAL(&mux);
  if (active && !previousActive) {
    Event &e = data.events[data.head];
    e = Event{};
    e.id = data.nextId++;
    e.startedUtc = utc;
    e.reasons = reasons;
    e.state = ACTIVE;
    data.head = (data.head + 1) % CAPACITY;
    if (data.count < CAPACITY)
      ++data.count;
    startedMs = now;
    dirty = true;
  } else if (!active && previousActive) {
    Event &e = data.events[(data.head + CAPACITY - 1) % CAPACITY];
    e.durationMs = now - startedMs;
    e.endedUtc = utc;
    e.state = COMPLETE;
    dirty = true;
  }
  previousActive = active;
  portEXIT_CRITICAL(&mux);
}

void PickupEventLog::service(uint32_t now) {
  if (dirty && (storageOk || now - lastSave >= 5000)) {
    lastSave = now;
    flush();
  }
}

String PickupEventLog::eventJson(unsigned slot) {
  Event e;
  bool exists;
  portENTER_CRITICAL(&mux);
  exists = slot < data.count;
  if (exists) {
    e = data.events[(data.head + CAPACITY - 1 - slot) % CAPACITY];
    if (e.state == ACTIVE)
      e.durationMs = esp_timer_get_time() / 1000 - startedMs;
  }
  portEXIT_CRITICAL(&mux);
  if (!exists)
    return "{\"slot\":" + String(slot) + ",\"event\":null}";
  String json;
  json.reserve(400);
  json = "{\"slot\":" + String(slot) + ",\"id\":" + String(e.id) +
         ",\"started_utc\":" + dateJson(e.startedUtc);
  json += ",\"ended_utc\":" + dateJson(e.endedUtc) + ",\"duration_ms\":";
  json += e.state == INTERRUPTED ? String("null") : String((unsigned long long)e.durationMs);
  json += ",\"state\":\"";
  json += e.state == ACTIVE ? "active" : e.state == COMPLETE ? "complete" : "interrupted";
  json += "\",\"reason_mask\":" + String(e.reasons) + ",\"reasons\":[";
  bool comma = false;
  const char *names[] = {"acceleration_departure", "tilt", "rotation"};
  for (unsigned i = 0; i < 3; ++i)
    if (e.reasons & (1 << i)) {
      if (comma)
        json += ',';
      json += '"';
      json += names[i];
      json += '"';
      comma = true;
    }
  return json + "]}";
}

String PickupEventLog::metadataJson() {
  portENTER_CRITICAL(&mux);
  const unsigned count = data.count;
  const uint32_t next = data.nextId;
  portEXIT_CRITICAL(&mux);
  return "{\"schema\":1,\"count\":" + String(count) +
         ",\"capacity\":10,\"newest_id\":" + String(next - 1) +
         ",\"order\":\"newest_first\",\"clock_utc\":" + dateJson(utcNow()) +
         ",\"storage_ok\":" + (storageOk ? "true" : "false") +
         ",\"pending_save\":" + (dirty ? "true" : "false") + "}";
}

String PickupEventLog::listText() {
  Event newest[CAPACITY];
  unsigned count;
  const uint64_t now = esp_timer_get_time() / 1000;
  portENTER_CRITICAL(&mux);
  count = data.count;
  for (unsigned i = 0; i < count; ++i) {
    newest[i] = data.events[(data.head + CAPACITY - 1 - i) % CAPACITY];
    if (newest[i].state == ACTIVE)
      newest[i].durationMs = now >= startedMs ? now - startedMs : 0;
  }
  portEXIT_CRITICAL(&mux);
  return formatList(newest, count);
}

String PickupEventLog::formatList(const Event *newest, unsigned count) {
  static_assert(sizeof("UTC|dur|state|reasons\n") - 1 + CAPACITY * 49 <= 512,
                "Ten text rows must fit GATT");
  if (!count)
    return "No pickup events.\n";
  String text;
  text.reserve(512);
  text = "UTC|dur|state|reasons\n";
  for (unsigned i = 0; i < count && i < CAPACITY; ++i) {
    const Event &e = newest[i];
    char stamp[20];
    struct tm value;
    time_t utc = e.startedUtc;
    if (e.startedUtc && gmtime_r(&utc, &value) && value.tm_year <= 8099 && value.tm_year >= 0 &&
        strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &value))
      text += stamp;
    else
      text += "unknown";
    text += '|';
    text += e.state == INTERRUPTED ? String("?") : durationText(e.durationMs);
    text += '|';
    text += e.state == ACTIVE ? "active" : e.state == COMPLETE ? "done" : "reboot";
    text += '|';
    bool comma = false;
    const char *names[] = {"accel", "tilt", "gyro"};
    for (unsigned bit = 0; bit < 3; ++bit)
      if (e.reasons & (1 << bit)) {
        if (comma)
          text += '+';
        text += names[bit];
        comma = true;
      }
    if (!comma)
      text += "none";
    text += '\n';
  }
  return text;
}

String PickupEventLog::durationText(uint64_t ms) {
  if (ms < 100000)
    return String(unsigned(ms / 1000)) + '.' + String(unsigned((ms / 100) % 10)) + "s";
  uint64_t amount = ms / 1000;
  const unsigned divisors[] = {60, 60, 24, 365};
  const char units[] = {'s', 'm', 'h', 'd', 'y'};
  unsigned unit = 0;
  while (amount > 9999 && unit < 4) {
    amount /= divisors[unit];
    ++unit;
  }
  if (amount > 9999)
    return ">999y";
  return String(unsigned(amount)) + units[unit];
}

int64_t PickupEventLog::utcNow() {
  const time_t now = time(nullptr);
  return now >= 1704067200 ? int64_t(now) : 0;
}

String PickupEventLog::dateJson(int64_t utc) {
  if (!utc)
    return "null";
  time_t raw = utc;
  struct tm value;
  gmtime_r(&raw, &value);
  char text[24];
  strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &value);
  return String('"') + text + '"';
}

void PickupEventLog::flush() {
  Stored snapshot;
  portENTER_CRITICAL(&mux);
  snapshot = data;
  portEXIT_CRITICAL(&mux);
  Preferences prefs;
  storageOk = false;
  if (prefs.begin("pickup-events", false)) {
    storageOk = prefs.putBytes("journal", &snapshot, sizeof(snapshot)) == sizeof(snapshot);
    prefs.end();
  }
  if (storageOk)
    dirty = false;
  else
    Serial.println("[events] NVS save failed; RAM log retained, retry scheduled");
}

namespace {
PickupEventLog journal;
}

PickupEventLog &pickupEventLog() {
  return journal;
}

void setupPickupEvents() {
  Serial.printf("[events] Journal self-tests: %s\n", runPickupEventSelfTests() ? "PASS" : "FAIL");
  journal.begin();
}
