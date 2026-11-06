// SPDX-License-Identifier: GPL-3.0-or-later
// Page-side bridge for the one-sample peer-history diagnostic. The browser
// owns the existing WebSocket peer; all native source work stays with the
// existing runtime-history controller.
import {createRuntimeMatchHistoryPeer} from '../runtime_match_history_peer.mjs';

function encode(bytes) {
  let binary = '';
  for (let offset = 0; offset < bytes.length; offset += 0x6000)
    binary += String.fromCharCode(...bytes.subarray(offset, offset + 0x6000));
  return btoa(binary);
}

function bytes(value, label) {
  if (Array.isArray(value) && value.every(byte => Number.isInteger(byte) && byte >= 0 && byte <= 255))
    return Uint8Array.from(value);
  throw Error(`${label} must be an explicit byte array`);
}

export function installRuntimePeerHistoryProbe({role, relayUrl, roomId,
  sourceIdentityJson, loadedIdentity} = {}) {
  if (globalThis.__meleeRuntimePeerHistoryProbe)
    throw Error('Runtime peer-history bridge is already installed');
  const owner = globalThis.__meleeRuntimeMatchHistory;
  if (!owner) throw Error('Existing runtime-history controller is unavailable');
  const peer = createRuntimeMatchHistoryPeer({role, relayUrl, roomId,
    timeoutMs: 5000, sampleTimeoutMs: 5000, sourceIdentityJson, loadedIdentity,
    sourceFrame: 0, validateNativePad: sample => owner.validatePad(sample)});

  const bridge = Object.freeze({
    async call(operation, value = null) {
      switch (operation) {
        case 'peerStart': {
          await peer.start();
          return peer.snapshot();
        }
        case 'peerPublish': {
          const sample = bytes(value.pad, 'Native local PAD');
          const input = owner.input();
          const local = input.raw.subarray(input.localPort * 11, (input.localPort + 1) * 11);
          if (input.inputReady !== 1 || input.correctedReady !== 0 ||
              input.localPort !== value.port || input.pollSerial !== value.pollSerial ||
              sample.length !== local.length || !sample.every((byte, i) => byte === local[i]))
            throw Error('Peer publication does not bind the actual native forward poll');
          const receipt = await peer.publishLocal({port: value.port, tick: value.tick,
            pollSerial: value.pollSerial, pad: sample});
          return {receipt, snapshot: peer.snapshot()};
        }
        case 'peerWaitRemote': {
          const receipt = await peer.waitForRemoteSample({timeoutMs: 5000});
          return {receipt, snapshot: peer.snapshot()};
        }
        case 'peerClose': {
          const snapshot = await peer.close();
          return {snapshot, closed: peer.closed};
        }
        case 'status': return owner.status();
        case 'command': return owner.command(value);
        case 'remotePad': owner.remotePad(bytes(value, 'Remote PAD')); return {accepted: true};
        case 'input': {
          const input = owner.input();
          return {schema: input.schema, bytes: input.bytes, inputReady: input.inputReady,
            correctedReady: input.correctedReady, pollSerial: input.pollSerial,
            localPort: input.localPort, remotePort: input.remotePort,
            rawBase64: encode(input.raw), forwardBase64: encode(input.forward),
            correctedBase64: encode(input.corrected)};
        }
        case 'endpoint':
          return owner.endpoint({...value, expectedSourceFrame: BigInt(value.expectedSourceFrame)});
        case 'checksum': return encode(owner.checksum());
        default: throw Error(`Unknown runtime peer-history operation ${operation}`);
      }
    },
  });
  globalThis.__meleeRuntimePeerHistoryProbe = bridge;
  return {role, peer_owner: 'browser-page', native_game_input_calls: 0,
    peer_snapshot: peer.snapshot()};
}
