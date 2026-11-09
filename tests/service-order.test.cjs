// Execute the sketch's orchestration against recording services. These orders
// protect DMA capacity, report boundaries and radio allocation on ESP32-WROOM.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '..', 'ESP32_1.ino'), 'utf8');
const functionBody = name => source.match(new RegExp('void '+name+'\\(\\) \\{([\\s\\S]*?)\\n\\}'))[1]
  .replace(/const uint32_t /g, 'const ');

function fixture(enabled) {
  const calls = [];
  const context = {I2C_SENSORS_ENABLED: enabled, SDA_PIN: 21, SCL_PIN: 22,
    Serial: {begin(){}, println(){}}, millis: () => 1234,
    Wire: Object.fromEntries(['begin','setClock','setTimeOut'].map(name => [name, () => calls.push('Wire.'+name)]))};
  const services = ['setupEnvironmentHistory','setupPickup','setupPickupEvents','setupOta',
    'setupEnvironment','setupMotion','disableEnvironment','disableMotion','invalidatePickup',
    'resetEnvironmentWindow','setupNetwork','setupMicrophone','setupHttp','serviceOta',
    'serviceMicrophoneAudio','publishWindow','acquireSensor','acquireMotion','serviceHttp',
    'logMicrophoneDiagnostics','servicePickup','servicePickupBle','serviceNetwork'];
  for (const name of services) context[name] = (...args) => calls.push(name);
  vm.createContext(context);
  return {calls, run(name) { vm.runInContext(functionBody(name), context); }};
}

let f = fixture(true);
f.run('setup');
assert.deepEqual(f.calls, ['setupEnvironmentHistory','setupPickup','setupPickupEvents','setupOta',
  'Wire.begin','Wire.setClock','Wire.setTimeOut','setupEnvironment','setupMotion','Wire.setClock',
  'resetEnvironmentWindow','setupNetwork','setupMicrophone','setupHttp','resetEnvironmentWindow']);
f = fixture(false);
f.run('setup');
assert.deepEqual(f.calls, ['setupEnvironmentHistory','setupPickup','setupPickupEvents','setupOta',
  'disableEnvironment','disableMotion','invalidatePickup','resetEnvironmentWindow',
  'setupNetwork','setupMicrophone','setupHttp','resetEnvironmentWindow']);
f.calls.length = 0;
f.run('loop');
assert.deepEqual(f.calls, ['serviceOta','serviceMicrophoneAudio','publishWindow','acquireSensor',
  'acquireMotion','serviceHttp','serviceMicrophoneAudio','logMicrophoneDiagnostics','acquireMotion',
  'servicePickup','servicePickupBle','serviceNetwork']);
console.log('PASS: combined/microphone startup, Wi-Fi allocation priority, DMA drains and report/service order');
