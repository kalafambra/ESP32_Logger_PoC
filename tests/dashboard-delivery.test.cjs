// Run the portable C++ ring's actual branches with C++ value-copy/uint32 semantics.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'..','dashboard_delivery.h'),'utf8');
function fixture(){
 const c={client:0,lastRequest:0,head:0,count:0,dropped:0,acknowledged:0,sent:0,rows:[],Capacity:2};
 vm.createContext(c);
 for(const name of ['publish','copy']){
  let code=source.match(new RegExp('  void '+name+'\\([^]*?\\) \\{([^]*?)\\n  \\}'))[1]
   .replace(/unsigned i/g,'let i').replace('DeliveryInfo{}','({acknowledged:0,dropped:0,oldest:0,gap:false})')
   .replace('now - lastRequest','((now-lastRequest)>>>0)')
   .replace('] = record;','] = {...record};').replace('= rows[(head + i) % Capacity];','= {...rows[(head + i) % Capacity]};');
  vm.runInContext('function '+name+'('+ (name==='copy'?'request,boot,now,latest,out':'record,now') +'){'+code+'}',c);
 }
 c.take=(ack=0,boot=7,client=1,now=1,latest=10)=>{const out={rows:[]};c.copy({client,boot,ack},7,now,{sequence:latest},out);return out;};
 return c;
}
let c=fixture(),s=c.take();assert.equal(s.count,1);assert.equal(s.rows[0].sequence,10);
s.rows[0].sequence=999;assert.equal(c.take().rows[0].sequence,10,'snapshot does not alias ring');
assert.equal(c.take(999).count,1,'future ACK cannot release unsent data');
assert.equal(c.take(10,8).count,1,'old boot cannot acknowledge new firmware data');
assert.equal(c.take(10).count,0,'confirmed receipt releases record');
assert.equal(c.take(10).count,0,'ACKed record is never reinserted on every poll');
c.publish({sequence:11},2);c.publish({sequence:12},3);c.publish({sequence:13},4);
s=c.take(10,7,1,5,13);assert.equal(s.info.gap,true);assert.equal(s.info.dropped,1);
assert.deepEqual(Array.from(s.rows.slice(0,s.count),r=>r.sequence),[12,13]);
assert.equal(c.take(10,7,1,6,13).count,2,'lost response is retransmitted');
assert.equal(c.take(13,7,1,7,13).count,0);
assert.equal(c.take(0,7,2,8,13).count,1,'new page starts with only latest');
assert.equal(c.take(0,7,0,9,13).count,0,'diagnostic polls do not steal subscriber data');
c.publish({sequence:14},4000);assert.equal(c.count,0,'expired subscriber has no pending storage');
s=c.take(13,7,2,4001,14);assert.equal(s.count,1);assert.equal(s.rows[0].sequence,14);
c=fixture();c.take(0,7,1,0xFFFFFF00,10);c.publish({sequence:11},0x100);
assert.equal(c.count,2,'lease crosses millis wrap');
console.log('PASS: delivery ACK/retry, future/old-boot rejection, overflow gap, latest-only refresh, expiry and rollover');
