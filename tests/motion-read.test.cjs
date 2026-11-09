const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'..','motion.h'),'utf8');
const body=source.match(/bool motionRead\(uint8_t reg, uint8_t \*data, uint8_t count\) \{([\s\S]*?)\n\}/)[1].replace('uint8_t i','let i');
function read({ack=0,returned=3,enabled=true}={}){
 const ops=[],data=[0,0,0];let next=10;
 const c={I2C_SENSORS_ENABLED:enabled,reg:0x74,data,count:3,MPU_ADDRESS:0x68,Wire:{beginTransmission:a=>ops.push(['begin',a]),write:r=>ops.push(['register',r]),
   endTransmission:stop=>{ops.push(['stop',stop]);return ack;},requestFrom:(a,n)=>{ops.push(['read',a,n]);return returned;},read:()=>next++}};
 vm.createContext(c);return {ok:vm.runInContext('(function(){'+body+'})()',c),ops,data};
}
let r=read();assert.equal(r.ok,true);assert.deepEqual(r.data,[10,11,12]);
assert.deepEqual(r.ops,[['begin',0x68],['register',0x74],['stop',true],['read',0x68,3]]);
r=read({ack:2});assert.equal(r.ok,false);assert.equal(r.ops.length,3,'do not read after failed register selection');
r=read({returned:2});assert.equal(r.ok,false);assert.deepEqual(r.data,[0,0,0],'short reads cannot enter the motion pipeline');
r=read({enabled:false});assert.equal(r.ok,false);assert.deepEqual(r.ops,[],'microphone build cannot touch the bus');
console.log('PASS: MPU STOP-separated register reads, NACK handling and short-read rejection');
