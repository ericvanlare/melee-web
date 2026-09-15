import assert from 'node:assert/strict';
import {createControllerManager, normalizeController, validateProfile, STANDARD_PROFILE} from '../web/controller-input.mjs';
import {detectBindingCandidate} from '../web/controller-panel.mjs';

import {standardPad, rawPad, rawProfile, mayflashMacPad} from './controller-fixtures.mjs';

const set = (p, index, value = 1, pressed = value > 0.5) => { p.buttons[index] = {pressed, value}; };
const memoryStorage = () => { let data = null; return {getItem: () => data, setItem: (_, v) => { data = v; }}; };
let current = [], manager = createControllerManager({getGamepads: () => current, storage: memoryStorage(), platform: 'test'});
function sample() { return manager.sample()[0]; }
let p = standardPad(3); current = [null, null, null, p]; sample();
assert.equal(sample().port, 0, 'browser index is not the player port');
for (const [input, expected] of [[0,256],[1,512],[2,1024],[3,2048],[5,16],[9,4096],
  [12,8],[13,4],[14,1],[15,2]]) {
  set(p, input); assert.equal(sample().output.buttons, expected, `standard button ${input}`);
  set(p, input, 0); assert.equal(sample().output.buttons, 0);
}
set(p, 6, 0.5, true);
assert.deepEqual(sample().output.triggers, [128,0]);
assert.equal(sample().output.buttons, 0, 'analog trigger pressed flag is not a full click');
set(p, 6); assert.equal(sample().output.buttons, 64);
set(p, 6, 0); set(p, 7); assert.equal(sample().output.buttons, 32); set(p,7,0);
p.axes = [0.5,-0.5,-1,1];
assert.deepEqual(sample().output.stick, [64,64]);
assert.deepEqual(sample().output.cstick, [-128,-128]);
manager.setTesting(true); assert.deepEqual(sample().output.stick, [0,0]);
assert.deepEqual(sample().pad.stick, [64,64], 'test display retains measured inputs');
manager.setTesting(false); assert.equal(sample().active, false, 'release after setup before playing');
p.axes.fill(0); sample(); set(p,0); assert.equal(sample().output.buttons,256); set(p,0,0);


p=rawPad(); current=[p];
let row=sample(); assert.equal(row.status,'needs-setup');
set(p,0); assert.equal(sample().output.buttons,0,'unknown X must not leak as A'); set(p,0,0);
manager.saveProfile(row.key,rawProfile); sample();
for (const [input,expected] of [[0,1024],[1,256],[2,512],[3,2048],[7,16],[9,4096],[4,64],[5,32]]) {
  set(p,input); assert.equal(sample().output.buttons,expected,`raw button ${input}`); set(p,input,0);
}
p.axes[3]=0; assert.deepEqual(sample().output.triggers,[128,0]); assert.equal(sample().output.buttons,0);
set(p,4); assert.equal(sample().output.buttons,64);
assert.equal(sample().output.triggers[0],128,'digital click must not rewrite pressure'); set(p,4,0); p.axes[3]=-1;
for(const [direction,bits] of [[0,8],[1,10],[2,2],[3,6],[4,4],[5,5],[6,1],[7,9]]) {
  p.axes[6]=-1+direction*2/7; assert.equal(sample().output.buttons,bits,`hat direction ${direction}`);
}
p.axes[6]=3.2857142857; assert.equal(sample().output.buttons,0,'hat neutral does not alias a diagonal');
p.axes[0]=0.625; assert.equal(sample().output.stick[0],80,'do not stretch GameCube stick travel'); p.axes[0]=0;

const invalid=structuredClone(rawProfile); invalid.buttons.B={kind:'button',index:1};
assert.throws(()=>validateProfile(invalid,p),/same button/);
const noClick=structuredClone(rawProfile); noClick.buttons.L=null;
assert.throws(()=>validateProfile(noClick,p),/Map L/);
const short=rawPad(); short.axes=[];
assert.throws(()=>validateProfile(rawProfile,short));
p.axes[0]=NaN; assert.equal(sample().status,'needs-setup'); assert.equal(sample().output.buttons,0); p.axes[0]=0;

