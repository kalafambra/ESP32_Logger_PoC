// Validate the Android import artifact against the advertised firmware UUIDs.
const fs=require('node:fs'),assert=require('node:assert/strict');
const source=fs.readFileSync('pickup_ble.h','utf8');
const definitions=JSON.parse(source.match(/R"NAMES\(([\s\S]*?)\)NAMES"/)[1]);
const uuid=name=>source.match(new RegExp(name+'\\[\\]="([^"]+)"'))[1].replaceAll('-','').toUpperCase();
assert.equal(definitions.uuid128bitServiceDefinitions[uuid('PICKUP_SERVICE_UUID')].name,'Pickup lists');
assert.deepEqual(definitions.uuid128bitCharacteristicDefinitions[uuid('PICKUP_LIST_UUID')],{name:'Pickup lists',format:'TEXT'});
assert.equal(Object.keys(definitions.uuid128bitCharacteristicDefinitions).length,1);
for(const group of Object.values(definitions))for(const [key,value] of Object.entries(group)){
 assert.match(key,/^[0-9A-F]{32}$/);assert.equal(typeof value.name,'string');
}
console.log('PASS: Android UUID definition schema, firmware UUID mappings and text presentation');
