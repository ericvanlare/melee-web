import {Buffer} from 'node:buffer';
import {LockstepPeer as PortableLockstepPeer} from './net_lockstep_core.mjs';

export * from './net_lockstep_core.mjs';

/** Node compatibility facade. The portable core emits Uint8Array frames;
 * existing Node callers keep the Buffer callback contract. */
export class LockstepPeer extends PortableLockstepPeer {
  constructor(options) {
    const {pushFrame, ...peerOptions} = options;
    super({...peerOptions, pushFrame: (firstTick, bytes) => pushFrame(firstTick, Buffer.from(bytes))});
  }
}

/** Node-only loopback TCP framing stays outside the browser-portable core. */
export function relayFrame(text) {
  const payload = Buffer.from(text, 'utf8');
  if (!payload.length || payload.length > 1024 * 1024) throw Error('Loopback packet exceeds its 1 MiB bound');
  const header = Buffer.alloc(4);
  header.writeUInt32BE(payload.length);
  return Buffer.concat([header, payload]);
}

export class RelayDecoder {
  constructor() { this.buffer = Buffer.alloc(0); }
  push(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    const rows = [];
    while (this.buffer.length >= 4) {
      const length = this.buffer.readUInt32BE(0);
      if (!length || length > 1024 * 1024) throw Error('Invalid loopback packet length');
      if (this.buffer.length < length + 4) break;
      rows.push(this.buffer.subarray(4, length + 4).toString('utf8'));
      this.buffer = this.buffer.subarray(length + 4);
    }
    return rows;
  }
}
