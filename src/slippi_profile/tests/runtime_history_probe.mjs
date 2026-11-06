// SPDX-License-Identifier: GPL-3.0-or-later
// One same-input ordinary-runtime restore control. The caller owns the page,
// browser, profile, server, held-start admission, and failure cleanup.
import {
  CPU_ENDPOINT_PROVENANCE, CPU_ENDPOINT_VIEW,
  compareCpuParticleEndpoints, compareCpuReferenceParticleEndpoints,
  decodeCpuParticleEndpointResult,
} from './cpu_endpoint.mjs';
import {
  requireCpuHistoryMotor,
} from './cpu_history_runner.mjs';
import {
  compareCpuCorrectedLoopMotorEvents,
  decodeCpuHistoryMotorEvidence,
  decodeCpuCorrectedLoopMotorCommitReceipt,
} from './cpu_corrected_loop_runner.mjs';
import {validateRuntimeMatchStartPrefix} from '../runtime_match_history_controller.mjs';

const PAD_BYTES = 11;
const FRAME_BYTES = 44;
const PORTS = Object.freeze({local: 0, remote: 1});
const PHASE = Object.freeze({
  capturedPre: 2, armedPre: 4, predictedSealed: 5, predictedCpu: 6,
  predictedDrawPending: 7, predictedDrawIdle: 8, predictedComplete: 9,
  restoredPrePending: 10, restoredPre: 11, correctedSealed: 12,
  correctedCpu: 13, correctedDrawPending: 14, correctedDrawIdle: 15,
  correctedComplete: 16, closed: 17, retired: 18,
});

const encodeBase64 = value => Buffer.from(value).toString('base64');
const decodeBase64 = (value, label) => {
  if (typeof value !== 'string') throw new Error(`${label} is missing`);
  return Buffer.from(value, 'base64');
};
const bytesEqual = (left, right) => Buffer.from(left).equals(Buffer.from(right));

function padFromHex(hex, label) {
  if (typeof hex !== 'string' || !/^[0-9a-f]{88}$/.test(hex))
    throw new Error(`${label} is not a literal 44-byte PAD row`);
  return Buffer.from(hex, 'hex');
}

function remotePortFromHeldPrefix(prefix) {
  const row = prefix.rows.at(-1);
  const fullPad = padFromHex(row.pad_hex, 'Held-prefix final PAD');
  return fullPad.subarray(PORTS.remote * PAD_BYTES, (PORTS.remote + 1) * PAD_BYTES);
}

function overwritePadPort(frame, port, pad) {
  if (frame.length !== FRAME_BYTES || pad.length !== PAD_BYTES)
    throw new Error('Runtime history PAD comparison extent');
  const result = Buffer.from(frame);
  pad.copy(result, port * PAD_BYTES);
  return result;
}

function firstDifference(left, right) {
  let offset = 0;
  while (offset < left.length && left[offset] === right[offset]) offset++;
  return offset;
}

function requireStatus(status, label, {phase, sourceFrame, restoreCount,
  forwardSteps, replaySteps, gpuIdle} = {}) {
  if (!status || status.schema !== 1 || status.bytes !== 128 ||
      status.ownerState !== 1 || status.firstFailure !== 0 ||
      status.pendingCommand !== 0 || status.completeSequence !== status.requestSequence ||
      status.rendererErrors !== 0 || !status.metadata || status.metadata.schema !== 1 ||
      status.metadata.bytes !== 64 || status.metadata.firstFailure !== 0 ||
      status.metadata.preFrame !== 0 || status.endpointActive !== 0)
    throw new Error(`${label} native runtime-history status/metadata is not healthy`);
  const checks = [
    ['phase', phase], ['currentFrame', sourceFrame], ['restoreCount', restoreCount],
    ['forwardSteps', forwardSteps], ['replaySteps', replaySteps],
  ];
  for (const [field, expected] of checks)
    if (expected !== undefined && status.metadata[field] !== expected)
      throw new Error(`${label} native metadata ${field}=${status.metadata[field]}, expected ${expected}`);
  if (gpuIdle !== undefined && status.gpuPending !== (gpuIdle ? 0 : 1))
    throw new Error(`${label} native GPU-pending state differs from the requested boundary`);
  return status;
}

