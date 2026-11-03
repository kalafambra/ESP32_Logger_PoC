#pragma once
#include <Arduino.h>

const char PICKUP_TUNING_HTML[] PROGMEM=R"TUNING(
<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pickup detector · Tuning lab</title>
<style>
:root{color-scheme:dark;font:15px system-ui,sans-serif;background:#21151d;color:#fff2e5}*{box-sizing:border-box}body{max-width:1120px;margin:auto;padding:26px 20px}a{color:#ffb47f}h1{font-size:2rem;letter-spacing:-.03em;margin:10px 0}h2{font-size:1rem;margin:0 0 8px}p,small{color:#d6b9b8;line-height:1.6}.panel,article,details{background:#30212a;border:1px solid #69414a;border-radius:15px;padding:18px;margin:16px 0}.active{background:#6b263c;border-color:#ffb47f}summary{cursor:pointer;font-weight:700}button,input,select{font:inherit;padding:9px 12px;border:1px solid #94606a;border-radius:8px;background:#21151d;color:#fff2e5}button{cursor:pointer}button:disabled{opacity:.5;cursor:wait}.controls{display:flex;flex-wrap:wrap;gap:10px;align-items:center}.settings{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:14px;margin:18px 0}label{display:flex;flex-direction:column;gap:6px;font-size:.88rem}input{width:100%}canvas{width:100%;height:220px;display:block}.legend{display:flex;gap:16px;flex-wrap:wrap;font-size:.82rem;margin:10px 0}.legend span{display:inline-flex;gap:6px;align-items:center}.swatch{width:18px;height:3px;display:inline-block}#error,#saveStatus{color:#ffbdad}.nav{display:flex;flex-wrap:wrap;gap:16px;margin-top:18px}.legend em{font-style:normal;color:#cbaaaa}button:focus-visible,input:focus-visible,select:focus-visible{outline:2px solid #ffb47f;outline-offset:2px}footer{font-size:.8rem;color:#cbaaaa;margin:24px 0}
</style>
<a href="/">← Environmental dashboard</a><h1>Pickup detector · Tuning lab</h1>
<p>Watch the signals the detector actually compares. Solid lines are measurements; dashed lines are thresholds/settings. Telemetry is sampled at 20 Hz and fetched in batches at 5 Hz. Short state changes are captured immediately.</p>
<div id="status" class="panel" role="status" aria-live="polite">Connecting…</div><div id="error" role="status"></div>
<details open><summary>Persistent detection settings</summary><p>Save applies validated settings and restarts baseline learning. Active alerts remain until stillness is confirmed. Defaults and runtime edits are saved on the ESP32.</p><form id="settingsForm"><div id="fields" class="settings"></div><div class="controls"><button id="save" type="submit" disabled>Save settings</button><button id="defaults" type="button" disabled>Restore defaults</button><button id="reload" type="button">Reload saved settings</button></div><p id="saveStatus" role="status">Loading settings…</p></form></details>
<div class="controls"><label>History window<select id="window"><option value="15">15 seconds</option><option value="30">30 seconds</option><option value="60" selected>60 seconds</option><option value="120">120 seconds</option><option value="600">600 seconds</option></select></label><button id="pause">Pause capture</button><button id="clear">Clear plots</button><button id="export">Export CSV</button><span id="captureInfo"></span></div>
<nav class="nav"><a href="#movement">Movement triggers</a><a href="#stillness">Stillness / clearing</a><a href="#timing">Filtering / timing</a><a href="#led">LED</a></nav>
<div id="charts"></div>
<div class="panel"><h2>Recent detector events</h2><div id="eventLog">Waiting for events.</div></div>
<footer>Plots show processing-time samples, not an oscilloscope trace. Nulls and missing records appear as gaps. RMS is unavailable when rotation rejects the quiet window before RMS evaluation; use instantaneous rotation to see why. LED traces describe commanded output observed by firmware, not an electrical measurement. While paused/closed, telemetry capture stops after a short lease. Historical thresholds are preserved when settings change. Pickup classification and LED ownership are unchanged.</footer>
<script>
'use strict';
const $=id=>document.getElementById(id);
const colors=['#ffb47f','#e7b5ca','#e9d899','#8bd0c4','#c2b2ef'];
// Each trace maps to an actual firmware measurement or a threshold snapshot in the same row.
const plotSpecs=[
 {id:'movement',title:'Acceleration departure',unit:'m/s²',traces:[['Deviation','accel_deviation',0],['Fixed trigger','accel_trigger',1,1],['Noise × multiplier','adaptive_accel',3,1],['Effective trigger (max)','effective_accel',2,1]]},
 {id:'noise',title:'Adaptive noise threshold',unit:'× baseline RMS',note:'The fixed acceleration floor can dominate the noise multiplier. Zero/unknown baseline noise is unavailable.',traces:[['Deviation / noise','noise_ratio',0],['Noise multiplier','noise_multiplier',1,1],['Effective required ratio','required_ratio',2,1]]},
 {id:'tilt',title:'Tilt from armed baseline',unit:'degrees',traces:[['Tilt','tilt_deg',0],['Trigger','tilt_limit',2,1]]},
 {id:'rotation',title:'Rotation trigger',unit:'deg/s',traces:[['Filtered gyro magnitude','gyro_deg_s',0],['Trigger','gyro_trigger_deg_s',2,1]]},
 {id:'stillness',title:'Stationary acceleration variation',unit:'m/s² RMS',traces:[['Window RMS','accel_rms',0],['Stationary limit','stationary_accel_rms',2,1]]},
 {id:'quiet-gyro',title:'Stationary rotation',unit:'deg/s',traces:[['Instantaneous magnitude','instant_gyro_deg_s',0],['Window RMS','gyro_rms_deg_s',1],['Stationary limit','stationary_gyro_deg_s',2,1]]},
 {id:'quiet-time',title:'Continuous quiet evidence / automatic clearing',unit:'ms',traces:[['Quiet window accumulated','quiet_ms',0],['Required stationary duration','stationary_ms',2,1]]},
 {id:'persistence',title:'Trigger persistence',unit:'ms',traces:[['Continuous trigger evidence','evidence_ms',0],['Required duration','trigger_ms',2,1]]},
 {id:'timing',title:'Filter response',unit:'m/s²',note:'Raw vs low-pass magnitude. Filter time constant is shown in the next timing plot.',traces:[['Raw magnitude','raw_accel',1],['Filtered magnitude','filtered_accel',0],['Resting reference','rest_accel',2,1]]},
 {id:'gap',title:'Timing budget and filter time constant',unit:'ms',traces:[['Peak delivery gap since capture','delivery_gap_ms',0],['Sample age','sample_age_ms',1],['Maximum allowed gap','max_gap_ms',2,1],['Filter time constant','smoothing_ms',3,1]]},
 {id:'led',title:'Detector and LED state',unit:'0 / 1',traces:[['Pickup active','active',0],['Armed','armed',3],['LED ON command','led_on',2]]},
 {id:'led-period',title:'LED blink period',unit:'ms',note:'Observed timing is available only after a complete blink cycle while alerting.',traces:[['Observed command period','observed_led_period_ms',0],['Configured period','led_period_ms',2,1]]},
 {id:'led-duty',title:'LED duty cycle',unit:'%',traces:[['Observed command duty','observed_led_duty_pct',0],['Configured duty','led_duty_pct',2,1]]}
];
const gyroUnitMap={gyro_rad_s:'gyro_deg_s',gyro_trigger:'gyro_trigger_deg_s',instant_gyro:'instant_gyro_deg_s',gyro_rms:'gyro_rms_deg_s',stationary_gyro:'stationary_gyro_deg_s'};
const triggerNames=['acceleration departure','tilt','filtered rotation'];
const quietNames=['acceleration RMS','instantaneous rotation','gyro RMS'];
/* Decode firmware decisions, rather than guessing causes from decimated graph lines. */
function maskNames(mask,names){return names.filter((_,i)=>(mask||0)&(1<<i)).join(', ');}
/* Preserve exact confirming and quiet-rejection frames; gaps never imply a clear event. */
function annotate(point,previous){
 point.markers=[];
 const consecutive=previous&&point.sequence===previous.sequence+1;
 if(consecutive&&point.events>previous.events)point.markers.push({kind:'trigger',color:'#ff657d',label:'Alert confirmed: '+maskNames(point.confirmed_trigger_mask,triggerNames),mask:point.confirmed_trigger_mask});
 if(consecutive&&previous.active&&!point.active&&point.valid&&point.armed&&point.settings_revision===previous.settings_revision)point.markers.push({kind:'clear',color:'#8bd0c4',label:'Alert cleared: full stationary window'});
 if(point.valid&&point.quiet_block_mask&&(!consecutive||point.quiet_block_mask!==previous.quiet_block_mask||point.active!==previous.active))point.markers.push({kind:'block',color:'#e9d899',label:'Quiet timer reset: '+maskNames(point.quiet_block_mask,quietNames),mask:point.quiet_block_mask});
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
 $('fields').innerHTML=fields.map(f=>'<label for="setting-'+f.key+'">'+esc(f.label)+' ('+esc(f.display_unit||f.unit)+')<input id="setting-'+f.key+'" name="'+f.key+'" type="number" min="'+f.min*(f.display_scale||1)+'" max="'+f.max*(f.display_scale||1)+'" step="'+((f.display_scale||1)===1?f.step:'any')+'" required><small>Default '+esc(f.default*(f.display_scale||1))+'</small></label>').join('');
 for(const f of fields)$('setting-'+f.key).value=String(f.value*(f.display_scale||1));
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
  for(const f of fields){const text=$('setting-'+f.key).value.trim(),v=Number(text)/(f.display_scale||1);if(!text||!Number.isFinite(v)||v<f.min||v>f.max)throw new Error('Check '+f.label);body.set(f.key,String(v));}
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
function ingest(data){
 if(!Array.isArray(data.columns)||!Array.isArray(data.rows)||!Number.isFinite(data.uptime_ms))throw new Error('Invalid debug stream');
 if(lastUptime!==null&&data.uptime_ms<lastUptime){rows=[];lastSequence=0;lost=0;}
 lastUptime=data.uptime_ms;columns=data.columns.map(k=>gyroUnitMap[k]||k);
 for(const packed of data.rows){
  if(!Array.isArray(packed)||packed.length!==columns.length)throw new Error('Invalid telemetry row');
  const point=Object.fromEntries(columns.map((key,i)=>[key,Number.isFinite(packed[i])?packed[i]*(gyroUnitMap[data.columns[i]]?180/Math.PI:1):null]));
  if(point.sequence<=lastSequence)continue;
  if(lastSequence&&point.sequence>lastSequence+1)lost+=point.sequence-lastSequence-1;
  point.noise_ratio=point.baseline_noise>0&&point.accel_deviation!==null?point.accel_deviation/point.baseline_noise:null;
  point.required_ratio=point.baseline_noise>0&&point.effective_accel!==null?point.effective_accel/point.baseline_noise:null;
  annotate(point,rows.at(-1));rows.push(point);lastSequence=point.sequence;
 }
 if(settingsRevision!==null&&data.settings_revision!==settingsRevision)$('saveStatus').textContent=dirty?'Active settings changed. Unsaved form retained; reload to compare.':'Active settings changed. Reload saved settings to update the form.';
 settingsRevision=data.settings_revision;
 const p=data.pickup;
 if(p){$('status').className='panel'+(p.active?' active':'');$('status').textContent=(p.active?'Picked up — alert active':p.state==='armed'?'Stationary · armed':'Detector: '+p.state)+' · quiet '+p.quiet_ms+' / '+p.stationary_ms+' ms · events '+p.events+(p.error?' · '+p.error:'');}
 const latest=rows.at(-1);
 if(latest){
  const reason=!latest.valid?'Sensor data unavailable: quiet evidence cannot accumulate.':latest.quiet_block_mask?'Quiet timer blocked by '+maskNames(latest.quiet_block_mask,quietNames)+'.':latest.active?'Waiting for the full stationary window.':latest.armed?'Armed. Current trigger-limit exceedances: '+(maskNames(latest.trigger_mask,triggerNames)||'none')+'.':'Learning stationary baseline.';
  $('status').textContent+=' · '+reason;
 }
 $('eventLog').innerHTML=rows.flatMap(r=>(r.markers||[]).map(m=>'<div><strong style="color:'+m.color+'">'+(r.uptime_ms/1000).toFixed(2)+' s</strong> · '+esc(m.label)+'</div>')).slice(-12).reverse().join('')||'No captured events. Times are seconds since boot; missing records are not inferred.';
 $('captureInfo').textContent=rows.length+' points · '+lost+' missing records';
}
/* Draw without invented smoothing. Missing sequence/time intervals and nulls break lines. */
function draw(){
 const seconds=Number($('window').value)||60;
 const end=rows.length?rows[rows.length-1].uptime_ms:0,start=rows.length?Math.max(end-seconds*1000,rows[0].uptime_ms):0,span=Math.max(50,end-start);
 rows=rows.filter(p=>p.uptime_ms>=end-600000);
 const visible=rows.filter(p=>p.uptime_ms>=start),latest=visible.at(-1);
 for(const p of plotSpecs){
  const canvas=$('plot-'+p.id),w=canvas.clientWidth||600,h=220,ratio=window.devicePixelRatio||1;
  if(canvas.width!==Math.round(w*ratio)||canvas.height!==Math.round(h*ratio)){canvas.width=Math.round(w*ratio);canvas.height=Math.round(h*ratio);}
  const c=canvas.getContext('2d');c.setTransform(ratio,0,0,ratio,0,0);c.clearRect(0,0,w,h);
  const left=70,right=w-15,top=24,bottom=h-32;
  const values=visible.flatMap(row=>p.traces.map(t=>row[t[1]])).filter(Number.isFinite);
  let lo=values.length?(p.id==='timing'?Math.min(...values):Math.min(0,...values)):0,hi=values.length?Math.max(...values):1;
  const pad=Math.max((hi-lo)*.08,.01);lo-=pad;hi+=pad;
  c.font='11px system-ui';c.fillStyle='#d6b9b8';c.textAlign='left';c.fillText(p.unit,left,13);
  for(let i=0;i<=4;i++){const y=top+(bottom-top)*i/4;c.strokeStyle='#62424d';c.setLineDash([]);c.beginPath();c.moveTo(left,y);c.lineTo(right,y);c.stroke();c.fillText((hi-(hi-lo)*i/4).toFixed(hi>100?0:3),2,y+4);}
  for(const f of [0,.5,1]){c.textAlign=f===0?'left':f===1?'right':'center';c.fillText(((start+f*span-end)/1000).toFixed(1),left+f*(right-left),h-8);}
  for(const t of p.traces){
   c.strokeStyle=colors[t[2]];c.lineWidth=t[3]?1.5:2;c.setLineDash(t[3]?[6,4]:[]);c.beginPath();let previous=null;
   for(const row of visible){
    const value=row[t[1]];if(!Number.isFinite(value)){previous=null;continue;}
    const x=left+(row.uptime_ms-start)/span*(right-left),y=bottom-(value-lo)/(hi-lo)*(bottom-top);
    if(!previous||row.sequence!==previous.sequence+1||row.uptime_ms-previous.uptime_ms>250)c.moveTo(x,y);else c.lineTo(x,y);previous=row;
   }c.stroke();
  }
  c.setLineDash([]);
  // Vertical event markers share timestamps across plots; small labels distinguish confirmation from clearing.
  for(const row of visible)for(const marker of row.markers||[]){
   if(!markerApplies(marker,p.id))continue;
   const x=left+(row.uptime_ms-start)/span*(right-left);
   c.strokeStyle=marker.color;c.lineWidth=1;c.globalAlpha=.7;c.setLineDash([3,3]);
   c.beginPath();c.moveTo(x,top);c.lineTo(x,bottom);c.stroke();c.setLineDash([]);c.globalAlpha=1;
   c.fillStyle=marker.color;c.textAlign='left';c.fillText(markerLabel(marker,p.id),Math.min(x+3,right-42),top+11);
  }
  canvas.title='Red CAUSE: this signal exceeded its limit at confirmation. Red ALERT*: another signal caused confirmation. Amber RESET: quiet evidence restarted. Green CLEAR: full quiet window completed. See recent events for exact causes and boot-relative timestamps.';
  $('legend-'+p.id).innerHTML=p.traces.map(t=>'<span><i class="swatch" style="background:'+colors[t[2]]+'"></i>'+esc(t[0])+': <em>'+((latest&&Number.isFinite(latest[t[1]]))?latest[t[1]].toFixed(3):'unavailable')+'</em></span>').join('');
  if(!values.length){c.fillStyle='#d6b9b8';c.textAlign='left';c.fillText('Waiting for applicable telemetry',left+8,(top+bottom)/2);}
 }
}
/* Poll in bounded batches, never overlapping requests. Pause capture releases the firmware lease. */
async function poll(){
 const started=performance.now();
 if(!paused){
  const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),2000);
  try{const response=await fetch('/api/pickup/debug?since='+lastSequence,{cache:'no-store',signal:controller.signal});if(!response.ok)throw new Error('HTTP '+response.status);ingest(await response.json());lastContact=performance.now();$('error').textContent='';draw();}
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
 const header=[...columns,'noise_ratio','required_ratio'];
 const csv=[header.join(','),...rows.map(p=>header.map(k=>p[k]??'').join(','))].join('\n');
 const url=URL.createObjectURL(new Blob([csv],{type:'text/csv;charset=utf-8'})),a=document.createElement('a');a.href=url;a.download='pickup-debug.csv';a.click();URL.revokeObjectURL(url);
});
buildCharts();loadSettings().catch(e=>{$('saveStatus').textContent='Settings unavailable: '+e.message;});poll();
</script></html>
)TUNING";
