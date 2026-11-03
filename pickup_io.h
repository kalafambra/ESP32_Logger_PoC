#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include "pickup_detector.h"
#include "pickup_selftest.h"
#include "pickup_debug.h"
extern WebServer server;

PickupDetector pickup;
PickupDebug pickupDebug;
uint32_t pickupSettingsRevision=1,pickupDebugGapPeak=0;
void appendPickup(String &json);
struct PickupField { const char *key, *label, *unit; float PickupSettings::*member; float min, max, step; };
const PickupField pickupFields[] = {
 {"stationary_ms","Stationary/baseline duration","ms",&PickupSettings::stationary_ms,500,10000,10},
 {"stationary_accel_rms","Stationary acceleration RMS","m/s²",&PickupSettings::stationary_accel_rms,0.01f,1,0.01f},
 {"stationary_gyro","Stationary rotation limit","rad/s",&PickupSettings::stationary_gyro,0.005f,0.3f,0.005f},
 {"accel_trigger","Acceleration deviation trigger","m/s²",&PickupSettings::accel_trigger,0.05f,10,0.01f},
 {"noise_multiplier","Adaptive noise multiplier","×",&PickupSettings::noise_multiplier,1,20,0.1f},
 {"tilt_deg","Tilt trigger","degrees",&PickupSettings::tilt_deg,1,90,0.5f},
 {"gyro_trigger","Rotation trigger","rad/s",&PickupSettings::gyro_trigger,0.05f,4,0.01f},
 {"trigger_ms","Trigger persistence","ms",&PickupSettings::trigger_ms,20,2000,10},
 {"smoothing_ms","Smoothing time constant","ms",&PickupSettings::smoothing_ms,0,1000,10},
 {"led_period_ms","Alert LED period","ms",&PickupSettings::led_period_ms,200,10000,10},
 {"led_duty_pct","Alert LED duty cycle","%",&PickupSettings::led_duty_pct,5,95,1},
 {"max_gap_ms","Maximum acquisition gap","ms",&PickupSettings::max_gap_ms,50,1000,10}
};
constexpr unsigned PICKUP_FIELD_COUNT = sizeof(pickupFields)/sizeof(pickupFields[0]);
bool pickupSelfTestsPassed = false, pickupPersisted = false, pickupDataValid = false;
uint32_t pickupLastSampleMs = 0;
const char *pickupDataError = "Waiting for motion samples";
hw_timer_t *pickupLedTimer = nullptr;
portMUX_TYPE pickupLedMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool pickupLedAlert = false, pickupLedOn = false;
volatile uint32_t pickupLedPhase = 0, pickupLedPeriod = 100, pickupLedDuty = 50;
bool pickupPreviousActive = false;

/* The detector's timer is the ONLY LED writer, so blinking also continues during OTA transfers. */
void ARDUINO_ISR_ATTR pickupLedISR() {
  portENTER_CRITICAL_ISR(&pickupLedMux);
  const bool on = pickupLedAlert && pickupLedPhase < pickupLedDuty;
  pickupLedOn = on;
  if (STATUS_LED_PIN >= 0) digitalWrite(STATUS_LED_PIN, on ? LED_ON : !LED_ON);
  pickupLedPhase = pickupLedAlert ? (pickupLedPhase + 1) % pickupLedPeriod : 0;
  portEXIT_CRITICAL_ISR(&pickupLedMux);
}

/* Validate the whole candidate before touching live settings or persistent storage. */
const char *validatePickupSettings(const PickupSettings &s) {
  for (const auto &field : pickupFields) {
    const float value = s.*(field.member);
    if (!isfinite(value) || value < field.min || value > field.max) return "Setting outside permitted range";
    if (field.step >= 1 && fabsf(value/field.step-roundf(value/field.step)) > 0.001f) return "Timing/duty settings must match their step";
  }
  if (s.stationary_accel_rms >= s.accel_trigger) return "Stationary acceleration limit must be below acceleration trigger";
  if (s.stationary_gyro >= s.gyro_trigger) return "Stationary gyro limit must be below rotation trigger";
  return nullptr;
}

/* Clear incomplete evidence on sample errors/calibration/OTA, without clearing an active alert. */
void invalidatePickup(const char *reason) {
  pickup.reset(); pickupDataValid = false; pickupDataError = reason;
}