function requireInput(receipt, status, label, {expectedLocalPad, expectedRemotePad,
  requireCorrected = false, priorPollSerial} = {}) {
  const raw = decodeBase64(receipt?.rawBase64, `${label} raw PAD`);
  const forward = decodeBase64(receipt?.forwardBase64, `${label} forward PAD`);
  const corrected = decodeBase64(receipt?.correctedBase64, `${label} corrected PAD`);
  if (receipt.schema !== 1 || receipt.bytes !== 168 || receipt.inputReady !== 1 ||
      receipt.localPort !== PORTS.local || receipt.remotePort !== PORTS.remote ||
      raw.length !== FRAME_BYTES || forward.length !== FRAME_BYTES || corrected.length !== FRAME_BYTES ||
      receipt.pollSerial !== status.metadata.pollSerial ||
      (priorPollSerial !== undefined && BigInt(receipt.pollSerial) <= BigInt(priorPollSerial)))
    throw new Error(`${label} native PAD receipt does not bind its owner/poll`);
  const local = raw.subarray(PORTS.local * PAD_BYTES, (PORTS.local + 1) * PAD_BYTES);
  const remote = raw.subarray(PORTS.remote * PAD_BYTES, (PORTS.remote + 1) * PAD_BYTES);
  if (!bytesEqual(local, expectedLocalPad))
    throw new Error(`${label} actual keyboard P1 PAD differs from the caller-supplied action bytes`);
  if (expectedRemotePad && !bytesEqual(remote, expectedRemotePad))
    throw new Error(`${label} actual raw P2 PAD differs from the held prefix's literal final row`);
  const expectedForward = overwritePadPort(raw, PORTS.remote, expectedRemotePad);
  if (!bytesEqual(forward, expectedForward))
    throw new Error(`${label} forward PAD differs outside the one held-prefix P2 contribution`);
  if (requireCorrected) {
    const expectedCorrected = overwritePadPort(raw, PORTS.remote, expectedRemotePad);
    if (receipt.correctedReady !== 1 || !bytesEqual(corrected, expectedCorrected))
      throw new Error(`${label} corrected PAD differs from retained P1 plus the same actual P2 row`);
  } else if (receipt.correctedReady !== 0) {
    throw new Error(`${label} corrected PAD became ready before restore/correction`);
  }
  return {raw, forward, corrected, local, remote};
}

async function pageOperation(page, operation, value) {
  return page.evaluate(async ({operation, value}) => {
    const owner = globalThis.__meleeRuntimeMatchHistory;
    if (!owner) throw new Error('Installed ordinary runtime-history controller is unavailable');
    const encode = bytes => {
      let binary = '';
      for (let start = 0; start < bytes.length; start += 0x6000)
        binary += String.fromCharCode(...bytes.subarray(start, start + 0x6000));
      return btoa(binary);
    };
    switch (operation) {
      case 'status': return owner.status();
      case 'command': return owner.command(value);
      case 'remotePad': owner.remotePad(Uint8Array.from(value)); return {accepted: true};
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
      case 'pcm': return encode(owner.pcm());
      case 'motor': return encode(owner.motor());
      case 'motorCommit': return encode(owner.motorCommit());
      default: throw new Error(`Unknown runtime-history probe operation ${operation}`);
    }
  }, {operation, value});
}

/**
 * Exercise one ordinary actual-input forward, source draw, typed restore, and
 * same-input correction on the already-owned page/controller. The held-prefix
 * P2 PAD is copied from its last literal row; no local PAD or neutral is
 * synthesized. `pressLocalAction` and `releaseLocalAction` must perform the
 * caller's actual keyboard P1 action while `forward` makes its one native poll.
 */
