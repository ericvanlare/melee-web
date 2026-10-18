// SPDX-License-Identifier: GPL-3.0-or-later
/** Small executable controls shared by the live synctest runner. */

export const DRAW_MOTION_BOUNDARY = Object.freeze({
  interactive: 0,
  entry: 1,
  death: 2,
  respawn: 3,
  unknown: 4,
});

export function requireDrawDiagnosticSize(actualBytes, expectedBytes) {
  if (!Number.isInteger(expectedBytes) || expectedBytes < 12 || actualBytes !== expectedBytes) {
    throw new Error(`Unsupported draw diagnostic ABI size: ${actualBytes}; expected ${expectedBytes}`);
  }
  return actualBytes;
}

export function decodeDrawDiagnostics(bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.byteLength < 12) {
    throw new Error('Draw diagnostic bytes are missing or truncated');
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const slotCount = view.getUint32(0, true);
  const expectedBytes = 12 + 20 * slotCount;
  if (!Number.isInteger(slotCount) || slotCount < 2 || slotCount > 32 ||
      bytes.byteLength !== expectedBytes) {
    throw new Error(`Draw diagnostic ABI is invalid: slots=${slotCount}, bytes=${bytes.byteLength}`);
  }
  const readU32 = offset => Array.from({length: slotCount}, (_, index) =>
    view.getUint32(offset + 4 * index, true));
  const readI32 = offset => Array.from({length: slotCount}, (_, index) =>
    view.getInt32(offset + 4 * index, true));
  const callbacks = readU32(8);
  const modelTransforms = readU32(8 + 4 * slotCount);
  const normalModelDispatches = readU32(8 + 8 * slotCount);
  const processedPadSlots = readI32(8 + 12 * slotCount);
  const fighterKinds = readI32(8 + 16 * slotCount);
  return {
    slotCount,
    status: view.getUint32(4, true),
    callbacks,
    modelTransforms,
    normalModelDispatches,
    processedPadSlots,
    fighterKinds,
    metadataSeenMask: view.getUint32(8 + 20 * slotCount, true),
  };
}

export function validateSourceInputMapping(mapping, slotCount) {
  if (!Number.isInteger(slotCount) || slotCount < 2 ||
      !Array.isArray(mapping) || mapping.length !== 2) {
    throw new Error('Exactly two source slots and copied raw-controller mappings are required');
  }
  const seenSourceSlots = new Set();
  const seenRawControllers = new Set();
  return mapping.map((entry, index) => {
    if (!entry || entry.sourceSlot !== index || entry.sourceSlot >= slotCount ||
        !Number.isInteger(entry.initializerPort) || entry.initializerPort < 0 ||
        !Number.isInteger(entry.rawController) || entry.rawController < 0 ||
        entry.rawController >= 4) {
      throw new Error(`Copied start-data input mapping is malformed for source slot ${index}`);
    }
    const copiedRawController = entry.initializerPort
      ? entry.initializerPort - 1 : entry.sourceSlot;
    if (entry.rawController !== copiedRawController) {
      throw new Error(`Copied start-data raw controller does not match the initializer formula for source slot ${entry.sourceSlot}: ${entry.rawController}`);
    }
    if (entry.rawController !== entry.sourceSlot) {
      throw new Error(`Copied start-data raw controller does not preserve source slot ${entry.sourceSlot}: ${entry.rawController}`);
    }
    if (seenSourceSlots.has(entry.sourceSlot) || seenRawControllers.has(entry.rawController)) {
      throw new Error('Copied start-data mapping duplicates a source slot or raw controller');
    }
    seenSourceSlots.add(entry.sourceSlot);
    seenRawControllers.add(entry.rawController);
    return {
      sourceSlot: entry.sourceSlot,
      initializerPort: entry.initializerPort,
      rawController: entry.rawController,
    };
  });
}

