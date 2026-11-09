const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'..','http_service.h'),'utf8');
const body=source.match(/void serviceHttpClients\(WebServer &http\) \{([\s\S]*?)\n\}/)[1]
 .replace(/  static [^\n]+\n/g,'').replace('auto client','const client').replace('const uint32_t now','const now');
const c={waiting:false,waitingSince:0,clock:0,handled:0,stopped:0,bytes:0,connected:true,
 millis:()=>c.clock,http:{handleClient:()=>c.handled++,client:()=>({connected:()=>c.connected,available:()=>c.bytes,stop:()=>c.stopped++})}};
vm.createContext(c);const tick=t=>{c.clock=t;vm.runInContext('(function(){'+body+'})()',c);};
tick(0);tick(999);assert.equal(c.stopped,0);tick(1000);assert.equal(c.stopped,1,'idle socket cannot occupy server for five seconds');
c.waiting=false;c.bytes=20;tick(2000);tick(4000);assert.equal(c.stopped,1,'never terminate a request with bytes available');
c.bytes=0;tick(5000);c.connected=false;tick(5900);assert.equal(c.waiting,false,'disconnect resets the timer');
c.connected=true;tick(6000);tick(6999);assert.equal(c.stopped,1,'new idle connection gets its own grace period');
assert.equal(c.handled,9,'normal WebServer servicing still occurs on every call');
console.log('PASS: idle HTTP connection timeout, active request preservation and disconnect recovery');