export async function runSameInstanceRuntimeHistory(page, {
  prefixJSON, publication, liveIdentity,
  actionLabel, expectedLocalPad, pressLocalAction, releaseLocalAction,
} = {}) {
  const report = {
    schema: 'melee-web-runtime-same-input-restore-control-v1',
    status: 'running',
    scope: 'one actual ordinary-runtime P1 poll, held-prefix P2 authority, one same-instance restore/correction, strict CPU/particle/PCM/motor comparisons',
    comparisons: {}, commands: [], statuses: [], inputs: [], endpoints: {},
    reference: {}, restored: {}, corrected: {},
    unobserved: [
      'legacy 1280-byte observation lane',
      'legacy 32-byte RNG profile lane',
      'legacy 40-byte transfer lane',
      'runtime draw-diagnostics lane',
    ],
    excludedClaim: 'No older five-lane comparison is claimed; no missing lane is synthesized.',
    drawCompletionEvidence: 'Native present, native idle with gpuPending=0, then the strict DRAW_END endpoint; no draw count is inferred.',
    cleanupOwner: 'caller',
  };
  let pressStarted = false;
  let releaseFailure = null;

  const noteStatus = async label => {
    const status = await pageOperation(page, 'status');
    report.statuses.push({label, value: status});
    return status;
  };
  const command = async name => {
    const beforeStatusIndex = report.statuses.length;
    const before = await noteStatus(`before:${name}`);
    try {
      const after = await pageOperation(page, 'command', name);
      const afterStatusIndex = report.statuses.length;
      report.statuses.push({label: `after:${name}`, value: after});
      report.commands.push({name, status: 'complete', beforeStatusIndex, afterStatusIndex});
      return after;
    } catch (error) {
      let after = null;
      let afterStatusIndex = null;
      try {
        afterStatusIndex = report.statuses.length;
        after = await noteStatus(`after-failed:${name}`);
      } catch (statusError) {
        report.statuses.push({label: `after-failed:${name}`, error: String(statusError)});
      }
      report.commands.push({name, status: 'failed', beforeStatusIndex, afterStatusIndex,
        error: String(error)});
      throw error;
    }
  };
  const input = async (label, status, options) => {
    let record = null;
    try {
      const receipt = await pageOperation(page, 'input');
      // Retain the copied native bytes in the report before any host-side
      // validation can refuse them. A mismatch is evidence, not a retry cue.
      record = {label, ...receipt};
      report.inputs.push(record);
      const decoded = requireInput(receipt, status, label, options);
      return {receipt, decoded, index: report.inputs.length - 1};
    } catch (error) {
      if (record) record.validationError = String(error);
      else report.inputs.push({label, error: String(error)});
      throw error;
    }
  };
  const endpoint = async (label, view, provenance, frame, expectedPhase) => {
    const beforeStatusIndex = report.statuses.length;
    await noteStatus(`before-endpoint:${label}`);
    let value;
    try {
      const wire = await pageOperation(page, 'endpoint', {
        view, provenance, expectedSourceFrame: String(frame), particleCellEndpoint: true,
      });
      report.endpoints[label] = wire;
      value = decodeCpuParticleEndpointResult(wire);
      if (value.view !== view || value.provenance !== provenance || value.sourceFrame !== String(frame))
        throw new Error(`${label} endpoint view/provenance/source-frame mismatch`);
      const after = await noteStatus(`after-endpoint:${label}`);
      requireStatus(after, `Endpoint ${label}`, {phase: expectedPhase, sourceFrame:
        label === 'restoredPre' ? 0 : frame});
      return value;
    } catch (error) {
      if (report.endpoints[label] === undefined)
        report.endpoints[label] = {status: 'failed', beforeStatusIndex};
      report.endpointErrors ??= {};
      report.endpointErrors[label] = String(error);
      throw error;
    }
  };
  const bytes = async operation => {
    const value = await pageOperation(page, operation);
    return decodeBase64(value, operation);
  };

  try {
    if (!page || typeof page.evaluate !== 'function') throw new Error('An existing Playwright page is required');
    if (typeof actionLabel !== 'string' || !actionLabel ||
        !(expectedLocalPad instanceof Uint8Array) || expectedLocalPad.length !== PAD_BYTES ||
        typeof pressLocalAction !== 'function' || typeof releaseLocalAction !== 'function')
      throw new Error('Caller must supply a named actual keyboard action and its exact 11-byte P1 PAD');
    const publicationBytes = publication?.raw;
    report.heldPrefix = {rawJSON: prefixJSON, liveIdentity,
      publicationRawValues: publicationBytes instanceof Uint8Array || Array.isArray(publicationBytes)
        ? Array.from(publicationBytes) : publicationBytes};
    let publicationRaw = publicationBytes;
    if (publicationBytes instanceof Uint8Array) publicationRaw = publicationBytes.slice();
    else if (Array.isArray(publicationBytes)) {
      if (publicationBytes.length !== 48 || publicationBytes.some(byte =>
          !Number.isInteger(byte) || byte < 0 || byte > 255))
        throw new Error('Held-start publication raw array is not an exact 48-byte receipt');
      publicationRaw = Uint8Array.from(publicationBytes);
    }
    const publicationCopy = {...publication, raw: publicationRaw};
    const prefix = validateRuntimeMatchStartPrefix(prefixJSON, {
      publication: publicationCopy, liveIdentity,
    });
    const remotePad = remotePortFromHeldPrefix(prefix);
    const expectedP1 = Buffer.from(expectedLocalPad);
    const prefixLastPollSerial = prefix.rows.at(-1).poll_serial;
    Object.assign(report.heldPrefix, {
      publication: {...publicationCopy, raw: undefined, rawBase64: encodeBase64(publicationCopy.raw)},
      sourceTicks: prefix.source_ticks, sourceDraws: prefix.source_draws,
      finalReadyTick: prefix.final_ready_tick, finalP2PadHex: remotePad.toString('hex'),
      finalPollSerial: prefixLastPollSerial,
    });
    delete report.heldPrefix.publicationRawValues;
    report.action = {label: actionLabel, expectedP1PadBase64: encodeBase64(expectedP1),
      inputSource: 'caller supplied actual keyboard callbacks'};
    report.comparisonScope = {
      supported: ['nine raw CPU domains and particle sidecar', 'literal 44-byte input receipt',
        'exact native PCM bytes', 'decoded native motor events and commit receipt'],
      unobserved: [...report.unobserved],
    };

    const initial = await noteStatus('before:capture');
    requireStatus(initial, 'Initial held takeover', {sourceFrame: 0, restoreCount: 0,
      forwardSteps: 0, replaySteps: 0});
    if (initial.expectedFrame !== 0 || initial.localPort !== PORTS.local ||
        initial.remotePort !== PORTS.remote)
      throw new Error('Existing terminal takeover is not held at frame zero on ports 0/1');

    await command('capture');
    const capturePre = await endpoint('capturePre', CPU_ENDPOINT_VIEW.pre,
      CPU_ENDPOINT_PROVENANCE.sharedPre, 0, PHASE.capturedPre);
    report.reference = {preEndpoint: 'capturePre'};

    await command('begin');
    const armedPre = await endpoint('armedPre', CPU_ENDPOINT_VIEW.pre,
      CPU_ENDPOINT_PROVENANCE.sharedPre, 0, PHASE.armedPre);
    report.comparisons.armedPre = compareCpuReferenceParticleEndpoints(capturePre, armedPre);

    await pageOperation(page, 'remotePad', [...remotePad]);
    report.remotePad = {source: 'held-prefix:last-row:port-1', bytesBase64: encodeBase64(remotePad)};
    let forwardError = null;
    try {
      pressStarted = true;
      await pressLocalAction(page);
      await command('forward');
    } catch (error) {
      forwardError = error;
      throw error;
    } finally {
      if (pressStarted) {
        try { await releaseLocalAction(page); }
        catch (error) {
          releaseFailure = error;
          report.action.releaseError = String(error);
          if (!forwardError) throw error;
        }
      }
    }

    const forwardStatus = await noteStatus('after:forward-poll');
    requireStatus(forwardStatus, 'Forward', {phase: PHASE.predictedSealed,
      sourceFrame: 1, restoreCount: 0, forwardSteps: 1, replaySteps: 0});
    const forwardInput = await input('after-forward', forwardStatus, {
      expectedLocalPad: expectedP1, expectedRemotePad: remotePad,
      priorPollSerial: prefixLastPollSerial,
    });
    report.reference.inputIndex = forwardInput.index;

    const referenceCpu = await endpoint('referenceCpu', CPU_ENDPOINT_VIEW.cpuEnd,
      CPU_ENDPOINT_PROVENANCE.reference, 1, PHASE.predictedCpu);
    report.reference.cpuEndpoint = 'referenceCpu';
    const referencePcm = await bytes('pcm');
    report.reference.pcmBase64 = referencePcm.toString('base64');
    const referenceMotorRaw = await bytes('motor');
    report.reference.motorRawBase64 = referenceMotorRaw.toString('base64');
    const referenceMotor = requireCpuHistoryMotor(decodeCpuHistoryMotorEvidence(referenceMotorRaw), {
      preFrame: '0', span: 1, selectedLane: 1,
    });
    report.reference.motor = referenceMotor;

    await command('present');
    const predictedPending = await noteStatus('after:reference-present');
    requireStatus(predictedPending, 'Reference present', {phase: PHASE.predictedDrawPending,
      sourceFrame: 1, restoreCount: 0, forwardSteps: 1, replaySteps: 0});
    await command('idle');
    const predictedIdle = await noteStatus('after:reference-idle');
    requireStatus(predictedIdle, 'Reference GPU idle', {phase: PHASE.predictedDrawIdle,
      sourceFrame: 1, restoreCount: 0, forwardSteps: 1, replaySteps: 0, gpuIdle: true});
    const referenceDraw = await endpoint('referenceDraw', CPU_ENDPOINT_VIEW.drawEnd,
      CPU_ENDPOINT_PROVENANCE.reference, 1, PHASE.predictedComplete);
    report.reference.drawEndpoint = 'referenceDraw';

    await command('restore');
    const restoredPending = await noteStatus('after:restore');
    requireStatus(restoredPending, 'Restore', {phase: PHASE.restoredPrePending,
      sourceFrame: 0, restoreCount: 1, forwardSteps: 1, replaySteps: 0});
    const restoredPre = await endpoint('restoredPre', CPU_ENDPOINT_VIEW.pre,
      CPU_ENDPOINT_PROVENANCE.sharedPre, 0, PHASE.restoredPre);
    report.restored.preEndpoint = 'restoredPre';
    report.comparisons.restoredPre = compareCpuReferenceParticleEndpoints(capturePre, restoredPre);

    await pageOperation(page, 'remotePad', [...remotePad]);
    await command('correct');
    const correctedStatus = await noteStatus('after:correct-without-poll');
    requireStatus(correctedStatus, 'Correction', {phase: PHASE.correctedSealed,
      sourceFrame: 1, restoreCount: 1, forwardSteps: 1, replaySteps: 1});
    if (correctedStatus.metadata.pollSerial !== forwardStatus.metadata.pollSerial)
      throw new Error('Correction advanced the native poll serial instead of reusing retained local input');
    const correctedInput = await input('after-correction-no-poll', correctedStatus, {
      expectedLocalPad: expectedP1, expectedRemotePad: remotePad, requireCorrected: true,
    });
    if (correctedInput.receipt.pollSerial !== forwardInput.receipt.pollSerial)
      throw new Error('Correction did not reuse the exact actual forward poll serial');
    report.corrected.inputIndex = correctedInput.index;

    const correctedCpu = await endpoint('correctedCpu', CPU_ENDPOINT_VIEW.cpuEnd,
      CPU_ENDPOINT_PROVENANCE.corrected, 1, PHASE.correctedCpu);
    report.corrected.cpuEndpoint = 'correctedCpu';
    // Preserve and compare the first corrected CPU endpoint before any draw.
    report.comparisons.cpu = compareCpuParticleEndpoints(referenceCpu, correctedCpu, {
      preLeft: capturePre, preRight: restoredPre,
    });
    const correctedPcm = await bytes('pcm');
    report.corrected.pcmBase64 = correctedPcm.toString('base64');
    const correctedMotorRaw = await bytes('motor');
    report.corrected.motorRawBase64 = correctedMotorRaw.toString('base64');
    const correctedMotor = requireCpuHistoryMotor(decodeCpuHistoryMotorEvidence(correctedMotorRaw), {
      preFrame: '0', span: 1, selectedLane: 3, baseline: referenceMotor.baseline,
    });
    report.corrected.motor = correctedMotor;
    report.comparisons.motor = compareCpuCorrectedLoopMotorEvents(
      referenceMotor.selected, correctedMotor.selected);

    if (!referencePcm.equals(correctedPcm))
      throw new Error(`Same-input corrected PCM differs at byte ${firstDifference(referencePcm, correctedPcm)}`);
    report.comparisons.pcm = {equal: true, bytes: referencePcm.length, semantics: 'exact raw bytes'};

    await command('present');
    const correctedPending = await noteStatus('after:corrected-present');
    requireStatus(correctedPending, 'Corrected present', {phase: PHASE.correctedDrawPending,
      sourceFrame: 1, restoreCount: 1, forwardSteps: 1, replaySteps: 1});
    await command('idle');
    const correctedIdle = await noteStatus('after:corrected-idle');
    requireStatus(correctedIdle, 'Corrected GPU idle', {phase: PHASE.correctedDrawIdle,
      sourceFrame: 1, restoreCount: 1, forwardSteps: 1, replaySteps: 1, gpuIdle: true});
    const correctedDraw = await endpoint('correctedDraw', CPU_ENDPOINT_VIEW.drawEnd,
      CPU_ENDPOINT_PROVENANCE.corrected, 1, PHASE.correctedComplete);
    report.corrected.drawEndpoint = 'correctedDraw';
    report.comparisons.draw = compareCpuParticleEndpoints(referenceDraw, correctedDraw, {
      preLeft: capturePre, preRight: restoredPre,
    });

    await command('close');
    const closed = await noteStatus('after:close');
    requireStatus(closed, 'Close', {phase: PHASE.closed, sourceFrame: 1,
      restoreCount: 1, forwardSteps: 1, replaySteps: 1});
    await command('retire');
    const retired = await noteStatus('after:retire');
    if (!retired || retired.metadata?.phase !== PHASE.retired ||
        retired.metadata.firstFailure !== 0 || retired.ownerState !== 1)
      throw new Error('Successful runtime-history retirement did not reach the retained owner boundary');
    const commitRaw = await bytes('motorCommit');
    report.corrected.motorCommitBase64 = commitRaw.toString('base64');
    report.corrected.motorCommit = decodeCpuCorrectedLoopMotorCommitReceipt(
      commitRaw, correctedMotor.selected, 3);

    report.status = 'same-input-restore-control-complete';
    report.result = 'Supported runtime CPU/particle, literal-input, PCM, and motor comparisons passed; legacy observation/RNG/transfer and draw-diagnostics lanes remain unobserved.';
    return report;
  } catch (error) {
    report.status = 'failed';
    report.firstFailure = String(error);
    if (releaseFailure) report.action.releaseError = String(releaseFailure);
    report.failureBoundary = report.commands.at(-1)?.name ?? 'preflight';
    try { error.sameInstanceRuntimeHistoryPartial = report; } catch {}
    throw error;
  }
}
