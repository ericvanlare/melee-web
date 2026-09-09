import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

// AudioWorkletProcessor is available only inside an audio worklet global. Run
// the shipped processor in a small equivalent harness so the state handshake
// and queue reset are tested without pretending Node is a browser audio graph.
const ring = fs.readFileSync(new URL('../web/audio-ring.mjs', import.meta.url), 'utf8')
  .replace('export class AudioRing', 'class AudioRing');
const processor = fs.readFileSync(new URL('../web/audio-worklet.js', import.meta.url), 'utf8')
  .replace("import { AudioRing } from './audio-ring.mjs';", '');
const registered = {};
const context = vm.createContext({
  AudioWorkletProcessor: class {
    constructor() {
      this.port = {
        messages: [], onmessage: null,
        postMessage: message => this.port.messages.push(message),
      };
    }
  },
  registerProcessor: (name, constructor) => { registered[name] = constructor; },
});
vm.runInContext(`${ring}\n${processor}`, context, {filename: 'audio-worklet.js'});

const output = new registered['melee-audio-output']();
const pcm = values => vm.runInContext(`Float32Array.from(${JSON.stringify(values)})`, context);
const state = enabled => {
  output.port.messages.length = 0;
  output.port.onmessage({data: {type: 'state', enabled}});
  assert.equal(output.port.messages.length, 1,
    'state changes produce one acknowledgement');
  assert.equal(output.port.messages[0].type, 'state-ack',
    'state changes are acknowledged after the worklet applies them');
  assert.equal(output.port.messages[0].enabled, enabled,
    'acknowledgement carries the applied state');
};

state(true);
output.port.onmessage({data: {type: 'pcm', pcm: pcm([.25, -.25, .5, -.5])}});
assert.equal(output.queue.available, 2, 'enabled worklet accepts source PCM');

state(false);
assert.equal(output.queue.available, 0, 'disabling audio resets queued PCM before preparation');
output.port.onmessage({data: {type: 'pcm', pcm: pcm([.25, -.25])}});
assert.equal(output.queue.available, 0, 'disabled worklet rejects source PCM');

state(true);
assert.equal(output.queue.available, 0, 're-enabling starts with an empty queue');
console.log('Audio worklet state acknowledgement and preparation queue reset passed');