export function assertDrawDiagnosticsClear(diagnostics, label) {
  if (!diagnostics || diagnostics.status !== 0 || diagnostics.metadataSeenMask !== 0 ||
      diagnostics.callbacks.some(value => value !== 0) ||
      diagnostics.modelTransforms.some(value => value !== 0) ||
      diagnostics.normalModelDispatches.some(value => value !== 0)) {
    throw new Error(`${label} unexpectedly recorded a source draw: ${JSON.stringify(diagnostics)}`);
  }
}

export function inspectInteractiveObservation(bytes, {
  classifyMotion, isMarioKind, slotCount, expectedInputMapping, expectedStocks,
}) {
  if (!(bytes instanceof Uint8Array) || bytes.byteLength !== 1280) {
    throw new Error(`Observation v2 boundary requires 1280 bytes; got ${bytes?.byteLength}`);
  }
  if (typeof classifyMotion !== 'function' || typeof isMarioKind !== 'function') {
    throw new Error('Source-derived motion and fighter-kind classifiers are required');
  }
  const inputMapping = validateSourceInputMapping(expectedInputMapping, slotCount);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const state = {
    sourceFrame: view.getUint32(0, true),
    randomSeed: view.getUint32(4, true),
    pcmFrames: view.getUint32(32, true),
    audioPhase: view.getUint32(36, true),
    ready: view.getUint32(8, true),
    ending: view.getUint32(12, true),
    complete: view.getUint32(16, true),
    paused: view.getUint32(20, true),
    players: [],
  };
  const reasons = [];
  if (state.ready !== 1) reasons.push('not-ready');
  if (state.paused !== 0) reasons.push('paused');
  if (state.ending !== 0 || state.complete !== 0) reasons.push('terminal');
  if (!Number.isInteger(slotCount) || slotCount < 2) reasons.push('invalid-source-slot-bound');
  if (!Array.isArray(expectedStocks) || expectedStocks.length !== 2 ||
      expectedStocks.some(value => !Number.isInteger(value) || value !== 4)) {
    reasons.push('invalid-stock-selection');
  }
  const selectedSlots = new Set();
  const motionNames = new Map([
    [DRAW_MOTION_BOUNDARY.interactive, 'interactive'],
    [DRAW_MOTION_BOUNDARY.entry, 'entry'],
    [DRAW_MOTION_BOUNDARY.death, 'death'],
    [DRAW_MOTION_BOUNDARY.respawn, 'respawn'],
    [DRAW_MOTION_BOUNDARY.unknown, 'unknown'],
  ]);
  for (let player = 0; player < 2; player++) {
    const base = 72 + 192 * player;
    const motion = view.getInt32(base + 20, true);
    const sourceSlot = view.getUint32(base + 136, true);
    const stocks = view.getInt32(base + 148, true);
    const fighterKind = view.getInt32(base + 152, true);
    const motionClass = classifyMotion(motion);
    const mapping = inputMapping[player];
    const row = {sourceSlot, rawController: mapping.rawController,
      initializerPort: mapping.initializerPort, fighterKind, stocks, motion, motionClass};
    state.players.push(row);
    if (!Number.isInteger(sourceSlot) || sourceSlot >= slotCount) {
      reasons.push(`player-${player}-source-slot-out-of-range`);
    } else if (selectedSlots.has(sourceSlot)) {
      reasons.push(`player-${player}-duplicate-source-slot`);
    } else {
      selectedSlots.add(sourceSlot);
    }
    if (sourceSlot !== mapping.sourceSlot) {
      reasons.push(`player-${player}-source-slot-differs-from-initializer`);
    }
    if (!isMarioKind(fighterKind)) reasons.push(`player-${player}-unexpected-fighter-kind`);
    if (stocks < 1) reasons.push(`player-${player}-no-live-stock`);
    if (stocks !== expectedStocks?.[player]) reasons.push(`player-${player}-stocks-differ-from-initializer`);
    const motionName = motionNames.get(motionClass) || 'unknown';
    if (motionName !== 'interactive') reasons.push(`player-${player}-motion-${motionName}`);
  }
  if (selectedSlots.size !== 2) reasons.push('active-source-player-count-mismatch');
  state.interactive = reasons.length === 0;
  state.refusalReasons = [...new Set(reasons)];
  return state;
}

