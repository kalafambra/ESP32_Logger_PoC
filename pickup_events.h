#pragma once
#include <Arduino.h>
#include <atomic>

/* A bounded journal preserves the last ten confirmed alerts, including an active one. */
class PickupEventLog {
public:
  PickupEventLog() = default;
  PickupEventLog(const PickupEventLog &) = delete;
  PickupEventLog &operator=(const PickupEventLog &) = delete;
  static constexpr unsigned CAPACITY = 10;
  enum State : uint8_t { ACTIVE = 1, COMPLETE = 2, INTERRUPTED = 3 };
  struct Event {
    uint32_t id = 0;
    int64_t startedUtc = 0, endedUtc = 0;
    uint64_t durationMs = 0;
    uint8_t reasons = 0, state = 0;
  };
  struct Stored {
    uint32_t version = 1, nextId = 1, head = 0, count = 0;
    Event events[CAPACITY];
  };

  /* Load only a compatible journal; a reboot interrupts an unfinished duration. */
  void begin();

  /* Observe every detector frame so FIFO batching cannot hide a short state transition. */
  void observe(bool active, uint8_t reasons);

  /* Commit only transitions, outside the FIFO path and outside critical sections. */
  void service(uint32_t now);

  /* Take a short locked snapshot; JSON construction and BLE responses never hold the lock. */
  String eventJson(unsigned slot);

  /* Read newest-first slots; IDs let a phone detect shifts while new events arrive. */
  String metadataJson();

  /* Snapshot the whole ring once so a new pickup cannot shift rows during a long BLE read. */
  String listText();

  /* Bound every row to 49 ASCII bytes: all ten rows plus the header fit one 512-byte GATT value. */
  static String formatList(const Event *newest, unsigned count);

private:
  std::atomic<bool> storageOk{true};
  Stored data;
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  uint64_t startedMs = 0;
  uint32_t lastSave = 0;
  bool previousActive = false;
  std::atomic<bool> dirty{false};

  /* Display short durations to tenths; scale long durations to keep each field within five bytes.
   */
  static String durationText(uint64_t ms);

  /* Invalid wall clocks remain explicit nulls; monotonic elapsed time still measures duration. */
  static int64_t utcNow();

  /* UTC ISO 8601 avoids ambiguity around daylight-saving changes and phone time zones. */
  static String dateJson(int64_t utc);

  /* A single versioned NVS blob gives atomic replacement without writing at sensor frequency. */
  void flush();
};

// Snapshot methods are safe from HTTP/BLE; mutation/NVS service belongs to processing.
PickupEventLog &pickupEventLog();

void setupPickupEvents(); // Self-test and restore journal before acquisition.
