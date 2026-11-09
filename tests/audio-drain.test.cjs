// Exercise the firmware's bounded drain loop with deterministic DMA backlog.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'..','microphone_spectrum.h'),'utf8');
const hardware=fs.readFileSync(path.join(__dirname,'..','microphone.h'),'utf8');
const buffers=Number(hardware.match(/MICROPHONE_DMA_BUFFERS = (\d+)/)[1]);
const body=source.match(/void serviceMicrophoneAudio\(\) \{([\s\S]*?)\n\}/)[1].replace('unsigned block','let block');
function fixture(queued,refill=false){
 const c={MICROPHONE_DMA_BUFFERS:buffers,microphoneBlockCount:0,reads:0,frames:0,partial:0,queued,
   serviceMicrophone(){c.reads++;c.microphoneBlockCount=c.queued?256:0;if(c.queued&&!refill)c.queued--;},
   serviceMicrophoneSpectrum(){c.partial+=c.microphoneBlockCount;c.frames+=Math.floor(c.partial/512);c.partial%=512;}};
 vm.createContext(c);c.drain=()=>vm.runInContext(body,c);return c;
}
let c=fixture(6);c.drain();assert.equal(c.queued,0);assert.equal(c.frames,3);
assert.equal(c.reads,Math.min(7,buffers),'never exceed the descriptor bound');
c=fixture(4);c.drain();assert.equal(c.reads,5,'stop as soon as DMA is empty, without waiting');
c=fixture(0);c.drain();assert.equal(c.reads,1);assert.equal(c.frames,0);
c=fixture(100,true);c.drain();assert.equal(c.reads,buffers,'even a continuous producer cannot make the drain unbounded');
c=fixture(0);
for(let tick=0;tick<20;tick++){c.queued+=5;c.drain();}
assert.equal(c.queued,0,'keep up when sensor/network work delays loop visits by 80 ms');
assert.equal(c.frames,50,'retain full FFT frames instead of repeatedly resetting half a frame');
console.log('PASS: audio DMA backlog catch-up, empty queue, bounded work and repeated slow-loop visits');