export function validateSelectedDrawPath(before, after, players, expectedInputMapping) {
  if (!before || !after || before.slotCount !== after.slotCount) {
    throw new Error('Draw diagnostic slot identity changed during the sample');
  }
  if (after.status !== 0) {
    throw new Error(`Draw diagnostic status is nonzero: ${after.status}`);
  }
  if (!Array.isArray(players) || players.length !== 2 ||
      !Array.isArray(expectedInputMapping) || expectedInputMapping.length !== 2) {
    throw new Error('Two source fighters and their copied start-data input map are required');
  }
  const inputMapping = validateSourceInputMapping(expectedInputMapping, after.slotCount);
  const seenSlots = new Set();
  const seenProcessedPadSlots = new Set();
  const seenRawControllers = new Set();
  const selected = [];
  for (let playerIndex = 0; playerIndex < players.length; playerIndex++) {
    const player = players[playerIndex];
    const slot = player.sourceSlot;
    const mapping = inputMapping[playerIndex];
    if (!Number.isInteger(slot) || slot < 0 || slot >= after.slotCount || seenSlots.has(slot)) {
      throw new Error(`Selected fighter source slot is invalid or duplicated: ${slot}`);
    }
    if (slot !== mapping.sourceSlot) {
      throw new Error(`Observed source slot differs from copied start-data order: ${slot}`);
    }
    seenSlots.add(slot);
    const bit = 1 << slot;
    if ((after.metadataSeenMask & bit) === 0) {
      throw new Error(`Source draw diagnostics have no metadata for selected slot ${slot}`);
    }
    const processedPadSlot = after.processedPadSlots[slot];
    const kind = after.fighterKinds[slot];
    if (processedPadSlot !== slot || seenProcessedPadSlots.has(processedPadSlot)) {
      throw new Error(`Processed PAD slot does not match source slot ${slot}: ${processedPadSlot}`);
    }
    if (mapping.rawController !== slot || seenRawControllers.has(mapping.rawController)) {
      throw new Error(`Copied raw controller does not map uniquely to source slot ${slot}: ${mapping.rawController}`);
    }
    seenProcessedPadSlots.add(processedPadSlot);
    seenRawControllers.add(mapping.rawController);
    if (kind !== player.fighterKind) {
      throw new Error(`Source draw fighter kind differs from observation for slot ${slot}`);
    }
    const callbackDelta = after.callbacks[slot] - before.callbacks[slot];
    const dispatchDelta = after.normalModelDispatches[slot] - before.normalModelDispatches[slot];
    const modelTransformDelta = after.modelTransforms[slot] - before.modelTransforms[slot];
    if (callbackDelta <= 0) {
      throw new Error(`Fighter callback entry was not reached for selected slot ${slot}`);
    }
    if (dispatchDelta <= 0) {
      throw new Error(`Normal fighter model dispatch was not reached for selected slot ${slot}`);
    }
    if (callbackDelta > 0xffffffff || dispatchDelta > 0xffffffff ||
        modelTransformDelta < 0 || modelTransformDelta > 0xffffffff) {
      throw new Error(`Draw diagnostic counter wrapped for selected slot ${slot}`);
    }
    selected.push({sourceSlot: slot, processedPadSlot, rawController: mapping.rawController,
      initializerPort: mapping.initializerPort, fighterKind: kind,
      callbackDelta, normalModelDispatchDelta: dispatchDelta,
      modelTransformDelta});
  }
  if (seenSlots.size !== inputMapping.length || seenProcessedPadSlots.size !== inputMapping.length ||
      seenRawControllers.size !== inputMapping.length) {
    throw new Error('Draw counters do not cover the copied source-slot/raw-controller mapping');
  }
  return selected;
}

