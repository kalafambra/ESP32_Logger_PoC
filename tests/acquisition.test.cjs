// Execute the environmental module's acquisition decisions with deterministic sensor/time
// stubs. Only C++ type syntax and the status byte reference need translation;
// the deadline and conversion branches come directly from the firmware.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '..', 'environment.cpp'), 'utf8');
let body = source.match(/void acquireSensor\(\) \{([\s\S]*?)\n\}/)[1]
  .replace(/const (uint32_t|float) /g, 'const ')
  .replace('const slots = (now - lastStart) / SAMPLE_INTERVAL_MS;',
    'const slots = Math.floor((now - lastStart) / SAMPLE_INTERVAL_MS);')
  .replace('uint8_t status;', 'let status = [0];')
  .replace('&status', 'status')
  .replace('uint8_t raw[8];', 'let raw = new Array(8).fill(0);')
  .replace('uint8_t id;', 'let id = [0];')
  .replace('&id', 'id')
  .replace('sizeof(raw)', 'raw.length')
  .replace(/(\d+\.\d+)F/g, '$1');
function fixture() {
  const c = {now:0,sensorReady:true,converting:false,lastStart:-200,
    conversionStart:0,SAMPLE_INTERVAL_MS:200,missedSampleDeadlines:0,
    windowScheduledSlots:0,lateConversionServices:0,windowFailed:false,
    status:0,readsOK:true,triggers:[],samples:0,isfinite:Number.isFinite,
    millis:()=>c.now,writeRegister:()=>{c.triggers.push(c.now);return true;},
    readRegisters:(reg,data)=>{if(!c.readsOK)return false;data[0]=reg===0xF3?c.status:reg===0xD0?0x60:0;return true;},
    bme:{readTemperature:()=>23,readHumidity:()=>45,readPressure:()=>100000},
    window:{add:()=>c.samples++},failSample:(error)=>{c.error=error;c.windowFailed=true;c.converting=false;}};
  vm.createContext(c);
  vm.runInContext('function acquireSensor(){'+body+'}', c);
  c.tick=(now)=>{c.now=now;vm.runInContext('acquireSensor()',c);};
  return c;
}
// A finished conversion remains usable after an 80 ms servicing delay.
let c=fixture();c.tick(0);c.tick(100);
assert.equal(c.samples,1);assert.equal(c.error,undefined);assert.equal(c.lateConversionServices,1);
// A sensor that is actually still busy must time out; bus failures stay errors.
c=fixture();c.status=8;c.tick(0);c.tick(100);
assert.match(c.error,/still busy/);assert.equal(c.samples,0);
c=fixture();c.tick(0);c.readsOK=false;c.tick(20);
assert.equal(c.error,'I2C status read failed');
// Jitter does not shift the grid or cause catch-up bursts.
c=fixture();c.tick(0);c.tick(20);c.tick(215);c.tick(235);c.tick(400);c.tick(420);
assert.equal(c.lastStart,400);assert.deepEqual(c.triggers,[0,215,400]);
c.tick(850);assert.equal(c.lastStart,800);assert.equal(c.missedSampleDeadlines,1);
c.tick(870);assert.equal(c.triggers.length,4);assert.equal(c.windowFailed,true);
// The publication reset must preserve the original second boundary.
const reset = source.match(/  \/\/ Start a non-overlapping window;[^\n]*\n([\s\S]*?)\n\}/)[1]
  .replace('SampleWindow{}','({})');
const p={windowStart:1000,now:2250,elapsedWindows:1,PUBLISH_INTERVAL_MS:1000,
  SAMPLE_INTERVAL_MS:200,missedReportDeadlines:0};
vm.createContext(p);vm.runInContext(reset,p);
assert.equal(p.windowStart,2000);assert.equal(p.lastStart,1800);
assert.equal(p.windowScheduledSlots,0);
console.log('Acquisition timing/status regression tests passed');
