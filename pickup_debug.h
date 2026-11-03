#pragma once
#include <Arduino.h>
#include <new>
#include "pickup_detector.h"

/* Compact telemetry is independent of detection and exists only while a tuning client subscribes. */
class PickupDebug {
 public:
  static constexpr unsigned CAPACITY=128,CAPTURE_MS=50;
  enum Field {VALID,ACTIVE,ARMED,LED,RAW_A,FILTERED_A,REST_A,ACCEL_DEV,FIXED_ACCEL,NOISE,NOISE_MULT,ADAPTIVE_ACCEL,EFFECTIVE_ACCEL,TILT,TILT_LIMIT,GYRO,GYRO_LIMIT,ACCEL_RMS,ACCEL_QUIET,GYRO_RMS,INSTANT_GYRO,GYRO_QUIET,QUIET_MS,QUIET_LIMIT,EVIDENCE_MS,EVIDENCE_LIMIT,SMOOTHING_MS,DELIVERY_GAP_MS,SAMPLE_AGE_MS,GAP_LIMIT,LED_PERIOD,LED_DUTY,OBSERVED_PERIOD,OBSERVED_DUTY,REVISION,TRIGGER_MASK,CONFIRMED_MASK,QUIET_BLOCK_MASK,EVENTS,COUNT};
  struct Record { uint32_t sequence,ms;float values[COUNT]; };
  Record *records=nullptr;
  uint32_t sequence=0,lastCapture=0,lastRequest=0;
  unsigned head=0,count=0;
  bool subscribed=false,lastLed=false;
  uint32_t lastRise=0,lastOnMs=0;
  float observedPeriod=NAN,observedDuty=NAN;
  bool lastQuietRejected=false;
  uint8_t lastTriggerMask=0;

  /* Refresh a short subscription lease, avoiding continuous debug work when the page is closed. */
  bool subscribe(uint32_t now){
    if(!records){records=new(std::nothrow) Record[CAPACITY];if(!records)return false;head=count=0;}
    subscribed=true;lastRequest=now;return true;
  }

