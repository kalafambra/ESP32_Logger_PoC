// Run with Node.js: node tests/dashboard.test.cjs
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '..', 'dashboard.h'), 'utf8');
const script = source.match(/<script>([\s\S]*?)<\/script>/)[1];
new vm.Script(script);
const elements = new Map(), strokes = [], paths = [];
let commands = [];
const drawing = new Proxy({globalAlpha: 1}, {get(target,key) {
  if(key==='beginPath')return ()=>{commands=[];};
  if(key==='moveTo')return ()=>commands.push('M');
  if(key==='lineTo')return ()=>commands.push('L');
  if(key==='stroke')return ()=>{strokes.push(target.globalAlpha);paths.push({alpha:target.globalAlpha,commands:commands.join('')});};
  return key in target?target[key]:()=>{};
}});
const document = {getElementById(id) {
  if(!elements.has(id))elements.set(id,{textContent:'',value:'',disabled:true,addEventListener(event,fn){this[event]=fn;},clientWidth:320,width:0,height:0,getContext:()=>drawing});
  return elements.get(id);
}};
let now=10000,fail=false,requested='';
const scheduled=[];
let failSettings=false;
const settingsPosts=[];
const pickupConfig={persisted:false,fields:[
 {key:'stationary_ms',label:'Stationary duration',unit:'ms',min:500,max:10000,step:10,value:3000,default:3000},
 {key:'accel_trigger',label:'Acceleration trigger',unit:'m/s²',min:0.05,max:10,step:0.01,value:0.6,default:0.6},
 {key:'stationary_gyro',label:'Stationary rotation',unit:'rad/s',display_unit:'deg/s',display_scale:180/Math.PI,min:0.005,max:0.3,step:0.005,value:0.05,default:0.05}
]};
const point=(sequence,age_ms=10,ok=true)=>({sequence,age_ms,ok,sample_count:5,
  mpu_temperature_c:31.5,
  temperature_c:ok?[23,22,24]:[null,null,null],humidity_pct:ok?[45,44,46]:[null,null,null],pressure_hpa:ok?[1000,999,1001]:[null,null,null]});
let response={sequence:1,default_altitude_m:0,uptime_ms:5000,sample_age_ms:10,sample_count:5,
  sensor_ok:true,wifi_connected:true,rssi_dbm:-50,temperature_c:23,humidity_pct:45,pressure_hpa:1000,error:'',history:[point(1)],
  motion:{sequence:1,sample_age_ms:20,ok:true,error:'',temperature_ok:true,temperature_c:31.5,temperature_error:'',acceleration_m_s2:[0.1,-0.2,9.81],gyroscope_rad_s:[0.01,-0.02,0.003]}};
