// Run with Node.js: node tests/pickup-tuning.test.cjs
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const script=fs.readFileSync('web_assets.cpp','utf8').split('R"TUNING(')[1].match(/<script>([\s\S]*?)<\/script>/)[1];
new vm.Script(script);
const elements=new Map(),commands=[],timers=[],requests=[];
const ctx=new Proxy({}, {get:(_,key)=>(...args)=>{if(key==='setLineDash')commands.push(args[0]);}});
const document={getElementById(id){if(!elements.has(id))elements.set(id,{value:id==='window'?'60':'',textContent:'',innerHTML:'',clientWidth:700,width:0,height:0,disabled:false,addEventListener(event,fn){this[event]=fn;},getContext(){return ctx;}});return elements.get(id);},createElement(){return {click(){}};}};
let now=1000,badSave=false;
const config={persisted:true,settings_revision:1,fields:[{key:'stationary_ms',label:'Stillness',unit:'ms',min:500,max:10000,step:10,value:3000,default:3000}]};
const hiddenValues={led_duty_pct:37,led_period_ms:1200,max_gap_ms:200,smoothing_ms:70,stationary_accel_rms:0.15,noise_multiplier:7.2};
for(const [key,value] of Object.entries(hiddenValues))config.fields.push({key,label:key,unit:'',min:0,max:10000,step:0.01,value,default:value});
const columns=['sequence','uptime_ms','valid','active','armed','led_on','accel_deviation','accel_trigger','baseline_noise','noise_multiplier','effective_accel','settings_revision'];
let packet={uptime_ms:1000,settings_revision:1,columns,rows:[[1,1000,1,0,1,0,0.2,0.6,0.02,6,0.6,1]],pickup:{active:false,state:'armed',quiet_ms:3000,stationary_ms:3000,events:0}};
const sandbox=vm.createContext({document,window:{devicePixelRatio:1,addEventListener(){},confirm(){return true;}},performance:{now:()=>now},AbortController,URLSearchParams,
 setTimeout(fn,ms){timers.push(ms);return 1;},clearTimeout(){},
 fetch:async(url,options)=>{requests.push({url,options});if(url.includes('/debug'))return {ok:true,json:async()=>packet};
 if(options?.method==='POST'&&badSave)return {ok:false,json:async()=>({error:'Invalid setting'})};
 return {ok:true,json:async()=>config};}
});
const read=code=>vm.runInContext(code,sandbox);
async function main(){
 vm.runInContext(script,sandbox);await new Promise(resolve=>setImmediate(resolve));
 assert.equal(read('plotSpecs.map(p=>p.id).join(",")'),'movement,tilt,rotation,quiet-gyro,quiet-time,persistence','only the six requested plots remain');
 assert.equal(read('rows.length'),1);assert.equal(read('rows[0].noise_ratio'),10);
 assert.equal(read('rows[0].required_ratio'),30);assert.ok(timers.includes(200),'5 Hz batched polling');
 assert.ok(commands.some(d=>d.length===2),'thresholds use dashed lines');
 assert.equal(elements.get('setting-stationary_ms').value,'3000');
 for(const key of Object.keys(hiddenValues)){
  assert.ok(!elements.has('setting-'+key),'hidden setting has no input: '+key);
  assert.ok(!elements.get('fields').innerHTML.includes('setting-'+key));
 }
 elements.get('setting-stationary_ms').value='4000';
 await elements.get('settingsForm').submit({preventDefault(){}});
 const submitted=new URLSearchParams(requests.filter(r=>r.options?.method==='POST').at(-1).options.body);
 assert.equal(submitted.get('stationary_ms'),'4000');
 for(const [key,value] of Object.entries(hiddenValues))assert.equal(Number(submitted.get(key)),value,'saving visible edits preserves '+key);
 read('ingest('+JSON.stringify(packet)+')');assert.equal(read('rows.length'),1,'deduplicated');
 packet={...packet,uptime_ms:1100,settings_revision:2,rows:[[2,1100,1,1,1,1,0.9,1.2,0.02,6,1.2,2]],pickup:{...packet.pickup,active:true}};
 read('dirty=true;ingest('+JSON.stringify(packet)+')');
 assert.equal(read('rows[0].accel_trigger'),0.6);assert.equal(read('rows[1].accel_trigger'),1.2,'historical thresholds retained');
 assert.match(elements.get('saveStatus').textContent,/Unsaved form retained/);
 assert.match(elements.get('status').textContent,/Picked up/);
 packet={...packet,uptime_ms:1500,rows:[[5,1500,0,1,0,0,null,1.2,0,6,1.2,2]]};read('ingest('+JSON.stringify(packet)+')');
 assert.equal(read('lost'),2);assert.equal(read('rows[2].noise_ratio'),null,'no division by zero');
 read('draw()');
 const before=requests.length;elements.get('pause').click();await read('poll()');assert.equal(requests.length,before,'pause stops network requests');
 elements.get('pause').click();
 elements.get('setting-stationary_ms').value='';await elements.get('settingsForm').submit({preventDefault(){}});assert.match(elements.get('saveStatus').textContent,/Not saved/);
 elements.get('setting-stationary_ms').value='4000';badSave=true;await elements.get('settingsForm').submit({preventDefault(){}});assert.match(elements.get('saveStatus').textContent,/Invalid setting/);
 packet={...packet,uptime_ms:10,rows:[[1,10,1,0,0,0,null,0.6,0,6,0.6,1]]};read('ingest('+JSON.stringify(packet)+')');assert.equal(read('rows.length'),1,'restart clears old data');
 const gyroPacket={...packet,uptime_ms:20,columns:['sequence','uptime_ms','gyro_rad_s','gyro_trigger'],rows:[[2,20,0.1,0.35]]};
 read('ingest('+JSON.stringify(gyroPacket)+')');
 assert.ok(Math.abs(read('rows.at(-1).gyro_deg_s')-0.1*180/Math.PI)<1e-8);
 assert.ok(read('columns.includes("gyro_deg_s")'),'CSV column names use degrees');
 sandbox.markerBase={sequence:20,uptime_ms:2000,valid:1,active:0,armed:1,events:0,settings_revision:1,quiet_block_mask:0};
 read('markerNext={...markerBase,sequence:21,active:1,events:1,confirmed_trigger_mask:5,quiet_block_mask:2};annotate(markerNext,markerBase)');
 assert.equal(read('markerNext.markers[0].kind'),'trigger');
 assert.match(read('markerNext.markers[0].label'),/acceleration departure, filtered rotation/);
 assert.equal(read('markerLabel(markerNext.markers[0],"movement")'),'CAUSE');
 assert.equal(read('markerLabel(markerNext.markers[0],"tilt")'),'ALERT*');
 assert.ok(!read('markerApplies(markerNext.markers[1],"stillness")'));
 assert.ok(read('markerApplies(markerNext.markers[1],"quiet-gyro")'));
 read('markerClear={...markerNext,sequence:22,active:0,quiet_block_mask:0};annotate(markerClear,markerNext)');
 assert.equal(read('markerClear.markers[0].kind'),'clear');
 read('markerClear.sequence=25;annotate(markerClear,markerNext)');
 assert.equal(read('markerClear.markers.length'),0,'missing data cannot invent a clearing transition');
 assert.match(read('decisionNote("tilt")'),/TRIGGER/);
 assert.match(read('decisionNote("quiet-gyro")'),/CLEARING/);
 const priorSequence=read('lastSequence'),priorCount=read('rows.length');
 const malformed={...packet,uptime_ms:now,rows:[[priorSequence+1,now,...Array(columns.length-2).fill(0)],[priorSequence+2]]};
 assert.throws(()=>read('ingest('+JSON.stringify(malformed)+')'),/Invalid telemetry/);
 assert.equal(read('lastSequence'),priorSequence,'malformed package cannot advance debug ACK');
 assert.equal(read('rows.length'),priorCount,'malformed package cannot partially enter history');
 await read('poll()');
 assert.ok(requests.some(r=>/\/debug\?client=\d+&boot=\d+&ack=/.test(r.url)),'debug sends explicit ACK requests');
 console.log('PASS: tuning plots, threshold history, metadata, settings validation, polling/pause, gaps and restart');
}
main().catch(error=>{console.error(error);process.exitCode=1;});