  /* Capture 20 Hz samples and immediate state/LED edges, including the confirming persistence frame. */
  bool capture(const PickupDetector &p,bool valid,uint32_t now,uint32_t gap,uint32_t age,bool led,uint32_t revision){
    if(!subscribed)return false;
    if(now-lastRequest>3000){
      delete[] records;records=nullptr;head=count=0;subscribed=false;
      lastRise=lastOnMs=0;observedPeriod=observedDuty=NAN;return false;
    }
    if(!p.active){lastRise=lastOnMs=0;observedPeriod=observedDuty=NAN;}
    else if(led!=lastLed){
      if(led){
        if(lastRise){observedPeriod=now-lastRise;observedDuty=observedPeriod?100.0f*lastOnMs/observedPeriod:NAN;}
        lastRise=now;
      }else if(lastRise)lastOnMs=now-lastRise;
    }
    lastLed=led;
    const Record *previous=count?&records[(head+CAPACITY-1)%CAPACITY]:nullptr;
    const bool quietRejected=valid&&p.quietBlockMask;
    const uint8_t triggerMask=valid?p.triggerMask:0;
    const bool edge=!previous||previous->values[VALID]!=valid||previous->values[ACTIVE]!=p.active||
      previous->values[ARMED]!=p.armed||previous->values[LED]!=led||previous->values[REVISION]!=revision||
      quietRejected!=lastQuietRejected||triggerMask!=lastTriggerMask||
      previous->values[QUIET_BLOCK_MASK]!=(valid?p.quietBlockMask:0)||previous->values[EVENTS]!=p.events;
    if(!edge&&now-lastCapture<CAPTURE_MS)return false;
    Record r{};r.sequence=++sequence;r.ms=now;for(float &v:r.values)v=NAN;
    r.values[VALID]=valid;r.values[ACTIVE]=p.active;r.values[ARMED]=p.armed;r.values[LED]=led;
    r.values[TRIGGER_MASK]=triggerMask;r.values[CONFIRMED_MASK]=p.confirmedTriggerMask;
    r.values[QUIET_BLOCK_MASK]=valid?p.quietBlockMask:0;r.values[EVENTS]=p.events;
    const auto &s=p.settings;
    r.values[FIXED_ACCEL]=s.accel_trigger;r.values[NOISE_MULT]=s.noise_multiplier;
    r.values[TILT_LIMIT]=s.tilt_deg;r.values[GYRO_LIMIT]=s.gyro_trigger;
    r.values[ACCEL_QUIET]=s.stationary_accel_rms;r.values[GYRO_QUIET]=s.stationary_gyro;
    r.values[QUIET_LIMIT]=s.stationary_ms;r.values[EVIDENCE_LIMIT]=s.trigger_ms;
    r.values[SMOOTHING_MS]=s.smoothing_ms;r.values[GAP_LIMIT]=s.max_gap_ms;
    r.values[LED_PERIOD]=s.led_period_ms;r.values[LED_DUTY]=s.led_duty_pct;r.values[REVISION]=revision;
    r.values[DELIVERY_GAP_MS]=gap;r.values[SAMPLE_AGE_MS]=age;
    r.values[OBSERVED_PERIOD]=observedPeriod;r.values[OBSERVED_DUTY]=observedDuty;
    if(valid){
      r.values[RAW_A]=p.rawMagnitude;r.values[FILTERED_A]=p.filteredMagnitude;
      r.values[GYRO]=p.rotationRate;r.values[INSTANT_GYRO]=p.instantaneousRotation;
      r.values[QUIET_MS]=p.quietMs();r.values[EVIDENCE_MS]=p.triggerEvidenceMs;
      if(p.quietWindowEvaluated){r.values[ACCEL_RMS]=p.accelRms;r.values[GYRO_RMS]=p.gyroRms;}
      if(p.referenceValid){
        r.values[REST_A]=p.baselineMagnitude;r.values[ACCEL_DEV]=p.accelDeviation;r.values[TILT]=p.tiltDegrees;
        r.values[NOISE]=p.baselineNoise;r.values[ADAPTIVE_ACCEL]=p.baselineNoise*s.noise_multiplier;
        r.values[EFFECTIVE_ACCEL]=fmaxf(s.accel_trigger,r.values[ADAPTIVE_ACCEL]);
      }
    }
    records[head]=r;head=(head+1)%CAPACITY;if(count<CAPACITY)++count;lastCapture=now;
    lastQuietRejected=quietRejected;lastTriggerMask=triggerMask;return true;
  }

  /* Serialize fixed-width rows with named columns; nulls represent unavailable signals, never fake zeros. */
  void appendJson(String &json,uint32_t since)const{
    static const char *names[]={"valid","active","armed","led_on","raw_accel","filtered_accel","rest_accel","accel_deviation","accel_trigger","baseline_noise","noise_multiplier","adaptive_accel","effective_accel","tilt_deg","tilt_limit","gyro_rad_s","gyro_trigger","accel_rms","stationary_accel_rms","gyro_rms","instant_gyro","stationary_gyro","quiet_ms","stationary_ms","evidence_ms","trigger_ms","smoothing_ms","delivery_gap_ms","sample_age_ms","max_gap_ms","led_period_ms","led_duty_pct","observed_led_period_ms","observed_led_duty_pct","settings_revision"};
    static_assert(sizeof(names)/sizeof(names[0])+4==COUNT,"Debug schema must match rows");
    const uint32_t oldest=count?records[(head+CAPACITY-count)%CAPACITY].sequence:0;
    json+=",\"capture_ms\":50,\"latest_sequence\":"+String(sequence)+",\"oldest_sequence\":"+String(oldest);
    json+=",\"columns\":[\"sequence\",\"uptime_ms\"";
    for(const char *name:names){json+=",\"";json+=name;json+='"';}
    json+=",\"trigger_mask\",\"confirmed_trigger_mask\",\"quiet_block_mask\",\"events\"],\"rows\":[";
    unsigned sent=0;
    for(unsigned i=0;i<count&&sent<24;++i){
      const auto &r=records[(head+CAPACITY-count+i)%CAPACITY];if(r.sequence<=since)continue;
      if(sent++)json+=',';json+='[';json+=String(r.sequence)+','+String(r.ms);
      for(float value:r.values){json+=',';json+=isfinite(value)?String(value,4):String("null");}json+=']';
    }
    json+=']';
  }
};
