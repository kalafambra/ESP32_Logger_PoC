#pragma once
#include <NimBLEDevice.h>
#include <NimBLE2904.h>
#include <atomic>
#include <sys/time.h>
#include "pickup_events.h"

// One custom characteristic replaces the per-slot GATT table. HTTP JSON remains unchanged.
constexpr char PICKUP_SERVICE_UUID[]="c7390001-7a32-4ec3-bb3b-81ae5d9a0000";
constexpr char PICKUP_LIST_UUID[]="c7390004-7a32-4ec3-bb3b-81ae5d9a0000";
// nRF Connect resolves custom names locally; serve this import file over the existing Wi-Fi connection.
const char PICKUP_UUID_DEFINITIONS[] PROGMEM=R"NAMES({
  "uuid16bitServiceDefinitions": {},
  "uuid16bitCharacteristicDefinitions": {},
  "uuid16bitDescriptorDefinitions": {},
  "uuid128bitServiceDefinitions": {
    "C73900017A324EC3BB3B81AE5D9A0000": {"name": "Pickup lists"}
  },
  "uuid128bitCharacteristicDefinitions": {
    "C73900047A324EC3BB3B81AE5D9A0000": {"name": "Pickup lists", "format": "TEXT"}
  },
  "uuid128bitDescriptorDefinitions": {}
})NAMES";
std::atomic<bool> pickupBleRestart{false};
bool pickupBleReady=false;

/* Generate a stable per-read snapshot; the BLE library handles long-read fragmentation. */
class PickupListCallbacks : public NimBLECharacteristicCallbacks {
 public:
  void onRead(NimBLECharacteristic *c,NimBLEConnInfo &) override {
    const String text=pickupEvents.listText();
    c->setValue(reinterpret_cast<const uint8_t *>(text.c_str()),text.length());
  }
  /* Reuse this characteristic for optional UTC writes, keeping only one custom entry in the app. */
  void onWrite(NimBLECharacteristic *c,NimBLEConnInfo &) override {
    String text(c->getValue().c_str());if(text.startsWith("TIME "))text.remove(0,5);
    if(text.length()!=10){Serial.println("[ble] Clock write rejected: use TIME followed by 10 Unix seconds digits");return;}
    for(unsigned i=0;i<text.length();++i)if(text[i]<'0'||text[i]>'9'){Serial.println("[ble] Clock write rejected: digits only");return;}
    const int64_t seconds=strtoll(text.c_str(),nullptr,10);
    if(seconds<1704067200LL||seconds>4102444799LL){Serial.println("[ble] Clock write rejected: UTC outside 2024..2099");return;}
    timeval value{};value.tv_sec=seconds;
    Serial.println(settimeofday(&value,nullptr)==0?"[ble] UTC clock set":"[ble] Clock setting failed");
  }
};
PickupListCallbacks pickupListCallbacks;

/* Schedule advertising recovery on the main task instead of doing work in a BLE callback. */
class PickupBleConnections : public NimBLEServerCallbacks {
  void onDisconnect(NimBLEServer *,NimBLEConnInfo &,int) override {pickupBleRestart.store(true);}
};
PickupBleConnections pickupBleConnections;

/* Keep the service read-only except for clock setting; no streaming load or notification MTU dependency. */
bool setupPickupBle(){
  if(!NimBLEDevice::init("Pickup lists")){Serial.println("[ble] Initialization failed; event logging remains available over HTTP");return false;}
  NimBLEDevice::setMTU(247);
  NimBLEServer *ble=NimBLEDevice::createServer();
  if(!ble){NimBLEDevice::deinit(true);Serial.println("[ble] Server creation failed");return false;}
  ble->setCallbacks(&pickupBleConnections,false); // Callback storage is static, not owned by the server.
  NimBLEService *service=ble->createService(PICKUP_SERVICE_UUID);
  if(!service){NimBLEDevice::deinit(true);Serial.println("[ble] Service creation failed");return false;}
  auto *list=service->createCharacteristic(PICKUP_LIST_UUID,NIMBLE_PROPERTY::READ|NIMBLE_PROPERTY::WRITE,512);
  list->setCallbacks(&pickupListCallbacks);
  auto *label=list->createDescriptor("2901",NIMBLE_PROPERTY::READ,64);
  label->setValue("Pickup lists (newest first; UTC)");
  list->create2904()->setFormat(NimBLE2904::FORMAT_UTF8); // Advertise UTF-8 text presentation.
  if(!ble->start()){NimBLEDevice::deinit(true);Serial.println("[ble] Server start failed");return false;}
  auto *advertising=NimBLEDevice::getAdvertising();advertising->addServiceUUID(PICKUP_SERVICE_UUID);
  advertising->enableScanResponse(true);advertising->setName("Pickup lists");
  if(!advertising->start()){NimBLEDevice::deinit(true);Serial.println("[ble] Advertising start failed");return false;}
  // Judge the actual allocation cost, leaving room for a debug lease and web/phone traffic.
  if(ESP.getFreeHeap()<49152){
    NimBLEDevice::deinit(true);pickupBleRestart.store(false);
    Serial.println("[ble] Stopped: post-start heap below 48 KiB; Wi-Fi remains available");return false;
  }
  Serial.println("[ble] Pickup lists ready: one text-list characteristic 0004, ten events newest first");
  Serial.printf("[ble] Free heap after BLE setup: %u; largest block: %u\n",ESP.getFreeHeap(),ESP.getMaxAllocHeap());
  pickupBleReady=true;
  return true;
}

/* Cooperative servicing leaves acquisition running while the phone reads event snapshots. */
void servicePickupBle(){if(pickupBleRestart.exchange(false))NimBLEDevice::startAdvertising();}
