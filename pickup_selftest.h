#pragma once
#include "pickup_detector.h"

/* Exercise the real detector before hardware startup; reuse its buffer rather than a second 24 KB instance. */
inline bool runPickupSelfTests(PickupDetector &p) {
  const float flat[3] = {0,0,9.80665f}, still[3] = {0,0,0};
  auto fresh = [&]() { p.settings = PickupSettings{}; p.reset(false); };
  auto feed = [&](const float *a, const float *w, unsigned count) { for (unsigned i=0;i<count;++i) p.process(a,w); };
  fresh(); feed(flat,still,299); if(p.armed||p.active)return false;
  feed(flat,still,1); if(!p.armed||p.active)return false;
  feed(flat,still,600); if(p.active)return false;
  const float bump[3]={0,0,12}; feed(bump,still,1); feed(flat,still,100); if(p.active)return false;
  const float lift[3]={0,0,11.5f}; feed(lift,still,80); if(!p.active)return false;
  feed(flat,still,200); if(!p.active)return false;
  feed(flat,still,110); if(p.active||!p.armed)return false;
  fresh(); feed(flat,still,300);
  const float tilt[3]={4.903325f,0,8.492809f}; feed(tilt,still,80); if(!p.active)return false;
  feed(tilt,still,310); if(p.active||!p.armed)return false; // New orientation can become stationary.
  fresh(); feed(flat,still,300);
  const float spin[3]={0,0,0.5f}; feed(flat,spin,80); if(!p.active)return false;
  if(!(p.confirmedTriggerMask&4)||p.quietBlockMask!=2)return false;
  feed(flat,spin,400); if(!p.active)return false; // Rotation cannot clear the alert.
  if(p.quietMs()!=0||p.quietBlockMask!=2)return false; // Exact gate blocking clearing.
  if(p.rotationRate<0.4f||p.quietWindowEvaluated)return false; // Live alert telemetry must not freeze.
  p.reset(); if(!p.active||p.armed)return false; // Sensor faults preserve alert.
  feed(flat,still,299); if(!p.active)return false;
  feed(flat,still,1); if(p.active||!p.armed)return false;
  feed(flat,spin,80); if(!p.active)return false;
  p.configure(PickupSettings{}); if(!p.active||p.armed)return false;
  feed(flat,still,300); if(p.active||!p.armed)return false;
  fresh(); feed(flat,spin,600); if(p.armed||p.active)return false;
  // Adaptive noise and thresholds remain finite for a perfectly quiet baseline.
  fresh(); feed(flat,still,300); if(!std::isfinite(p.baselineNoise))return false;
  fresh(); return true;
}