const context=vm.createContext({document,window:{devicePixelRatio:1,addEventListener(){},confirm(){return true;}},
  performance:{now:()=>now},AbortController,URLSearchParams,setTimeout(fn,ms){scheduled.push(ms);return 1;},clearTimeout(){},setInterval(){},
    fetch:async (url,options)=>{
    if(url.startsWith('/api/pickup')){
      if(options?.method==='POST'){
        settingsPosts.push({url,body:options.body});
        if(failSettings)return {ok:false,json:async()=>({error:'Storage unavailable'})};
        if(url.endsWith('/defaults'))for(const f of pickupConfig.fields)f.value=f.default;
        else {const values=new URLSearchParams(options.body);for(const f of pickupConfig.fields)f.value=Number(values.get(f.key));}
        pickupConfig.persisted=true;
      }
      return {ok:true,json:async()=>pickupConfig};
    }
    if(fail)throw new Error('Offline');
    if(url.startsWith('/api/motion'))return {ok:true,json:async()=>({uptime_ms:response.uptime_ms,motion:response.motion,
      history:[{sequence:response.motion.sequence,age_ms:response.motion.sample_age_ms,ok:response.motion.ok,a:response.motion.acceleration_m_s2}]})};
    requested=url;return {ok:true,json:async()=>({...response})};
  }
});
const read=expression=>vm.runInContext(expression,context);
async function main(){
  vm.runInContext(script,context);await new Promise(resolve=>setImmediate(resolve));
  assert.equal(elements.get('temperature').textContent,'23.00');
  assert.equal(elements.get('mpuTemperature').textContent,'31.50','MPU chip reading has a separate current value');
  assert.equal(read('history[0].m'),31.5,'MPU temperature shares the environmental graph timeline');
  assert.equal(elements.get('pickup-stationary_ms').value,'3000','runtime metadata populates settings');
  assert.ok(Math.abs(Number(elements.get('pickup-stationary_gyro').value)-0.05*180/Math.PI)<1e-8,'gyro settings displayed in degrees');
  elements.get('pickup-stationary_ms').value='4000';
  await elements.get('pickupSettingsForm').submit({preventDefault(){}});
  assert.match(settingsPosts.at(-1).body,/stationary_ms=4000/);
  assert.ok(Math.abs(Number(new URLSearchParams(settingsPosts.at(-1).body).get('stationary_gyro'))-0.05)<1e-8,'gyro form posts internal radians');
  assert.equal(elements.get('pickup-stationary_ms').value,'4000');
  const posts=settingsPosts.length;
  elements.get('pickup-stationary_ms').value='';
  await elements.get('pickupSettingsForm').submit({preventDefault(){}});
  assert.equal(settingsPosts.length,posts,'invalid input does not POST');
  assert.match(elements.get('pickupSettingsStatus').textContent,/Not saved/);
  elements.get('pickup-stationary_ms').value='4000';failSettings=true;
  await elements.get('pickupSettingsForm').submit({preventDefault(){}});
  assert.match(elements.get('pickupSettingsStatus').textContent,/Storage unavailable/);
  failSettings=false;await elements.get('pickupDefaults').click();
  assert.equal(elements.get('pickup-stationary_ms').value,'3000','defaults restored from server');
  read('updatePickupStatus({active:true,state:"picked_up",stationary_ms:3000,led_available:true})');
  assert.match(elements.get('pickupStatus').textContent,/has been picked up/);
  read('updatePickupStatus({active:false,state:"armed",led_available:true})');
  assert.match(elements.get('pickupStatus').textContent,/Stationary/);
  read('updatePickupStatus({active:true,state:"sensor_unavailable",error:"I2C failed",led_available:true})');
  assert.match(elements.get('pickupStatus').textContent,/picked up.*I2C failed/);
  assert.equal(read('history.length'),1);
  assert.equal(read('motionHistory.length'),1);
  assert.match(elements.get('motionValues').textContent,/Z: 9.810/);
  assert.match(elements.get('gyroValues').textContent,/Y: -1.146/);
  assert.match(elements.get('gyroValues').textContent,/deg\/s/);
  assert.equal(read('motionHistory[0].a[1]'),-0.2,'signed acceleration preserved');
  assert.ok(scheduled.filter(ms=>ms===1000).length>=2,'both polling streams scheduled at 1 Hz');
  assert.ok(!scheduled.includes(100),'no remaining 10 Hz polling');
  assert.ok(scheduled.includes(1000),'environmental polling remains 1 Hz');
  assert.equal(read('history[0].t[1]'),22);
  assert.ok(strokes.includes(0.3),'extrema drawn at 30% opacity');
  assert.ok(strokes.includes(1),'mean drawn at full opacity');
  await read('poll()');assert.equal(read('history.length'),1,'deduplicated');
  assert.equal(read('motionHistory.length'),1,'motion sequence deduplicated');
  assert.match(requested,/since=1$/);
  fail=true;await read('poll()');assert.match(elements.get('connection').textContent,/unreachable/);
  assert.equal(elements.get('mpuTemperature').textContent,'\u2014','network failure clears the chip reading');
  assert.equal(read('history.length'),1,'network error does not create false sensor gaps');
  fail=false;now+=16000; // Includes >1.25 s timing jitter relative to the first point.
  response={...response,sequence:4,uptime_ms:20000,history:[point(2,10010),point(3,5010),point(4)]};
  await read('poll()');
  assert.equal(read('history.map(p=>p.sequence).join(",")'),'1,2,3,4','missed reports backfilled in order');
  assert.ok(paths.some(p=>p.alpha===1&&p.commands==='MLLL'),'consecutive mean reports stay connected despite poll jitter');
  assert.ok(paths.some(p=>p.alpha===0.3&&p.commands==='MLLL'),'extrema stay connected at 30% opacity');
  response={...response,sequence:5,uptime_ms:25000,sensor_ok:false,error:'Incomplete window',temperature_c:null,history:[point(5,0,false)]};
  await read('poll()');assert.match(elements.get('error').textContent,/Incomplete/);
  assert.equal(read('history[4].t'),null,'invalid one-second window creates a real gap');
  assert.equal(read('history[4].m'),31.5,'BME failure does not discard valid MPU temperature history');
  assert.equal(elements.get('mpuTemperature').textContent,'31.50','BME failure does not clear the chip reading');
  assert.match(elements.get('motionValues').textContent,/9.810/,'BME error does not hide valid motion');
  response.motion={sequence:2,sample_age_ms:0,ok:false,error:'MPU6050 I2C read failed',acceleration_m_s2:[null,null,null]};
  await read('pollMotion()');
  assert.match(elements.get('motionError').textContent,/I2C read failed/);
  assert.equal(elements.get('gyroValues').textContent,'\u2014','gyro values clear on sensor error');
  assert.equal(read('motionHistory[1].a'),null,'failed MPU sample creates graph gap');
  const environmentalCount=read('history.length');
  const burst={uptime_ms:26000,motion:{sequence:12,ok:true,sample_count:100,fifo_overflows:2,warning:'No INT edges',acceleration_m_s2:[1,-2,9]},
    history:Array.from({length:10},(_,i)=>({sequence:i+3,age_ms:(9-i)*1000,ok:true,a:[i,-i,9]}))};
  read('consumeMotion('+JSON.stringify(burst)+',performance.now())');
  assert.equal(read('lastMotionSequence'),12,'batch backfill consumes all ten reports');
  assert.equal(read('motionHistory.length'),12,'missed one-second reports backfilled');
  assert.match(elements.get('motionValues').textContent,/n=100/);
  assert.match(elements.get('motionValues').textContent,/overflows: 2/);
  assert.match(elements.get('motionError').textContent,/No INT edges/);
  read('consumeMotion('+JSON.stringify(burst)+',performance.now())');
  assert.equal(read('motionHistory.length'),12,'batch backfill remains deduplicated');
  assert.equal(read('history.length'),environmentalCount,'motion updates do not alter BME history');
  read('consumeMotion('+JSON.stringify({...burst,motion:{...burst.motion,gyroscope_deg_s:[1,-2,3]}})+',performance.now())');
  assert.match(elements.get('gyroValues').textContent,/X: 1.000/,'degree API values are not converted twice');
  const calibrating={...burst,motion:{...burst.motion,calibrating:true,calibration_samples:250}};
  read('consumeMotion('+JSON.stringify(calibrating)+',performance.now())');
  assert.equal(elements.get('motionCalibrate').disabled,true);
  assert.match(elements.get('motionCalibrationStatus').textContent,/250\/500/);
  read('consumeMotion('+JSON.stringify({...burst,motion:{...burst.motion,calibrating:false,calibration_error:'Module moved'}})+',performance.now())');
  assert.equal(elements.get('motionCalibrate').disabled,false);
  assert.match(elements.get('motionCalibrationStatus').textContent,/Module moved/);
  assert.equal(elements.get('temperature').textContent,'\u2014');
  response={...response,sequence:1,uptime_ms:100,sensor_ok:true,error:'',temperature_c:24,history:[point(1)]};
  await read('poll()');assert.equal(read('history.length'),1,'restart clears history');
  assert.ok(Math.abs(read('dewPointC(20,50)')-9.27)<0.1,'known dew point');
  assert.equal(read('dewPointC(20,100)'),20,'saturated air');
  assert.equal(read('dewPointC(20,0)'),null,'zero RH unavailable');
  assert.equal(read('seaLevelPressure(1000,0)'),1000,'zero altitude');
  assert.ok(read('seaLevelPressure(1000,100)')>1000,'positive altitude raises sea-level pressure');
  const original=read('history[0].p[0]');
  elements.get('altitude').value='100';elements.get('altitude').input();
  assert.ok(Number(elements.get('pressure').textContent)>1000);
  assert.equal(read('history[0].p[0]'),original,'raw historical pressure is retained');
  elements.get('altitude').value='';elements.get('altitude').input();
  assert.equal(read('altitudeM'),100,'blank altitude rejected');
  elements.get('altitude').value='10000';elements.get('altitude').input();
  assert.equal(read('altitudeM'),100,'out of range altitude rejected');
  read('history.length=0;history.push({at:1000},{at:11000})');
  assert.equal(read('timeRange(11000).span'),10000,'short history fills width');
  read('history[1].at=701000');assert.equal(read('timeRange(701000).span'),600000,'600 second cap');
  read('history.length=0;history.push({at:performance.now(),sequence:1,t:[1,1,1],h:[1,1,1],p:[1000,1000,1000]})');
  now+=61000;read('draw()');assert.equal(read('history.length'),1,'history retained beyond 60 s');
  now+=540000;read('draw()');assert.equal(read('history.length'),0,'600-second expiry');
  // Coarse revisions must replace fine rows even when their source sequence is behind the live cursor.
  read('history.length=0;lastSequence=null;lastUptime=null');now+=30000;
  response={...response,sequence:30,uptime_ms:30000,history_mode:'tiered_30s',
    history:Array.from({length:30},(_,i)=>({...point(i+1,(29-i)*1000),first_sequence:i+1,revision:i+1,report_count:1,resolution_ms:1000}))};
  await read('poll()');assert.equal(read('history.length'),30);
  now+=1000;
  const bucket=(last,reports,revision,ok=true)=>({...point(last,30000,ok),first_sequence:1,revision,report_count:reports,sample_count:reports*5,resolution_ms:30000});
  response={...response,sequence:31,uptime_ms:31000,history:[bucket(1,1,31),point(31,0)]};
  await read('poll()');assert.equal(read('history.filter(p=>p.resolution===1000).length'),30);
  assert.equal(read('history[0].resolution'),30000);assert.equal(read('lastSequence'),31);
  await read('poll()');assert.equal(read('history.length'),31,'repeated coarse revisions deduplicated');
  now+=1000;response={...response,sequence:32,uptime_ms:32000,history:[bucket(2,2,32),point(32,0)]};
  await read('poll()');assert.equal(read('history[0].sequence'),2);
  assert.equal(read('history.at(-1).sequence'),32,'coarse revision does not suppress the simultaneous live report');
  assert.equal(read('history[0].t[0]'),23,'valid multi-report bucket accepted');
  now+=28000;response={...response,sequence:60,uptime_ms:60000,
    history:[bucket(30,30,60),...Array.from({length:28},(_,i)=>point(i+33,(27-i)*1000))]};
  await read('poll()');assert.equal(read('history.length'),31);
  assert.equal(read('history.filter(p=>p.resolution===1000&&p.sequence<=30).length'),0,'coarse bucket replaces every summarized fine row');
  assert.ok(paths.some(p=>p.alpha===1&&p.commands==='M'+'L'.repeat(30)),'mixed-resolution ranges draw a continuous line');
  response={...response,sequence:61,uptime_ms:61000,history:[{...bucket(30,30,61,false)},point(61,0)]};
  await read('poll()');assert.equal(read('history[0].t'),null,'invalid constituent bucket produces a real graph gap');
  response={...response,sequence:1,uptime_ms:100,history:[point(1,0)]};
  await read('poll()');assert.equal(read('history.length'),1,'restart clears coarse and fine histories together');
  response={...response,sequence:2,uptime_ms:1100,temperature_c:24,sensor_ok:true,
    motion:{...response.motion,temperature_ok:false,temperature_c:null,temperature_error:'Temperature I2C failed'},
    history:[{...point(2,0),mpu_temperature_c:null}]};
  await read('poll()');
  assert.equal(elements.get('temperature').textContent,'24.00','MPU temperature failure leaves BME reading available');
  assert.equal(elements.get('mpuTemperature').textContent,'\u2014');
  assert.match(elements.get('mpuTemperatureError').textContent,/I2C failed/);
  assert.equal(read('history.at(-1).m'),null,'MPU temperature error makes a gap, not a zero');
  assert.ok(read('history.at(-1).t!==null'));
  console.log('PASS: dashboard graphs, calibration, pickup settings/status, validation, backfill and retention');
}
main().catch(error=>{console.error(error);process.exitCode=1;});