const p2=standardPad(9,'DualSense Wireless Controller'); current=[p,p2]; manager.sample();
let rows=manager.sample(); assert.deepEqual(rows.map(r=>r.port),[0,1]);
manager.assign(rows[1].key,0); rows=manager.sample(); assert.deepEqual(rows.map(r=>r.port),[1,0]);
current=[p2]; assert.deepEqual(manager.sample().map(r=>r.port),[0]);
const replacement=standardPad(0,'Different controller'); current=[replacement,p2];
rows=manager.sample(); assert.equal(rows[0].profile,'Browser standard','index reuse cannot inherit raw profile');
assert.deepEqual(rows.map(r=>r.port),[1,0]);
const heap=new Int32Array(40).fill(12345); manager.writeSamples(heap,16);
assert.equal(heap[0],12345); assert.equal(heap[4],1); assert.equal(heap[12],1);
assert.equal(heap[20],0); assert.equal(heap[28],0); assert.equal(heap[36],12345);

const blocked=createControllerManager({getGamepads:()=>[rawPad()],storage:{getItem(){throw Error();},setItem(){throw Error();}}});
row=blocked.inspect()[0]; blocked.saveProfile(row.key,rawProfile);
assert.equal(blocked.inspect()[0].status,'ready'); assert.match(blocked.inspect()[0].storageWarning,/tab only/);
assert.equal(normalizeController(standardPad(),STANDARD_PROFILE).buttons,0);
const baseline=rawPad().axes;
assert.equal(detectBindingCandidate(rawPad(),'Up',baseline),null,'released triggers are not hats');
const direction=rawPad(); direction.axes[6]=-1;
assert.deepEqual(detectBindingCandidate(direction,'Up',baseline),{kind:'hat',index:6,direction:0});
direction.axes[6]=baseline[6]; direction.axes[0]=-.625;
assert.deepEqual(detectBindingCandidate(direction,'stickX',baseline),{kind:'axis',index:0,rest:0,end:-1},'learn hardware sign without stretching stick travel');
const centeredDpad={buttons:[],axes:[0,0]}, heldDpad={buttons:[],axes:[-1,1]};
assert.deepEqual(detectBindingCandidate(heldDpad,'Left',centeredDpad.axes),{kind:'axis',index:0,rest:0,end:-1});
const signedDpad=structuredClone(rawProfile);
signedDpad.buttons.Up={kind:'axis',index:6,rest:0,end:-1};signedDpad.buttons.Down={kind:'axis',index:6,rest:0,end:1};
signedDpad.buttons.Left={kind:'axis',index:7,rest:0,end:-1};signedDpad.buttons.Right={kind:'axis',index:7,rest:0,end:1};
const signedRaw=rawPad();signedRaw.axes[6]=-1;signedRaw.axes.push(1);
validateProfile(signedDpad,signedRaw);assert.equal(normalizeController(signedRaw,signedDpad).buttons,10);
const pressureButtons=structuredClone(rawProfile);pressureButtons.axes.triggerL={kind:'button',index:6};pressureButtons.axes.triggerR={kind:'button',index:8};
const buttonRaw=rawPad();buttonRaw.buttons[6]={pressed:true,value:.5};
validateProfile(pressureButtons,buttonRaw);assert.deepEqual(normalizeController(buttonRaw,pressureButtons).triggers,[128,0]);
assert.equal(normalizeController(buttonRaw,pressureButtons).buttons,0,'GameCube analog button pressure is not a digital click');
pressureButtons.buttons.L={kind:'button',index:6};assert.throws(()=>validateProfile(pressureButtons,buttonRaw),/independent/);
rows=manager.sample();manager.assign(rows[0].key,-1);
assert.equal(manager.sample()[0].active,false,'unassigned adapter ports do not supply gameplay input');
const unavailable=createControllerManager({getGamepads(){throw Error('denied');},storage:null});
assert.deepEqual(unavailable.inspect(),[]);assert.match(unavailable.error,/could not read/);
console.log('Controller mappings: standard/raw buttons, sticks, trigger pressure/clicks, hats, setup gating, storage, hotplug and four-port ABI pass.');

