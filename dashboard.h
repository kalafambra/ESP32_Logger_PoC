#pragma once
#include <Arduino.h>

const char DASHBOARD_HTML[] PROGMEM = R"DASHBOARD(
<!doctype html>
<html lang="en">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Environmental Monitor</title>
<style>
:root{color-scheme:dark;font-family:system-ui,-apple-system,"Segoe UI",sans-serif;background:#21151d;color:#fff2e5}
*{box-sizing:border-box}body{max-width:1100px;margin:auto;padding:32px 24px;background:radial-gradient(ellipse at top left,#48232d88,transparent 65%)}
.brand{display:flex;gap:24px;align-items:center;margin-bottom:28px}.fox-mark{width:116px;height:116px;flex:none;object-fit:cover;object-position:bottom;background:#ffdfcb;border:3px solid #e6a078;border-radius:24px;image-rendering:pixelated;box-shadow:0 8px 28px #10090e66}
h1{font-size:clamp(1.7rem,4vw,2.65rem);letter-spacing:-.04em;margin:4px 0 10px;font-weight:750}.eyebrow{text-transform:uppercase;letter-spacing:.18em;color:#f5b384;font-size:.72rem;font-weight:750}.brand p{margin:0}
p{color:#d6b9b8;line-height:1.6}.status{padding:16px 20px;background:#30212a;border:1px solid #69414a;border-left:3px solid #f5b384;border-radius:14px;margin:20px 0}
#connection{font-weight:700;color:#ffdfcb}#detail{margin-top:4px;color:#d6b9b8;font-size:.87rem}#error{color:#ffadba;min-height:1.5em}.cards{display:grid;grid-template-columns:minmax(0,1fr);gap:20px}
input{font:inherit;padding:10px 12px;width:140px;border:1px solid #94606a;border-radius:9px;background:#21151d;color:#fff2e5}input:focus-visible{outline:2px solid #ffc294;outline-offset:3px}input:disabled{opacity:.5}#altitudeError{color:#ffadba}.settings{display:flex;gap:14px;flex-wrap:wrap;align-items:center;margin:20px 0}.settings label{font-weight:600;font-size:.9rem}
article{background:linear-gradient(135deg,#34242e,#2b1e27);border:1px solid #69414a;border-radius:20px;padding:24px;min-width:0;box-shadow:0 6px 24px #10090e30}h2{font-size:.82rem;text-transform:uppercase;letter-spacing:.1em;color:#eec4b4;margin:0 0 12px}
.value{font-size:2.5rem;font-weight:650;letter-spacing:-.04em;font-variant-numeric:tabular-nums}.unit{font-size:1rem;color:#d6b9b8}canvas{width:100%;height:240px;display:block;margin-top:18px}#dewPoint{color:#ffd5a8}
small{color:#cbaaaa}footer{margin:24px 0;color:#cbaaaa;font-size:.8rem;line-height:1.6}
@media(max-width:540px){body{padding:20px 14px}.brand{gap:16px}.fox-mark{width:82px;height:82px;border-radius:18px}.brand p{font-size:.8rem}article{padding:18px}}
.pickup-banner{padding:16px 20px;border:1px solid #94606a;border-radius:14px;margin:16px 0;font-weight:650}.pickup-banner.active{background:#6b263c;border-color:#ffb47f;color:#fff2e5}.pickup-settings{margin:20px 0;padding:18px;border:1px solid #69414a;border-radius:14px;background:#30212a}.pickup-settings summary{cursor:pointer;font-weight:650}.pickup-fields{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:16px;margin:20px 0}.pickup-fields label{display:flex;flex-direction:column;gap:7px;font-size:.85rem}.pickup-fields input{width:100%}.pickup-settings button{font:inherit;padding:10px 14px;border:1px solid #94606a;border-radius:8px;background:#48232d;color:#fff2e5;cursor:pointer;margin:4px}.pickup-settings button:disabled{opacity:.5;cursor:wait}
</style>
<header class="brand"><img class="fox-mark" src="/fox.png" alt="Pixel-art fox mascot" width="116" height="116"><div><div class="eyebrow">Fox den · Live environment</div><h1>Environmental monitor</h1><p>ESP32 + BME280 · BME280 5 readings/s · one-second reports</p></div></header>
<div class="status" role="status" aria-live="polite"><div id="connection">Connecting to ESP32…</div><div id="detail">Waiting for sensor data</div><div id="error"></div></div>
<div id="pickupStatus" class="pickup-banner" role="status" aria-live="polite">Pickup detector: waiting for sensor data</div>
<p><a href="/pickup">Open pickup tuning lab: threshold plots, settings and CSV export →</a></p>
<details class="pickup-settings"><summary>Pickup detection settings</summary><p>Saved on the ESP32 and restored after reboot. Changing settings restarts baseline learning. An active alert clears only after confirmed stillness, including when held still in your hand.</p><form id="pickupSettingsForm"><div id="pickupFields" class="pickup-fields"></div><button id="pickupSave" type="submit" disabled>Save settings</button><button id="pickupDefaults" type="button" disabled>Restore defaults</button><p id="pickupSettingsStatus" role="status">Loading settings…</p></form></details>
<div class="settings"><label for="altitude">Altitude above sea level (m)</label><input id="altitude" type="number" min="-500" max="9000" step="0.1" disabled aria-describedby="altitudeError"><span id="altitudeError" role="status"></span></div>
<p>Dew point: <strong id="dewPoint">—</strong> °C <small>(calculated from one-second mean temperature and humidity)</small></p>
<p>Solid line: mean of 5 readings. Faint lines (30% opacity): minimum and maximum.</p><main class="cards">
<article><h2>Temperature</h2><span class="value" id="temperature">—</span> <span class="unit">°C</span><canvas id="tempChart" role="img" aria-label="Temperature in degrees Celsius over up to 600 seconds"></canvas><small>Time relative to now (seconds)</small></article>
<article><h2>Relative humidity</h2><span class="value" id="humidity">—</span> <span class="unit">%</span><canvas id="humChart" role="img" aria-label="Relative humidity in percent over up to 600 seconds"></canvas><small>Time relative to now (seconds)</small></article>
<article><h2>Estimated sea-level pressure</h2><span class="value" id="pressure">—</span> <span class="unit">hPa</span><canvas id="pressureChart" role="img" aria-label="Sea-level pressure over up to 600 seconds"></canvas><small>Time relative to now (seconds)</small></article>
<article><h2>MPU6050 acceleration</h2><p id="motionValues">Waiting for motion sensor</p><p><strong>Gyroscope</strong><br><span id="gyroValues">Waiting for motion sensor</span><br><small>One-second mean · saved zero-rate bias corrected · deg/s</small></p><button id="gyroCalibrate" type="button">Calibrate gyro (keep stationary)</button><p id="motionError" role="status"></p><button id="motionCalibrate" type="button" style="padding:10px 14px;border:1px solid #94606a;border-radius:9px;background:#48232d;color:#fff2e5;font:inherit;cursor:pointer">Calibrate flat (Z up)</button><p id="motionCalibrationStatus" role="status">Calibration removes fixed bias; gravity is retained.</p><small><span style="color:#ffb47f">X</span> · <span style="color:#e7b5ca">Y</span> · <span style="color:#e9d899">Z</span> · m/s², includes gravity · one-second means · 100 Hz acquisition target</small><canvas id="motionChart" role="img" aria-label="MPU6050 X Y Z acceleration in meters per second squared"></canvas><small>Time relative to now (seconds)</small></article>
</main>
<footer>Up to 600 seconds of history; shorter histories fill the graph width. Gaps indicate unavailable reports. Altitude adjusts the pressure estimate in this tab; reload restores the configured altitude. No internet services required.</footer>
<script>
'use strict';
const history=[],motionHistory=[];
let lastMotionSequence=null;
const charts=[['tempChart','t','#ffb47f','°C'],['humChart','h','#e7b5ca','%'],['pressureChart','p','#e9d899','hPa'],['motionChart','a','#ffb47f','m/s²']];
let lastSequence=null,lastUptime=null,lastSuccess=0;
let altitudeM=0,altitudeInitialized=false,currentReading=null;
const $=id=>document.getElementById(id);
/* Clear derived fields too, so a network/sensor error cannot leave plausible stale values. */
function clearValues(){currentReading=null;for(const id of ['temperature','humidity','pressure','dewPoint'])$(id).textContent='—';}
// Standard-atmosphere reduction, matching Adafruit's seaLevelForAltitude.
/* Reduce measured pressure to a common sea-level reference using this tab's altitude. */
function seaLevelPressure(pressure,altitude){return pressure/Math.pow(1-altitude/44330,5.255);}
// Bolton/Magnus approximation over liquid water; zero RH has no finite dew point.
/* Estimate condensation temperature from mean inputs; reject undefined zero-humidity results. */
function dewPointC(t,rh){
 if(!Number.isFinite(t)||!Number.isFinite(rh)||rh<=0||rh>100)return null;
 const gamma=Math.log(rh/100)+17.67*t/(243.5+t);
 const dew=243.5*gamma/(17.67-gamma);return Number.isFinite(dew)?dew:null;
}
/* Recalculate displayed pressure/dew point when fresh data or a new altitude arrives. */
function updateCurrent(){
 if(!currentReading)return;
 $('temperature').textContent=currentReading.temperature_c.toFixed(2);
 $('humidity').textContent=currentReading.humidity_pct.toFixed(2);
 $('pressure').textContent=seaLevelPressure(currentReading.pressure_hpa,altitudeM).toFixed(2);
 const dew=dewPointC(currentReading.temperature_c,currentReading.humidity_pct);
 $('dewPoint').textContent=dew===null?'Unavailable (RH = 0%)':dew.toFixed(2);
}
/* Convert pressure at render time to preserve raw history when altitude changes. */
function channelValues(point,key){return key==='p'&&point.p?point.p.map(p=>seaLevelPressure(p,altitudeM)):point[key];}
/* Fill the plot with available history instead of reserving empty space for ten minutes. */
function timeRange(now,records=history){
 // Anchor to newest report so sparse one-second reports fill the full width.
 const end=records.length?records[records.length-1].at:now;
 const start=records.length>1?Math.max(end-600000,records[0].at):end-5000;
 return {start,end,span:Math.max(1,end-start)};
}
/* Keep the previous valid correction while the user is typing an incomplete altitude. */
$('altitude').addEventListener('input',()=>{
 const value=Number($('altitude').value);
 if($('altitude').value.trim()===''||!Number.isFinite(value)||value< -500||value>9000){
  $('altitudeError').textContent='Enter an altitude from −500 to 9000 m. Keeping the last valid altitude.';return;
 }
 altitudeM=value;$('altitudeError').textContent='';updateCurrent();draw();
});
/* Redraw three independent plots with shared time bounds and faint min/max envelopes. */
function draw(onlyMotion=false){
 const now=performance.now();
 while(history.length && history[0].at<now-600000)history.shift();
 while(motionHistory.length && motionHistory[0].at<now-600000)motionHistory.shift();
 for(const [id,key,color,unit] of charts){
  if(onlyMotion===true&&key!=='a')continue;
  const records=key==='a'?motionHistory:history;
  const {start,end,span}=timeRange(now,records);
  const canvas=$(id),ratio=window.devicePixelRatio||1,w=canvas.clientWidth,h=240;
  if(canvas.width!==Math.round(w*ratio)||canvas.height!==Math.round(h*ratio)){canvas.width=Math.round(w*ratio);canvas.height=Math.round(h*ratio);}
  const ctx=canvas.getContext('2d');ctx.setTransform(ratio,0,0,ratio,0,0);ctx.clearRect(0,0,w,h);
  const left=64,right=w-12,top=25,bottom=h-30;
  const points=records.filter(p=>p[key]&&p[key].every(Number.isFinite));
  const values=points.flatMap(p=>channelValues(p,key)); // Include extrema in vertical scaling.
  let lo=values.length?Math.min(...values):0,hi=values.length?Math.max(...values):1;
  const margin=Math.max((hi-lo)*.15,key==='p'?.5:.2);lo-=margin;hi+=margin;
  ctx.font='11px system-ui';ctx.fillStyle='#d6b9b8';ctx.fillText(unit,left,13);
  for(let i=0;i<=4;i++){
   const y=top+(bottom-top)*i/4;
   ctx.strokeStyle='#62424d';ctx.beginPath();ctx.moveTo(left,y);ctx.lineTo(right,y);ctx.stroke();
   ctx.fillText((hi-(hi-lo)*i/4).toFixed(1),2,y+4);
  }
  for(const fraction of [0,0.5,1]){
   const x=left+fraction*(right-left),seconds=(start+fraction*span-now)/1000;
   ctx.textAlign=fraction===1?'right':fraction===0?'left':'center';ctx.fillText(seconds.toFixed(1),x,h-8);
  }
  ctx.textAlign='left';ctx.strokeStyle=color;
  // Render min and max first, each at exactly 30% opacity, then the mean.
  for(const index of [1,2,0]){
   ctx.globalAlpha=key==='a'?1:index===0?1:0.3;ctx.lineWidth=index===0||key==='a'?2:1.5;
   ctx.strokeStyle=key==='a'?['#ffb47f','#e7b5ca','#e9d899'][index]:color;
   ctx.beginPath();let previous=null;
   for(const point of records){
    if(!point[key]||!Number.isFinite(point[key][index])){previous=null;continue;}
    const x=left+(point.at-start)/span*(right-left),y=bottom-(channelValues(point,key)[index]-lo)/(hi-lo)*(bottom-top);
    // Sequence continuity, not browser-poll timing, determines real gaps.
    if(!previous||point.sequence!==previous.sequence+1)ctx.moveTo(x,y);else ctx.lineTo(x,y);
    previous=point;
   }
   ctx.stroke();
  }
  ctx.globalAlpha=1;
  if(!points.length){ctx.fillStyle='#d6b9b8';ctx.fillText('Waiting for valid samples',left+6,(top+bottom)/2);}
 }
}
/* Fetch incremental reports and backfill missed polls, without overlapping HTTP requests. */
async function poll(){
 const started=performance.now(),controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),2000);
 try{
  const response=await fetch('/api/readings?since='+(lastSequence??0),{cache:'no-store',signal:controller.signal});
  if(!response.ok)throw new Error('HTTP '+response.status);
  const data=await response.json();
  if(typeof data.sequence!=='number'||typeof data.uptime_ms!=='number')throw new Error('Invalid API response');
  const now=performance.now();lastSuccess=now;
  // Load the firmware default once; later responses must not overwrite a user's edit.
  if(!altitudeInitialized){
   const value=data.default_altitude_m;
   if(!Number.isFinite(value)||value< -500||value>9000)throw new Error('Invalid default altitude in firmware');
   altitudeM=value;altitudeInitialized=true;$('altitude').value=String(value);$('altitude').disabled=false;
  }
  // A reboot invalidates sequence continuity and the previous device-uptime timeline.
  if(lastUptime!==null && (data.uptime_ms<lastUptime||data.sequence<lastSequence)){history.length=0;lastSequence=null;}
  lastUptime=data.uptime_ms;
  if(!Array.isArray(data.history))throw new Error('Missing aggregate history; refresh after firmware update');
  for(const point of data.history){
   if(point.sequence<=lastSequence)continue; // Repeated API polls must not duplicate reports.
   const channels=[point.temperature_c,point.humidity_pct,point.pressure_hpa];
   const ok=point.ok&&point.sample_count===5&&channels.every(c=>Array.isArray(c)&&c.length===3&&c.every(Number.isFinite));
   // Translate device-relative age into the browser's monotonic clock domain.
   history.push({sequence:point.sequence,at:now-Math.min(point.age_ms,600000),
                 t:ok?point.temperature_c:null,h:ok?point.humidity_pct:null,p:ok?point.pressure_hpa:null});
   lastSequence=point.sequence;
  }
  const valid=data.sensor_ok&&[data.temperature_c,data.humidity_pct,data.pressure_hpa].every(Number.isFinite);
  $('connection').textContent=data.wifi_connected?'Connected to ESP32':'ESP32 Wi-Fi disconnected';
  $('error').textContent=valid?'':('Sensor: '+(data.error||'Waiting for a complete one-second window'));
  $('detail').textContent='Report #'+data.sequence+' · '+data.sample_count+'/5 readings · '+(data.sample_age_ms===null?'No report yet':data.sample_age_ms+' ms old')+' · Wi-Fi '+data.rssi_dbm+' dBm';
  if(valid){
   currentReading=data;updateCurrent();
  }else clearValues();
 }catch(error){
  clearValues();$('connection').textContent='Disconnected / ESP32 unreachable';
  $('error').textContent='Request failed: '+error.message;
  $('detail').textContent=lastSuccess?'Last contact '+Math.round((performance.now()-lastSuccess)/1000)+' s ago':'Check the device IP and local Wi-Fi connection';
  // Preserve history: the next successful request backfills missed reports.
 }finally{
  // Schedule after completion so slow connections never accumulate pending fetches.
  clearTimeout(timeout);draw();setTimeout(poll,Math.max(0,1000-(performance.now()-started)));
 }
}
let lastMotionUptime=null;
/* Process only completed one-second aggregates; keep raw acquisition off the network. */
function consumeMotion(data,now){
 const motion=data.motion;
 if(!motion)throw new Error('Missing motion status');
 if(lastMotionUptime!==null&&(data.uptime_ms<lastMotionUptime||motion.sequence<lastMotionSequence)){
  motionHistory.length=0;lastMotionSequence=null;
 }
 lastMotionUptime=data.uptime_ms;
 updatePickupStatus(data.pickup);
 $('motionCalibrate').disabled=!!motion.calibrating;$('gyroCalibrate').disabled=!!motion.calibrating;
 $('motionCalibrationStatus').textContent=motion.calibrating?'Calibrating '+(motion.calibration_kind||'accel')+': '+motion.calibration_samples+'/500 samples. Keep still.':motion.calibration_error||'Calibration removes fixed bias; gravity is retained.';
 const gyroValues=motion.gyroscope_deg_s??motion.gyroscope_rad_s?.map(v=>v*180/Math.PI);
 const gyroOK=motion.ok&&Array.isArray(gyroValues)&&gyroValues.length===3&&gyroValues.every(Number.isFinite);
 $('gyroValues').textContent=gyroOK?gyroValues.map((v,i)=>['X','Y','Z'][i]+': '+v.toFixed(3)).join(' · ')+' deg/s':'—';
 const ok=motion.ok&&Array.isArray(motion.acceleration_m_s2)&&motion.acceleration_m_s2.length===3&&motion.acceleration_m_s2.every(Number.isFinite);
 $('motionError').textContent=ok?(motion.warning||''):(motion.error||'MPU6050 unavailable');
 $('motionValues').textContent=ok?motion.acceleration_m_s2.map((v,i)=>['X','Y','Z'][i]+': '+v.toFixed(3)).join(' · ')+' m/s² · n='+(motion.sample_count??'?')+' · FIFO overflows: '+(motion.fifo_overflows??0):'—';
 for(const point of data.history||[]){
  if(point.sequence<=lastMotionSequence)continue;
  const valid=point.ok&&Array.isArray(point.a)&&point.a.length===3&&point.a.every(Number.isFinite);
  motionHistory.push({sequence:point.sequence,at:now-Math.min(point.age_ms,600000),a:valid?point.a:null});
  lastMotionSequence=point.sequence;
 }
}
/* Use an independent 1 Hz request stream, with bounded backfill and no overlapping fetches. */
async function pollMotion(){
 const started=performance.now(),controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),2000);
 try{
  const response=await fetch('/api/motion?since='+(lastMotionSequence??0),{cache:'no-store',signal:controller.signal});
  if(!response.ok)throw new Error('HTTP '+response.status);
  consumeMotion(await response.json(),performance.now());
 }catch(error){
  $('motionValues').textContent='—';$('gyroValues').textContent='—';$('motionError').textContent='Motion request failed: '+error.message;
 }finally{
  clearTimeout(timeout);
  // Coalesce redraws to the publication rate; do not repaint environmental plots here.
  draw(true);setTimeout(pollMotion,Math.max(0,1000-(performance.now()-started)));
 }
}
/* Calibration is explicit because assuming the board is flat on every boot would destroy valid orientation data. */
$('motionCalibrate').addEventListener('click',async()=>{
 if(!window.confirm('Place the module flat with positive Z up. Keep it completely still for five seconds. Start bias calibration?'))return;
 $('motionCalibrate').disabled=true;
 try{
  const response=await fetch('/api/motion/calibrate',{method:'POST'});
  if(!response.ok)throw new Error('HTTP '+response.status);
  $('motionCalibrationStatus').textContent='Calibration starting. Keep flat and still for five seconds.';
 }catch(error){$('motionCalibrationStatus').textContent='Calibration failed: '+error.message;$('motionCalibrate').disabled=false;}
});
/* Explicit stationary gyro calibration persists offsets without assuming a flat orientation. */
$('gyroCalibrate').addEventListener('click',async()=>{
 if(!window.confirm('Keep the module completely stationary for five seconds. Calibrate gyro zero-rate bias?'))return;
 $('gyroCalibrate').disabled=true;
 try{const response=await fetch('/api/motion/calibrate-gyro',{method:'POST'});if(!response.ok)throw new Error('HTTP '+response.status);$('motionCalibrationStatus').textContent='Gyro calibration starting. Keep completely still.';}
 catch(error){$('motionCalibrationStatus').textContent='Calibration failed: '+error.message;$('gyroCalibrate').disabled=false;}
});
let pickupSettingFields=[];
/* Keep server-provided labels as text rather than interpreting them as HTML. */
function pickupEscape(value){return String(value).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
/* Reflect detector state independently of the one-second graph's mean values. */
function updatePickupStatus(status){
 if(!status)return;
 const node=$('pickupStatus');node.className='pickup-banner'+(status.active?' active':'');
 if(status.active)node.textContent='Sensor has been picked up — LED alert active. '+(status.error||'Waiting for '+(status.stationary_ms/1000)+' s of stillness.');
 else if(status.state==='armed')node.textContent='Stationary · pickup detector armed';
 else if(status.state==='learning_baseline')node.textContent='Learning stationary baseline: '+(status.quiet_ms/1000).toFixed(1)+' / '+(status.stationary_ms/1000)+' s';
 else node.textContent='Pickup detector unavailable: '+(status.error||status.state);
 if(!status.led_available)node.textContent+=' · LED unavailable';
}
/* Render settings and validation limits from firmware metadata to avoid diverging UI defaults. */
function renderPickupSettings(data){
 if(!Array.isArray(data.fields)||!data.fields.length)throw new Error('Invalid settings response');
 for(const f of data.fields)if(!/^[a-z_]+$/.test(f.key)||![f.min,f.max,f.step,f.value].every(Number.isFinite))throw new Error('Invalid setting metadata');
 pickupSettingFields=data.fields;
 $('pickupFields').innerHTML=data.fields.map(f=>'<label for="pickup-'+f.key+'">'+pickupEscape(f.label)+' ('+pickupEscape(f.display_unit||f.unit)+')<input id="pickup-'+f.key+'" name="'+f.key+'" type="number" required min="'+f.min*(f.display_scale||1)+'" max="'+f.max*(f.display_scale||1)+'" step="'+((f.display_scale||1)===1?f.step:'any')+'"><small>Default: '+pickupEscape(f.default*(f.display_scale||1))+'</small></label>').join('');
 for(const f of data.fields)$('pickup-'+f.key).value=String(f.value*(f.display_scale||1));
 $('pickupSettingsStatus').textContent=data.persisted?'Settings saved on ESP32. Fresh stationary baseline required.':'Code defaults in use. Save to persist settings.';
 $('pickupSave').disabled=false;$('pickupDefaults').disabled=false;
}
/* Return structured validation/storage errors instead of pretending failed saves took effect. */
async function requestPickupSettings(options){
 const response=await fetch('/api/pickup/'+(options?.defaults?'defaults':'settings'),options?.request||{cache:'no-store'});
 const data=await response.json();if(!response.ok)throw new Error(data.error||'HTTP '+response.status);
 renderPickupSettings(data);
}
/* Apply a complete form; firmware validates finite numbers and cross-field relationships again. */
$('pickupSettingsForm').addEventListener('submit',async event=>{
 event.preventDefault();$('pickupSave').disabled=true;$('pickupDefaults').disabled=true;
 try{
  const body=new URLSearchParams();
  for(const f of pickupSettingFields){
   const text=$('pickup-'+f.key).value.trim(),value=Number(text)/(f.display_scale||1);
   if(!text||!Number.isFinite(value)||value<f.min||value>f.max)throw new Error('Check '+f.label);
   body.set(f.key,String(value));
  }
  await requestPickupSettings({request:{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body.toString()}});
 }catch(error){$('pickupSettingsStatus').textContent='Not saved: '+error.message;}
 finally{$('pickupSave').disabled=false;$('pickupDefaults').disabled=false;}
});
/* Restore defaults explicitly, preserving active alerts while the detector relearns stillness. */
$('pickupDefaults').addEventListener('click',async()=>{
 if(!window.confirm('Restore and save pickup defaults? An active alert still requires confirmed stillness.'))return;
 $('pickupSave').disabled=true;$('pickupDefaults').disabled=true;
 try{await requestPickupSettings({defaults:true,request:{method:'POST'}});}
 catch(error){$('pickupSettingsStatus').textContent='Defaults not saved: '+error.message;}
 finally{$('pickupSave').disabled=false;$('pickupDefaults').disabled=false;}
});
requestPickupSettings().catch(error=>{$('pickupSettingsStatus').textContent='Could not load settings: '+error.message;});
window.addEventListener('resize',draw);
setInterval(draw,1000);poll();pollMotion();
</script>
</html>
)DASHBOARD";

