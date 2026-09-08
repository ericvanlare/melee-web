import { AudioRing } from './audio-ring.mjs';
class MeleeAudioOutput extends AudioWorkletProcessor {
  constructor() {
    super(); this.queue = new AudioRing(); this.enabled = false; this.blocks = 0;
    this.port.onmessage = ({ data }) => {
      if (data.type === 'state') { this.enabled = data.enabled; this.queue.reset(); }
      if (data.type === 'pcm' && this.enabled) {
        try { if (!this.queue.push(data.pcm)) this.port.postMessage({ error: 'Audio output queue overflow' }); }
        catch (error) { this.port.postMessage({ error: error.message }); }
      }
    };
  }
  process(_inputs, outputs) {
    const [left, right] = outputs[0];
    if (this.enabled && left && right) this.queue.consume(left, right);
    if (++this.blocks % 128 === 0) this.port.postMessage({ queued: this.queue.available, underruns: this.queue.underruns, overflows: this.queue.overflows });
    return true;
  }
}
registerProcessor('melee-audio-output', MeleeAudioOutput);
