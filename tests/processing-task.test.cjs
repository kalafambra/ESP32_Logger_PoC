// Exercise production task dispatch without pretending to simulate the RTOS scheduler.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const read=file=>fs.readFileSync(path.join(__dirname,'..',file),'utf8');
const source=read('i2c_bus_service.cpp');
const body=name=>source.match(new RegExp('void '+name+'\\([^\\n]*\\) \\{([\\s\\S]*?)\\n\\}'))[1];
const translate=code=>code.replace(/ProcessingStateLock \w+;/g,'')
 .replace(/ScopedServiceTiming \w+\([^;]+;/g,'').replace('Command command;','let command={};')
 .replace('&command','command');
function iteration(command,paused=false) {
 const stop=Symbol('stop'),c={pdTRUE:true,portMAX_DELAY:999,processingPaused:paused,
  processingCommands:1,processingCompletion:2,waits:[],calls:[],millis:()=>123,
  pdMS_TO_TICKS:v=>v,
  ulTaskNotifyTake:(_,timeout)=>{c.waits.push(timeout);if(c.waits.length===3)throw stop;},
  xQueueReceive:(_,out)=>{if(!command)return false;
   out.function=vm.runInContext('(function(){'+command+'})',c);return true;},
  xSemaphoreGive:()=>c.calls.push('ack'),processSensorPackets:()=>c.calls.push('samples'),
  servicePickup:()=>c.calls.push('pickup'),
 };
 vm.createContext(c);
 try{vm.runInContext('(function(){'+translate(body('processingMain'))+'})()',c);}catch(e){if(e!==stop)throw e;}
 return c;
}
let c=iteration();assert.deepEqual(c.calls,['samples','pickup']);
assert.deepEqual(c.waits,[999,20,20],'startup gate followed by bounded idle waits');
c=iteration(null,true);assert.deepEqual(c.calls,[],'paused worker does not process samples or mutate detector');
c=iteration('processingPaused=false;',true);assert.deepEqual(c.calls,['ack','samples','pickup']);
c=iteration('processingPaused=true;');assert.deepEqual(c.calls,['ack'],'pause acknowledged before further data work');
const pause=translate(body('pauseProcessingOwner'));
const resume=translate(body('resumeProcessingOwner'));
const owner={processingPaused:false,calls:[],millis:()=>12,
 invalidateMotionProcessing:r=>owner.calls.push('motion'),
 invalidateEnvironmentProcessing:r=>owner.calls.push('environment'),
 servicePickup:()=>owner.calls.push('pickup')};
vm.createContext(owner);vm.runInContext(pause,owner);
assert.equal(owner.processingPaused,true);assert.deepEqual(owner.calls,['motion','environment','pickup']);
vm.runInContext(resume,owner);assert.equal(owner.processingPaused,false);
assert.ok(source.includes('if (processingTask) xTaskNotifyGive(processingTask)'));
const sketch=read('ESP32_1.ino');
const loop=sketch.match(/void loop\(\) \{([\s\S]*?)\n\}/)[1];
assert.ok(!loop.includes('processSensorPackets('));assert.ok(!loop.includes('servicePickup('));
const http=read('http_api.cpp');
for(const borrowed of ['pickupDetector()','latestMotionReport()','motionHistoryAt(','environmentHistory()'])
 assert.ok(!http.includes(borrowed),'HTTP must not borrow mutable state: '+borrowed);
for(const file of ['motion.cpp','pickup_service.cpp'])assert.ok(read(file).includes('runProcessingCommand('));
// Signed lease/ordering comparisons must tolerate FIFO estimates preceding subscription/service.
const debug=read('pickup_debug.cpp');
let prefix=debug.match(/bool PickupDebug::capture\([^]*?\) \{([^]*?)  if \(!p.active\)/)[1]
 .replace('delete[] records;','records = null;').replace(/nullptr/g,'null');
function capturePrefix(now,lastRequest,count=0,lastCapture=0){
 const c={now,lastRequest,count,lastCapture,subscribed:true,records:[],head:0,lastRise:0,lastOnMs:0,
  observedPeriod:0,observedDuty:0,NAN:NaN,int32_t:v=>v|0};
 vm.createContext(c);c.result=vm.runInContext('(function(){'+prefix+'return true;})()',c);return c;
}
assert.equal(capturePrefix(995,1000).subscribed,true,'older sample does not expire fresh subscription');
assert.equal(capturePrefix(995,1000,1,1000).result,false,'debug timestamps do not go backward');
assert.equal(capturePrefix(4001,1000).subscribed,false,'actual lease expiration still frees records');
assert.equal(capturePrefix(0x100,0xFFFFFE00).subscribed,true,'lease comparison survives millis wrap');
console.log('PASS: processing wake/idle/pause dispatch, HTTP ownership boundaries and debug lease/timestamp ordering');