let devices=[standardPad(0,'Unique controller')];
const reconnect=createControllerManager({getGamepads:()=>devices,storage:null});
reconnect.assign(reconnect.inspect()[0].key,2);devices=[];reconnect.sample();devices=[standardPad(8,'Unique controller')];
assert.equal(reconnect.sample()[0].port,2,'unique controller reconnect keeps explicit port');
devices=[standardPad(1,'Identical'),standardPad(2,'Identical')];reconnect.sample();devices=[];reconnect.sample();
devices=[standardPad(3,'Identical'),standardPad(4,'Identical')];
assert.deepEqual(reconnect.sample().map(r=>r.port),[-1,-1],'ambiguous reconnect requires visible assignment');

const mayflash = mayflashMacPad();
const macOptions = {getGamepads:()=>[mayflash],storage:memoryStorage(),platform:'MacIntel',userAgent:'Chrome/153.0.0.0'};
const suggested = createControllerManager(macOptions);
let suggestedRow = suggested.sample()[0];
assert.equal(suggestedRow.profileSource,'suggested');assert.equal(suggestedRow.status,'ready');
assert.equal(macOptions.storage.getItem(),null,'suggestion does not persist a manual override');
for(const [index,bits] of [[0,1024],[1,256],[2,512],[3,2048],[7,16],[9,4096],[4,64],[5,32]]) {
  set(mayflash,index); assert.equal(suggested.sample()[0].output.buttons,bits,`suggested raw button ${index}`);set(mayflash,index,0);
}
mayflash.axes[3]=0;assert.deepEqual(suggested.sample()[0].output.triggers,[128,0]);
assert.equal(suggested.sample()[0].output.buttons,0,'suggested light pressure does not invent a click');
mayflash.axes[3]=-1; mayflash.axes[5]=.625; mayflash.axes[2]=-.5;
assert.deepEqual(suggested.sample()[0].output.cstick,[80,64]);mayflash.axes[5]=0;mayflash.axes[2]=0;
for(const [direction,bits] of [[0,8],[1,10],[2,2],[3,6],[4,4],[5,5],[6,1],[7,9]]) {
  mayflash.axes[9]=-1+direction*2/7; assert.equal(suggested.sample()[0].output.buttons,bits);
}
mayflash.axes[9]=3.2857142857;assert.equal(suggested.sample()[0].output.buttons,0);
const override=structuredClone(suggestedRow.profileConfig);override.name='Manual correction';override.buttons.R={kind:'button',index:6};
suggested.saveProfile(suggestedRow.key,override);suggested.sample();set(mayflash,6);
assert.equal(suggested.sample()[0].output.buttons,32);assert.equal(suggested.sample()[0].profileSource,'saved');set(mayflash,6,0);
assert.equal(createControllerManager(macOptions).sample()[0].profile,'Manual correction');
suggested.clearProfile(suggestedRow.key);assert.equal(suggested.sample()[0].profileSource,'suggested');
for(const options of [{platform:'Win32'},{userAgent:'Firefox/143.0'},{getGamepads:()=>[rawPad()]},
  {getGamepads:()=>[{...mayflash,id:'Unknown controller'}]}, {getGamepads:()=>[{...mayflash,mapping:'standard'}]}]) {
  assert.equal(createControllerManager({...macOptions,storage:null,...options}).sample()[0].status,'needs-setup','do not apply a native mapping to an unrecognized browser layout');
}
console.log('Mayflash macOS suggestion, layout guards, independent pressure/clicks, hat axis 9 and saved override precedence pass.');