/* Load versioned, validated settings; corrupt or absent storage safely falls back to code defaults. */
void setupPickup() {
  pickupSelfTestsPassed = runPickupSelfTests(pickup);
  Serial.printf("[pickup] Algorithm self-tests: %s\n", pickupSelfTestsPassed ? "PASS" : "FAIL (detector disabled)");
  struct Stored { uint32_t version; PickupSettings settings; } stored;
  Preferences prefs;
  if (prefs.begin("pickup",true)) {
    if (prefs.getBytesLength("settings") == sizeof(stored) && prefs.getBytes("settings",&stored,sizeof(stored)) == sizeof(stored) &&
        stored.version == 1 && !validatePickupSettings(stored.settings)) {
      pickup.configure(stored.settings); pickupPersisted = true;
    }
    prefs.end();
  }
  if (STATUS_LED_PIN >= 0) {
    pinMode(STATUS_LED_PIN,OUTPUT); digitalWrite(STATUS_LED_PIN,!LED_ON);
    pickupLedTimer = timerBegin(1000000); // Supported 1 MHz counter resolution; service LED every 10 ms.
    if (pickupLedTimer) { timerAttachInterrupt(pickupLedTimer,pickupLedISR); timerAlarm(pickupLedTimer,10000,true,0); }
  }
}

/* Feed raw-frame vectors, with gyro in rad/s; FIFO read timestamps must not replace the 10 ms sensor cadence. */
void processPickupSample(const float *a,const float *w,uint32_t now) {
  if (!pickupSelfTestsPassed) return;
  if(pickupDataValid)pickupDebugGapPeak=max(pickupDebugGapPeak,now-pickupLastSampleMs);
  if (pickupDataValid && now-pickupLastSampleMs > pickup.settings.max_gap_ms) pickup.reset();
  pickupLastSampleMs = now; pickupDataValid = true; pickupDataError = "";
  pickup.process(a,w);
  if(pickupDebug.capture(pickup,pickupDataValid,now,pickupDebugGapPeak,0,pickupLedOn,pickupSettingsRevision))pickupDebugGapPeak=0;
}

/* Publish state transitions and synchronize the dedicated LED timer without delays or acquisition pulses. */
void servicePickup(uint32_t now) {
  if (pickupDataValid && now-pickupLastSampleMs > pickup.settings.max_gap_ms) invalidatePickup("Motion sample gap; waiting for fresh baseline");
  if (pickup.active != pickupPreviousActive) {
    Serial.printf("[pickup] %s\n",pickup.active ? "Picked up" : "Stationary again; alert cleared and detector re-armed");
    pickupPreviousActive = pickup.active;
  }
  const unsigned period = unsigned(pickup.settings.led_period_ms/10);
  unsigned duty = unsigned(roundf(period*pickup.settings.led_duty_pct/100));
  if (!duty) duty = 1; if (duty >= period) duty = period-1;
  portENTER_CRITICAL(&pickupLedMux);
  if (pickupLedAlert != pickup.active) pickupLedPhase = 0;
  pickupLedAlert = pickup.active; pickupLedPeriod = period; pickupLedDuty = duty;
  portEXIT_CRITICAL(&pickupLedMux);
  if(pickupDebug.capture(pickup,pickupDataValid,now,pickupDebugGapPeak,now-pickupLastSampleMs,pickupLedOn,pickupSettingsRevision))pickupDebugGapPeak=0;
}

/* Lightweight, leased debug stream: bounded batches rather than resending every graph's history. */
void sendPickupDebug() {
  const uint32_t now=millis();
  if(!pickupDebug.subscribe(now)){server.send(503,"application/json","{\"error\":\"Not enough memory for debug history\"}");return;}
  uint32_t since=server.hasArg("since")?strtoul(server.arg("since").c_str(),nullptr,10):0;
  if(since>pickupDebug.sequence)since=0;
  String json;json.reserve(9000);json="{\"uptime_ms\":"+String(now)+",\"settings_revision\":"+String(pickupSettingsRevision);
  json+=",\"free_heap\":"+String(ESP.getFreeHeap())+",\"largest_free_block\":"+String(ESP.getMaxAllocHeap());
  appendPickup(json);pickupDebug.appendJson(json,since);json+='}';
  server.sendHeader("Cache-Control","no-store");server.send(200,"application/json",json);
}

