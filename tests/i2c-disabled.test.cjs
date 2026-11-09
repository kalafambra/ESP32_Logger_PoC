const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const root=path.join(__dirname,'..'),sketch=fs.readFileSync(path.join(root,'ESP32_1.ino'),'utf8'),motion=fs.readFileSync(path.join(root,'motion.h'),'utf8');
for(const name of ['config.example.h','config.h'])if(fs.existsSync(path.join(root,name)))assert.ok(/constexpr bool I2C_SENSORS_ENABLED = false;/.test(fs.readFileSync(path.join(root,name),'utf8')),name+' must select microphone-only mode');
// Run the complete register helpers with a bus that fails on any access.
for(const [source,name] of [[sketch,'readRegisters'],[sketch,'writeRegister'],[motion,'motionRead'],[motion,'motionWrite']]){
 const body=source.match(new RegExp('bool '+name+'\\([^\\n]*\\) \\{([\\s\\S]*?)\\n\\}'))[1].replace(/uint8_t i/g,'let i');
 const c={I2C_SENSORS_ENABLED:false,Wire:new Proxy({},{get(){throw new Error(name+' touched I2C');}})};
 vm.createContext(c);assert.equal(vm.runInContext('(function(){'+body+'})()',c),false);
}
// Execute the real disabled startup branch and require every setup bus access
// and sensor initialization to stay inside the enabled branch.
const start=sketch.indexOf('  if (I2C_SENSORS_ENABLED) {',sketch.indexOf('void setup()'));
let end=start+sketch.slice(start).indexOf('{'),depth=1;
for(end++;depth;end++){if(sketch[end]==='{')depth++;else if(sketch[end]==='}')depth--;}
const enabled=sketch.slice(start,end);
for(const call of ['Wire.begin(','Wire.setClock(','Wire.setTimeOut(','bme.begin(','bme.setSampling(','setupMotion()'])assert.ok(enabled.includes(call),call+' must be within enabled startup');
const disabled=sketch.slice(end).match(/^ else \{([\s\S]*?)\n  \}/)[1];
const c={sensorReady:true,motionReady:true,sensorError:'',motionError:'',motionTemperatureError:'',latest:{},invalidatePickup(){},Serial:{println(){}}};
vm.createContext(c);vm.runInContext(disabled,c);
assert.equal(c.sensorReady,false);assert.equal(c.motionReady,false);assert.match(c.latest.error,/disabled/);
for(const name of ['setupMotion','acquireMotion','acquireMotionTemperature'])assert.match(motion,new RegExp('void '+name+'\\([^\\n]*\\)\\s*\\{\\s*if \\(!I2C_SENSORS_ENABLED\\) return;'));
assert.match(sketch,/void publishWindow\(uint32_t now\) \{\s*if \(!I2C_SENSORS_ENABLED\) return;/);
console.log('PASS: microphone-only startup, all register helper guards, skipped MPU/temperature acquisition and disabled status');
