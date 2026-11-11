#include "web_assets.h"

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
select{font:inherit;padding:8px;border:1px solid #46566a;border-radius:8px;background:#11161d;color:#e4e9ef;margin:8px}input{font:inherit;padding:10px 12px;width:140px;border:1px solid #46566a;border-radius:9px;background:#11161d;color:#e4e9ef}input:focus-visible{outline:2px solid #9fc5e5;outline-offset:3px}input:disabled{opacity:.5}#altitudeError{color:#e0a0a6}.settings{display:flex;gap:14px;flex-wrap:wrap;align-items:center;margin:20px 0}.settings label{font-weight:600;font-size:.9rem}
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
<article><h2>Temperature</h2><div class="temperature-values"><div><small class="bme-label">BME280 · ambient</small><span class="value" id="temperature">—</span> <span class="unit">°C</span></div><div><small class="mpu-label">MPU6050 · chip</small><span class="value" id="mpuTemperature">—</span> <span class="unit">°C</span></div></div><small id="mpuTemperatureError" role="status"></small><canvas id="tempChart" role="img" aria-label="BME280 ambient and MPU6050 chip temperatures in degrees Celsius over up to 600 seconds"></canvas><label for="tempChartRange">Visible time </label><select id="tempChartRange"><option value="15">15 seconds</option><option value="30">30 seconds</option><option value="60">60 seconds</option><option value="120">120 seconds</option><option value="300">300 seconds</option><option value="600" selected>600 seconds</option></select><small id="tempChartTime"></small><small><span class="bme-label">Blue: BME280</span> · <span class="mpu-label">Amber: MPU6050 chip</span> · Local clock time (HH:mm:ss)</small></article>
<article><h2>Relative humidity</h2><span class="value" id="humidity">—</span> <span class="unit">%</span><canvas id="humChart" role="img" aria-label="Relative humidity in percent over up to 600 seconds"></canvas><label for="humChartRange">Visible time </label><select id="humChartRange"><option value="15">15 seconds</option><option value="30">30 seconds</option><option value="60">60 seconds</option><option value="120">120 seconds</option><option value="300">300 seconds</option><option value="600" selected>600 seconds</option></select><small id="humChartTime"></small><small>Local clock time (HH:mm:ss)</small></article>
<article><h2>Estimated sea-level pressure</h2><span class="value" id="pressure">—</span> <span class="unit">hPa</span><canvas id="pressureChart" role="img" aria-label="Sea-level pressure over up to 600 seconds"></canvas><label for="pressureChartRange">Visible time </label><select id="pressureChartRange"><option value="15">15 seconds</option><option value="30">30 seconds</option><option value="60">60 seconds</option><option value="120">120 seconds</option><option value="300">300 seconds</option><option value="600" selected>600 seconds</option></select><small id="pressureChartTime"></small><small>Local clock time (HH:mm:ss)</small></article>
<article><h2>MPU6050 acceleration</h2><p id="motionValues">Waiting for motion sensor</p><p><strong>Gyroscope</strong><br><span id="gyroValues">Waiting for motion sensor</span><br><small>One-second mean · saved zero-rate bias corrected · deg/s</small></p><button id="gyroCalibrate" type="button">Calibrate gyro (keep stationary)</button><p id="motionError" role="status"></p><button id="motionCalibrate" type="button" style="padding:10px 14px;border:1px solid #46566a;border-radius:9px;background:#253344;color:#e4e9ef;font:inherit;cursor:pointer">Calibrate flat (Z up)</button><p id="motionCalibrationStatus" role="status">Calibration removes fixed bias; gravity is retained.</p><small><span style="color:#8db4d6">X</span> · <span style="color:#86b8ad">Y</span> · <span style="color:#c9b789">Z</span> · m/s², includes gravity · one-second means · 100 Hz acquisition target</small><canvas id="motionChart" role="img" aria-label="MPU6050 X Y Z acceleration in meters per second squared"></canvas><label for="motionChartRange">Visible time </label><select id="motionChartRange"><option value="15">15 seconds</option><option value="30">30 seconds</option><option value="60">60 seconds</option><option value="120">120 seconds</option><option value="300">300 seconds</option><option value="600" selected>600 seconds</option></select><small id="motionChartTime"></small><small>Local clock time (HH:mm:ss)</small></article>
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
<footer>Plots accumulate in this browser tab, up to 600 seconds. Refresh starts fresh. Labels use the browser local clock, estimated from initial request timing. Range selection preserves retained data. The ESP32 keeps only small unacknowledged delivery buffers. Faint lines retain minima/maxima; missing reports appear as gaps. Altitude adjusts pressure in this tab; reload restores the configured altitude. No internet services required.</footer>
<script>
'use strict';

// Fixed browser wall-clock mapping per device boot; later network jitter cannot move points.
const pageWallOrigin=Date.now()-performance.now(),deviceClocks=new Map();
function syncDeviceClock(data,received=performance.now()){
 if(!Number.isInteger(data.uptime_ms)||data.uptime_ms<0||data.uptime_ms>0xFFFFFFFF)throw new Error('Invalid device time');
 const boot=data.boot_id??0,uptime=data.uptime_ms>>>0;
 let clock=deviceClocks.get(boot);
 if(!clock){
  const timing=data._requestTiming;
  const midpoint=timing?(timing.sent+timing.received)/2:received;
  clock={uptime,epoch:pageWallOrigin+midpoint};deviceClocks.set(boot,clock);
  if(deviceClocks.size>4)deviceClocks.delete(deviceClocks.keys().next().value);
 }else{
  const advance=(uptime-clock.uptime)|0;
  if(advance>0){clock.epoch+=advance;clock.uptime=uptime;}
 }
 return {epoch:sample=>clock.epoch+((sample-clock.uptime)|0)};
}
function formatClock(epoch,fractional=false){
 if(!Number.isFinite(epoch))return '—';
 const date=new Date(epoch),text=date.toLocaleTimeString([],{hour:'2-digit',minute:'2-digit',second:'2-digit',hour12:false});
 return text+(fractional?'.'+String(date.getMilliseconds()).padStart(3,'0'):'');
}
function formatPlotDate(epoch){return new Date(epoch).toLocaleDateString()+' · '+Intl.DateTimeFormat().resolvedOptions().timeZone;}
function selectedSeconds(id,fallback=600){
 const seconds=Number($(id).value);return [15,30,60,120,300,600].includes(seconds)?seconds:fallback;
}

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
  try{const sent=performance.now();const response=await fetch(url,{cache:'no-store',signal:controller.signal});if(!response.ok)throw new Error('HTTP '+response.status);const data=await response.json();Object.defineProperty(data,'_requestTiming',{value:{sent,received:performance.now()}});return data;}
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
function timeRange(now,records=history,seconds=600){
 // Anchor to newest report so sparse one-second reports fill the full width.
 const end=records.length?records[records.length-1].at:now;
 const start=records.length>1?Math.max(end-seconds*1000,records[0].at):end-5000;
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
  const retained=key==='a'?motionHistory:history;
  const {start,end,span}=timeRange(now,retained,selectedSeconds(id+'Range'));
  const records=retained.filter(point=>point.at>=start&&point.at<=end);
  $(id+'Time').textContent=' · '+formatPlotDate(pageWallOrigin+start)+' – '+formatPlotDate(pageWallOrigin+end);
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
   const x=left+fraction*(right-left),epoch=pageWallOrigin+start+fraction*span;
   ctx.textAlign=fraction===1?'right':fraction===0?'left':'center';ctx.fillText(formatClock(epoch,span<10000),x,h-8);
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

const deliveryClient=(Math.floor(Math.random()*0xFFFFFFFF)>>>0)||1;
let environmentBoot=0,motionBoot=0;
function deliveryUrl(path,boot,ack){return path+'?client='+deliveryClient+'&boot='+boot+'&ack='+(ack??0);}
/* Fetch incremental reports and backfill missed polls, without overlapping HTTP requests. */
async function poll(){
 if(!i2cSensorsEnabled)return;
 const started=performance.now();
 try{
  const data=await requestDashboardJson(deliveryUrl('/api/readings',environmentBoot,lastSequence));
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
  if(lastUptime!==null && ((environmentBoot&&data.boot_id!==environmentBoot)||(!data.boot_id&&data.uptime_ms<lastUptime)||data.sequence<lastSequence)){history.length=0;lastSequence=null;}
  const clock=syncDeviceClock(data,now);
  lastUptime=data.uptime_ms;
  environmentBoot=data.boot_id??0;
  if(!Array.isArray(data.history))throw new Error('Missing aggregate history; refresh after firmware update');
  // Validate the complete package before advancing the ACK cursor.
  const incoming=[];
  for(const point of data.history){
   if(!Number.isInteger(point.sequence)||point.sequence<1||!Number.isFinite(point.age_ms)||point.age_ms<0||(point.sample_ms!==undefined&&(!Number.isInteger(point.sample_ms)||point.sample_ms<0||point.sample_ms>0xFFFFFFFF)))throw new Error('Invalid report record');
   if(point.sequence<=(lastSequence??0))continue;
   const channels=[point.temperature_c,point.humidity_pct,point.pressure_hpa];
   if(!channels.every(c=>Array.isArray(c)&&c.length===3&&c.every(v=>v===null||Number.isFinite(v))))throw new Error('Invalid report channels');
   const ok=point.ok&&point.sample_count===5&&channels.every(c=>c.every(Number.isFinite));
   incoming.push({firstSequence:point.sequence,sequence:point.sequence,resolution:1000,
    epoch_ms:clock.epoch((point.sample_ms??(data.uptime_ms-point.age_ms))>>>0),
    at:clock.epoch((point.sample_ms??(data.uptime_ms-point.age_ms))>>>0)-pageWallOrigin,t:ok?point.temperature_c:null,h:ok?point.humidity_pct:null,
    p:ok?point.pressure_hpa:null,m:Number.isFinite(point.mpu_temperature_c)?point.mpu_temperature_c:null});
  }
  for(let i=1;i<incoming.length;i++)if(incoming[i].sequence<=incoming[i-1].sequence)throw new Error('Unordered reports');
  for(const point of incoming){history.push(point);lastSequence=point.sequence;}
  if(history.length>720)history.splice(0,history.length-720);
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
 if(lastMotionUptime!==null&&((motionBoot&&data.boot_id!==motionBoot)||(!data.boot_id&&data.uptime_ms<lastMotionUptime)||motion.sequence<lastMotionSequence)){
  motionHistory.length=0;lastMotionSequence=null;
 }
 const clock=syncDeviceClock(data,now);
 lastMotionUptime=data.uptime_ms;
 motionBoot=data.boot_id??0;
 updatePickupStatus(data.pickup);
 $('motionCalibrate').disabled=!!motion.calibrating;$('gyroCalibrate').disabled=!!motion.calibrating;
 $('motionCalibrationStatus').textContent=motion.calibrating?'Calibrating '+(motion.calibration_kind||'accel')+': '+motion.calibration_samples+'/500 samples. Keep still.':motion.calibration_error||'Calibration removes fixed bias; gravity is retained.';
 const gyroValues=motion.gyroscope_deg_s??motion.gyroscope_rad_s?.map(v=>v*180/Math.PI);
 const gyroOK=motion.ok&&Array.isArray(gyroValues)&&gyroValues.length===3&&gyroValues.every(Number.isFinite);
 $('gyroValues').textContent=gyroOK?gyroValues.map((v,i)=>['X','Y','Z'][i]+': '+v.toFixed(3)).join(' · ')+' deg/s':'—';
 const ok=motion.ok&&Array.isArray(motion.acceleration_m_s2)&&motion.acceleration_m_s2.length===3&&motion.acceleration_m_s2.every(Number.isFinite);
 $('motionError').textContent=ok?(motion.warning||''):(motion.error||'MPU6050 unavailable');
 $('motionValues').textContent=ok?motion.acceleration_m_s2.map((v,i)=>['X','Y','Z'][i]+': '+v.toFixed(3)).join(' · ')+' m/s² · n='+(motion.sample_count??'?')+' · FIFO overflows: '+(motion.fifo_overflows??0):'—';
 const incoming=[];
 for(const point of data.history||[]){
  if(!Number.isInteger(point.sequence)||point.sequence<1||!Number.isFinite(point.age_ms)||point.age_ms<0||(point.sample_ms!==undefined&&(!Number.isInteger(point.sample_ms)||point.sample_ms<0||point.sample_ms>0xFFFFFFFF))||!Array.isArray(point.a)||point.a.length!==3||!point.a.every(v=>v===null||Number.isFinite(v)))throw new Error('Invalid motion record');
  if(point.sequence<=(lastMotionSequence??0))continue;
  const valid=point.ok&&point.a.every(Number.isFinite);
  incoming.push({sequence:point.sequence,epoch_ms:clock.epoch((point.sample_ms??(data.uptime_ms-point.age_ms))>>>0),at:clock.epoch((point.sample_ms??(data.uptime_ms-point.age_ms))>>>0)-pageWallOrigin,a:valid?point.a:null});
 }
 for(let i=1;i<incoming.length;i++)if(incoming[i].sequence<=incoming[i-1].sequence)throw new Error('Unordered motion reports');
 for(const point of incoming){motionHistory.push(point);lastMotionSequence=point.sequence;}
 if(motionHistory.length>720)motionHistory.splice(0,motionHistory.length-720);

}
/* Use an independent 1 Hz request stream, with bounded backfill and no overlapping fetches. */
async function pollMotion(){
 if(!i2cSensorsEnabled)return;
 const started=performance.now();
 try{
  consumeMotion(await requestDashboardJson(deliveryUrl('/api/motion',motionBoot,lastMotionSequence)),performance.now());
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
for(const [id] of charts)$(id+'Range').addEventListener('change',()=>draw());
setInterval(draw,1000);poll();pollMotion();
</script>
</html>
)DASHBOARD";

const char PICKUP_TUNING_HTML[] PROGMEM = R"TUNING(
<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pickup detector · Tuning lab</title>
<style>
:root{color-scheme:dark;font:15px system-ui,sans-serif;background:#11161d;color:#e4e9ef}*{box-sizing:border-box}body{max-width:1120px;margin:auto;padding:26px 20px}a{color:#8db4d6}h1{font-size:2rem;letter-spacing:-.03em;margin:10px 0}h2{font-size:1rem;margin:0 0 8px}p,small{color:#a5b1bf;line-height:1.6}.panel,article,details{background:#19212b;border:1px solid #354253;border-radius:15px;padding:18px;margin:16px 0}.active{background:#30222b;border-color:#bd7f89}summary{cursor:pointer;font-weight:700}button,input,select{font:inherit;padding:9px 12px;border:1px solid #46566a;border-radius:8px;background:#11161d;color:#e4e9ef}button{cursor:pointer}button:disabled{opacity:.5;cursor:wait}.controls{display:flex;flex-wrap:wrap;gap:10px;align-items:center}.settings{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:14px;margin:18px 0}label{display:flex;flex-direction:column;gap:6px;font-size:.88rem}input{width:100%}canvas{width:100%;height:220px;display:block}.legend{display:flex;gap:16px;flex-wrap:wrap;font-size:.82rem;margin:10px 0}.legend span{display:inline-flex;gap:6px;align-items:center}.swatch{width:18px;height:3px;display:inline-block}#error,#saveStatus{color:#e0a0a6}.nav{display:flex;flex-wrap:wrap;gap:16px;margin-top:18px}.legend em{font-style:normal;color:#94a3b4}button:focus-visible,input:focus-visible,select:focus-visible{outline:2px solid #8db4d6;outline-offset:2px}footer{font-size:.8rem;color:#94a3b4;margin:24px 0}
</style>
<a href="/">← Environmental dashboard</a><h1>Pickup detector · Tuning lab</h1>
<p>Watch the signals the detector actually compares. Solid lines are measurements; dashed lines are thresholds/settings. Telemetry is sampled at 20 Hz and fetched in batches at 5 Hz. Short state changes are captured immediately.</p>
<div id="status" class="panel" role="status" aria-live="polite">Connecting…</div><div id="error" role="status"></div>
<details open><summary>Persistent detection settings</summary><p>Save applies validated settings and restarts baseline learning. Active alerts remain until stillness is confirmed. Save preserves hidden parameters; Restore defaults resets all parameters. Changes are saved on the ESP32.</p><form id="settingsForm"><div id="fields" class="settings"></div><div class="controls"><button id="save" type="submit" disabled>Save settings</button><button id="defaults" type="button" disabled>Restore defaults</button><button id="reload" type="button">Reload saved settings</button></div><p id="saveStatus" role="status">Loading settings…</p></form></details>
<div class="controls"><label>History window<select id="window"><option value="15">15 seconds</option><option value="30">30 seconds</option><option value="60" selected>60 seconds</option><option value="120">120 seconds</option><option value="300">300 seconds</option><option value="600">600 seconds</option></select></label><button id="pause">Pause capture</button><button id="clear">Clear plots</button><button id="export">Export CSV</button><span id="captureInfo"></span></div>
<nav class="nav"><a href="#movement">Movement triggers</a><a href="#quiet-gyro">Stillness / clearing</a><a href="#persistence">Trigger persistence</a></nav>
<div id="charts"></div>
<div class="panel"><h2>Recent detector events</h2><div id="eventLog">Waiting for events.</div></div>
<footer>Plots use estimated local clock timestamps, not an oscilloscope trace. CSV includes UTC ISO timestamps and device uptime. Nulls and missing records appear as gaps. RMS is unavailable when rotation rejects the quiet window before RMS evaluation; use instantaneous rotation to see why. While paused/closed, telemetry capture stops after a short lease. Historical thresholds are preserved when settings change. Pickup classification and LED ownership are unchanged.</footer>
<script>
'use strict';

// Fixed browser wall-clock mapping per device boot; later network jitter cannot move points.
const pageWallOrigin=Date.now()-performance.now(),deviceClocks=new Map();
function syncDeviceClock(data,received=performance.now()){
 if(!Number.isInteger(data.uptime_ms)||data.uptime_ms<0||data.uptime_ms>0xFFFFFFFF)throw new Error('Invalid device time');
 const boot=data.boot_id??0,uptime=data.uptime_ms>>>0;
 let clock=deviceClocks.get(boot);
 if(!clock){
  const timing=data._requestTiming;
  const midpoint=timing?(timing.sent+timing.received)/2:received;
  clock={uptime,epoch:pageWallOrigin+midpoint};deviceClocks.set(boot,clock);
  if(deviceClocks.size>4)deviceClocks.delete(deviceClocks.keys().next().value);
 }else{
  const advance=(uptime-clock.uptime)|0;
  if(advance>0){clock.epoch+=advance;clock.uptime=uptime;}
 }
 return {epoch:sample=>clock.epoch+((sample-clock.uptime)|0)};
}
function formatClock(epoch,fractional=false){
 if(!Number.isFinite(epoch))return '—';
 const date=new Date(epoch),text=date.toLocaleTimeString([],{hour:'2-digit',minute:'2-digit',second:'2-digit',hour12:false});
 return text+(fractional?'.'+String(date.getMilliseconds()).padStart(3,'0'):'');
}
function formatPlotDate(epoch){return new Date(epoch).toLocaleDateString()+' · '+Intl.DateTimeFormat().resolvedOptions().timeZone;}
function selectedSeconds(id,fallback=600){
 const seconds=Number($(id).value);return [15,30,60,120,300,600].includes(seconds)?seconds:fallback;
}

const $=id=>document.getElementById(id);
const colors=['#8db4d6','#86b8ad','#c9b789','#79b5a3','#a6a2c9'];
// Each trace maps to an actual firmware measurement or a threshold snapshot in the same row.
const plotSpecs=[
 {id:'movement',title:'Acceleration departure',unit:'m/s²',traces:[['Deviation','accel_deviation',0],['Fixed trigger','accel_trigger',1,1],['Noise × multiplier','adaptive_accel',3,1],['Effective trigger (max)','effective_accel',2,1]]},
 {id:'tilt',title:'Tilt from armed baseline',unit:'degrees',traces:[['Tilt','tilt_deg',0],['Trigger','tilt_limit',2,1]]},
 {id:'rotation',title:'Rotation trigger',unit:'deg/s',traces:[['Filtered gyro magnitude','gyro_deg_s',0],['Trigger','gyro_trigger_deg_s',2,1]]},
 {id:'quiet-gyro',title:'Stationary rotation',unit:'deg/s',traces:[['Instantaneous magnitude','instant_gyro_deg_s',0],['Window RMS','gyro_rms_deg_s',1],['Stationary limit','stationary_gyro_deg_s',2,1]]},
 {id:'quiet-time',title:'Continuous quiet evidence / automatic clearing',unit:'ms',traces:[['Quiet window accumulated','quiet_ms',0],['Required stationary duration','stationary_ms',2,1]]},
 {id:'persistence',title:'Trigger persistence',unit:'ms',traces:[['Continuous trigger evidence','evidence_ms',0],['Required duration','trigger_ms',2,1]]}
];
// Hidden fields remain in the validated settings payload, preserving saved detector configuration.
const hiddenSettings=new Set(['led_duty_pct','led_period_ms','max_gap_ms','smoothing_ms','stationary_accel_rms','noise_multiplier']);
const gyroUnitMap={gyro_rad_s:'gyro_deg_s',gyro_trigger:'gyro_trigger_deg_s',instant_gyro:'instant_gyro_deg_s',gyro_rms:'gyro_rms_deg_s',stationary_gyro:'stationary_gyro_deg_s'};
const triggerNames=['acceleration departure','tilt','filtered rotation'];
const quietNames=['acceleration RMS','instantaneous rotation','gyro RMS'];
/* Decode firmware decisions, rather than guessing causes from decimated graph lines. */
function maskNames(mask,names){return names.filter((_,i)=>(mask||0)&(1<<i)).join(', ');}
/* Preserve exact confirming and quiet-rejection frames; gaps never imply a clear event. */
function annotate(point,previous){
 point.markers=[];
 const consecutive=previous&&point.sequence===previous.sequence+1;
 if(consecutive&&point.events>previous.events)point.markers.push({kind:'trigger',color:'#df8a96',label:'Alert confirmed: '+maskNames(point.confirmed_trigger_mask,triggerNames),mask:point.confirmed_trigger_mask});
 if(consecutive&&previous.active&&!point.active&&point.valid&&point.armed&&point.settings_revision===previous.settings_revision)point.markers.push({kind:'clear',color:'#79b5a3',label:'Alert cleared: full stationary window'});
 if(point.valid&&point.quiet_block_mask&&(!consecutive||point.quiet_block_mask!==previous.quiet_block_mask||point.active!==previous.active))point.markers.push({kind:'block',color:'#c9b789',label:'Quiet timer reset: '+maskNames(point.quiet_block_mask,quietNames),mask:point.quiet_block_mask});
}
/* Each plot identifies its decision role; unrelated timing/LED settings never trigger pickup. */
function decisionNote(id){
 if(['movement','noise','tilt','rotation','persistence'].includes(id))return 'TRIGGER: any movement limit must remain exceeded continuously for the trigger duration. Red = alert confirmed; the event list names the confirming cause.';
 if(['stillness','quiet-gyro','quiet-time'].includes(id))return 'CLEARING: all stationary checks must pass for the full quiet duration. Amber = quiet timer reset; green = alert cleared. Old tilt/departure do not block clearing.';
 return 'DIAGNOSTIC: these settings do not directly trigger pickup. Red = confirmed alert; green = cleared. Missing/late sensor data restarts quiet learning and preserves an active alert.';
}
/* Highlight only blocker plots that participated, while alert/clear times align across all plots. */
function markerApplies(marker,id){
 if(marker.kind!=='block')return true;
 return id==='quiet-time'||id==='stillness'&&(marker.mask&1)||id==='quiet-gyro'&&(marker.mask&6);
}
/* A confirmation line is synchronized everywhere; only contributing trigger plots say CAUSE. */
function markerLabel(marker,id){
 if(marker.kind==='clear')return 'CLEAR';if(marker.kind==='block')return 'RESET';
 const bit={movement:1,noise:1,tilt:2,rotation:4}[id];
 return bit?(marker.mask&bit?'CAUSE':'ALERT*'):'ALERT';
}
let rows=[],columns=[],lastSequence=0,lastUptime=null,paused=false,fields=[],dirty=false,settingsRevision=null,lost=0,lastContact=0;
/* Escape all metadata before constructing labels; values remain numeric and server validated. */
function esc(value){return String(value).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
/* Build one self-contained canvas per threshold family, with a labeled trace legend. */
function buildCharts(){
 $('charts').innerHTML=plotSpecs.map(p=>'<article id="'+p.id+'"><h2>'+esc(p.title)+'</h2>'+(p.note?'<small>'+esc(p.note)+'</small>':'')+'<div class="legend" id="legend-'+p.id+'"></div><canvas id="plot-'+p.id+'" role="img" aria-label="'+esc(p.title)+' in '+esc(p.unit)+'"></canvas></article>').join('');
 for(const p of plotSpecs){const note=document.createElement('p');note.textContent=decisionNote(p.id);$('legend-'+p.id).before?.(note);}
}
/* Snapshot settings from the ESP32; graph thresholds never use unsubmitted form edits. */
function renderSettings(data){
 if(!Array.isArray(data.fields)||!data.fields.length)throw new Error('Invalid settings response');
 for(const f of data.fields)if(!/^[a-z_]+$/.test(f.key)||![f.value,f.min,f.max,f.step].every(Number.isFinite))throw new Error('Invalid setting metadata');
 fields=data.fields;
 settingsRevision=data.settings_revision??settingsRevision;
 $('fields').innerHTML=fields.filter(f=>!hiddenSettings.has(f.key)).map(f=>'<label for="setting-'+f.key+'">'+esc(f.label)+' ('+esc(f.display_unit||f.unit)+')<input id="setting-'+f.key+'" name="'+f.key+'" type="number" min="'+f.min*(f.display_scale||1)+'" max="'+f.max*(f.display_scale||1)+'" step="'+((f.display_scale||1)===1?f.step:'any')+'" required><small>Default '+esc(f.default*(f.display_scale||1))+'</small></label>').join('');
 for(const f of fields)if(!hiddenSettings.has(f.key))$('setting-'+f.key).value=String(f.value*(f.display_scale||1));
 dirty=false;$('save').disabled=false;$('defaults').disabled=false;
 $('saveStatus').textContent=data.persisted?'Saved settings loaded.':'Code defaults loaded; Save persists settings.';
}
/* Make failures and storage rejection explicit, without discarding unsaved form values. */
async function loadSettings(options={cache:'no-store'},path='settings'){
 const response=await fetch('/api/pickup/'+path,options),data=await response.json();
 if(!response.ok)throw new Error(data.error||'HTTP '+response.status);renderSettings(data);
}
$('settingsForm').addEventListener('input',()=>{dirty=true;$('saveStatus').textContent='Unsaved edits — plots still use saved, active thresholds.';});
/* Submit the complete form so validation/storage is atomic on the device. */
$('settingsForm').addEventListener('submit',async event=>{
 event.preventDefault();$('save').disabled=true;$('defaults').disabled=true;
 try{
  const body=new URLSearchParams();
  for(const f of fields){
   if(hiddenSettings.has(f.key)){body.set(f.key,String(f.value));continue;}
   const text=$('setting-'+f.key).value.trim(),v=Number(text)/(f.display_scale||1);if(!text||!Number.isFinite(v)||v<f.min||v>f.max)throw new Error('Check '+f.label);body.set(f.key,String(v));
  }
  await loadSettings({method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body.toString()});
  $('saveStatus').textContent='Saved on ESP32. Baseline learning restarted; an active alert is retained until stillness.';
 }catch(error){$('saveStatus').textContent='Not saved: '+error.message;}
 finally{$('save').disabled=false;$('defaults').disabled=false;}
});
/* Restore defaults only on explicit confirmation; reloading never silently overwrites dirty inputs. */
$('defaults').addEventListener('click',async()=>{
 if(!window.confirm('Restore and save default settings?'))return;
 try{await loadSettings({method:'POST'},'defaults');}catch(error){$('saveStatus').textContent=error.message;}
});
$('reload').addEventListener('click',()=>{if(!dirty||window.confirm('Discard unsaved edits?'))loadSettings().catch(e=>{$('saveStatus').textContent=e.message;});});
/* Decode a versioned named-column stream; retain nulls for unavailable signals. */
const debugClient=(Math.floor(Math.random()*0xFFFFFFFF)>>>0)||1;
let debugBoot=0;
function ingest(data){
 if(!Array.isArray(data.columns)||!Array.isArray(data.rows)||!Number.isFinite(data.uptime_ms))throw new Error('Invalid debug stream');
 if(data.columns[0]!=='sequence'||data.columns[1]!=='uptime_ms'||new Set(data.columns).size!==data.columns.length)throw new Error('Invalid debug schema');
 if(lastUptime!==null&&((debugBoot&&data.boot_id!==debugBoot)||(!data.boot_id&&data.uptime_ms<lastUptime))){rows=[];lastSequence=0;lost=0;}
 const clock=syncDeviceClock(data);
 debugBoot=data.boot_id??0;
 lastUptime=data.uptime_ms;columns=data.columns.map(k=>gyroUnitMap[k]||k);
 for(const packed of data.rows){
  if(!Array.isArray(packed)||packed.length!==columns.length||!Number.isInteger(packed[0])||packed[0]<1||!Number.isInteger(packed[1])||packed[1]<0||packed[1]>0xFFFFFFFF||!packed.every(v=>v===null||Number.isFinite(v)))throw new Error('Invalid telemetry package');
 }
 for(let i=1;i<data.rows.length;i++)if(data.rows[i][0]<=data.rows[i-1][0])throw new Error('Unordered telemetry');
 for(const packed of data.rows){
  if(!Array.isArray(packed)||packed.length!==columns.length)throw new Error('Invalid telemetry row');
  const point=Object.fromEntries(columns.map((key,i)=>[key,Number.isFinite(packed[i])?packed[i]*(gyroUnitMap[data.columns[i]]?180/Math.PI:1):null]));
  if(point.sequence<=lastSequence)continue;
  if(lastSequence&&point.sequence>lastSequence+1)lost+=point.sequence-lastSequence-1;
  point.noise_ratio=point.baseline_noise>0&&point.accel_deviation!==null?point.accel_deviation/point.baseline_noise:null;
  point.required_ratio=point.baseline_noise>0&&point.effective_accel!==null?point.effective_accel/point.baseline_noise:null;
  point.epoch_ms=clock.epoch(point.uptime_ms>>>0);point.timestamp_iso=new Date(point.epoch_ms).toISOString();
  annotate(point,rows.at(-1));rows.push(point);lastSequence=point.sequence;
 }
 if(rows.length>20000)rows.splice(0,rows.length-20000);
 if(settingsRevision!==null&&data.settings_revision!==settingsRevision)$('saveStatus').textContent=dirty?'Active settings changed. Unsaved form retained; reload to compare.':'Active settings changed. Reload saved settings to update the form.';
 settingsRevision=data.settings_revision;
 const p=data.pickup;
 if(p){$('status').className='panel'+(p.active?' active':'');$('status').textContent=(p.active?'Picked up — alert active':p.state==='armed'?'Stationary · armed':'Detector: '+p.state)+' · quiet '+p.quiet_ms+' / '+p.stationary_ms+' ms · events '+p.events+(p.error?' · '+p.error:'');}
 const latest=rows.at(-1);
 if(latest){
  const reason=!latest.valid?'Sensor data unavailable: quiet evidence cannot accumulate.':latest.quiet_block_mask?'Quiet timer blocked by '+maskNames(latest.quiet_block_mask,quietNames)+'.':latest.active?'Waiting for the full stationary window.':latest.armed?'Armed. Current trigger-limit exceedances: '+(maskNames(latest.trigger_mask,triggerNames)||'none')+'.':'Learning stationary baseline.';
  $('status').textContent+=' · '+reason;
 }
 $('eventLog').innerHTML=rows.flatMap(r=>(r.markers||[]).map(m=>'<div><strong style="color:'+m.color+'">'+formatClock(r.epoch_ms,true)+'</strong> · '+esc(m.label)+'</div>')).slice(-12).reverse().join('')||'No captured events. Times use the browser local clock; missing records are not inferred.';
 $('captureInfo').textContent=rows.length+' points · '+lost+' missing records · '+(rows.length?formatPlotDate(rows.at(-1).epoch_ms):'Local clock time');
}
/* Draw without invented smoothing. Missing sequence/time intervals and nulls break lines. */
function draw(){
 const seconds=selectedSeconds('window',60);
 const end=rows.length?rows[rows.length-1].epoch_ms:0,start=rows.length?Math.max(end-seconds*1000,rows[0].epoch_ms):0,span=Math.max(50,end-start);
 rows=rows.filter(p=>p.epoch_ms>=end-600000);
 const visible=rows.filter(p=>p.epoch_ms>=start),latest=visible.at(-1);
 for(const p of plotSpecs){
  const canvas=$('plot-'+p.id),w=canvas.clientWidth||600,h=220,ratio=window.devicePixelRatio||1;
  if(canvas.width!==Math.round(w*ratio)||canvas.height!==Math.round(h*ratio)){canvas.width=Math.round(w*ratio);canvas.height=Math.round(h*ratio);}
  const c=canvas.getContext('2d');c.setTransform(ratio,0,0,ratio,0,0);c.clearRect(0,0,w,h);
  const left=70,right=w-15,top=24,bottom=h-32;
  const values=visible.flatMap(row=>p.traces.map(t=>row[t[1]])).filter(Number.isFinite);
  let lo=values.length?(p.id==='timing'?Math.min(...values):Math.min(0,...values)):0,hi=values.length?Math.max(...values):1;
  const pad=Math.max((hi-lo)*.08,.01);lo-=pad;hi+=pad;
  c.font='11px system-ui';c.fillStyle='#a5b1bf';c.textAlign='left';c.fillText(p.unit,left,13);
  for(let i=0;i<=4;i++){const y=top+(bottom-top)*i/4;c.strokeStyle='#344253';c.setLineDash([]);c.beginPath();c.moveTo(left,y);c.lineTo(right,y);c.stroke();c.fillText((hi-(hi-lo)*i/4).toFixed(hi>100?0:3),2,y+4);}
  for(const f of [0,.5,1]){c.textAlign=f===0?'left':f===1?'right':'center';c.fillText(formatClock(rows.length?start+f*span:pageWallOrigin,span<=30000),left+f*(right-left),h-8);}
  for(const t of p.traces){
   c.strokeStyle=colors[t[2]];c.lineWidth=t[3]?1.5:2;c.setLineDash(t[3]?[6,4]:[]);c.beginPath();let previous=null;
   for(const row of visible){
    const value=row[t[1]];if(!Number.isFinite(value)){previous=null;continue;}
    const x=left+(row.epoch_ms-start)/span*(right-left),y=bottom-(value-lo)/(hi-lo)*(bottom-top);
    if(!previous||row.sequence!==previous.sequence+1||row.epoch_ms-previous.epoch_ms>250)c.moveTo(x,y);else c.lineTo(x,y);previous=row;
   }c.stroke();
  }
  c.setLineDash([]);
  // Vertical event markers share timestamps across plots; small labels distinguish confirmation from clearing.
  for(const row of visible)for(const marker of row.markers||[]){
   if(!markerApplies(marker,p.id))continue;
   const x=left+(row.epoch_ms-start)/span*(right-left);
   c.strokeStyle=marker.color;c.lineWidth=1;c.globalAlpha=.7;c.setLineDash([3,3]);
   c.beginPath();c.moveTo(x,top);c.lineTo(x,bottom);c.stroke();c.setLineDash([]);c.globalAlpha=1;
   c.fillStyle=marker.color;c.textAlign='left';c.fillText(markerLabel(marker,p.id),Math.min(x+3,right-42),top+11);
  }
  canvas.title='Red CAUSE: this signal exceeded its limit at confirmation. Red ALERT*: another signal caused confirmation. Amber RESET: quiet evidence restarted. Green CLEAR: full quiet window completed. See recent events for exact causes and local clock timestamps.';
  $('legend-'+p.id).innerHTML=p.traces.map(t=>'<span><i class="swatch" style="background:'+colors[t[2]]+'"></i>'+esc(t[0])+': <em>'+((latest&&Number.isFinite(latest[t[1]]))?latest[t[1]].toFixed(3):'unavailable')+'</em></span>').join('');
  if(!values.length){c.fillStyle='#a5b1bf';c.textAlign='left';c.fillText('Waiting for applicable telemetry',left+8,(top+bottom)/2);}
 }
}
/* Poll in bounded batches, never overlapping requests. Pause capture releases the firmware lease. */
async function poll(){
 const started=performance.now();
 if(!paused){
  const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),2000);
  try{const sent=performance.now();const response=await fetch('/api/pickup/debug?client='+debugClient+'&boot='+debugBoot+'&ack='+lastSequence,{cache:'no-store',signal:controller.signal});if(!response.ok)throw new Error('HTTP '+response.status);const data=await response.json();data._requestTiming={sent,received:performance.now()};ingest(data);lastContact=performance.now();$('error').textContent='';draw();}
  catch(error){$('error').textContent='Telemetry unavailable: '+error.message+(lastContact?' · last contact '+Math.round((performance.now()-lastContact)/1000)+' s ago':'');}
  finally{clearTimeout(timeout);}
 }
 setTimeout(poll,Math.max(0,200-(performance.now()-started)));
}
$('pause').addEventListener('click',()=>{paused=!paused;$('pause').textContent=paused?'Resume capture':'Pause capture';});
$('clear').addEventListener('click',()=>{rows=[];lost=0;draw();});
$('window').addEventListener('change',draw);window.addEventListener('resize',draw);
/* Export numeric samples plus their historical thresholds for offline threshold analysis. */
$('export').addEventListener('click',()=>{
 if(!rows.length)return;
 const header=['timestamp_iso','epoch_ms',...columns,'noise_ratio','required_ratio'];
 const csv=[header.join(','),...rows.map(p=>header.map(k=>p[k]??'').join(','))].join('\n');
 const url=URL.createObjectURL(new Blob([csv],{type:'text/csv;charset=utf-8'})),a=document.createElement('a');a.href=url;a.download='pickup-debug.csv';a.click();URL.revokeObjectURL(url);
});
buildCharts();loadSettings().catch(e=>{$('saveStatus').textContent='Settings unavailable: '+e.message;});poll();
</script></html>
)TUNING";

// nRF Connect resolves custom UUID names locally; HTTP serves this import file.
const char PICKUP_UUID_DEFINITIONS[] PROGMEM = R"NAMES({
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
