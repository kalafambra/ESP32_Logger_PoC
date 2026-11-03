#pragma once
#include <NimBLEDevice.h>
#include <atomic>
#include <sys/time.h>
#include "pickup_events.h"

// Stable custom GATT UUIDs. Slot UUID suffixes 0100..0109 map to newest-first indices 0..9.
constexpr char PICKUP_SERVICE_UUID[]="c7390001-7a32-4ec3-bb3b-81ae5d9a0000";
std::atomic<bool> pickupBleRestart{false};
bool pickupBleReady=false;

/* Generate a stable per-read snapshot; the BLE library handles long-read fragmentation. */
class PickupEventRead : public NimBLECharacteristicCallbacks {
 public:
  int slot=-1;
  void onRead(NimBLECharacteristic *c,NimBLEConnInfo &) override {
    const String json=slot<0?pickupEvents.metadataJson():pickupEvents.eventJson(unsigned(slot));
    c->setValue(reinterpret_cast<const uint8_t *>(json.c_str()),json.length());
  }
};
PickupEventRead pickupEventReaders[11];

/* An optional ASCII Unix-seconds write lets the phone set UTC when NTP is unavailable. */
class PickupClockWrite : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c,NimBLEConnInfo &) override {
    const String text(c->getValue().c_str());
    if(text.length()!=10){c->setValue("ERR: use 10 ASCII Unix seconds");return;}
    for(unsigned i=0;i<text.length();++i)if(text[i]<'0'||text[i]>'9'){c->setValue("ERR: digits only");return;}
    const int64_t seconds=strtoll(text.c_str(),nullptr,10);
    if(seconds<1704067200LL||seconds>4102444799LL){c->setValue("ERR: UTC outside 2024..2099");return;}
    timeval value{};value.tv_sec=seconds;
    c->setValue(settimeofday(&value,nullptr)==0?"OK":"ERR: clock not set");
  }
};
PickupClockWrite pickupClockWriter;

/* Schedule advertising recovery on the main task instead of doing work in a BLE callback. */
class PickupBleConnections : public NimBLEServerCallbacks {
  void onDisconnect(NimBLEServer *,NimBLEConnInfo &,int) override {pickupBleRestart.store(true);}
};
PickupBleConnections pickupBleConnections;

/* Keep the service read-only except for clock setting; no streaming load or notification MTU dependency. */
bool setupPickupBle(){
  if(!NimBLEDevice::init("ESP32-Pickup")){Serial.println("[ble] Initialization failed; event logging remains available over HTTP");return false;}
  NimBLEDevice::setMTU(247);
  NimBLEServer *ble=NimBLEDevice::createServer();
  if(!ble){NimBLEDevice::deinit(true);Serial.println("[ble] Server creation failed");return false;}
  ble->setCallbacks(&pickupBleConnections,false); // Callback storage is static, not owned by the server.
  NimBLEService *service=ble->createService(PICKUP_SERVICE_UUID);
  if(!service){NimBLEDevice::deinit(true);Serial.println("[ble] Service creation failed");return false;}
  auto *meta=service->createCharacteristic("c7390002-7a32-4ec3-bb3b-81ae5d9a0000",NIMBLE_PROPERTY::READ);
  meta->setCallbacks(&pickupEventReaders[10]);
  for(unsigned i=0;i<10;++i){
    char uuid[37];snprintf(uuid,sizeof(uuid),"c739%04x-7a32-4ec3-bb3b-81ae5d9a0000",0x100+i);
    auto *c=service->createCharacteristic(uuid,NIMBLE_PROPERTY::READ);
    pickupEventReaders[i].slot=i;c->setCallbacks(&pickupEventReaders[i]);
    auto *label=c->createDescriptor("2901",NIMBLE_PROPERTY::READ,64);
    const String text="Pickup slot "+String(i)+(i==0?" (newest)":"");
    label->setValue(reinterpret_cast<const uint8_t *>(text.c_str()),text.length());
  }
  auto *clock=service->createCharacteristic("c7390003-7a32-4ec3-bb3b-81ae5d9a0000",NIMBLE_PROPERTY::WRITE|NIMBLE_PROPERTY::READ);
  clock->setValue("Write ASCII Unix seconds (UTC)");clock->setCallbacks(&pickupClockWriter);
  if(!ble->start()){NimBLEDevice::deinit(true);Serial.println("[ble] Server start failed");return false;}
  auto *advertising=NimBLEDevice::getAdvertising();advertising->addServiceUUID(PICKUP_SERVICE_UUID);
  advertising->enableScanResponse(true);advertising->setName("ESP32-Pickup");
  if(!advertising->start()){NimBLEDevice::deinit(true);Serial.println("[ble] Advertising start failed");return false;}
  // Judge the actual allocation cost, leaving room for a debug lease and web/phone traffic.
  if(ESP.getFreeHeap()<49152){
    NimBLEDevice::deinit(true);pickupBleRestart.store(false);
    Serial.println("[ble] Stopped: post-start heap below 48 KiB; Wi-Fi remains available");return false;
  }
  Serial.println("[ble] NimBLE ESP32-Pickup ready: metadata 0002, event slots 0100..0109, UTC clock 0003");
  Serial.printf("[ble] Free heap after BLE setup: %u; largest block: %u\n",ESP.getFreeHeap(),ESP.getMaxAllocHeap());
  pickupBleReady=true;
  return true;
}

/* Cooperative servicing leaves acquisition running while the phone reads event snapshots. */
void servicePickupBle(){if(pickupBleRestart.exchange(false))NimBLEDevice::startAdvertising();}
