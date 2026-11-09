// Exercise the capture implementation's borrowed block contract with a fake I2S driver.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '..', 'microphone.cpp'), 'utf8');
const body = source.match(/AudioBlock serviceMicrophone\(\) \{([\s\S]*?)\n\}/)[1]
  .replace('size_t bytes = 0;', 'let bytes = [0];')
  .replace('&bytes', 'bytes')
  .replace('sizeof(microphoneBlock)', 'microphoneBlock.length * 4')
  .replace('bytes / sizeof(microphoneBlock[0])', 'bytes[0] / 4')
  .replace('size_t i', 'let i')
  .replaceAll('return {microphoneBlock, 0};', 'return {samples: microphoneBlock, count: 0};')
  .replace('return {microphoneBlock, microphoneBlockCount};', 'return {samples: microphoneBlock, count: microphoneBlockCount};');
const c = {microphoneReady: true, microphonePaused: false, microphoneError: 0,
  microphoneBlockCount: 99, microphoneBlock: new Int32Array(256), microphoneSamples: 0,
  microphoneRx: {}, ESP_OK: 0, ESP_ERR_TIMEOUT: 1, code: 0, words: [], reads: 0,
  i2s_channel_read(channel, block, capacity, bytes, wait) {
    assert.equal(wait, 0, 'capture must never wait for DMA');
    assert.equal(capacity, 1024);
    c.reads++;
    block.set(c.words);
    bytes[0] = c.words.length * 4;
    return c.code;
  }};
vm.createContext(c);
const capture = () => vm.runInContext('(function(){' + body + '\n})()', c);

c.code = c.ESP_ERR_TIMEOUT;
let block = capture();
assert.equal(block.count, 0);
assert.equal(c.microphoneSamples, 0);
c.words = [0x12345600, -256, -2147483648];
block = capture();
assert.equal(block.count, 3, 'a timeout can still deliver partial audio');
assert.deepEqual(Array.from(block.samples.slice(0, 3)), [0x123456, -1, -8388608]);
assert.equal(c.microphoneSamples, 3);
assert.equal(c.microphoneError, c.ESP_ERR_TIMEOUT);
const borrowed = block.samples;
c.code = c.ESP_OK;
c.words = [256];
block = capture();
assert.equal(block.samples, borrowed, 'the next call reuses module-owned storage');
assert.equal(borrowed[0], 1);
assert.equal(c.microphoneSamples, 4);
c.code = 2;
assert.equal(capture().count, 0, 'driver errors never deliver a block');
assert.equal(c.microphoneSamples, 4);
const reads = c.reads;
c.microphonePaused = true;
assert.equal(capture().count, 0);
c.microphonePaused = false;
c.microphoneReady = false;
assert.equal(capture().count, 0);
assert.equal(c.reads, reads, 'paused/unavailable capture must not access I2S');
console.log('PASS: nonblocking capture, partial timeout data, signed PCM conversion and borrowed buffer lifetime');
