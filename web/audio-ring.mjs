// Fixed-capacity stereo transport; simulation owns the source audio clock.
export class AudioRing {
  constructor(capacity = 16384, prefill = 1024) {
    if (!Number.isInteger(capacity) || capacity < 128 || !Number.isInteger(prefill) || prefill < 0 || prefill > capacity) throw new RangeError('Invalid audio queue bounds');
    this.capacity = capacity; this.prefill = prefill;
    this.data = new Float32Array(capacity * 2); this.underruns = 0; this.overflows = 0;
    this.reset();
  }
  reset() { this.read = 0; this.write = 0; this.available = 0; this.started = false; }
  push(pcm) {
    if (!(pcm instanceof Float32Array) || pcm.length % 2) throw new TypeError('Expected interleaved stereo PCM');
    const frames = pcm.length / 2;
    if (frames > this.capacity - this.available) { this.overflows += frames; return false; }
    for (const value of pcm) if (!Number.isFinite(value) || value < -1 || value > 1) throw new RangeError('Invalid source PCM sample');
    for (let i = 0; i < frames; i++) {
      const at = ((this.write + i) % this.capacity) * 2;
      this.data[at] = pcm[i * 2]; this.data[at + 1] = pcm[i * 2 + 1];
    }
    this.write = (this.write + frames) % this.capacity; this.available += frames;
    return true;
  }
  consume(left, right) {
    if (left.length !== right.length) throw new RangeError('Stereo output lengths differ');
    left.fill(0); right.fill(0);
    if (!this.started) { if (this.available < this.prefill) return; this.started = true; }
    const frames = Math.min(left.length, this.available);
    for (let i = 0; i < frames; i++) {
      const at = ((this.read + i) % this.capacity) * 2;
      left[i] = this.data[at]; right[i] = this.data[at + 1];
    }
    this.read = (this.read + frames) % this.capacity; this.available -= frames;
    if (frames < left.length) { this.underruns += left.length - frames; this.started = false; }
  }
}
