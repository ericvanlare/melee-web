// SPDX-License-Identifier: GPL-3.0-or-later
// Page-side bridge for the bounded peer-history diagnostic. The browser
// owns the existing WebSocket peer; all native source work stays with the
// existing runtime-history controller.
import {createRuntimeMatchHistoryPeer, RUNTIME_MATCH_HISTORY_EPISODE_INPUT_TICKS} from '../runtime_match_history_peer.mjs';
import {MatchInputTimeline} from '../../../scripts/net_rollback_input_timeline.mjs';

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
  sourceIdentityJson, loadedIdentity, inputTicks = 1, initialRemotePad} = {}) {
  if (globalThis.__meleeRuntimePeerHistoryProbe)
    throw Error('Runtime peer-history bridge is already installed');
  const owner = globalThis.__meleeRuntimeMatchHistory;
  if (!owner) throw Error('Existing runtime-history controller is unavailable');
  const peer = createRuntimeMatchHistoryPeer({role, relayUrl, roomId, inputTicks,
    timeoutMs: 5000, sampleTimeoutMs: 5000, sourceIdentityJson, loadedIdentity,
    sourceFrame: 0, validateNativePad: sample => owner.validatePad(sample)});

  // The transport may receive while native work is in flight. It retains its
  // bounded peer receipts, and only this serialized owner admits them to the
  // timeline between operations.
  const localPort = role === 'alpha' ? 0 : 1;
  const timeline = inputTicks === RUNTIME_MATCH_HISTORY_EPISODE_INPUT_TICKS
    ? new MatchInputTimeline({localPort, remotePort: 1-localPort,
      initialRemotePad: bytes(initialRemotePad, 'Held-prefix prediction'),
      maxHistoryTicks: 8, maxLookaheadTicks: inputTicks})
    : null;
  const padAt = (frame, port) => frame.subarray(port * 11, (port + 1) * 11);
  const same = (a, b) => a.length === b.length && a.every((byte, i) => byte === b[i]);
  const publicRow = row => ({...row, raw: [...row.raw], forward: [...row.forward], selected: [...row.selected]});
  const requireApplied = (before, after, frame, state) => {
    if (after.ticketId !== before.ticketId || after.pollSerial !== before.pollSerial ||
        after.sourceFrame !== before.sourceFrame || after.incarnation !== before.incarnation ||
        after.localPort !== before.localPort ||
        after.remotePort !== before.remotePort || after.state !== state || !same(after.raw,before.raw) ||
        !same(after.selected,frame))
      throw Error('Native episode receipt differs from the retained ticket and timeline frame');
  };
  let episodeBusy = false, episodeFailure = null;
  const episode = async (operation, value) => {
    if (!timeline || episodeBusy) throw Error('Episode timeline owner unavailable');
    episodeBusy = true;
    try {
      switch (operation) {
        case 'episodeEnable': return owner.enableEpisode();
        case 'arenaEnable': return owner.enableArenaHistory();
        case 'episodePoll': {
          const status = await owner.command('pollTicket'), ticket = owner.ticket();
          if (ticket.sourceFrame !== value.tick || ticket.ticketId !== status.completeSequence ||
              ticket.localPort !== localPort || ticket.remotePort !== 1-localPort)
            throw Error('Episode poll does not bind its requested frame, sequence and ports');
          return {status, ticket: {...ticket, raw: [...ticket.raw]}};
        }
        case 'episodePublish': {
          const row = owner.receipt(value.tick);
          if (row.ordinal !== value.tick || row.sourceFrame !== value.tick ||
              row.ticketId !== value.ticketId || row.localPort !== localPort)
            throw Error('Episode publication does not bind a retained native poll');
          return await peer.publishLocal({tick: value.tick, port: localPort, pollSerial: row.pollSerial,
            pad: padAt(row.raw, localPort)});
        }
        case 'episodeAdmit': {
          const receipt = await peer.waitForRemoteSample({tick: value.tick});
          if (receipt.protocol_input_tick !== value.tick || receipt.remote_port !== 1-localPort ||
              receipt.accepted_by !== 'lockstep-protocol')
            throw Error('Episode requires an indexed actual peer packet receipt');
          const admission = timeline.acceptRemote(value.tick, receipt.pad);
          if (admission.status !== 'accepted') throw Error('Episode remote sample admission refused');
          return {receipt, admission, timeline: timeline.status()};
        }
        case 'episodeForward': {
          const ticket = owner.ticket();
          if (ticket.ticketId !== value.ticketId || ticket.sourceFrame !== value.tick ||
              ticket.localPort !== localPort || ticket.remotePort !== 1-localPort)
            throw Error('Episode forward does not bind its actual poll ticket');
          if (timeline.recordLocal(value.tick, padAt(ticket.raw, localPort)).status !== 'accepted')
            throw Error('Episode local sample admission refused');
          const plan = timeline.planForward();
          if (plan.kind !== 'forward' || plan.tick !== value.tick ||
              (plan.remote_source === 'actual') !== value.actual)
            throw Error('Episode forward plan differs from the required authority');
          const frame = Uint8Array.from(plan.frame);
          const status = await owner.forwardTicket(ticket.ticketId, frame);
          const applied = owner.receipt(value.tick);
          requireApplied(ticket, applied, frame, 1);
          if (!same(applied.forward, frame) || status.metadata.currentFrame !== value.tick+1)
            throw Error('Episode source forward did not consume the exact ticket at the expected frame');
          return {status, row: publicRow(applied), plan, timeline: timeline.commitForward(plan)};
        }
        case 'episodeReplay': {
          const plan = timeline.planReplay();
          if (plan.kind !== 'replay' || plan.from !== value.dirty || plan.through !== value.through ||
              plan.speculative_ticks !== value.speculative || plan.frames.length !== plan.through-plan.from+1)
            throw Error('Episode replay plan differs from the required dirty suffix');
          const retained = Array.from({length: plan.through+1}, (_, tick) => owner.receipt(tick));
          const rows = [];
          for (const [tick, original] of retained.entries()) {
            const planned = tick < plan.from ? null : plan.frames[tick-plan.from];
            if (original.ordinal !== tick || original.sourceFrame !== tick || original.state < 1 ||
                (planned && planned.tick !== tick))
              throw Error('Episode replay lost its retained prefix or planned suffix');
            const frame = planned ? Uint8Array.from(planned.frame) : original.selected;
            const status = await owner.replayTicket(original.ticketId, frame);
            const applied = owner.receipt(tick);
            requireApplied(original, applied, frame, 2);
            if (!same(applied.forward, original.forward) || status.metadata.currentFrame !== tick+1)
              throw Error('Episode replay changed its original forward receipt or missed its frame');
            rows.push({status, row: publicRow(applied), unchangedPrefix: planned === null});
          }
          const status = await owner.command('finishReplay');
          if (status.metadata.currentFrame !== plan.through+1)
            throw Error('Episode replay finish did not reach its old END');
          return {status, rows, plan, timeline: timeline.commitReplay(plan)};
        }
        case 'episodeTimeline': return timeline.status();
        default: throw Error(`Unknown episode operation ${operation}`);
      }
    } catch (error) {
      episodeFailure ??= error;
      throw episodeFailure;
    } finally {
      episodeBusy = false;
    }
  };

  const bridge = Object.freeze({
    async call(operation, value = null) {
      if (episodeFailure) {
        // The timeline cannot acknowledge a source mutation after this
        // failure. Retain observation and owned teardown only.
        if (operation === 'episodeTimeline') return timeline.status();
        if (!['status', 'input', 'peerClose'].includes(operation) &&
            !(operation === 'command' && value === 'cleanup'))
          throw episodeFailure;
      }
      if (operation.startsWith('episode')) return episode(operation, value);
      if (episodeBusy) throw Error('Episode command owns the page');
      switch (operation) {
        case 'peerStart': {
          await peer.start();
          return peer.snapshot();
        }
        case 'peerPublish': {
          const sample = bytes(value.pad, 'Native local PAD');
          const input = owner.input();
          const status = owner.status();
          const local = input.raw.subarray(input.localPort * 11, (input.localPort + 1) * 11);
          if (input.inputReady !== 1 || input.correctedReady !== 0 ||
              input.localPort !== value.port || input.pollSerial !== value.pollSerial ||
              status.metadata.preFrame !== value.tick || status.expectedFrame !== value.tick ||
              sample.length !== local.length || !sample.every((byte, i) => byte === local[i]))
            throw Error('Peer publication does not bind this window\'s actual native forward poll');
          const receipt = await peer.publishLocal({port: value.port, tick: value.tick,
            pollSerial: value.pollSerial, pad: sample});
          return {receipt, snapshot: peer.snapshot()};
        }
        case 'peerWaitRemote': {
          const receipt = await peer.waitForRemoteSample({tick: value?.tick ?? 0, timeoutMs: 5000});
          return {receipt, snapshot: peer.snapshot()};
        }
        case 'peerClose': {
          const snapshot = await peer.close();
          return {snapshot, closed: peer.closed};
        }
        case 'status': return owner.status();
        case 'command': return owner.command(value);
        case 'rebase': return owner.command('rebase', value);
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
