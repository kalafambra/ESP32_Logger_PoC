const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'..','motion.cpp'),'utf8');
function fixture() {
 const c={now:1000,lastProcessedMotionMs:0,motionWindowFailed:false,motionProcessingError:'',
  motionCalibrating:false,motionGyroCalibrating:false,motionCalibrationSamples:0,motionCalibrationStartedMs:0,
  motionSamples:0,motionSum:[0,0,0],motionGyroSum:[0,0,0],motionMin:[0,0,0],motionMax:[0,0,0],
  motionBias:[0,0,0],motionGyroBias:[0,0,0],motionCalibrationSum:[0,0,0],
  motionCalibrationSquares:[0,0,0],gyroCalibrationSum:[0,0,0],gyroCalibrationSquares:[0,0,0],
  calls:[],Kind:{MotionFault:1},millis:()=>c.now,int32_t:v=>v|0,
  pickupDetector:()=>({settings:{max_gap_ms:100}}),
  decodeMotionRaw:(a,b)=>((a<<8)|b)>=32768?((a<<8)|b)-65536:((a<<8)|b),
  invalidatePickup:r=>c.calls.push(['invalid',r]),
  publishMotion:t=>c.calls.push(['publish',t,c.motionSamples]),
  processPickupSample:(a,w,t)=>c.calls.push(['detector',t,[...a],[...w]]),
  finishMotionCalibration:()=>{c.motionCalibrating=false;c.calls.push(['calibrated']);},
 };
 vm.createContext(c);
 for(const name of ['invalidateMotionProcessing','processMotionPacket']) {
  let code=source.match(new RegExp('void '+name+'\\([^\\n]*\\) \\{([\\s\\S]*?)\\n\\}'))[1]
   .replace(/const (bool|unsigned|float|int32_t) /g,'const ')
   .replace('unsigned axis','let axis')
   .replace('float detectorA[3], detectorW[3];','let detectorA=[],detectorW=[];')
   .replace(/SensorPacketKind::/g,'Kind.').replace(/(\d)f\b/g,'$1')
   .replace(/double\(([^)]+)\)/g,'($1)');
  vm.runInContext('function '+name+'('+ (name==='processMotionPacket'?'packet':'message') +'){'+code+'}',c);
 }
 c.sample=()=>c.processMotionPacket({kind:0,sampleMs:995,raw:[0,0,0,0,0x40,0,0,0,0,0,0,0]});
 return c;
}
let c=fixture();c.sample();
assert.equal(c.motionSamples,1);assert.ok(Math.abs(c.motionSum[2]-9.80665)<1e-6);
assert.equal(c.calls.find(x=>x[0]==='detector')[1],995,'detector receives sample estimate, not dequeue time');
assert.equal(c.calls[0][0],'publish','close prior window before admitting next frame');
c=fixture();c.now=1200;c.sample();assert.equal(c.motionSamples,0);assert.equal(c.motionWindowFailed,true);
assert.match(c.motionProcessingError,/too old/);assert.ok(!c.calls.some(x=>x[0]==='detector'));
c=fixture();c.motionCalibrating=true;c.motionCalibrationSamples=499;c.sample();
assert.ok(c.calls.some(x=>x[0]==='calibrated'));assert.ok(!c.calls.some(x=>x[0]==='detector'));
c=fixture();c.motionCalibrating=true;c.processMotionPacket({kind:1,error:'FIFO lost'});
assert.equal(c.motionCalibrating,false);assert.equal(c.motionProcessingError,'FIFO lost');
c=fixture();c.motionCalibrating=true;c.motionCalibrationStartedMs=1000;c.sample();
assert.equal(c.motionCalibrationSamples,0,'queued pre-command samples do not enter calibration');
console.log('PASS: raw motion decode, sample-time delivery, stale rejection, calibration completion and fault cancellation');
