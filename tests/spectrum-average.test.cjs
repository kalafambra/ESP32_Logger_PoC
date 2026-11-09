// Execute the firmware aggregation function against deterministic frame powers.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'..','microphone_spectrum.cpp'),'utf8');
function translate(body){return body
 .replace('  const MicrophoneStatus microphone = microphoneStatus();','')
 .replace(/microphone\.(ready|paused|overflows)/g,(_,field)=>({ready:'microphoneReady',paused:'microphonePaused',overflows:'microphoneOverflows'}[field]))
 .replace(/block\.count/g,'microphoneBlockCount').replace(/block\.samples/g,'microphoneBlock')
 .replace(/report\.(\w+)/g,(_,field)=>({valid:'spectrumValid',binsDb:'spectrumDb',rmsDb:'spectrumRmsDb',bandsDb:'spectrumBandsDb',peakHz:'spectrumPeakHz',frames:'spectrumPublishedFrames',clipped:'spectrumPublishedClipped',sequence:'spectrumSequence',sampleMs:'spectrumMs'}[field]))
 .replace(/const (uint32_t|float|unsigned) /g,'const ').replace(/size_t i/g,'let i').replace(/unsigned (i|k)/g,'let $1')
 .replace('float bands[3] = {}, peak = 0;','let bands=[0,0,0],peak=0;')
 .replace(/for \(float &value : spectrumSum\)\s+value = 0;/g,'spectrumSum.fill(0);').replace(/SpectrumAnalysis::/g,'SpectrumAnalysis.');}
const service=source.match(/void serviceMicrophoneSpectrum\(const AudioBlock &block\) \{([\s\S]*?)\n\}/)[1];
const reset=source.match(/void resetSpectrumWindow\(\) \{([\s\S]*?)\n\}/)[1];
function fixture(interval){
 const c={AUDIO_SPECTRUM_AVERAGE_MS:interval,clock:0,microphoneReady:true,microphonePaused:false,microphoneOverflows:0,spectrumLastOverflow:0,
  spectrumValid:false,spectrumWindowMs:0,spectrumMs:0,spectrumSequence:0,spectrumFrames:0,spectrumPublishedFrames:0,spectrumClipped:0,spectrumPublishedClipped:0,
  spectrumSum:[0,0,0],spectrumDb:[0,0,0],spectrumBandsDb:[0,0,0],spectrumRmsSum:0,spectrumRmsDb:0,spectrumPeakHz:0,
  microphoneBlock:[1],microphoneBlockCount:1,SpectrumAnalysis:{BINS:3,BIN_HZ:1000,db:p=>10*Math.log10(Math.max(p,1e-12))},
  microphoneFft:{power:[0,0,0],rmsPower:0,clipped:0,resets:0,push:()=>true,reset(){this.resets++;}}};
 c.millis=()=>c.clock;vm.createContext(c);vm.runInContext('function resetSpectrumWindow(){'+translate(reset)+'}\nfunction service(){'+translate(service)+'}',c);
 c.tick=(at,power)=>{c.clock=at;c.microphoneFft.power=[0,power,0];c.microphoneFft.rmsPower=power;vm.runInContext('service()',c);};return c;
}
let c=fixture(1000);c.tick(400,0.1);assert.equal(c.spectrumSequence,0);c.tick(1000,0.3);
assert.equal(c.spectrumPublishedFrames,2);assert.ok(Math.abs(c.spectrumDb[1]-10*Math.log10(0.2))<1e-8,'average linear power, not dB');
assert.equal(c.spectrumFrames,0);assert.equal(c.spectrumMs,1000);
c=fixture(2000);c.tick(1000,0.1);assert.equal(c.spectrumSequence,0);c.tick(2000,0.3);assert.equal(c.spectrumPublishedFrames,2);
c=fixture(1000);c.tick(400,0.1);c.microphoneOverflows=1;c.tick(1000,0.3);assert.equal(c.spectrumPublishedFrames,2,'DMA gap keeps completed frames in average');
c.microphonePaused=true;c.tick(1200,1);assert.equal(c.spectrumValid,false);assert.equal(c.spectrumFrames,0,'OTA pause discards interrupted average');
console.log('PASS: configured averaging interval, linear-power averaging, frame accounting, DMA gaps and OTA reset');
