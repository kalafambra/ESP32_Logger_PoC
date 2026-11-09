#pragma once
#include <Arduino.h>

const char DASHBOARD_HTML[] PROGMEM = R"DASHBOARD(
<!doctype html>
<html lang="en">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Environmental Monitor</title>
<style>
:root{color-scheme:dark;font-family:system-ui,-apple-system,"Segoe UI",sans-serif;background:#11161d;color:#e4e9ef}
*{box-sizing:border-box}body{max-width:1100px;margin:auto;padding:32px 24px;background:#11161d}
.brand{margin-bottom:28px}
.temperature-values{display:flex;gap:28px;flex-wrap:wrap}.temperature-values small{display:block;margin-bottom:4px}#mpuTemperatureError{color:#e0a0a6}.bme-label{color:#8db4d6}.mpu-label{color:#c9b789}
a{color:#91b6d8;text-underline-offset:3px}a:hover{color:#c1d8ec}
button{font:inherit;padding:10px 14px;border:1px solid #46566a;border-radius:8px;background:#253344;color:#e4e9ef;cursor:pointer}button:hover{background:#304259}button:focus-visible{outline:2px solid #9fc5e5;outline-offset:3px}
h1{font-size:clamp(1.7rem,4vw,2.65rem);letter-spacing:-.04em;margin:4px 0 10px;font-weight:750}.eyebrow{text-transform:uppercase;letter-spacing:.18em;color:#91b6d8;font-size:.72rem;font-weight:750}.brand p{margin:0}
p{color:#a5b1bf;line-height:1.6}.status{padding:16px 20px;background:#19212b;border:1px solid #354253;border-left:3px solid #91b6d8;border-radius:14px;margin:20px 0}
#connection{font-weight:700;color:#d3e0ec}#detail{margin-top:4px;color:#a5b1bf;font-size:.87rem}#error{color:#e0a0a6;min-height:1.5em}.cards{display:grid;grid-template-columns:minmax(0,1fr);gap:20px}
input{font:inherit;padding:10px 12px;width:140px;border:1px solid #46566a;border-radius:9px;background:#11161d;color:#e4e9ef}input:focus-visible{outline:2px solid #9fc5e5;outline-offset:3px}input:disabled{opacity:.5}#altitudeError{color:#e0a0a6}.settings{display:flex;gap:14px;flex-wrap:wrap;align-items:center;margin:20px 0}.settings label{font-weight:600;font-size:.9rem}
article{background:#19212b;border:1px solid #354253;border-radius:20px;padding:24px;min-width:0;box-shadow:0 6px 24px #00000020}h2{font-size:.82rem;text-transform:uppercase;letter-spacing:.1em;color:#b7c7d6;margin:0 0 12px}
.value{font-size:2.5rem;font-weight:650;letter-spacing:-.04em;font-variant-numeric:tabular-nums}.unit{font-size:1rem;color:#a5b1bf}canvas{width:100%;height:240px;display:block;margin-top:18px}#dewPoint{color:#a9c9e3}
small{color:#94a3b4}footer{margin:24px 0;color:#94a3b4;font-size:.8rem;line-height:1.6}
@media(max-width:540px){body{padding:20px 14px}.brand p{font-size:.8rem}article{padding:18px}}
.pickup-banner{padding:16px 20px;border:1px solid #46566a;border-radius:14px;margin:16px 0;font-weight:650}.pickup-banner.active{background:#30222b;border-color:#bd7f89;color:#e4e9ef}.pickup-settings{margin:20px 0;padding:18px;border:1px solid #354253;border-radius:14px;background:#19212b}.pickup-settings summary{cursor:pointer;font-weight:650}.pickup-fields{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:16px;margin:20px 0}.pickup-fields label{display:flex;flex-direction:column;gap:7px;font-size:.85rem}.pickup-fields input{width:100%}.pickup-settings button{font:inherit;padding:10px 14px;border:1px solid #46566a;border-radius:8px;background:#253344;color:#e4e9ef;cursor:pointer;margin:4px}.pickup-settings button:disabled{opacity:.5;cursor:wait}
</style>
<header class="brand"><div class="eyebrow">Live sensor readings</div><h1>Environmental monitor</h1><p>ESP32 + BME280 · BME280 5 readings/s · one-second reports</p></header>
<div class="status" role="status" aria-live="polite"><div id="connection">Connecting to ESP32…</div><div id="detail">Waiting for sensor data</div><div id="error"></div></div>
<div id="pickupStatus" class="pickup-banner" role="status" aria-live="polite">Pickup detector: waiting for sensor data</div>
<p><a href="/pickup">Open pickup tuning lab: threshold plots, settings and CSV export →</a></p>
<details class="pickup-settings"><summary>Pickup detection settings</summary><p>Saved on the ESP32 and restored after reboot. Changing settings restarts baseline learning. An active alert clears only after confirmed stillness, including when held still in your hand.</p><form id="pickupSettingsForm"><div id="pickupFields" class="pickup-fields"></div><button id="pickupSave" type="submit" disabled>Save settings</button><button id="pickupDefaults" type="button" disabled>Restore defaults</button><p id="pickupSettingsStatus" role="status">Loading settings…</p></form></details>
<div class="settings"><label for="altitude">Altitude above sea level (m)</label><input id="altitude" type="number" min="-500" max="9000" step="0.1" disabled aria-describedby="altitudeError"><span id="altitudeError" role="status"></span></div>
<p>Dew point: <strong id="dewPoint">—</strong> °C <small>(calculated from one-second mean temperature and humidity)</small></p>
<p>BME280: solid mean of 5 readings, faint minimum/maximum (30% opacity). MPU6050 temperature: one sample per second.</p><main class="cards">
<article><h2>Temperature</h2><div class="temperature-values"><div><small class="bme-label">BME280 · ambient</small><span class="value" id="temperature">—</span> <span class="unit">°C</span></div><div><small class="mpu-label">MPU6050 · chip</small><span class="value" id="mpuTemperature">—</span> <span class="unit">°C</span></div></div><small id="mpuTemperatureError" role="status"></small><canvas id="tempChart" role="img" aria-label="BME280 ambient and MPU6050 chip temperatures in degrees Celsius over up to 600 seconds"></canvas><small><span class="bme-label">Blue: BME280</span> · <span class="mpu-label">Amber: MPU6050 chip</span> · Time relative to now (seconds)</small></article>
<article><h2>Relative humidity</h2><span class="value" id="humidity">—</span> <span class="unit">%</span><canvas id="humChart" role="img" aria-label="Relative humidity in percent over up to 600 seconds"></canvas><small>Time relative to now (seconds)</small></article>
<article><h2>Estimated sea-level pressure</h2><span class="value" id="pressure">—</span> <span class="unit">hPa</span><canvas id="pressureChart" role="img" aria-label="Sea-level pressure over up to 600 seconds"></canvas><small>Time relative to now (seconds)</small></article>
<article><h2>MPU6050 acceleration</h2><p id="motionValues">Waiting for motion sensor</p><p><strong>Gyroscope</strong><br><span id="gyroValues">Waiting for motion sensor</span><br><small>One-second mean · saved zero-rate bias corrected · deg/s</small></p><button id="gyroCalibrate" type="button">Calibrate gyro (keep stationary)</button><p id="motionError" role="status"></p><button id="motionCalibrate" type="button" style="padding:10px 14px;border:1px solid #46566a;border-radius:9px;background:#253344;color:#e4e9ef;font:inherit;cursor:pointer">Calibrate flat (Z up)</button><p id="motionCalibrationStatus" role="status">Calibration removes fixed bias; gravity is retained.</p><small><span style="color:#8db4d6">X</span> · <span style="color:#86b8ad">Y</span> · <span style="color:#c9b789">Z</span> · m/s², includes gravity · one-second means · 100 Hz acquisition target</small><canvas id="motionChart" role="img" aria-label="MPU6050 X Y Z acceleration in meters per second squared"></canvas><small>Time relative to now (seconds)</small></article>
<article><h2>Microphone spectrum</h2>
<p id="spectrumStatus" role="status">Waiting for microphone</p>
<p id="spectrumValues">No audio measurements yet</p>
<canvas id="spectrumChart" role="img" aria-label="Live microphone spectrum from zero to eight kilohertz in digital decibels, with optional exposed reference"></canvas>
<small>Frequency (Hz) · RMS power per FFT bin (dBFS) · fixed scale -120 to 0 dBFS · blue: live · amber: reference</small>
<p id="spectrumBands"></p>
<button id="spectrumSave" type="button" disabled>Save exposed reference</button>
<button id="spectrumClear" type="button" disabled>Clear reference</button>
<p id="spectrumComparison" role="status">For a useful comparison, play the same steady broadband sound at the same volume and distance, then cover the microphone. A quieter room alone can also lower these levels.</p>
<small id="spectrumResolution">Mono Hann FFT · power averaged over about one second. Relative digital levels, not calibrated dB SPL. Reference stays in this tab until cleared or reloaded. No audio recordings are transmitted.</small>
</article>
</main>
<footer>Environmental history: latest 30 seconds at 1 Hz; older data averaged into 30-second buckets, up to 600 seconds. The newest older bucket may be partial. Faint lines retain minima/maxima. Shorter histories fill the graph width; gaps indicate unavailable reports. Motion keeps its own one-second history. Altitude adjusts pressure in this tab; reload restores the configured altitude. No internet services required.</footer>
<script>
'use strict';
const history=[],motionHistory=[];
let lastMotionSequence=null;
const charts=[['tempChart','t','#8db4d6','°C'],['humChart','h','#86b8ad','%'],['pressureChart','p','#c9b789','hPa'],['motionChart','a','#8db4d6','m/s²']];
let lastSequence=null,lastUptime=null,lastSuccess=0;
let i2cSensorsEnabled=true;
let altitudeM=0,altitudeInitialized=false,currentReading=null;
const $=id=>document.getElementById(id);
// The ESP32 serves one HTTP client at a time. Keep periodic requests in one
// queue, including reading each body, rather than filling TCP slots in parallel.
let dashboardRequest=Promise.resolve();
function requestDashboardJson(url,timeoutMs=2000){
 const run=async()=>{
  const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),timeoutMs);
  try{const response=await fetch(url,{cache:'no-store',signal:controller.signal});if(!response.ok)throw new Error('HTTP '+response.status);return await response.json();}
  finally{clearTimeout(timeout);}
 };
 const result=dashboardRequest.then(run,run);
 dashboardRequest=result.catch(()=>{});return result;
}
/* Clear derived fields too, so a network/sensor error cannot leave plausible stale values. */
function clearValues(){currentReading=null;for(const id of ['temperature','humidity','pressure','dewPoint'])$(id).textContent='—';}
/* Chip temperature has its own validity, so a BME280 failure cannot hide a healthy MPU6050 value. */
function updateMpuTemperature(motion){
 const valid=motion?.temperature_ok&&Number.isFinite(motion.temperature_c);
 $('mpuTemperature').textContent=valid?motion.temperature_c.toFixed(2):'—';
 $('mpuTemperatureError').textContent=valid?'':(motion?.temperature_error||'MPU6050 temperature unavailable');
}
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
  const values=points.flatMap(p=>channelValues(p,key)); // Include extrema and both temperature sources in scaling.
  if(key==='t')for(const point of records)if(Number.isFinite(point.m))values.push(point.m);
  let lo=values.length?Math.min(...values):0,hi=values.length?Math.max(...values):1;
  const margin=Math.max((hi-lo)*.15,key==='p'?.5:.2);lo-=margin;hi+=margin;
  ctx.font='11px system-ui';ctx.fillStyle='#a5b1bf';ctx.fillText(unit,left,13);
  for(let i=0;i<=4;i++){
   const y=top+(bottom-top)*i/4;
   ctx.strokeStyle='#344253';ctx.beginPath();ctx.moveTo(left,y);ctx.lineTo(right,y);ctx.stroke();
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
   ctx.strokeStyle=key==='a'?['#8db4d6','#86b8ad','#c9b789'][index]:color;
   ctx.beginPath();let previous=null;
   for(const point of records){
    if(!point[key]||!Number.isFinite(point[key][index])){previous=null;continue;}
    const x=left+(point.at-start)/span*(right-left),y=bottom-(channelValues(point,key)[index]-lo)/(hi-lo)*(bottom-top);
    // Sequence continuity, not browser-poll timing, determines real gaps.
    if(!previous||(point.firstSequence??point.sequence)!==previous.sequence+1)ctx.moveTo(x,y);else ctx.lineTo(x,y);
    previous=point;
   }
   ctx.stroke();
  }
  ctx.globalAlpha=1;
  if(key==='t'){
   ctx.strokeStyle='#c9b789';ctx.lineWidth=2;ctx.beginPath();let previous=null;
   for(const point of records){
    if(!Number.isFinite(point.m)){previous=null;continue;}
    const x=left+(point.at-start)/span*(right-left),y=bottom-(point.m-lo)/(hi-lo)*(bottom-top);
    if(!previous||(point.firstSequence??point.sequence)!==previous.sequence+1)ctx.moveTo(x,y);else ctx.lineTo(x,y);
    previous=point;
   }
   ctx.stroke();
  }
  if(!values.length){ctx.fillStyle='#a5b1bf';ctx.fillText('Waiting for valid samples',left+6,(top+bottom)/2);}
 }
}
/* Fetch incremental reports and backfill missed polls, without overlapping HTTP requests. */
async function poll(){
 if(!i2cSensorsEnabled)return;
 const started=performance.now();
 try{
  const data=await requestDashboardJson('/api/readings?since='+(lastSequence??0));
  if(typeof data.sequence!=='number'||typeof data.uptime_ms!=='number')throw new Error('Invalid API response');
  i2cSensorsEnabled=data.i2c_enabled!==false;
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
  const cursor=lastSequence??0;
  for(const point of data.history){
   const coarse=point.resolution_ms===30000,first=point.first_sequence??point.sequence;
   const revision=point.revision??point.sequence;
   if(!coarse&&point.sequence<=cursor)continue;
   const existing=coarse?history.find(p=>p.resolution===30000&&p.firstSequence===first):null;
   if(existing&&existing.revision>=revision)continue;
   const channels=[point.temperature_c,point.humidity_pct,point.pressure_hpa];
   const reports=point.report_count??1;
   const ok=point.ok&&reports>0&&point.sample_count===reports*5&&channels.every(c=>Array.isArray(c)&&c.length===3&&c.every(Number.isFinite));
   // A revised coarse bucket replaces its previous version AND the fine points it summarizes.
   if(coarse)for(let i=history.length-1;i>=0;--i){
    const p=history[i];if(p.sequence>=first&&(p.firstSequence??p.sequence)<=point.sequence)history.splice(i,1);
   }
   // Translate device-relative age into the browser's monotonic clock domain.
   history.push({firstSequence:first,sequence:point.sequence,revision,resolution:coarse?30000:1000,at:now-Math.min(point.age_ms,600000),
                 t:ok?point.temperature_c:null,h:ok?point.humidity_pct:null,p:ok?point.pressure_hpa:null,
                 m:Number.isFinite(point.mpu_temperature_c)?point.mpu_temperature_c:null});
   lastSequence=Math.max(lastSequence??0,point.sequence);
  }
  history.sort((a,b)=>a.firstSequence-b.firstSequence);
  if(data.history_mode==='tiered_30s')lastSequence=Math.max(lastSequence??0,data.sequence);
  const valid=data.sensor_ok&&[data.temperature_c,data.humidity_pct,data.pressure_hpa].every(Number.isFinite);
  updateMpuTemperature(data.motion);
  $('connection').textContent=data.wifi_connected?'Connected to ESP32':'ESP32 Wi-Fi disconnected';
  $('error').textContent=valid?'':('Sensor: '+(data.error||'Waiting for a complete one-second window'));
  $('detail').textContent='Report #'+data.sequence+' · '+data.sample_count+'/5 readings · '+(data.sample_age_ms===null?'No report yet':data.sample_age_ms+' ms old')+' · Wi-Fi '+data.rssi_dbm+' dBm';
  if(!i2cSensorsEnabled){$('error').textContent='';$('detail').textContent='Microphone build · I²C sensors disabled · Wi-Fi '+data.rssi_dbm+' dBm';$('motionCalibrate').disabled=true;$('gyroCalibrate').disabled=true;}
  if(valid){
   currentReading=data;updateCurrent();
  }else clearValues();
 }catch(error){
  clearValues();$('connection').textContent='Disconnected / ESP32 unreachable';
  $('mpuTemperature').textContent='—';$('mpuTemperatureError').textContent='Temperature request failed';
  $('error').textContent='Request failed: '+error.message;
  $('detail').textContent=lastSuccess?'Last contact '+Math.round((performance.now()-lastSuccess)/1000)+' s ago':'Check the device IP and local Wi-Fi connection';
  // Preserve history: the next successful request backfills missed reports.
 }finally{
  // Schedule after completion so slow connections never accumulate pending fetches.
  draw();if(i2cSensorsEnabled)setTimeout(poll,Math.max(0,1000-(performance.now()-started)));
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
 if(!i2cSensorsEnabled)return;
 const started=performance.now();
 try{
  consumeMotion(await requestDashboardJson('/api/motion?since='+(lastMotionSequence??0)),performance.now());
 }catch(error){
  $('motionValues').textContent='—';$('gyroValues').textContent='—';$('motionError').textContent='Motion request failed: '+error.message;
 }finally{
  // Coalesce redraws to the publication rate; do not repaint environmental plots here.
  draw(true);if(i2cSensorsEnabled)setTimeout(pollMotion,Math.max(0,1000-(performance.now()-started)));
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
let spectrumLive=null,spectrumReference=null,spectrumReceivedAt=0;
const spectrumFresh=data=>data&&performance.now()-spectrumReceivedAt+data.age_ms<(data.stale_after_ms??2500);
function drawSpectrum(){
 const canvas=$('spectrumChart'),ratio=window.devicePixelRatio||1,w=canvas.clientWidth,h=240;
 canvas.width=Math.round(w*ratio);canvas.height=Math.round(h*ratio);
 const ctx=canvas.getContext('2d');ctx.setTransform(ratio,0,0,ratio,0,0);ctx.clearRect(0,0,w,h);
 const left=48,right=w-14,top=18,bottom=h-30,nyquist=(spectrumLive||spectrumReference)?.sample_rate_hz/2||8000;
 ctx.font='11px system-ui';ctx.fillStyle='#a5b1bf';
 for(let db=-120;db<=0;db+=30){const y=bottom-(db+120)/120*(bottom-top);ctx.strokeStyle='#344253';ctx.beginPath();ctx.moveTo(left,y);ctx.lineTo(right,y);ctx.stroke();ctx.fillText(String(db),3,y+4);}
 for(let i=0;i<=4;i++){const hz=nyquist*i/4;ctx.textAlign=i===0?'left':i===4?'right':'center';ctx.fillText(hz.toFixed(0),left+i/4*(right-left),h-8);}ctx.textAlign='left';
 for(const [data,color] of [[spectrumReference,'#c9b789'],[spectrumLive,'#8db4d6']]){
  if(!data)continue;ctx.strokeStyle=color;ctx.lineWidth=2;ctx.beginPath();
  data.bins_dbfs.forEach((db,k)=>{const x=left+k*data.bin_hz/nyquist*(right-left),y=bottom-(Math.max(-120,Math.min(0,db))+120)/120*(bottom-top);if(k===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);});ctx.stroke();
 }
 if(!spectrumLive){ctx.fillText('Waiting for fresh audio spectrum',left+5,top+20);}
}
function updateSpectrumComparison(){
 if(!spectrumReference){$('spectrumComparison').textContent='Play a steady broadband sound at a fixed volume and distance. Save an exposed reference, then place the microphone in the box without moving the sound source.';return;}
 if(!spectrumLive){$('spectrumComparison').textContent='Reference saved; waiting for fresh audio to compare.';return;}
 const delta=v=>(v>=0?'+':'')+v.toFixed(1)+' dB';
 $('spectrumComparison').textContent='Change from exposed reference: overall '+delta(spectrumLive.rms_dbfs-spectrumReference.rms_dbfs)+'; '+['low','mid','high'].map((name,i)=>name+' '+delta(spectrumLive.bands_dbfs[i]-spectrumReference.bands_dbfs[i])).join(' / ')+'. Negative values mean less received sound. This is a comparison, not a covered/uncovered decision.';
}
function clearLiveSpectrum(message){
 spectrumLive=null;$('spectrumStatus').textContent=message;$('spectrumValues').textContent='Audio measurements unavailable';$('spectrumBands').textContent='';$('spectrumSave').disabled=true;updateSpectrumComparison();drawSpectrum();
}
function applySpectrum(data){
 if(!data.ok){clearLiveSpectrum(data.error||'Microphone unavailable');return;}
 const n=data.fft_size;
 if(!Number.isInteger(n)||n<128||n>16384||(n&(n-1))!==0||!Number.isInteger(data.sample_rate_hz)||data.sample_rate_hz<8000||data.sample_rate_hz>48000||!Number.isFinite(data.bin_hz)||Math.abs(data.bin_hz-data.sample_rate_hz/n)>1e-6||data.bins_dbfs?.length!==n/2+1||!data.bins_dbfs.every(Number.isFinite)||data.bands_dbfs?.length!==3||!data.bands_dbfs.every(Number.isFinite)||!Number.isFinite(data.rms_dbfs)||!Number.isFinite(data.age_ms)||data.age_ms>=(data.stale_after_ms??2500))throw new Error('Invalid or stale spectrum');
 if(spectrumReference&&(spectrumReference.fft_size!==n||spectrumReference.sample_rate_hz!==data.sample_rate_hz)){spectrumReference=null;$('spectrumClear').disabled=true;}
 $('spectrumChart').ariaLabel='Microphone spectrum from zero to '+data.sample_rate_hz/2+' Hz in digital decibels';
 $('spectrumResolution').textContent=(data.sample_rate_hz/1000)+' kHz mono · '+n+'-point Hann FFT · '+(n/2+1)+' bins · '+data.bin_hz.toFixed(4)+' Hz spacing · '+(1000*n/data.sample_rate_hz).toFixed(0)+' ms frames · '+(data.average_target_ms??1000)+' ms power-averaging target. Relative digital levels, not calibrated dB SPL. Reference stays in this tab until cleared or reloaded. No audio recordings are transmitted.';
 if(data.i2c_enabled===false){i2cSensorsEnabled=false;$('connection').textContent='Connected to ESP32';$('error').textContent='';$('detail').textContent='Microphone build · I²C sensors disabled';$('motionCalibrate').disabled=true;$('gyroCalibrate').disabled=true;}
 spectrumLive=data;spectrumReceivedAt=performance.now();$('spectrumSave').disabled=false;
 $('spectrumStatus').textContent='Live audio spectrum · '+data.frames+' FFT frames averaged ('+(data.averaged_audio_ms??1000*data.frames*n/data.sample_rate_hz).toFixed(0)+' ms audio) · DMA overflows: '+data.dma_overflows+(data.clipped_samples?' · Warning: clipped samples: '+data.clipped_samples:'');
 $('spectrumValues').textContent='Signal RMS: '+data.rms_dbfs.toFixed(1)+' dBFS · dominant bin (above 80 Hz): '+(Number.isFinite(data.peak_hz)?data.peak_hz.toFixed(0)+' Hz':'unavailable');
 $('spectrumBands').textContent=['Low 80–500 Hz','Mid 500–2000 Hz','High 2000–'+data.sample_rate_hz/2+' Hz'].map((label,i)=>label+': '+data.bands_dbfs[i].toFixed(1)+' dBFS').join(' · ');
 updateSpectrumComparison();drawSpectrum();
}
async function pollSpectrum(){
 const started=performance.now();
 try{applySpectrum(await requestDashboardJson('/api/microphone',4000));}
 catch(error){
  // A failed request does not invalidate a still-fresh measurement. The normal
  // age watchdog continues to expire it, even while a retry is in progress.
  if(spectrumFresh(spectrumLive))$('spectrumStatus').textContent='Audio update delayed; retrying: '+error.message;
  else clearLiveSpectrum('Audio request failed: '+error.message);
 }
 finally{setTimeout(pollSpectrum,Math.max(0,1000-(performance.now()-started)));}
}
$('spectrumSave').addEventListener('click',()=>{
 if(!spectrumFresh(spectrumLive))return;
 spectrumReference={...spectrumLive,bins_dbfs:[...spectrumLive.bins_dbfs],bands_dbfs:[...spectrumLive.bands_dbfs]};$('spectrumClear').disabled=false;updateSpectrumComparison();drawSpectrum();
});
$('spectrumClear').addEventListener('click',()=>{spectrumReference=null;$('spectrumClear').disabled=true;updateSpectrumComparison();drawSpectrum();});
setInterval(()=>{if(spectrumLive&&!spectrumFresh(spectrumLive))clearLiveSpectrum('Audio spectrum stale');},500);
setTimeout(pollSpectrum,1000);
window.addEventListener('resize',()=>{draw();drawSpectrum();});
setInterval(draw,1000);poll();pollMotion();
</script>
</html>
)DASHBOARD";

