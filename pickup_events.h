#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <esp_timer.h>
#include <time.h>
#include <atomic>

/* A bounded journal preserves the last ten confirmed alerts, including an active one. */
class PickupEventLog {
 public:
  static constexpr unsigned CAPACITY=10;
  enum State : uint8_t { ACTIVE=1, COMPLETE=2, INTERRUPTED=3 };
  struct Event {
    uint32_t id=0;
    int64_t startedUtc=0, endedUtc=0;
    uint64_t durationMs=0;
    uint8_t reasons=0,state=0;
  };
  struct Stored {
    uint32_t version=1,nextId=1,head=0,count=0;
    Event events[CAPACITY];
  };
  std::atomic<bool> storageOk{true};

  /* Load only a compatible journal; a reboot interrupts an unfinished duration. */
  void begin() {
    Preferences prefs;
    if(prefs.begin("pickup-events",true)) {
      Stored saved;
      if(prefs.getBytesLength("journal")==sizeof(saved)&&prefs.getBytes("journal",&saved,sizeof(saved))==sizeof(saved)) {
        bool valid=saved.version==1&&saved.count<=CAPACITY&&saved.head<CAPACITY&&saved.nextId>0;
        for(const auto &e:saved.events)if(e.state>INTERRUPTED||(e.reasons&~7))valid=false;
        if(valid)data=saved;else Serial.println("[events] Invalid journal; starting empty");
      }
      prefs.end();
    }
    for(auto &e:data.events)if(e.state==ACTIVE){e.state=INTERRUPTED;dirty=true;}
    Serial.printf("[events] Restored %u records; capacity %u\n",data.count,CAPACITY);
    flush();
  }

  /* Observe every detector frame so FIFO batching cannot hide a short state transition. */
  void observe(bool active,uint8_t reasons) {
    const uint64_t now=esp_timer_get_time()/1000;
    const int64_t utc=utcNow();
    portENTER_CRITICAL(&mux);
    if(active&&!previousActive){
      Event &e=data.events[data.head];e=Event{};
      e.id=data.nextId++;e.startedUtc=utc;e.reasons=reasons;e.state=ACTIVE;
      data.head=(data.head+1)%CAPACITY;if(data.count<CAPACITY)++data.count;
      startedMs=now;dirty=true;
    } else if(!active&&previousActive){
      Event &e=data.events[(data.head+CAPACITY-1)%CAPACITY];
      e.durationMs=now-startedMs;e.endedUtc=utc;e.state=COMPLETE;dirty=true;
    }
    previousActive=active;
    portEXIT_CRITICAL(&mux);
  }

  /* Commit only transitions, outside the FIFO path and outside critical sections. */
  void service(uint32_t now) {
    if(dirty&&(storageOk||now-lastSave>=5000)){lastSave=now;flush();}
  }

  /* Take a short locked snapshot; JSON construction and BLE responses never hold the lock. */
  String eventJson(unsigned slot) {
    Event e;bool exists;
    portENTER_CRITICAL(&mux);
    exists=slot<data.count;
    if(exists){e=data.events[(data.head+CAPACITY-1-slot)%CAPACITY];if(e.state==ACTIVE)e.durationMs=esp_timer_get_time()/1000-startedMs;}
    portEXIT_CRITICAL(&mux);
    if(!exists)return "{\"slot\":"+String(slot)+",\"event\":null}";
    String json;json.reserve(400);
    json="{\"slot\":"+String(slot)+",\"id\":"+String(e.id)+",\"started_utc\":"+dateJson(e.startedUtc);
    json+=",\"ended_utc\":"+dateJson(e.endedUtc)+",\"duration_ms\":";
    json+=e.state==INTERRUPTED?String("null"):String((unsigned long long)e.durationMs);
    json+=",\"state\":\"";json+=e.state==ACTIVE?"active":e.state==COMPLETE?"complete":"interrupted";
    json+="\",\"reason_mask\":"+String(e.reasons)+",\"reasons\":[";
    bool comma=false;const char *names[]={"acceleration_departure","tilt","rotation"};
    for(unsigned i=0;i<3;++i)if(e.reasons&(1<<i)){if(comma)json+=',';json+='"';json+=names[i];json+='"';comma=true;}
    return json+"]}";
  }

  /* Read newest-first slots; IDs let a phone detect shifts while new events arrive. */
  String metadataJson() {
    portENTER_CRITICAL(&mux);const unsigned count=data.count;const uint32_t next=data.nextId;portEXIT_CRITICAL(&mux);
    return "{\"schema\":1,\"count\":"+String(count)+",\"capacity\":10,\"newest_id\":"+String(next-1)+
      ",\"order\":\"newest_first\",\"clock_utc\":"+dateJson(utcNow())+
      ",\"storage_ok\":"+(storageOk?"true":"false")+",\"pending_save\":"+(dirty?"true":"false")+"}";
  }

  /* Snapshot the whole ring once so a new pickup cannot shift rows during a long BLE read. */
  String listText(){
    Event newest[CAPACITY];unsigned count;
    const uint64_t now=esp_timer_get_time()/1000;
    portENTER_CRITICAL(&mux);
    count=data.count;
    for(unsigned i=0;i<count;++i){
      newest[i]=data.events[(data.head+CAPACITY-1-i)%CAPACITY];
      if(newest[i].state==ACTIVE)newest[i].durationMs=now>=startedMs?now-startedMs:0;
    }
    portEXIT_CRITICAL(&mux);
    return formatList(newest,count);
  }