export function validateAuroraFrame(status, previousBeginFrameId, previousEndFrameId) {
  if (!status || !Number.isSafeInteger(status.drawCallCount) || status.drawCallCount <= 0) {
    throw new Error('Actual source draw submitted no Aurora draw calls');
  }
  if (!Number.isSafeInteger(status.lastBeginFrameId) ||
      status.lastBeginFrameId <= previousBeginFrameId) {
    throw new Error('Aurora begin-frame identity did not advance');
  }
  if (!Number.isSafeInteger(status.lastEndFrameId) ||
      status.lastEndFrameId < status.lastBeginFrameId ||
      status.lastEndFrameId <= previousEndFrameId) {
    throw new Error('Aurora end-frame identity did not advance after begin-frame');
  }
  return {beginFrameId: status.lastBeginFrameId, endFrameId: status.lastEndFrameId};
}

export async function checkedAsyncCcall(module, label, identifier, argumentTypes = [], arguments_ = []) {
  const result = await module.ccall(identifier, 'number', argumentTypes, arguments_, {async: true});
  if (result !== 1) {
    const error = module.UTF8ToString(module._melee_web_snapshot_error());
    throw new Error(`${label}: ${error}`);
  }
  return result;
}

export function chromeUserDataDirectoryFromSpawnArgs(spawnargs) {
  if (!Array.isArray(spawnargs)) throw new Error('Owned Chrome process did not expose spawn arguments');
  for (let index = 0; index < spawnargs.length; index++) {
    const argument = spawnargs[index];
    if (typeof argument !== 'string') continue;
    if (argument.startsWith('--user-data-dir=')) {
      const directory = argument.slice('--user-data-dir='.length);
      if (directory) return directory;
    }
    if (argument === '--user-data-dir') {
      const directory = spawnargs[index + 1];
      if (typeof directory === 'string' && directory) return directory;
    }
  }
  throw new Error('Owned Chrome command line has no --user-data-dir');
}

export function recordBrowserProcessInfo(report, processInfo) {
  if (!Array.isArray(processInfo) || processInfo.length === 0) {
    throw new Error('Chrome process inventory is empty or malformed');
  }
  const malformed = processInfo.find(row => !row || !Number.isSafeInteger(row.id) ||
    row.id <= 0 || typeof row.type !== 'string' || row.type.length === 0);
  if (malformed) throw new Error(`Chrome process inventory contains a malformed row: ${JSON.stringify(malformed)}`);
  const browser = processInfo.find(row => row.type === 'browser');
  if (!browser) throw new Error('Chrome process inventory has no browser process row');
  report.browser_process_info = processInfo.map(row => ({...row}));
  report.browser_process_info_source = 'Chrome CDP SystemInfo.getProcessInfo';
  return {browserPid: browser.id, processIds: [...new Set(processInfo.map(row => row.id))]};
}

export async function launchOwnedChromeServer(chromium, launchOptions) {
  if (!chromium || typeof chromium.launchServer !== 'function') {
    throw new Error('Installed Playwright Chromium does not expose launchServer');
  }
  if ((launchOptions?.args || []).some(argument => typeof argument === 'string' &&
      (argument === '--user-data-dir' || argument.startsWith('--user-data-dir=')))) {
    throw new Error('Do not pass --user-data-dir to launchServer; record Playwright’s owned profile from spawnargs');
  }
  const server = await chromium.launchServer(launchOptions);
  const child = server.process();
  if (!child || !Number.isSafeInteger(child.pid) || child.pid <= 0) {
    try { await server.close(); } catch {}
    throw new Error('Playwright did not expose the owned headless Chrome child process');
  }
  return {server, child};
}

export function ownedProcessPresent(pid) {
  if (!Number.isInteger(pid) || pid <= 0) throw new Error(`Invalid owned process PID: ${pid}`);
  try {
    process.kill(pid, 0);
    return true;
  } catch (error) {
    if (error?.code === 'ESRCH') return false;
    throw error;
  }
}

export function applyCleanupVerdict(report, cleanupFailures) {
  report.cleanup = {...(report.cleanup || {}), failures: cleanupFailures};
  if (cleanupFailures.length) {
    report.result = 'failed';
    report.failure ??= {message: 'Owned cleanup failed', cleanupFailures};
  }
  return report.result;
}
