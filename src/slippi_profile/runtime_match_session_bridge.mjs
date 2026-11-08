// SPDX-License-Identifier: GPL-3.0-or-later
// Page-side bridge for the bounded peer-history diagnostic. The browser
// owns the existing WebSocket peer; all native source work stays with the
// existing runtime-history controller.
import {createRuntimeMatchHistoryPeer, RUNTIME_MATCH_HISTORY_EPISODE_INPUT_TICKS,
  RUNTIME_MATCH_HISTORY_REBASE_INPUT_TICKS, RUNTIME_MATCH_HISTORY_PACED_INPUT_TICKS,
  RUNTIME_MATCH_HISTORY_RESPAWN_INPUT_TICKS, RUNTIME_MATCH_HISTORY_MATCH_FINAL_INPUT_TICKS, RUNTIME_MATCH_HISTORY_STREAM_INPUT_TICKS}
  from './runtime_match_history_peer.mjs';
import {MatchInputTimeline, ROLLBACK_MAX_PREDICTION_TICKS}
  from '../../scripts/net_rollback_input_timeline.mjs';

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

export function createRuntimeMatchSessionBridge({owner, role, relayUrl, roomId,
  sourceIdentityJson, loadedIdentity, inputTicks, initialRemotePad,
  deferredLocalTick = null, deferredLocalTicks, matchFinal = false, streaming = false, createEndpoint} = {}) {
  inputTicks ??= streaming ? RUNTIME_MATCH_HISTORY_STREAM_INPUT_TICKS : 1;
  if (!owner) throw Error('Existing runtime-history controller is unavailable');
  if (deferredLocalTick !== null &&
      (inputTicks !== RUNTIME_MATCH_HISTORY_PACED_INPUT_TICKS ||
       !Number.isSafeInteger(deferredLocalTick) || deferredLocalTick < 0 || deferredLocalTick >= inputTicks))
    throw Error('Deferred peer publication requires one explicit in-range tick of the paced-120 session');
  if (inputTicks === RUNTIME_MATCH_HISTORY_MATCH_FINAL_INPUT_TICKS && matchFinal !== true)
    throw Error('The 3600-input full-match peer requires final-checksum agreement');
  const peer = createRuntimeMatchHistoryPeer({role, relayUrl, roomId, inputTicks,deferredLocalTick,deferredLocalTicks,
    streaming,...(createEndpoint?{createEndpoint}:{}),
    timeoutMs: 5000, sampleTimeoutMs: 5000, sourceIdentityJson, loadedIdentity, matchFinal,
    sourceFrame: 0, validateNativePad: sample => owner.validatePad(sample)});

  // The transport may receive while native work is in flight. It retains its
  // bounded peer receipts, and only this serialized owner admits them to the
  // timeline between operations.
  const deferred = new Set(deferredLocalTicks ?? (deferredLocalTick === null ? [] : [deferredLocalTick]));
  const networkBackpressure=matchFinal&&new URL(relayUrl).protocol==='wss:';
  const localPort = role === 'alpha' ? 0 : 1;
  const arenaHistoryScope = streaming || inputTicks === RUNTIME_MATCH_HISTORY_REBASE_INPUT_TICKS ||
    inputTicks === RUNTIME_MATCH_HISTORY_PACED_INPUT_TICKS ||
    inputTicks === RUNTIME_MATCH_HISTORY_RESPAWN_INPUT_TICKS ||
    inputTicks === RUNTIME_MATCH_HISTORY_MATCH_FINAL_INPUT_TICKS;
  const timelineLookaheadTicks = inputTicks === RUNTIME_MATCH_HISTORY_EPISODE_INPUT_TICKS
    ? RUNTIME_MATCH_HISTORY_EPISODE_INPUT_TICKS
    : RUNTIME_MATCH_HISTORY_REBASE_INPUT_TICKS;
  const timeline = (arenaHistoryScope || inputTicks === RUNTIME_MATCH_HISTORY_EPISODE_INPUT_TICKS)
    ? new MatchInputTimeline({localPort, remotePort: 1-localPort,
      initialRemotePad: bytes(initialRemotePad, 'Held-prefix prediction'),
      maxHistoryTicks: ROLLBACK_MAX_PREDICTION_TICKS+1,
      maxLookaheadTicks: timelineLookaheadTicks})
    : null;
  const preFrame = () => {
    const status = owner.status();
    if (status.metadata.preFrame !== status.expectedFrame)
      throw Error('Episode source PRE does not bind the live native owner');
    return status.expectedFrame;
  };
  const ordinalAt = (tick, pre) => {
    if (!Number.isInteger(tick) || tick < pre || tick-pre >= 7)
      throw Error('Episode source frame is outside its current native PRE');
    return tick-pre;
  };
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
  let episodeBusy = false, episodeFailure = null, arenaEnabled = false, lastPollSerial = 0n;
  let pendingPrefix = null;
  // Immutable first-forward receipts bind original native polls across later
  // capsules. Current capsule rows separately record each replay selection.
  const actualOrigins = new Map();
  const terminalTailApplied = new Map();
  let pendingTerminalReplay = null, terminalTailPublished = null, nativeConsumedThrough = -1;
  const prune = () => {
    if (!streaming || !timeline) return;
    const input = timeline.status();
    if (input.plan_outstanding || pendingPrefix || pendingTerminalReplay) return;
    const live = owner.status();
    if(live.ownerState!==1)return;
    const consumed = nativeConsumedThrough;
    peer.prune({confirmedThrough:input.confirmed_through, retainedFrom:input.retained_from,
      nativeConsumedThrough:consumed, planFrom:null});
    const floor = Math.min(input.retained_from, live.metadata.preFrame);
    for (const tick of actualOrigins.keys()) if (tick < floor) actualOrigins.delete(tick);
    for (const tick of terminalTailApplied.keys())
      if (tick < Math.min(input.retained_from, consumed)) terminalTailApplied.delete(tick);
  };
  const retireOriginsBefore = frame => {
    for (const tick of actualOrigins.keys()) if (tick < frame) actualOrigins.delete(tick);
  };
  const episode = async (operation, value) => {
    if (!timeline || episodeBusy) throw Error('Episode timeline owner unavailable');
    episodeBusy = true;
    try {
      if (pendingPrefix && !['episodePrefixRebase', 'episodePrefixAdopt', 'episodeTimeline'].includes(operation))
        throw Error('Confirmed-prefix handoff owns the pending replay plan');
      switch (operation) {
        case 'episodeEnable': return owner.enableEpisode();
        case 'inlineEnable': return owner.enableInlineDispatch();
        case 'arenaEnable': {
          const enabled = owner.enableArenaHistory();
          arenaEnabled = true;
          return enabled;
        }
        case 'episodeRebase': {
          const status = timeline.status();
          if (!arenaHistoryScope || !arenaEnabled || !Number.isInteger(value) || value <= preFrame() ||
              status.confirmed_through !== value-1 || status.simulated_through !== value-1 ||
              status.dirty_from !== null || status.plan_outstanding || status.terminal || status.disposed)
            throw Error('Arena episode rebase requires the fully confirmed selected END');
          const result = await owner.command('rebase', value);
          retireOriginsBefore(value);
          return {...result, timeline: timeline.status()};
        }
        case 'episodePoll': {
          ordinalAt(value.tick, preFrame());
          const status = await owner.command('pollTicket'), ticket = owner.ticket();
          if (ticket.sourceFrame !== value.tick || ticket.ticketId !== status.completeSequence ||
              ticket.localPort !== localPort || ticket.remotePort !== 1-localPort ||
              BigInt(ticket.pollSerial) <= lastPollSerial)
            throw Error('Episode poll does not bind its requested frame, sequence and ports');
          lastPollSerial = BigInt(ticket.pollSerial);
          return {status, ticket: {...ticket, raw: [...ticket.raw]}};
        }
        case 'episodePublish': {
          const ordinal = ordinalAt(value.tick, preFrame());
          const deferSend = value?.deferSend === true;
          if (deferSend !== deferred.has(value.tick))
            throw Error('Episode publication defer policy differs from its explicitly configured source tick');
          const row = owner.receipt(ordinal);
          if (row.ordinal !== ordinal || row.sourceFrame !== value.tick ||
              row.ticketId !== value.ticketId || row.localPort !== localPort)
            throw Error('Episode publication does not bind a retained native poll');
          const receipt = await peer.publishLocal({tick: value.tick, port: localPort, pollSerial: row.pollSerial,
            pad: padAt(row.raw, localPort), deferSend});
          return deferSend ? {receipt, deferred_send: true} : receipt;
        }
        case 'episodeReleaseLocal': {
          const tick = value?.tick;
          if (!deferred.has(tick) || !Number.isSafeInteger(tick))
            throw Error('Episode deferred publication release differs from its configured source tick');
          const origin = actualOrigins.get(tick);
          if (!origin || origin.sourceFrame !== tick || origin.localPort !== localPort ||
              origin.pollSerial !== lastPollSerial.toString(10))
            throw Error('Deferred network release lost the original native source poll identity');
          const receipt = await peer.releaseDeferredLocal(tick);
          if (receipt.protocol_input_tick !== tick || receipt.local_port !== localPort ||
              receipt.poll_serial !== origin.pollSerial ||
              !same(Uint8Array.from(receipt.pad), padAt(origin.raw, localPort)))
            throw Error('Deferred network release changed its original actual local PAD sample');
          return {receipt, source: publicRow(origin)};
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
          const ordinal = ordinalAt(value.tick, preFrame());
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
          const applied = owner.receipt(ordinal);
          requireApplied(ticket, applied, frame, 1);
          const terminal=status.metadata.phase===29;
          if (!same(applied.forward, frame) ||
              (terminal?status.metadata.currentFrame!==value.tick:
                status.metadata.currentFrame!==value.tick+1))
            throw Error('Episode source forward did not consume the exact ticket at its actual source boundary');
          let terminalReceipt=null;
          if(terminal){
            terminalReceipt=owner.terminalReceipt();
            const selectedOrdinal=status.metadata.preFrame+status.metadata.forwardSteps;
            if(terminalReceipt.flags!==3||terminalReceipt.selectedOrdinal!==selectedOrdinal||
               terminalReceipt.selectedSourceFrame!==value.tick||terminalReceipt.ticketId!==ticket.ticketId||
               terminalReceipt.pollSerial!==ticket.pollSerial||terminalReceipt.localPort!==localPort||
               terminalReceipt.remotePort!==1-localPort||terminalReceipt.selectedOrdinal!==value.tick+1)
              throw Error('Native first terminal receipt does not bind its exact actual source poll and ordinal');
          }
          if (actualOrigins.has(value.tick)) throw Error('Actual first-forward receipt already exists');
          actualOrigins.set(value.tick, applied);
          nativeConsumedThrough=value.tick;
          return {status, row: publicRow(applied), plan, terminal:terminalReceipt,
            timeline: timeline.commitForward(plan)};
        }
        case 'episodeReplay': {
          const plan = timeline.planReplay();
          if(matchFinal&&(plan.speculative_ticks!==0||
              timeline.status().remote_contiguous_through<plan.through))
            throw Error('Full-match replay requires actual remote input through its old frontier');
          if (plan.kind !== 'replay' || plan.from !== value.dirty || plan.through !== value.through ||
              plan.speculative_ticks !== value.speculative || plan.frames.length !== plan.through-plan.from+1)
            throw Error('Episode replay plan differs from the required dirty suffix');
          const pre = preFrame();
          const lastOrdinal = ordinalAt(plan.through, pre);
          if (plan.from < pre)
            throw Error('Episode replay correction precedes its current native PRE');
          const retained = Array.from({length: lastOrdinal+1}, (_, ordinal) => owner.receipt(ordinal));
          const rows = [];
          for (const [ordinal, original] of retained.entries()) {
            const tick = pre+ordinal;
            const planned = tick < plan.from ? null : plan.frames[tick-plan.from];
            if (original.ordinal !== ordinal || original.sourceFrame !== tick || original.state < 1 ||
                (planned && planned.tick !== tick))
              throw Error('Episode replay lost its retained prefix or planned suffix');
            const frame = planned ? Uint8Array.from(planned.frame) : original.selected;
            const status = await owner.replayTicket(original.ticketId, frame);
            const applied = owner.receipt(ordinal);
            requireApplied(original, applied, frame, 2);
            if (!same(applied.forward, original.forward))
              throw Error('Episode replay changed its original forward receipt');
            if(status.metadata.phase===11&&status.metadata.currentFrame===tick){
              if(!matchFinal||!planned)
                throw Error('Terminal replay selected an ending outside its changed actual suffix');
              const receipt=owner.terminalReceipt();
              const selectedOrdinal=pre+status.metadata.replaySteps;
              if(receipt.flags!==3||receipt.selectedOrdinal!==selectedOrdinal||
                 receipt.selectedSourceFrame!==tick||receipt.ticketId!==original.ticketId||
                 receipt.pollSerial!==original.pollSerial||receipt.localPort!==localPort||
                 receipt.remotePort!==1-localPort||selectedOrdinal!==tick+1)
                throw Error('Replay terminal receipt does not bind its selected retained source row');
              pendingTerminalReplay={plan,pre,frontier:plan.through+1,terminal:receipt};
              rows.push({status,row:publicRow(applied),unchangedPrefix:planned===null,terminal:true});
              return {status,rows,plan,terminal:receipt,timeline:timeline.status()};
            }
            if(status.metadata.currentFrame!==tick+1)
              throw Error('Episode replay did not reach its expected source frame');
            rows.push({status, row: publicRow(applied), unchangedPrefix: planned === null});
          }
          const status = await owner.command('finishReplay');
          if (status.metadata.currentFrame !== plan.through+1)
            throw Error('Episode replay finish did not reach its old END');
          nativeConsumedThrough=plan.through;
          return {status, rows, plan, timeline: timeline.commitReplay(plan)};
        }
        case 'episodePrefixBegin': {
          const pre = preFrame(), state = timeline.status();
          const c = value.confirmedEnd, frontier = value.through+1;
          if (!arenaEnabled || !arenaHistoryScope || !Number.isInteger(c) || c <= pre || c >= frontier ||
              state.remote_contiguous_through !== c-1 || state.simulated_through !== frontier-1 ||
              state.dirty_from !== value.dirty ||
              (value.dirty !== null && (value.dirty >= c || value.dirty < pre)) ||
              owner.status().metadata.phase !== 11)
            throw Error('Confirmed-prefix replay requires actual authority through C and restored old PRE');
          const plan = timeline.planConfirmedPrefix({preFrame: pre, confirmedEnd: c});
          if (plan.kind !== 'confirmed-prefix' || plan.from !== pre || plan.through !== value.through ||
              plan.confirmed_end !== c || plan.frames.length !== frontier-pre ||
              plan.speculative_ticks !== frontier-c || plan.speculative_ticks !== value.speculative)
            throw Error('Confirmed-prefix plan differs from its actual source interval');
          const originals = Array.from({length: ordinalAt(frontier-1, pre)+1},
            (_, ordinal) => owner.receipt(ordinal));
          const origins = Array.from({length: frontier-c}, (_, ordinal) => {
            const row = actualOrigins.get(c+ordinal);
            if (!row) throw Error('Confirmed-prefix suffix lost its actual first-forward receipt');
            return row;
          });
          pendingPrefix = {pre, c, frontier, plan, originals, origins, rows: []};
          for (let tick=pre; tick<c; ++tick) {
            const original = originals[tick-pre], planned = plan.frames[tick-pre];
            if (original.sourceFrame !== tick || original.ordinal !== tick-pre || original.state < 1 ||
                !planned || planned.tick !== tick || planned.remote_source !== 'actual' || planned.speculative)
              throw Error('Confirmed prefix lost its authoritative frame or retained original input');
            const frame = Uint8Array.from(planned.frame);
            const status = await owner.replayTicket(original.ticketId, frame);
            const applied = owner.receipt(tick-pre);
            requireApplied(original, applied, frame, 2);
            if (!same(applied.forward, original.forward) || status.metadata.currentFrame !== tick+1)
              throw Error('Confirmed-prefix execution changed original input or missed its source frame');
            pendingPrefix.rows.push({status, row: publicRow(applied),
              unchangedPrefix: state.dirty_from === null || tick < state.dirty_from});
          }
          const status = await owner.command('finishPrefix', c);
          const handoff = owner.prefixHandoff();
          if (handoff.oldPreFrame !== pre || handoff.confirmedFrame !== c ||
              handoff.selectedEndFrame !== frontier || handoff.suffixCount !== frontier-c ||
              handoff.suffixCursor !== 0 || status.metadata.currentFrame !== c)
            throw Error('Native prefix handoff differs from actual replay interval');
          return {status, rows: pendingPrefix.rows, handoff, plan, timeline: timeline.status()};
        }
        case 'episodePrefixRebase': {
          if (!pendingPrefix || value !== pendingPrefix.c || owner.status().metadata.phase !== 26 ||
              !timeline.status().plan_outstanding)
            throw Error('Confirmed-prefix rebase requires its retired old owner and pending replay plan');
          const result = await owner.command('rebaseConfirmedPrefix', value);
          return {...result, handoff: owner.prefixHandoff(), timeline: timeline.status()};
        }
        case 'episodePrefixAdopt': {
          if (!pendingPrefix || preFrame() !== pendingPrefix.c ||
              owner.status().metadata.currentFrame !== pendingPrefix.c ||
              owner.status().metadata.phase !== 4 || !timeline.status().plan_outstanding)
            throw Error('Confirmed-prefix adoption requires the exact newly armed PRE');
          const {pre, c, frontier, plan, origins} = pendingPrefix;
          const rows = [];
          for (let tick=c; tick<frontier; ++tick) {
            const original = origins[tick-c], origin = owner.prefixOrigin(tick-c);
            if (origin.ticketId !== original.ticketId || origin.pollSerial !== original.pollSerial ||
                origin.sourceFrame !== tick || origin.incarnation !== original.incarnation ||
                origin.localPort !== original.localPort || origin.remotePort !== original.remotePort ||
                origin.state !== original.state || !same(origin.raw, original.raw) ||
                !same(origin.forward, original.forward) || !same(origin.selected, original.selected))
              throw Error('Native handoff origin differs from the original suffix poll receipt');
            const adopted = await owner.command('adoptNextPrefixTicket');
            const ticket = owner.adoptedTicket(origin);
            const planned = plan.frames[tick-pre];
            if (planned?.tick !== tick) throw Error('Retained suffix lost its pending replay frame');
            const frame = Uint8Array.from(planned.frame);
            const status = await owner.forwardTicket(ticket.ticketId, frame);
            const applied = owner.receipt(tick-c);
            requireApplied(ticket, applied, frame, 1);
            if (!same(applied.forward, frame) || status.metadata.currentFrame !== tick+1)
              throw Error('Retained suffix failed exact source execution');
            rows.push({adopted, status, origin: publicRow(origin), row: publicRow(applied)});
          }
          const handoff = owner.prefixHandoff();
          if (handoff.suffixCursor !== handoff.suffixCount || owner.status().metadata.currentFrame !== frontier)
            throw Error('Confirmed-prefix suffix did not return to its original frontier');
          const status = owner.status(), committed = timeline.commitConfirmedPrefix(plan);
          retireOriginsBefore(c);
          pendingPrefix = null;nativeConsumedThrough=frontier-1;
          return {status, rows, handoff, timeline: committed};
        }
        case 'episodeTerminalSealReplay': {
          if(!matchFinal||!pendingTerminalReplay||!timeline.status().plan_outstanding)
            throw Error('Terminal replay seal requires its outstanding actual replay plan');
          const {terminal}=pendingTerminalReplay;
          return await owner.command('finishPrefix',terminal.selectedSourceFrame);
        }
        case 'episodeTerminalBegin': {
          if(!matchFinal||!arenaEnabled||pendingPrefix)
            throw Error('Terminal tail requires the active arena match without another handoff');
          const status=owner.status(),state=timeline.status();
          const receipt=owner.terminalReceipt();
          const selectedOrdinal=receipt.selectedOrdinal;
          const retainedPlan=pendingTerminalReplay;
          if(status.ownerState!==1||status.metadata.phase!==35||status.lastCompletedCommand!==9||
             receipt.flags!==3||!Number.isSafeInteger(selectedOrdinal)||selectedOrdinal<=0||
             receipt.selectedSourceFrame!==status.metadata.currentFrame||
             (retainedPlan?
               (!state.plan_outstanding||state.simulated_through!==retainedPlan.plan.through||
                state.remote_contiguous_through<retainedPlan.plan.through||
                retainedPlan.frontier!==retainedPlan.plan.through+1||
                retainedPlan.plan.kind!=='replay'||
                retainedPlan.plan.frames.some(row=>row.tick>=selectedOrdinal&&
                  (row.remote_source!=='actual'||row.speculative))):
               (state.simulated_through!==selectedOrdinal-1||state.confirmed_through!==selectedOrdinal-1||
                state.dirty_from!==null||state.plan_outstanding))||
             state.terminal||state.disposed)
            throw Error('Terminal tail requires the exact retired selected terminal traversal');
          const actual=actualOrigins.get(receipt.selectedSourceFrame);
          if(!actual||actual.sourceFrame!==status.metadata.currentFrame||
             actual.ticketId!==receipt.ticketId||actual.pollSerial!==receipt.pollSerial||
             actual.localPort!==receipt.localPort||actual.remotePort!==receipt.remotePort||actual.state<1)
            throw Error('Terminal selection has no retained actual first-forward poll receipt');
          const begun=await owner.terminalTailBegin(selectedOrdinal);
          const descriptor=owner.terminalTailDescriptor();
          if(descriptor.selectedOrdinal!==selectedOrdinal||descriptor.sourceFrame!==status.metadata.currentFrame||
             descriptor.selectedTicketId!==actual.ticketId||descriptor.selectedPollSerial!==actual.pollSerial||
             descriptor.localPort!==localPort||descriptor.remotePort!==1-localPort||
             descriptor.suffixCount!==(retainedPlan?retainedPlan.frontier-selectedOrdinal:0))
            throw Error('Native terminal-tail descriptor differs from its actual selected poll');
          if(retainedPlan&&descriptor.suffixCount===0){
            if(selectedOrdinal!==retainedPlan.frontier)
              throw Error('Terminal replay plan cannot commit before its old frontier');
            const committed=timeline.commitReplay(retainedPlan.plan);
            pendingTerminalReplay=null;
            return {status:begun,descriptor,timeline:committed,selected:publicRow(actual),
              retained_suffix_complete:true};
          }
          return {status:begun,descriptor,timeline:timeline.status(),selected:publicRow(actual)};
        }
        case 'episodeTerminalTailTicket': {
          if(!matchFinal||owner.status().metadata.phase!==36||pendingPrefix||
             owner.terminalTailDescriptor().consumedOrdinal>=inputTicks)
            throw Error('Terminal-tail ticket requires its active owner and remaining input capacity');
          const status=await owner.requestTerminalTailTicket();
          const descriptor=owner.terminalTailDescriptor();
          const ticket=owner.terminalTailTicket();
          if(status.metadata.phase!==37||ticket.inputOrdinal!==descriptor.consumedOrdinal||
             ticket.sourceFrame!==descriptor.sourceFrame||ticket.localPort!==localPort||
             ticket.remotePort!==1-localPort||ticket.requestSequence!==status.completeSequence||
             (ticket.flags===0&&BigInt(ticket.pollSerial)<=lastPollSerial))
            throw Error('Terminal-tail ticket does not bind the cumulative ordinal and actual native poll');
          return {status,descriptor,ticket:{...ticket,rawPad:[...ticket.rawPad],
            appliedPad:[...ticket.appliedPad]}};
        }
        case 'episodeTerminalTailPublish': {
          const status=owner.status(),ticket=owner.terminalTailTicket();
          const tick=ticket.inputOrdinal,localPad=padAt(ticket.rawPad,localPort);
          if(!matchFinal||status.metadata.phase!==37||terminalTailPublished||pendingTerminalReplay||
             ticket.flags!==0||ticket.ticketId!==ticket.requestSequence||
             ticket.requestSequence!==status.completeSequence||ticket.originTicketId||
             ticket.originSourceFrame||ticket.originIncarnation||BigInt(ticket.pollSerial)<=lastPollSerial)
            throw Error('Terminal-tail actual poll publication lost its owner or poll order');
          const receipt=await peer.publishLocal({port:localPort,tick,pollSerial:ticket.pollSerial,
            pad:localPad,deferSend:false});
          if(receipt.protocol_input_tick!==tick||receipt.poll_serial!==ticket.pollSerial||
             !same(Uint8Array.from(receipt.pad),localPad))
            throw Error('Terminal-tail local publication differs from its actual native poll');
          terminalTailPublished={tick,requestSequence:ticket.requestSequence,pollSerial:ticket.pollSerial};
          lastPollSerial=BigInt(ticket.pollSerial);
          return receipt;
        }
        case 'episodeTerminalTailForward': {
          if(!arenaEnabled||pendingPrefix)
            throw Error('Terminal-tail forward requires the active arena terminal owner');
          const descriptor=owner.terminalTailDescriptor(),ticket=owner.terminalTailTicket();
          const tick=ticket.inputOrdinal;
          if(ticket.sourceFrame!==descriptor.sourceFrame||tick!==descriptor.consumedOrdinal||
             ticket.localPort!==localPort||ticket.remotePort!==1-localPort||
             ticket.requestSequence!==owner.status().completeSequence)
            throw Error('Terminal-tail forward lost its exact distinct source/tick cursor');
          const localPad=padAt(ticket.rawPad,localPort);
          let plan,frame,retainedSuffixPlan=null;
          if(pendingTerminalReplay&&tick<pendingTerminalReplay.frontier){
            retainedSuffixPlan=pendingTerminalReplay;
            if(ticket.flags!==1)
              throw Error('Retained terminal replay suffix requires its original adopted ticket');
            plan=retainedSuffixPlan.plan.frames.find(row=>row.tick===tick);
            const remote=peer.remoteSnapshot(tick);
            const local=peer.snapshot().local_contributions.find(row=>row.protocol_input_tick===tick);
            const origin=actualOrigins.get(tick);
            if(!plan||plan.remote_source!=='actual'||plan.speculative||!remote||
               remote.protocol_input_tick!==tick||remote.remote_port!==1-localPort||
               remote.accepted_by!=='lockstep-protocol'||!local||!origin||
               origin.ticketId!==ticket.originTicketId||origin.sourceFrame!==ticket.originSourceFrame||
               origin.incarnation!==ticket.originIncarnation||origin.pollSerial!==ticket.pollSerial||
               !same(origin.raw,ticket.rawPad)||local.local_port!==localPort||local.poll_serial!==ticket.pollSerial||
               !same(Uint8Array.from(local.pad),localPad))
              throw Error('Retained terminal suffix lost its exact already-confirmed peer rows');
            frame=Uint8Array.from(plan.frame);
            if(!same(padAt(frame,localPort),localPad)||
               !same(padAt(frame,1-localPort),Uint8Array.from(remote.pad)))
              throw Error('Retained terminal suffix replay plan differs from its immutable peer inputs');
          }else{
            if(pendingTerminalReplay||ticket.flags===1)
              throw Error('Terminal suffix adoption ended before the retained old frontier');
            if(terminalTailPublished?.tick!==tick||
               terminalTailPublished.requestSequence!==ticket.requestSequence||
               terminalTailPublished.pollSerial!==ticket.pollSerial)
              throw Error('Terminal-tail forward requires its single actual local publication');
            if(!peer.remoteSnapshot(tick))return {waiting:true,tick};
            while(timeline.status().remote_contiguous_through<tick){
              const remoteTick=timeline.status().remote_contiguous_through+1;
              const receipt=peer.remoteSnapshot(remoteTick);
              if(receipt?.protocol_input_tick!==remoteTick||receipt.remote_port!==1-localPort||
                 receipt.accepted_by!=='lockstep-protocol')
                throw Error('Terminal-tail frame requires the exact indexed remote PAD receipt');
              const accepted=timeline.acceptRemote(remoteTick,receipt.pad);
              if(accepted.status!=='accepted')
                throw Error('Terminal-tail remote PAD timeline admission refused');
              if(timeline.status().dirty_from!==null)
                throw Error(`Terminal-tail actual PAD changed an executed input at ${timeline.status().dirty_from}`);
            }
            const localAdmission=timeline.recordLocal(tick,localPad);
            if(!['accepted','duplicate'].includes(localAdmission.status))
              throw Error('Terminal-tail local PAD timeline admission refused');
            plan=timeline.planForward();
            if(plan.kind!=='forward'||plan.tick!==tick||plan.remote_source!=='actual'||plan.speculative_ticks!==0)
              throw Error('Terminal-tail source step requires the actual two-peer PAD row');
            frame=Uint8Array.from(plan.frame);
          }
          owner.terminalTailSetRemotePad(padAt(frame,1-localPort));
          const status=await owner.forwardTerminalTailTicket();
          const applied=owner.terminalTailTicket(),after=owner.terminalTailDescriptor();
          if(status.metadata.phase!==38||status.metadata.currentFrame!==descriptor.sourceFrame||
             applied.inputOrdinal!==tick||applied.sourceFrame!==descriptor.sourceFrame||
             !same(applied.appliedPad,frame)||after.consumedOrdinal!==tick+1||
             after.sourceFrame!==descriptor.sourceFrame)
            throw Error('Native terminal-tail traversal changed source frame or exact applied PAD');
          let committed;
          if(retainedSuffixPlan){
            if(after.suffixCursor===after.suffixCount){
              if(tick+1!==retainedSuffixPlan.frontier||
                 after.consumedOrdinal!==retainedSuffixPlan.frontier)
                throw Error('Native terminal suffix did not consume the retained replay plan to its old frontier');
              committed=timeline.commitReplay(retainedSuffixPlan.plan);
              pendingTerminalReplay=null;
            }else{
              if(tick+1>=retainedSuffixPlan.frontier)
                throw Error('Native terminal suffix has an unconsumed replay-plan row at its frontier');
              committed=timeline.status();
            }
          }else{
            committed=timeline.commitForward(plan);
            terminalTailPublished=null;
            if(committed.simulated_through!==tick||committed.dirty_from!==null)
              throw Error('Terminal-tail timeline acknowledgment lost its cumulative input ordinal');
          }
          terminalTailApplied.set(tick,frame.slice());
          nativeConsumedThrough=tick;
          return {status,ticket:{...applied,rawPad:[...applied.rawPad],appliedPad:[...applied.appliedPad]},
            remoteReceipt:peer.remoteSnapshot(tick),plan,timeline:committed,descriptor:after};
        }
        case 'episodeTerminalTailPresent': {
          const status=await owner.presentTerminalTail();
          const presentation=owner.presentation();
          if(status.metadata.phase!==39||presentation.command!==29||
             presentation.sourceFrameAfter!==status.metadata.currentFrame)
            throw Error('Terminal-tail present did not retain its actual source draw receipt');
          const idle=await owner.command('idle');
          const descriptor=owner.terminalTailDescriptor();
          if(![36,40].includes(idle.metadata.phase)||idle.metadata.currentFrame!==descriptor.sourceFrame||
             idle.gpuPending||idle.endpointActive)
            throw Error('Terminal-tail idle failed its exact native present completion boundary');
          return {status,idle,presentation,descriptor,complete:idle.metadata.phase===40};
        }
        case 'episodeTerminalFinish': {
          const final=owner.terminalTailFinal(),receipt=final.receipt,ticket=final.ticket;
          const tick=receipt.lastInputOrdinal,descriptor=owner.terminalTailDescriptor();
          const timelineState=timeline.status(),frame=terminalTailApplied.get(tick);
          const peerState=peer.snapshot();
          const local=peerState.local_contributions.find(row=>row.protocol_input_tick===tick);
          const remote=peerState.remote_contributions.find(row=>row.protocol_input_tick===tick);
          if(receipt.consumedOrdinal!==tick+1||descriptor.consumedOrdinal!==tick+1||
             descriptor.sourceFrame!==receipt.sourceFrame||timelineState.simulated_through!==tick||
             timelineState.remote_contiguous_through<tick||timelineState.dirty_from!==null||
             timelineState.plan_outstanding||terminalTailPublished||pendingTerminalReplay||!frame||!local||!remote||
             !same(Uint8Array.from(local.pad),padAt(ticket.rawPad,localPort))||
             !same(Uint8Array.from(remote.pad),padAt(ticket.appliedPad,1-localPort))||
             !same(ticket.appliedPad,frame))
            throw Error('Native final receipt lacks the exact applied PAD and actual peer rows at its final ordinal');
          const agreement=await peer.publishMatchFinal({inputOrdinal:tick,sourceFrame:receipt.sourceFrame,
            outcome:receipt.outcome,winner:receipt.winner,checksum:final.checksum});
          const status=await owner.finishTerminalTail(agreement);
          if(status.ownerState!==2||status.metadata.phase!==41||status.lastCompletedCommand!==30)
            throw Error('Native terminal-tail finish did not return control to the original Results flow');
          return {status,receipt,checksum:[...final.checksum],agreement,
            timeline:timeline.status(),peer:peer.snapshot()};
        }
        case 'episodeAwaitNetworkInput': {
          const {kind,tick,requiredThrough}=value;
          const input=timeline.status();
          if(!networkBackpressure||!arenaEnabled||input.plan_outstanding||input.terminal||input.disposed||
             ![1,2,3,4].includes(kind)||!Number.isSafeInteger(tick)||tick<0||tick>=inputTicks||
             !Number.isSafeInteger(requiredThrough)||requiredThrough<tick||requiredThrough>=inputTicks||
             tick!==input.remote_contiguous_through+1||
             (kind===4&&(requiredThrough!==tick||terminalTailPublished?.tick!==tick||
               owner.status().metadata.phase!==37)))
            throw Error('Network wait does not bind the earliest required actual input');
          const validate=receipt=>{
            if(receipt?.accepted_by!=='lockstep-protocol'||receipt.protocol_input_tick!==tick||
               receipt.remote_port!==1-localPort)
              throw Error('Network wait received an unrelated peer PAD receipt');
            return receipt;
          };
          const startedAt=performance.now();
          const available=peer.remoteSnapshot(tick);
          if(available){
            const receipt=validate(available),observedAt=performance.now();
            return {receipt,excluded:false,timing:{tick,startedAt,observedAt,endedAt:observedAt,excludedMs:0}};
          }
          owner.pacingWaitBegin(kind,tick);
          const receipt=validate(await peer.waitForRemoteSample({tick}));
          const observedAt=performance.now();
          const clock=owner.pacingWaitEnd(kind,tick);
          return {receipt,excluded:true,clock,timing:{tick,startedAt,observedAt,
            endedAt:performance.now(),excludedMs:clock.excludedMs}};
        }
        case 'episodeTimeline': return timeline.status();
        default: throw Error(`Unknown episode operation ${operation}`);
      }
    } catch (error) {
      episodeFailure ??= error;
      throw episodeFailure;
    } finally {
      episodeBusy = false;
      if (!episodeFailure)try{prune();}catch(error){episodeFailure=error;throw error;}
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
      if (operation.startsWith('episode') || operation === 'arenaEnable' || operation === 'inlineEnable')
        return episode(operation, value);
      if (episodeBusy) throw Error('Episode command owns the page');
      if (pendingPrefix && operation === 'command' &&
          !['capture', 'beginEpisode', 'closePrefix', 'retirePrefix', 'cleanup'].includes(value))
        throw Error('Confirmed-prefix handoff forbids presenting or changing its pending replay');
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
        case 'peerRemoteSnapshot': return peer.remoteSnapshot(value?.tick??0);
        case 'peerClose': {
          const snapshot = await peer.close();
          return {snapshot, closed: peer.closed};
        }
        case 'status': return owner.status();
        case 'command': return owner.command(value);
        case 'rebase':
          if (timeline) throw Error('Episode rebase requires its page-owned timeline');
          return owner.command('rebase', value);
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
  return Object.freeze({...bridge, snapshot:()=>peer.snapshot(), close:()=>peer.close(),
    role, inputTicks, streaming});
}
