// Execute production BME scheduling/read/consumer branches with a deterministic bus.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'..','environment.cpp'),'utf8');
function translate(name) {
 let body=source.match(new RegExp('void '+name+'\\([^\\n]*\\) \\{([\\s\\S]*?)\\n\\}'))[1];
 return body.replace(/ScopedServiceTiming \w+\([^;]+;/g,'')
  .replace(/const (uint32_t|float|MotionTemperature) /g,'const ')
  .replace(/SensorPacket (\w+);/g,'let $1 = {};')
  .replace(/SensorPacketKind::/g,'Kind.').replace('SampleWindow{}','newWindow()')
  .replace('uint8_t status;','let status = [0];').replace('&status','status')
  .replace('uint8_t raw[8];','let raw = new Array(8).fill(0);')
  .replace('uint8_t id;','let id = [0];').replace('&id','id')
  .replace('sizeof(raw)','raw.length').replace(/(\d+\.\d+)F/g,'$1')
  .replace('(now - lastStart) / SAMPLE_INTERVAL_MS','Math.floor(((now-lastStart)>>>0)/SAMPLE_INTERVAL_MS)')
  .replace('(lastStart - acquisitionWindowStart) / PUBLISH_INTERVAL_MS','Math.floor(((lastStart-acquisitionWindowStart)>>>0)/PUBLISH_INTERVAL_MS)')
  .replace(/now - lastStart/g,'((now-lastStart)>>>0)')
  .replace(/lastStart - acquisitionWindowStart/g,'((lastStart-acquisitionWindowStart)>>>0)');
}
function fixture(start=1000) {
 const kinds=['Motion','Environment','EnvironmentFault','Boundary','Reset'];
 const c={now:start,sensorReady:true,converting:false,firstTrigger:true,lastStart:start,
  acquisitionWindowStart:start,conversionStart:0,environmentSequence:0,
  SAMPLE_INTERVAL_MS:200,PUBLISH_INTERVAL_MS:1000,missedSampleDeadlines:0,
  lateConversionServices:0,windowStart:start,windowFailed:false,sensorError:'',latest:{},
  status:0,readsOK:true,triggerOK:true,ops:[],packets:[],reports:[],counts:0,
  Kind:Object.fromEntries(kinds.map(k=>[k,k])),isfinite:Number.isFinite,
  millis:()=>c.now,countEnvironmentSample:()=>c.counts++,
  writeRegister:()=>{c.ops.push('trigger');return c.triggerOK;},
  readRegisters:(reg,data)=>{c.ops.push('read:'+reg);if(!c.readsOK)return false;
   data[0]=reg===0xF3?c.status:reg===0xD0?0x60:0;return true;},
  bme:{readTemperature:()=>23,readHumidity:()=>45,readPressure:()=>100000},
  enqueueSensorPacket:p=>{c.packets.push({...p});return true;},
  acquireMotionTemperature:()=>c.ops.push('chipTemperature'),
  motionTemperature:()=>({celsius:27,ok:true}),
  newWindow:()=>({count:0,add(){this.count++;}}),
  publishWindow:t=>{if(((t-c.windowStart)>>>0)<1000)return;
   c.reports.push({count:c.window.count,ok:!c.windowFailed,at:t});
   c.window=c.newWindow();c.windowStart=t;c.windowFailed=false;}
 };
 c.window=c.newWindow();vm.createContext(c);
 for(const name of ['failSample','readPreviousEnvironment','acquireSensor',
                    'invalidateEnvironmentProcessing','processEnvironmentPacket'])
  vm.runInContext('function '+name+'('+ (name==='failSample'||name==='invalidateEnvironmentProcessing'?'message':name==='readPreviousEnvironment'?'now':name==='processEnvironmentPacket'?'packet':'') +'){'+translate(name)+'}',c);
 c.tick=t=>{c.now=t;vm.runInContext('acquireSensor()',c);};
 c.drain=()=>{for(const packet of c.packets.splice(0)){c.packet=packet;vm.runInContext('processEnvironmentPacket(packet)',c);}};
 return c;
}
let c=fixture();c.tick(1000);assert.deepEqual(c.ops,['trigger']);
c.tick(1050);c.tick(1150);assert.equal(c.ops.length,1,'no polling between deadlines');
c.tick(1200);assert.equal(c.packets[0].sampleMs,1000);
assert.equal(c.ops.at(-1),'trigger','read before trigger');
for(const t of [1400,1600,1800,2000]){c.tick(t);c.drain();}
assert.equal(c.reports.length,1);assert.deepEqual(c.reports[0],{count:5,ok:true,at:2000});
assert.equal(c.counts,5);assert.equal(c.window.count,0);
assert.equal(c.latest.mpuTemperatureC,27);assert.equal(c.latest.mpuTemperatureOk,true);
c=fixture();c.tick(1000);c.status=8;c.tick(1200);c.drain();
assert.match(c.sensorError,/still busy/);assert.equal(c.window.count,0);assert.equal(c.windowFailed,true);
assert.equal(c.ops.at(-1),'trigger','attempt next conversion after fault');
c=fixture();c.tick(1000);c.readsOK=false;c.tick(1200);c.drain();
assert.equal(c.sensorError,'I2C status read failed');
c=fixture();c.tick(1000);c.tick(1215);c.tick(1400);
assert.equal(c.lastStart,1400,'jitter does not shift schedule');
c.tick(1850);assert.equal(c.lastStart,1800);assert.equal(c.missedSampleDeadlines,1);
assert.equal(c.ops.filter(x=>x==='trigger').length,4,'no catch-up burst');
c=fixture();c.sensorReady=false;
for(let t=1000;t<=2000;t+=200){c.tick(t);c.drain();}
assert.equal(c.reports.length,1,'missing BME still delivers report boundaries');
assert.equal(c.reports[0].count,0);assert.ok(!c.ops.includes('trigger'));
c=fixture(0xFFFFFF00);c.tick(0xFFFFFF00);c.tick((0xFFFFFF00+200)>>>0);c.tick((0xFFFFFF00+400)>>>0);
assert.equal(c.ops.filter(x=>x==='trigger').length,3);
console.log('PASS: BME read-before-trigger, five-sample boundary ordering, no polling, faults, grid and rollover');