  /* Bound every row to 49 ASCII bytes: all ten rows plus the header fit one 512-byte GATT value. */
  static String formatList(const Event *newest,unsigned count){
    static_assert(sizeof("UTC|dur|state|reasons\n")-1+CAPACITY*49<=512,"Ten text rows must fit GATT");
    if(!count)return "No pickup events.\n";
    String text;text.reserve(512);text="UTC|dur|state|reasons\n";
    for(unsigned i=0;i<count&&i<CAPACITY;++i){
      const Event &e=newest[i];char stamp[20];struct tm value;time_t utc=e.startedUtc;
      if(e.startedUtc&&gmtime_r(&utc,&value)&&value.tm_year<=8099&&value.tm_year>=0&&
         strftime(stamp,sizeof(stamp),"%Y-%m-%d %H:%M:%S",&value))text+=stamp;
      else text+="unknown";
      text+='|';text+=e.state==INTERRUPTED?String("?"):durationText(e.durationMs);
      text+='|';text+=e.state==ACTIVE?"active":e.state==COMPLETE?"done":"reboot";
      text+='|';bool comma=false;const char *names[]={"accel","tilt","gyro"};
      for(unsigned bit=0;bit<3;++bit)if(e.reasons&(1<<bit)){if(comma)text+='+';text+=names[bit];comma=true;}
      if(!comma)text+="none";text+='\n';
    }
    return text;
  }

 private:
  Stored data;
  portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
  uint64_t startedMs=0;
  uint32_t lastSave=0;
  bool previousActive=false;
  std::atomic<bool> dirty{false};

  /* Display short durations to tenths; scale long durations to keep each field within five bytes. */
  static String durationText(uint64_t ms){
    if(ms<100000)return String(unsigned(ms/1000))+'.'+String(unsigned((ms/100)%10))+"s";
    uint64_t amount=ms/1000;
    const unsigned divisors[]={60,60,24,365};const char units[]={'s','m','h','d','y'};
    unsigned unit=0;while(amount>9999&&unit<4){amount/=divisors[unit];++unit;}
    if(amount>9999)return ">999y";
    return String(unsigned(amount))+units[unit];
  }

  /* Invalid wall clocks remain explicit nulls; monotonic elapsed time still measures duration. */
  static int64_t utcNow(){const time_t now=time(nullptr);return now>=1704067200?int64_t(now):0;}

  /* UTC ISO 8601 avoids ambiguity around daylight-saving changes and phone time zones. */
  static String dateJson(int64_t utc){
    if(!utc)return "null";
    time_t raw=utc;struct tm value;gmtime_r(&raw,&value);char text[24];
    strftime(text,sizeof(text),"%Y-%m-%dT%H:%M:%SZ",&value);return String('"')+text+'"';
  }

  /* A single versioned NVS blob gives atomic replacement without writing at sensor frequency. */
  void flush(){
    Stored snapshot;
    portENTER_CRITICAL(&mux);snapshot=data;portEXIT_CRITICAL(&mux);
    Preferences prefs;storageOk=false;
    if(prefs.begin("pickup-events",false)){storageOk=prefs.putBytes("journal",&snapshot,sizeof(snapshot))==sizeof(snapshot);prefs.end();}
    if(storageOk)dirty=false;
    else Serial.println("[events] NVS save failed; RAM log retained, retry scheduled");
  }
};

PickupEventLog pickupEvents;

/* Exercise wraparound, reason snapshots and active/completed reads without touching NVS. */
inline bool runPickupEventSelfTests(){
  PickupEventLog test;
  if(test.listText()!="No pickup events.\n")return false;
  if(test.eventJson(0)!="{\"slot\":0,\"event\":null}")return false;
  for(unsigned i=1;i<=12;++i){test.observe(true,5);test.observe(false,0);}
  if(test.metadataJson().indexOf("\"count\":10")<0)return false;
  if(test.eventJson(0).indexOf("\"id\":12,")<0||test.eventJson(9).indexOf("\"id\":3,")<0)return false;
  if(test.eventJson(0).indexOf("\"state\":\"complete\"")<0||test.eventJson(0).indexOf("\"reason_mask\":5")<0)return false;
  test.observe(true,2);
  const String active=test.eventJson(0);
  if(active.indexOf("\"id\":13,")<0||active.indexOf("\"state\":\"active\"")<0||active.indexOf("\"reasons\":[\"tilt\"]")<0)return false;
  test.observe(true,7); // Later motion must not overwrite the confirming-frame reason.
  if(test.eventJson(0).indexOf("\"reason_mask\":2")<0)return false;
  // Maximum-width rows must include every event without truncation at the GATT limit.
  PickupEventLog::Event worst[10];
  for(auto &e:worst){e.startedUtc=1704067200;e.durationMs=9999000;e.state=PickupEventLog::ACTIVE;e.reasons=7;}
  const String full=PickupEventLog::formatList(worst,10);
  if(full.length()!=512||!full.endsWith("active|accel+tilt+gyro\n"))return false;
  worst[0].state=PickupEventLog::INTERRUPTED;
  if(PickupEventLog::formatList(worst,1).indexOf("|?|reboot|")<0)return false;
  worst[0].state=PickupEventLog::COMPLETE;worst[0].durationMs=UINT64_MAX;
  return PickupEventLog::formatList(worst,1).indexOf("|>999y|done|")>=0;
}
