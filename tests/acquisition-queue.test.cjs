const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const service=fs.readFileSync(path.join(__dirname,'..','i2c_bus_service.cpp'),'utf8');
const body=name=>service.match(new RegExp('(?:bool|void) '+name+'\\([^\\n]*\\) \\{([\\s\\S]*?)\\n\\}'))[1];
function fixture() {
 const c={samples:true,CAPACITY:2,generation:0,drops:0,highWater:0,seenGeneration:0,
  pdTRUE:true,queue:[],processed:[],invalidations:[],Kind:{Motion:0,MotionFault:1,Reset:5},
  max:Math.max,millis:()=>1000,uint32_t:v=>v,
  xQueueSend:(_,packet)=>{if(c.queue.length===c.CAPACITY)return false;c.queue.push({...packet});return true;},
  uxQueueMessagesWaiting:()=>c.queue.length,
  xQueueReceive:(_,packet)=>{if(!c.queue.length)return false;Object.assign(packet,c.queue.shift());return true;},
  invalidateMotionProcessing:r=>c.invalidations.push('motion:'+r),
  invalidateEnvironmentProcessing:r=>c.invalidations.push('environment:'+r),
  processMotionPacket:p=>c.processed.push({...p}),processEnvironmentPacket:p=>c.processed.push({...p}),
  resetMotionProcessing:()=>{},publishProcessedMotion:()=>{},
 };
 vm.createContext(c);
 for(const name of ['enqueueSensorPacket','processSensorPackets']) {
  const code=body(name).replace('static uint32_t seenGeneration = 0;','')
   .replace('SensorPacket packet;','let packet = {};').replace('&packet','packet')
   .replace('unsigned i','let i').replace(/SensorPacketKind::/g,'Kind.');
  vm.runInContext('function '+name+'('+ (name==='enqueueSensorPacket'?'packet':'') +'){'+code+'}',c);
 }
 return c;
}
let c=fixture();
for(let sequence=1;sequence<=3;sequence++)c.enqueueSensorPacket({kind:0,sequence});
assert.equal(c.queue.length,2);assert.equal(c.drops,1);assert.equal(c.highWater,2);
c.processSensorPackets();assert.deepEqual(c.processed.map(p=>p.sequence),[1,2]);
assert.equal(c.invalidations.length,0,'accepted pre-loss frames preserve order');
c.enqueueSensorPacket({kind:0,sequence:4});c.processSensorPackets();
assert.equal(c.invalidations.length,2,'both consumers see the discontinuity');
assert.equal(c.processed.at(-1).sequence,4);
c.enqueueSensorPacket({kind:0,sequence:5});c.processSensorPackets();
assert.equal(c.invalidations.length,2,'same generation does not reset repeatedly');
c=fixture();c.samples=null;c.processSensorPackets();
assert.equal(c.processed.length,0,'disabled sensors have no consumer work');
const isr=service.match(/void ARDUINO_ISR_ATTR notifyAcquisitionFromISR\(\) \{([\s\S]*?)\n\}/)[1];
assert.ok(!/Wire\.|acquireSensor|acquireMotion/.test(isr));
assert.ok(isr.includes('vTaskNotifyGiveFromISR'));
assert.ok(service.includes('ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10))'));
const pause=body('pauseOwner'),resume=body('resumeOwner');
assert.ok(pause.indexOf('paused = true')<pause.indexOf('pauseMotion()'));
assert.ok(resume.indexOf('xQueueReset')<resume.indexOf('discardEnvironmentWindow'));
assert.ok(resume.indexOf('discardEnvironmentWindow')<resume.indexOf('paused = false'));
assert.ok(pause.includes('if (samples) xQueueReset(samples)'));
assert.ok(service.includes('xSemaphoreTake(completion, portMAX_DELAY)'),'RPC argument lifetime lasts through owner completion');
console.log('PASS: bounded queue, ordered delivery, loss generations, disabled path and acquisition/OTA ownership');