/* Serialize status separately from graphs so faults cannot be mistaken for a quiet, safe detector. */
void appendPickup(String &json) {
  json += ",\"pickup\":{\"active\":"; json += pickup.active ? "true" : "false";
  json += ",\"state\":\"";
  json += !pickupSelfTestsPassed ? "self_test_failed" : !pickupDataValid ? "sensor_unavailable" : pickup.active ? "picked_up" : pickup.armed ? "armed" : "learning_baseline";
  json += "\",\"error\":\""; json += pickupDataError; json += '"';
  json += ",\"quiet_ms\":" + String(pickup.quietMs()) + ",\"stationary_ms\":" + String(pickup.settings.stationary_ms,0);
  json += ",\"events\":" + String(pickup.events) + ",\"self_tests_passed\":"; json += pickupSelfTestsPassed ? "true" : "false";
  json += ",\"led_available\":"; json += pickupLedTimer ? "true" : "false";
  json += ",\"led_on\":"; json += pickupLedOn ? "true" : "false";
  json += ",\"accel_deviation\":" + String(pickup.accelDeviation,4) + ",\"tilt_deg\":" + String(pickup.tiltDegrees,2);
  json += ",\"rotation_rad_s\":" + String(pickup.rotationRate,4) + ",\"accel_rms\":" + String(pickup.accelRms,4);
  json += ",\"gyro_rms\":" + String(pickup.gyroRms,4) + ",\"baseline_noise\":" + String(pickup.baselineNoise,4) + '}';
}

/* Provide lightweight diagnostics without transferring graph history. */
void sendPickupStatus() {
  String json="{\"uptime_ms\":"+String(millis());appendPickup(json);json+='}';
  server.sendHeader("Cache-Control","no-store");server.send(200,"application/json",json);
}

/* Include validation metadata so the dashboard uses the same limits and units as the firmware. */
void sendPickupSettings() {
  String json = "{\"persisted\":"; json += pickupPersisted ? "true" : "false";
  json += ",\"settings_revision\":"+String(pickupSettingsRevision);
  json += ",\"fields\":[";
  const PickupSettings defaults;
  for (unsigned i=0;i<PICKUP_FIELD_COUNT;++i) {
    const auto &f = pickupFields[i]; if(i)json+=',';
    json += "{\"key\":\"" + String(f.key) + "\",\"label\":\"" + String(f.label) + "\",\"unit\":\"" + String(f.unit) + "\"";
    json += ",\"min\":"+String(f.min,4)+",\"max\":"+String(f.max,4)+",\"step\":"+String(f.step,4);
    json += ",\"value\":"+String(pickup.settings.*(f.member),4)+",\"default\":"+String(defaults.*(f.member),4);
    json += ",\"display_unit\":\"";json += String(f.unit)=="rad/s"?"deg/s":f.unit;
    json += "\",\"display_scale\":"+String(String(f.unit)=="rad/s"?57.29577951308232:1.0,10)+'}';
  }
  json += "]}"; server.sendHeader("Cache-Control","no-store"); server.send(200,"application/json",json);
}

/* Persist one versioned blob before applying; a failed save must not silently change runtime behavior. */
bool savePickupSettings(const PickupSettings &settings) {
  struct Stored { uint32_t version; PickupSettings settings; } stored{1,settings};
  Preferences prefs;
  if (!prefs.begin("pickup",false)) return false;
  const bool ok = prefs.putBytes("settings",&stored,sizeof(stored)) == sizeof(stored);
  prefs.end();
  if (ok) { pickup.configure(settings); pickupPersisted = true; ++pickupSettingsRevision; }
  return ok;
}

/* Accept only a complete, finite, validated form; unknown/missing fields leave saved settings untouched. */
void updatePickupSettings() {
  if (server.args()!=PICKUP_FIELD_COUNT) {server.send(400,"application/json","{\"error\":\"Supply every setting exactly once\"}");return;}
  PickupSettings next;
  for(const auto &f:pickupFields) {
    if(!server.hasArg(f.key)){server.send(400,"application/json","{\"error\":\"Missing setting\"}");return;}
    String value=server.arg(f.key);value.trim();char *end=nullptr;
    const float number=strtof(value.c_str(),&end);
    if(value.isEmpty()||*end){server.send(400,"application/json","{\"error\":\"Invalid numeric setting\"}");return;}
    next.*(f.member)=number;
  }
  const char *error=validatePickupSettings(next);
  if(error){server.send(400,"application/json",String("{\"error\":\"")+error+"\"}");return;}
  if(!savePickupSettings(next)){server.send(500,"application/json","{\"error\":\"Could not save settings\"}");return;}
  servicePickup(millis());sendPickupSettings();
}

/* Restore AND persist defaults while preserving any active alert until confirmed stillness. */
void restorePickupDefaults() {
  if(!savePickupSettings(PickupSettings{})){server.send(500,"application/json","{\"error\":\"Could not save defaults\"}");return;}
  servicePickup(millis());sendPickupSettings();
}
