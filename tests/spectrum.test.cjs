// Execute the firmware's DSP loops with C++ declarations translated to JS,
// following acquisition.test.cjs. This checks the actual algorithm, not a second FFT.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '..', 'spectrum_analysis.cpp'), 'utf8');
const config=fs.readFileSync(path.join(__dirname,'..','audio_config.h'),'utf8');
const size = Number(config.match(/AUDIO_FFT_SIZE = (\d+)/)[1]);
const rate = Number(config.match(/AUDIO_SAMPLE_RATE_HZ = (\d+)/)[1]);
function translate(body) {
  return body.replace(/\bconst (float|unsigned)\b/g, 'const')
    .replace(/\b(float|unsigned)\b/g, 'let')
    .replace(/(\d+(?:\.\d*)?(?:e[+-]?\d+)?)f\b/g, '$1');
}
const ctor = source.match(/SpectrumAnalysis::SpectrumAnalysis\(\) \{([\s\S]*?)\n\}/)[1];
const analyze = source.match(/void SpectrumAnalysis::analyze\(\) \{([\s\S]*?)\n\}/)[1];
const push = source.match(/bool SpectrumAnalysis::push\(int32_t sample\) \{([\s\S]*?)\n\}/)[1];
const state = {SIZE:size,BINS:size/2+1,real:new Float32Array(size),imag:new Float32Array(size),
  window:new Float32Array(size/2+1),windowEnergy:0,rmsPower:0,
  count:0,clipped:0,cosf:Math.cos,sinf:Math.sin,fabsf:Math.abs};
state.power=state.imag.subarray(0,size/2+1); // Firmware exposes only BINS entries of reused scratch.
vm.createContext(state);
vm.runInContext(translate(ctor),state);
vm.runInContext('function analyze(){'+translate(analyze)+'}\nfunction push(sample){'+translate(push)+'}',state);
const near=(actual,expected,tolerance=1e-5)=>assert.ok(Math.abs(actual-expected)<tolerance,`${actual} vs ${expected}`);
function frame(make) {
  for(let i=0;i<size;i++) {
    state.sample=Math.round(make(i)*8388608);
    const complete=vm.runInContext('push(sample)',state);
    assert.equal(complete,i===size-1,'only complete contiguous frames trigger analysis');
  }
  return [...state.power].reduce((sum,p)=>sum+p,0);
}
near(frame(()=>0),0);
near(frame(()=>0.25),0);near(state.rmsPower,0); // DC removal.
const toneBin=Math.round(1000*size/rate);
let energy=frame(i=>0.5*Math.sin(2*Math.PI*toneBin*i/size)); // 1 kHz, -9.03 dBFS RMS.
near(energy,0.125);near(state.rmsPower,0.125);
assert.equal([...state.power].indexOf(Math.max(...state.power)),toneBin);
near(10*Math.log10(state.rmsPower),-9.0309,0.001);
near(state.power[toneBin]/energy,2/3,0.001); // Hann spreads a bin-centered tone across 3 bins.
energy=frame(i=>0.5*Math.cos(Math.PI*i)); // Nyquist gets no factor-of-two correction.
near(energy,0.25);near(state.rmsPower,0.25);
energy=frame(i=>0.25*Math.sin(2*Math.PI*Math.round(250*size/rate)*i/size)+0.5*Math.sin(2*Math.PI*Math.round(3000*size/rate)*i/size));
near(energy,0.15625); // Low + high tones retain summed power.
frame(i=>0.5*Math.sin(2*Math.PI*(toneBin+0.5)*i/size));
near([...state.power].reduce((a,b)=>a+b,0),0.125,0.0001); // Off-bin leakage conserves energy.
frame(()=>0.9995);assert.equal(state.clipped,size);
const positive=frame(i=>0.5*Math.sin(2*Math.PI*toneBin*i/size));
const negative=frame(i=>-0.5*Math.sin(2*Math.PI*toneBin*i/size));
near((positive+negative)/2,0.125); // Power averaging preserves phase-inverted sound.
near(frame(i=>(0.5*Math.sin(2*Math.PI*toneBin*i/size)-0.5*Math.sin(2*Math.PI*toneBin*i/size))/2),0); // Waveform averaging cancels it.
console.log('PASS: firmware FFT streaming, DC removal, tone frequency, RMS scaling, Hann power, Nyquist and clipping');
