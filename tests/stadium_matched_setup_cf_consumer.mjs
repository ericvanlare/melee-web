const PLAN_SCHEMA = 'melee-web-stadium-typed-pad-action-plan';
const PLAN_BUNDLES = 86;
const PLAN_ACTIONS = 171;
const PAD_STATUS_BYTES = 11;
const PAD_PORTS = 4;

function exactKeys(value, keys, label) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      Object.keys(value).sort().join(',') !== [...keys].sort().join(','))
    throw new TypeError(`${label} has an unexpected shape`);
}

export function validateRuntimePayload(payload) {
  exactKeys(payload, ['schema', 'version', 'bundles', 'schedule'], 'runtime payload');
  if (payload.schema !== PLAN_SCHEMA || payload.version !== 1 ||
      !Array.isArray(payload.bundles) || payload.bundles.length !== PLAN_BUNDLES ||
      !Array.isArray(payload.schedule) || payload.schedule.length !== PLAN_ACTIONS)
    throw new TypeError('runtime payload identity or fixed bounds differ');
  for (let bundleIndex = 0; bundleIndex < PLAN_BUNDLES; ++bundleIndex) {
    const bundle = payload.bundles[bundleIndex];
    exactKeys(bundle, ['bundle', 'ports'], `bundle ${bundleIndex}`);
    if (bundle.bundle !== bundleIndex || !Array.isArray(bundle.ports) ||
        bundle.ports.length !== PAD_PORTS)
      throw new TypeError(`bundle ${bundleIndex} is not the exact four-port source bundle`);
    for (let port = 0; port < PAD_PORTS; ++port) {
      const status = bundle.ports[port];
      exactKeys(status, ['port', 'status_hex'], `bundle ${bundleIndex} port ${port}`);
      if (status.port !== port || typeof status.status_hex !== 'string' ||
          !/^[0-9a-f]{22}$/.test(status.status_hex))
        throw new TypeError(`bundle ${bundleIndex} port ${port} lacks exact 11-byte source data`);
    }
  }
  for (let index = 0; index < PLAN_ACTIONS; ++index) {
    const action = payload.schedule[index];
    const ordinal = index < 168 ? Math.floor(index / 2) : index === 168 ? 84 : index === 169 ? 84 : 85;
    const kind = index === 168 || index === 170 || (index < 168 && index % 2 === 0)
      ? 'tick' : 'draw';
    exactKeys(action, kind === 'tick' ? ['action', 'ordinal', 'bundle'] : ['action', 'ordinal'],
      `schedule ${index}`);
    if (action.action !== kind || action.ordinal !== ordinal ||
        (kind === 'tick' && action.bundle !== ordinal))
      throw new TypeError(`schedule ${index} differs from the fixed tick/draw continuation`);
  }
  return payload;
}

export function validateRun2RuntimePayload(payload) {
  exactKeys(payload, ['bundles', 'schedule', 'schema', 'version'], 'run2 runtime payload');
  if (payload.schema !== 'melee-web-stadium-pad-action-plan' || payload.version !== 1 ||
      !Array.isArray(payload.bundles) || !Array.isArray(payload.schedule) ||
      payload.bundles.length === 0 || payload.schedule.length === 0)
    throw new TypeError('run2 action payload identity or arrays differ');
  for (let bundleIndex = 0; bundleIndex < payload.bundles.length; ++bundleIndex) {
    const bundle = payload.bundles[bundleIndex];
    exactKeys(bundle, ['bundle', 'ports'], `run2 bundle ${bundleIndex}`);
    if (bundle.bundle !== bundleIndex || !Array.isArray(bundle.ports) || bundle.ports.length !== PAD_PORTS)
      throw new TypeError(`run2 bundle ${bundleIndex} is not the exact four-port source bundle`);
    for (let port = 0; port < PAD_PORTS; ++port) {
      const status = bundle.ports[port];
      exactKeys(status, ['port', 'status_hex'], `run2 bundle ${bundleIndex} port ${port}`);
      if (status.port !== port || typeof status.status_hex !== 'string' ||
          !/^[0-9a-f]{22}$/.test(status.status_hex))
        throw new TypeError(`run2 bundle ${bundleIndex} port ${port} lacks exact 11-byte source data`);
    }
  }
  let tick = 0, draw = 0;
  for (let index = 0; index < payload.schedule.length; ++index) {
    const action = payload.schedule[index];
    if (!action || action.action_index !== index)
      throw new TypeError(`run2 action ${index} is missing or out of source order`);
    if (action.action === 'tick') {
      exactKeys(action, ['action', 'action_index', 'bundle'], `run2 tick ${index}`);
      if (action.bundle !== tick || action.bundle >= payload.bundles.length)
        throw new TypeError(`run2 tick ${index} has a discontinuous or missing PAD bundle`);
      ++tick;
    } else if (action.action === 'draw') {
      exactKeys(action, ['action', 'action_index', 'draw'], `run2 draw ${index}`);
      if (action.draw !== draw) throw new TypeError(`run2 draw ${index} has a discontinuous draw counter`);
      ++draw;
    } else throw new TypeError(`run2 action ${index} has an unknown action kind`);
  }
  if (tick !== payload.bundles.length)
    throw new TypeError('run2 schedule does not consume every exact PAD bundle once');
  return payload;
}

function run2Coordinate(row, label, event, boundary) {
  const raw = row?.raw_identity;
  if (!raw || raw.event !== event || (boundary && raw.boundary !== boundary) ||
      !Number.isSafeInteger(raw.sequence) || !Number.isSafeInteger(raw.source_tick) ||
      !Number.isSafeInteger(raw.draw_ordinal))
    throw new TypeError(`run2 ${label} source identity is malformed`);
  return raw;
}

export function deriveRun2ProperPrefix(runtimePayload, hostOnly) {
  const payload = validateRun2RuntimePayload(runtimePayload);
  if (!hostOnly || hostOnly.schema !== 'melee-web-stadium-source-host-expectations' ||
      hostOnly.version !== 1 || !hostOnly.expected_state ||
      !Array.isArray(hostOnly.boundary_rng_observations))
    throw new TypeError('run2 host-only typed expectations and boundary RNG observations are required');
  const state = hostOnly.expected_state;
  const go = run2Coordinate(state.GO, 'GO', 'progress', null);
  if (state.GO.raw_identity.phase !== 'go_after') throw new TypeError('run2 GO marker differs');
  const c = run2Coordinate(state.C, 'C', 'boundary', 'source_tick');
  const f = run2Coordinate(state.F, 'F', 'boundary', 'source_tick');
  if (c.sequence === f.sequence || c.source_tick !== go.source_tick ||
      f.source_tick !== c.source_tick + 1 ||
      c.draw_ordinal !== go.draw_ordinal)
    throw new TypeError('run2 GO/C/F source coordinates are not one dynamic adjacent boundary');
  const seedRows = hostOnly.boundary_rng_observations;
  if (seedRows.length !== 2 || seedRows.map(row => row?.label).join(',') !== 'C,F')
    throw new TypeError('run2 host-only C/F seed rows are missing or reordered');
  for (const [row, raw] of [[seedRows[0], c], [seedRows[1], f]]) {
    if (row.sequence !== raw.sequence || row.source_tick !== raw.source_tick ||
        row.draw_ordinal !== raw.draw_ordinal ||
        row.post_go_ordinal !== raw.source_tick - c.source_tick ||
        !/^[0-9a-f]{8}$/.test(row.seed_hex ?? '') ||
        !/^[0-9a-f]{8}$/.test(row.pointer_hex ?? ''))
      throw new TypeError(`run2 ${row.label} seed row differs from its typed raw source identity`);
  }
  const cIndex = payload.schedule.findIndex(action => action.action === 'tick' && action.bundle === c.source_tick);
  const fIndex = payload.schedule.findIndex(action => action.action === 'tick' && action.bundle === f.source_tick);
  if (cIndex < 0 || fIndex <= cIndex ||
      payload.schedule.slice(cIndex + 1, fIndex).some(action => action.action === 'tick') ||
      payload.schedule.slice(0, cIndex).filter(action => action.action === 'draw').length !== c.draw_ordinal ||
      payload.schedule.slice(0, fIndex).filter(action => action.action === 'draw').length !== f.draw_ordinal)
    throw new TypeError('run2 schedule does not contain the unique dynamic C/F tick boundaries');
  if (payload.schedule[fIndex + 1]?.action !== 'draw' ||
      payload.schedule[fIndex + 2]?.action !== 'tick' ||
      payload.schedule[fIndex + 3]?.action !== 'draw')
    throw new TypeError('run2 schedule lacks the next draw/tick/draw continuation beyond F');
  const terminalIndex = fIndex + 3;
  const terminal = payload.schedule[terminalIndex];
  const prefix = {schema: 'melee-web-stadium-run2-proper-prefix-v2', version: 2,
    bundles: payload.bundles.slice(0, terminal.action === 'tick' ? terminal.bundle + 1 :
      payload.schedule.slice(0, terminalIndex + 1).filter(action => action.action === 'tick').length),
    schedule: payload.schedule.slice(0, terminalIndex + 1),
    checkpoints: {c_action_index: cIndex, f_action_index: fIndex,
      c_source_tick: c.source_tick, f_source_tick: f.source_tick,
      terminal_action_index: terminalIndex, terminal_draw_ordinal: terminal.draw}};
  return {prefix, c, f, go, expectedState: state,
    expectedSeeds: {C: seedRows[0].seed_hex, F: seedRows[1].seed_hex}};
}

export function deriveRun2FullSchedule(runtimePayload, hostOnly) {
  const derived = deriveRun2ProperPrefix(runtimePayload, hostOnly);
  const payload = validateRun2RuntimePayload(runtimePayload);
  if (payload.schedule.length !== 7167 || payload.bundles.length !== 3585 ||
      payload.schedule.filter(row => row.action === 'draw').length !== 3582 ||
      derived.prefix.checkpoints.c_action_index !== 168 || derived.prefix.checkpoints.f_action_index !== 170 ||
      derived.c.source_tick !== 84 || derived.f.source_tick !== 85 ||
      payload.schedule.at(-1)?.action !== 'draw' || payload.schedule.at(-1)?.draw !== 3581)
    throw new TypeError('original full7167 schedule dimensions or C/F coordinates differ');
  return {...derived, prefix: {schema: 'melee-web-stadium-run2-full-schedule-v3', version: 3,
    bundles: payload.bundles, schedule: payload.schedule,
    checkpoints: {...derived.prefix.checkpoints, terminal_action_index: 7166,
      terminal_draw_ordinal: 3581}}};
}

export function encodeRun2FullNativeManifest(full) {
  exactKeys(full, ['bundles', 'checkpoints', 'schedule', 'schema', 'version'], 'full schedule manifest');
  exactKeys(full.checkpoints, ['c_action_index', 'c_source_tick', 'f_action_index',
    'f_source_tick', 'terminal_action_index', 'terminal_draw_ordinal'], 'full schedule checkpoints');
  validateRun2RuntimePayload({schema: 'melee-web-stadium-pad-action-plan', version: 1,
    bundles: full.bundles, schedule: full.schedule});
  const c = full.checkpoints;
  if (full.schema !== 'melee-web-stadium-run2-full-schedule-v3' || full.version !== 3 ||
      full.schedule.length !== 7167 || full.bundles.length !== 3585 ||
      full.schedule.filter(row => row.action === 'draw').length !== 3582 ||
      c.c_action_index !== 168 || c.f_action_index !== 170 || c.c_source_tick !== 84 ||
      c.f_source_tick !== 85 || c.terminal_action_index !== 7166 || c.terminal_draw_ordinal !== 3581 ||
      full.schedule[168]?.action !== 'tick' || full.schedule[168]?.bundle !== 84 ||
      full.schedule[170]?.action !== 'tick' || full.schedule[170]?.bundle !== 85 ||
      full.schedule[171]?.action !== 'draw' || full.schedule[172]?.action !== 'tick' ||
      full.schedule[173]?.action !== 'draw' || full.schedule.at(-1)?.action !== 'draw' ||
      full.schedule.at(-1)?.draw !== 3581)
    throw new TypeError('original full7167 manifest dimensions or terminal differ');
  return encodeNativeManifestRows(full, [0x4d, 0x57, 0x46, 0x33]);
}

export function encodeRun2NativeManifest(prefix) {
  exactKeys(prefix, ['bundles', 'checkpoints', 'schedule', 'schema', 'version'],
    'run2 native prefix manifest');
  exactKeys(prefix.checkpoints, ['c_action_index', 'c_source_tick', 'f_action_index',
    'f_source_tick', 'terminal_action_index', 'terminal_draw_ordinal'],
  'run2 native prefix checkpoints');
  if (prefix.schema !== 'melee-web-stadium-run2-proper-prefix-v2' || prefix.version !== 2 ||
      !prefix.checkpoints || prefix.schedule.length > 7167 ||
      prefix.schedule.length !== prefix.checkpoints.terminal_action_index + 1 ||
      prefix.checkpoints.f_action_index + 3 !== prefix.schedule.length - 1 ||
      prefix.checkpoints.c_source_tick + 1 !== prefix.checkpoints.f_source_tick)
    throw new TypeError('run2 native prefix dimensions differ');
  validateRun2RuntimePayload({schema: 'melee-web-stadium-pad-action-plan', version: 1,
    bundles: prefix.bundles, schedule: prefix.schedule});
  const c = prefix.schedule[prefix.checkpoints.c_action_index];
  const f = prefix.schedule[prefix.checkpoints.f_action_index];
  if (c?.action !== 'tick' || c.bundle !== prefix.checkpoints.c_source_tick ||
      f?.action !== 'tick' || f.bundle !== prefix.checkpoints.f_source_tick ||
      prefix.schedule.slice(c.action_index + 1, f.action_index).some(action => action.action === 'tick') ||
      prefix.schedule[f.action_index + 1]?.action !== 'draw' ||
      prefix.schedule[f.action_index + 2]?.action !== 'tick' ||
      prefix.schedule.at(-1)?.action !== 'draw' ||
      prefix.schedule.at(-1)?.draw !== prefix.checkpoints.terminal_draw_ordinal)
    throw new TypeError('run2 native prefix C/F and terminal actions differ');
  return encodeNativeManifestRows(prefix, [0x4d, 0x57, 0x50, 0x32]);
}

function encodeNativeManifestRows(prefix, magic) {
  const bytes = new Uint8Array(24 + prefix.schedule.length * 57);
  bytes.set(magic);
  const write32 = (offset, value) => {
    if (!Number.isSafeInteger(value) || value < 0 || value > 0xffffffff)
      throw new TypeError('run2 native manifest integer is outside uint32');
    bytes[offset] = value & 0xff;
    bytes[offset + 1] = (value >>> 8) & 0xff;
    bytes[offset + 2] = (value >>> 16) & 0xff;
    bytes[offset + 3] = (value >>> 24) & 0xff;
  };
  write32(4, prefix.schedule.length);
  write32(8, prefix.checkpoints.c_action_index);
  write32(12, prefix.checkpoints.f_action_index);
  write32(16, prefix.checkpoints.c_source_tick);
  write32(20, prefix.checkpoints.f_source_tick);
  let draws = 0;
  for (let index = 0; index < prefix.schedule.length; ++index) {
    const action = prefix.schedule[index], offset = 24 + index * 57;
    const tick = action.action === 'tick';
    bytes[offset] = tick ? 1 : 2;
    write32(offset + 1, tick ? action.bundle : action.draw);
    write32(offset + 5, tick ? action.bundle : 0xffffffff);
    write32(offset + 9, draws);
    if (tick) bytes.set(bundleBytes(prefix.bundles[action.bundle]), offset + 13);
    else ++draws;
  }
  return bytes;
}

function requiredHex32(value, label) {
  if (typeof value !== 'string' || !/^[0-9a-fA-F]{8}$/.test(value))
    throw new TypeError(`${label} is not an observed 32-bit source word`);
  return value.toLowerCase();
}

function compareField(fields, path, expected, actual) {
  fields.push({path, expected, actual});
  return expected === actual ? null : {path, expected, actual};
}

function requiredInteger(value, label) {
  if (!Number.isSafeInteger(value)) throw new TypeError(`${label} is not an observed integer`);
  return value;
}

function requiredBoolean(value, label) {
  if (typeof value !== 'boolean') throw new TypeError(`${label} is not an observed boolean`);
  return value;
}

export function compareOriginalIdleSnapshot({label, expected, expectedGo, expectedSeedHex, actual,
  coordinates = null}) {
  if (!['C', 'F'].includes(label)) throw new TypeError('only C and F are comparable boundaries');
  if (!expected || expected.label !== label || !expectedGo || expectedGo.label !== 'GO' ||
      !actual || typeof actual !== 'object')
    throw new TypeError(`${label} comparison lacks its exact host-only source records`);
  const dynamic = coordinates ?? {cTick: 84, fTick: 85, goSequence: 2925,
    cSequence: 2926, fSequence: 2931, goDraw: 84, cDraw: 84, fDraw: 85};
  const ordinal = label === 'C' ? dynamic.cTick : dynamic.fTick;
  const drawOrdinal = label === 'C' ? dynamic.cDraw : dynamic.fDraw;
  const goSequence = requiredInteger(expectedGo.sequence, 'original GO event sequence');
  const rowSequence = requiredInteger(expected.sequence, `${label} original state sequence`);
  if (goSequence !== dynamic.goSequence ||
      rowSequence !== (label === 'C' ? dynamic.cSequence : dynamic.fSequence) ||
      expectedGo.source_tick !== dynamic.cTick || expectedGo.draw_ordinal !== dynamic.goDraw ||
      expected.source_tick !== ordinal ||
      expected.draw_ordinal !== drawOrdinal || actual.ordinal !== ordinal || actual.label !== label)
    throw new TypeError(`${label} comparison is attached to the wrong input boundary`);

  const source = expected;
  if (source.map_root_allocation_lifetime_proved !== false ||
      source.map_root_object_type_observed !== false)
    throw new TypeError(`${label} original map-root evidence scope changed`);
  const idle = actual.idle_snapshot;
  if (!idle || typeof idle !== 'object') throw new TypeError(`${label} native idle receipt is absent`);
  if (source.display.timer_initialization_observed !== false || source.display.xF0_observed !== false ||
      idle.display_timer_initialization_observed !== 0 ||
      !/^[0-9a-fA-F]{8}$/.test(source.display.timer_raw_hex ?? '') ||
      !Array.isArray(idle.display_timer_native_bytes) || idle.display_timer_native_bytes.length !== 4 ||
      idle.display_timer_native_bytes.some(byte => !Number.isInteger(byte) || byte < 0 || byte > 255))
    throw new TypeError(`${label} raw timer/F0 initialization evidence is malformed`);
  const originalFlags = Number.parseInt(source.display.packed_flags_raw_hex, 16);
  if (!/^[0-9a-fA-F]{2}$/.test(source.display.packed_flags_raw_hex ?? '') ||
      !Number.isInteger(idle.display_flags) || idle.display_flags < 0 || idle.display_flags > 7 ||
      (idle.display_flags & ~7) !== 0)
    throw new TypeError(`${label} packed flag observations are malformed`);

  const fields = [];
  let mismatch = null;
  const add = (path, expectedValue, actualValue) => {
    const found = compareField(fields, path, expectedValue, actualValue);
    if (!mismatch && found) mismatch = found;
  };
  const sourceMap = source.map2, sourceDisplay = source.display;
  if (!sourceMap || !sourceDisplay) throw new TypeError(`${label} original typed state is absent`);
  const integerPairs = [
    ['map2.countdown_s32', sourceMap.countdown_s32, idle.map2_countdown],
    ['map2.phase_s16', sourceMap.phase_s16, idle.map2_phase],
    ['map2.form_s16', sourceMap.form_s16, idle.map2_form],
    ['map2.previous_form_s16', sourceMap.previous_form_s16, idle.map2_history_e0],
    ['map2.prior_form_s16', sourceMap.prior_form_s16, idle.map2_history_e2],
    ['map2.gate_mask_80', Number(requiredBoolean(sourceMap.gate_mask_80, `${label} map2 gate mask 0x80`)), idle.map2_gate],
    ['display.mode_s16', sourceDisplay.mode_s16, idle.display_mode],
    ['display.flags_s16', sourceDisplay.flags_s16, idle.display_e6],
    ['display.previous_flags_s16', sourceDisplay.previous_flags_s16, idle.display_e8],
    ['display.previous_mode_s16', sourceDisplay.previous_mode_s16, idle.display_previous_mode],
    ['display.focused_player_s16', sourceDisplay.focused_player_s16, idle.display_selected_player],
    ['display.selection_counter_s16', sourceDisplay.selection_counter_s16, idle.display_f2],
  ];
  for (const [path, expectedValue, actualValue] of integerPairs)
    add(path, requiredInteger(expectedValue, `${label} original ${path}`),
      requiredInteger(actualValue, `native ${path}`));
  for (const [index, mask] of [0x80, 0x40, 0x20].entries())
    add(`display.F8_${index}`, (originalFlags & mask) !== 0 ? 1 : 0,
      (idle.display_flags & (1 << index)) !== 0 ? 1 : 0);
  add('seed.raw32', requiredHex32(expectedSeedHex, `${label} original seed`),
    requiredHex32(actual.seed_hex, `${label} native seed`));

  const go = actual.go_trace;
  if (!go || go.failure !== 0 || go.armed !== 1 || actual.expected_branch !== 0 ||
      !Number.isSafeInteger(idle.world_generation) || idle.world_generation <= 0 ||
      go.generation !== idle.world_generation ||
      go.armed_world_ticks !== 0 || go.armed_source_frame !== 0 ||
      go.expected_branch !== 0 || go.stage_before?.sequence !== 1 ||
      go.stage_after?.sequence !== 2 || go.go_after?.sequence !== 3 ||
      go.hud_after?.sequence !== 0 || go.go_after?.source_branch !== 0 ||
      go.go_after?.world_ticks !== dynamic.cTick || idle.world_ticks !== ordinal + 1 ||
      [go.stage_before, go.stage_after, go.go_after].some(event =>
        event?.world_ticks !== dynamic.cTick || event?.generation !== idle.world_generation) ||
      go.next_sequence !== 3 ||
      go.stage_before?.sequence >= go.go_after?.sequence ||
      go.stage_after?.sequence >= go.go_after?.sequence)
    mismatch ??= {path: 'go_trace.membership', expected: `one ordered GO event in tick ${dynamic.cTick}`,
      actual: go ?? null};

  const approved = mismatch === null;
  return {
    schema: 'melee-web-stadium-matched-setup-cf-comparison-v1',
    phase: label,
    decision: approved ? 'PASS_EXACT_DECLARED_C_F_FIELDS' : 'REFUSE_FIRST_C_F_FIELD_MISMATCH',
    compared_fields: fields,
    first_mismatch: mismatch,
    unpaired: {
      original_timer_raw_hex: source.display.timer_raw_hex,
      native_timer_raw_hex: idle.display_timer_native_bytes.map(byte => byte.toString(16).padStart(2, '0')).join(''),
      timer_initialization_observed: false,
      original_f0_observed: false,
      original_title: source.title,
      native_sis: {
        font_slot: idle.sis_font_slot,
        static_text_role: idle.sis_static_text_role,
        dynamic_text_role: idle.sis_dynamic_text_role,
        title_entry_seen: idle.sis_title_entry_seen,
        title_entry_index: idle.sis_title_entry_index,
        title_bytes_captured: idle.title_bytes_captured,
      },
      original_map_root_lifetime_proved: source.map_root_allocation_lifetime_proved,
      original_map_root_object_type_observed: source.map_root_object_type_observed,
      original_gate_mask_40: source.map2.gate_mask_40,
    },
  };
}

export function createHostCallbacks({setupApproved, originalSnapshots, originalSeeds, coordinates = null}) {
  if (typeof setupApproved !== 'function') throw new TypeError('strict Setup approval callback is required');
  if (!Array.isArray(originalSnapshots) || originalSnapshots.length !== 3 ||
      originalSnapshots.some(snapshot => !snapshot || !['GO', 'C', 'F'].includes(snapshot.label)))
    throw new TypeError('host-only original GO/C/F state records are required exactly once');
  const byLabel = new Map(structuredClone(originalSnapshots).map(snapshot => [snapshot.label, snapshot]));
  const seeds = new Map(Object.entries(originalSeeds ?? {}));
  if (byLabel.size !== 3 || !['C', 'F'].every(label => seeds.has(label)) ||
      [...seeds.keys()].some(label => !['C', 'F'].includes(label)))
    throw new TypeError('host-only original GO/C/F state and observed C/F RNG evidence are required');
  const receipts = [];
  let nextBoundary = 'C';
  let refused = false;
  return {
    setupApproved() { return setupApproved() === true; },
    compare(phase, actualJson) {
      if (refused) return false;
      if (phase !== nextBoundary) {
        refused = true;
        receipts.push({schema: 'melee-web-stadium-matched-setup-cf-comparison-v1',
          phase, decision: 'REFUSE_C_F_ORDER', expected_phase: nextBoundary});
        return false;
      }
      try {
        const actual = JSON.parse(actualJson);
        const receipt = compareOriginalIdleSnapshot({label: phase,
          expected: byLabel.get(phase), expectedGo: byLabel.get('GO'),
          expectedSeedHex: seeds.get(phase), actual, coordinates});
        receipts.push(receipt);
        const accepted = receipt.decision === 'PASS_EXACT_DECLARED_C_F_FIELDS';
        if (accepted) nextBoundary = phase === 'C' ? 'F' : 'complete';
        else refused = true;
        return accepted;
      } catch (error) {
        refused = true;
        receipts.push({schema: 'melee-web-stadium-matched-setup-cf-comparison-v1',
          phase, decision: 'REFUSE_INVALID_C_F_RECEIPT', error: String(error)});
        return false;
      }
    },
    receipts() { return structuredClone(receipts); },
  };
}

export function hostEvidenceFromPacket(packet, {fullSchedule = false} = {}) {
  if (typeof fullSchedule !== 'boolean') throw new TypeError('fullSchedule must be boolean');
  if (packet?.schema === 'melee-web-stadium-source-action-replay-packet' && packet.version === 2)
    return hostEvidenceFromRun2Packet(packet, {fullSchedule});
  if (fullSchedule) throw new TypeError('Full schedule requires original run2 packet');
  exactKeys(packet, ['schema', 'version', 'host_only', 'runtime_payload'], 'matched Setup packet');
  if (packet.schema !== 'melee-web-stadium-setup-cf-input-schedule' ||
      packet.version !== 1 || !packet.host_only || typeof packet.host_only !== 'object')
    throw new TypeError('matched Setup packet identity differs');
  validateRuntimePayload(packet.runtime_payload);
  const summary = packet.host_only.expected_original_state?.summary;
  const snapshots = summary?.snapshots;
  const rng = packet.host_only.boundary_rng_observations;
  if (summary?.decision !== 'PASS_ORIGINAL_STATE_ONLY_COUNTERPART' ||
      summary?.schema !== 'melee-web-original-stadium-idle-state' ||
      summary?.source?.dol_sha256 !== 'dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646' ||
      summary?.source?.dol_sha1 !== '08e0bf20134dfcb260699671004527b2d6bb1a45' ||
      !Array.isArray(snapshots) || snapshots.length !== 3 ||
      snapshots.map(row => row?.label).join(',') !== 'GO,C,F' ||
      summary.guest_pointer_numeric_comparison !== false ||
      !Array.isArray(rng) || rng.length !== 3 ||
      rng.map(row => row?.label).join(',') !== 'GO,C,F')
    throw new TypeError('matched Setup host-only original evidence differs');
  const seeds = {};
  for (const row of rng) {
    if (row.sequence !== ({GO: 2925, C: 2926, F: 2931})[row.label] ||
        row.pc !== (row.label === 'GO' ? '0x8016b824' : '0x80390eb4') ||
        row.pointer_slot_address !== 0x804D5F94 ||
        !/^[0-9a-f]{8}$/.test(row.pointer_raw_hex ?? '') ||
        Number.parseInt(row.pointer_raw_hex, 16) !== row.seed_address ||
        row.seed_raw_hex !== row.seed_hex ||
        row.event !== (row.label === 'GO' ? 'progress' : 'boundary') ||
        row.source_tick !== (row.label === 'F' ? 85 : 84) ||
        row.draw_ordinal !== 84 + (row.label === 'F' ? 1 : 0) ||
        row.pointer_equals_seed_address !== true ||
        !/^[0-9a-f]{8}$/.test(row.seed_hex ?? '') ||
        !/^[0-9a-f]{8}$/.test(row.seed_raw_hex ?? ''))
      throw new TypeError(`original ${row.label} RNG observation is malformed`);
    if (row.label === 'C' || row.label === 'F') seeds[row.label] = row.seed_hex;
  }
  return {runtimePayload: packet.runtime_payload, originalSnapshots: snapshots,
    originalSeeds: seeds};
}

export function hostEvidenceFromRun2Packet(packet, {fullSchedule = false} = {}) {
  if (typeof fullSchedule !== 'boolean') throw new TypeError('fullSchedule must be boolean');
  exactKeys(packet, ['host_only', 'runtime_payload', 'schema', 'version'], 'run2 action packet');
  if (packet.schema !== 'melee-web-stadium-source-action-replay-packet' || packet.version !== 2)
    throw new TypeError('run2 action packet identity differs');
  const hostOnly = packet.host_only;
  if (hostOnly?.schema !== 'melee-web-stadium-source-host-expectations' || hostOnly.version !== 1 ||
      hostOnly.source_identity?.strict_route?.source?.dol_sha256 !==
        'dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646' ||
      hostOnly.source_identity?.strict_route?.source?.dol_sha1 !==
        '08e0bf20134dfcb260699671004527b2d6bb1a45' ||
      hostOnly.expected_state?.comparison_scope?.setup_profile_verified_by_observer !== false ||
      hostOnly.expected_state?.comparison_scope?.profile_equivalence?.startsWith('pending') !== true)
    throw new TypeError('run2 host-only source identity or evidence scope differs');
  const derived = fullSchedule ? deriveRun2FullSchedule(packet.runtime_payload, hostOnly)
    : deriveRun2ProperPrefix(packet.runtime_payload, hostOnly);
  const normalize = label => {
    const row = derived.expectedState[label];
    const raw = row?.raw_identity;
    const fields = row?.typed_fields;
    if (row?.boundary_role !== label || !raw || raw.event !== 'boundary' ||
        raw.boundary !== 'source_tick' || !fields || fields.label !== label ||
        fields.sequence !== raw.sequence || fields.source_tick !== raw.source_tick ||
        fields.draw_ordinal !== raw.draw_ordinal || !fields.map2 || !fields.display ||
        fields.map_root_allocation_lifetime_proved !== false ||
        fields.map_root_object_type_observed !== false)
      throw new TypeError(`run2 ${label} typed state expectation is malformed`);
    return {label, sequence: raw.sequence, source_tick: raw.source_tick,
      draw_ordinal: raw.draw_ordinal, map2: fields.map2, display: fields.display,
      title: fields.title, map_root_allocation_lifetime_proved:
        fields.map_root_allocation_lifetime_proved,
      map_root_object_type_observed: fields.map_root_object_type_observed};
  };
  const goRow = derived.expectedState.GO;
  const goRaw = goRow?.raw_identity, goFields = goRow?.typed_fields;
  if (!goRaw || goRaw.event !== 'progress' || !goFields || goFields.label !== 'GO' ||
      goFields.sequence !== goRaw.sequence || goFields.source_tick !== goRaw.source_tick ||
      goFields.draw_ordinal !== goRaw.draw_ordinal || goRaw.phase !== 'go_after')
    throw new TypeError('run2 GO expected source row is malformed');
  const go = {label: 'GO', sequence: goRaw.sequence, source_tick: goRaw.source_tick,
    draw_ordinal: goRaw.draw_ordinal};
  const snapshots = [go, normalize('C'), normalize('F')];
  const coordinates = {cTick: derived.c.source_tick, fTick: derived.f.source_tick,
    goSequence: go.sequence, cSequence: derived.c.sequence, fSequence: derived.f.sequence,
    goDraw: go.draw_ordinal, cDraw: derived.c.draw_ordinal, fDraw: derived.f.draw_ordinal};
  return {runtimePayload: derived.prefix, sourceRuntimePayload: packet.runtime_payload,
    originalSnapshots: snapshots, originalSeeds: derived.expectedSeeds, coordinates,
    checkpoints: derived.prefix.checkpoints, mode: fullSchedule ? 'run2-full-schedule-v3' : 'run2-proper-prefix-v2'};
}

function bundleBytes(bundle) {
  const hex = bundle.ports.map(port => port.status_hex).join('');
  return Uint8Array.from(hex.match(/.{2}/g), pair => Number.parseInt(pair, 16));
}

export class StadiumSetupCfActionDriver {
  constructor({runtimePayload, native}) {
    this.mode = runtimePayload?.schema === 'melee-web-stadium-run2-full-schedule-v3'
      ? 'run2-full-schedule-v3' : runtimePayload?.schema === 'melee-web-stadium-run2-proper-prefix-v2'
      ? 'run2-proper-prefix-v2' : 'legacy-v1';
    if (this.mode === 'legacy-v1') this.payload = validateRuntimePayload(runtimePayload);
    else {
      exactKeys(runtimePayload, ['bundles', 'checkpoints', 'schedule', 'schema', 'version'],
        'run2 proper-prefix manifest');
      if (runtimePayload.version !== (this.mode === 'run2-full-schedule-v3' ? 3 : 2) || !Array.isArray(runtimePayload.bundles) ||
          !Array.isArray(runtimePayload.schedule) || !runtimePayload.schedule.length ||
          runtimePayload.schedule.length !== runtimePayload.checkpoints.terminal_action_index + 1 ||
          runtimePayload.schedule.at(-1)?.action !== 'draw')
        throw new TypeError('run2 proper-prefix manifest is malformed');
      this.payload = runtimePayload;
      this.nativeManifest = this.mode === 'run2-full-schedule-v3'
        ? encodeRun2FullNativeManifest(runtimePayload) : encodeRun2NativeManifest(runtimePayload);
    }
    if (!native || typeof native.arm !== 'function' || typeof native.queue !== 'function' ||
        typeof native.approve !== 'function')
      throw new TypeError('bounded native arm/queue/approval API is required');
    this.native = native;
    this.cursor = 0;
    this.armed = false;
    this.pending = false;
    this.inFlight = false;
    this.failed = false;
    this.complete = false;
    this.firstError = null;
    this.sourceFrame = 0; // Completed Setup is admitted at original match clock zero.
  }

  async arm(setupApproved) {
    if (this.inFlight || this.armed || this.failed || this.complete)
      return this._fail('duplicate or terminal arm');
    if (setupApproved !== true)
      return this._fail('fresh completed Setup owner/approval was refused');
    this.inFlight = true;
    try {
      const manifest = this.mode === 'legacy-v1' ? null : this.nativeManifest;
      if (await this.native.arm(manifest) !== 1)
        return this._fail('fresh completed Setup owner/approval was refused');
      if (this.failed) return false;
      this.armed = true;
      return true;
    } catch (error) {
      return this._fail(`native arm rejected: ${error?.message ?? String(error)}`);
    } finally { this.inFlight = false; }
  }

  async submit(action) {
    if (this.inFlight || !this.armed || this.failed || this.complete || this.pending)
      return this._fail('action submitted without a ready one-use continuation');
    const expected = this.payload.schedule[this.cursor];
    if (!expected || JSON.stringify(action) !== JSON.stringify(expected))
      return this._fail('out-of-order or altered source action');
    const scheduleIndex = this.cursor;
    const bytes = action.action === 'tick'
      ? bundleBytes(this.payload.bundles[action.bundle]) : new Uint8Array();
    this.inFlight = true;
    try {
      if (await this.native.queue(scheduleIndex, action, bytes) !== 1)
        return this._fail('native source action was refused');
      if (this.failed) return false;
      this.pending = true;
      return true;
    } catch (error) {
      return this._fail(`native queue rejected: ${error?.message ?? String(error)}`);
    } finally { this.inFlight = false; }
  }

  async accept(observation, hostComparison = null) {
    if (this.inFlight || !this.pending || this.failed || this.complete)
      return this._fail('unexpected or repeated native action receipt');
    const action = this.payload.schedule[this.cursor];
    if (this.mode === 'run2-full-schedule-v3' && (observation?.schema !== 'melee-web-stadium-source-action-native-v3' ||
        observation?.scope !== 'original-full7167-schedule-observation-only' || observation?.action_count !== 7167))
      return this._fail('Full schedule native observation scope differs');
    const result = observation?.last_action;
    const ordinal = this.mode === 'legacy-v1' ? action?.ordinal :
      action?.action === 'tick' ? action.bundle : action?.draw;
    if (this.mode !== 'legacy-v1') {
      const ticksBefore = this.payload.schedule.slice(0, this.cursor)
        .filter(row => row.action === 'tick').length;
      const drawsBefore = this.payload.schedule.slice(0, this.cursor)
        .filter(row => row.action === 'draw').length;
      const ticksAfter = ticksBefore + (action.action === 'tick' ? 1 : 0);
      const drawsAfter = drawsBefore + (action.action === 'draw' ? 1 : 0);
      const consumed = action.action === 'tick'
        ? this.payload.bundles[action.bundle].ports.map(port => port.status_hex).join('') : null;
      if (result?.source_ticks_before !== ticksBefore || result?.source_ticks_after !== ticksAfter ||
          result?.source_draws_before !== drawsBefore || result?.source_draws_after !== drawsAfter ||
          result?.world_ticks !== ticksAfter ||
          result?.source_tick_input_pad_hex !== consumed)
        return this._fail('native v2 receipt counters or literal consumed PAD differ from the manifest');
      const before = result.source_frames_before, after = result.source_frames_after;
      // Original fn_8016CD98 may keep the match clock still during a tick
      // (notably HUD0 Ready); draw/pending/ack intervals cannot advance it.
      if (!Number.isSafeInteger(before) || !Number.isSafeInteger(after) ||
          before < 0 || after < before || before > 0xffffffff || after > 0xffffffff ||
          before !== this.sourceFrame || result.source_frames !== after ||
          (action.action === 'tick' ? after - before > 1 : after !== before))
        return this._fail('native v2 original match clock transition or continuity differs');
    }
    if (observation?.failed || !result || result.complete !== true ||
        result.schedule_index !== this.cursor || result.action !== action.action ||
        result.ordinal !== ordinal ||
        (action.action === 'tick' && result.bundle !== action.bundle))
      return this._fail(observation?.error || 'native completion receipt differs from the pending action');
    const cIndex = this.mode === 'legacy-v1' ? 168 : this.payload.checkpoints.c_action_index;
    const fIndex = this.mode === 'legacy-v1' ? 170 : this.payload.checkpoints.f_action_index;
    const isBoundary = action.action === 'tick' && (this.cursor === cIndex || this.cursor === fIndex);
    let refusal = null;
    if (isBoundary) {
      const expectedLabel = this.cursor === cIndex ? 'C' : 'F';
      if (this.mode !== 'legacy-v1' &&
          result.observation?.idle_snapshot?.source_frame !== result.source_frames_after)
        refusal = `${expectedLabel} snapshot match clock differs from its action receipt`;
      if (hostComparison?.phase !== expectedLabel ||
          hostComparison?.decision !== 'PASS_EXACT_DECLARED_C_F_FIELDS')
        refusal = `${expectedLabel} host comparison failed or is absent`;
    } else if (hostComparison !== null) {
      refusal = 'host comparison was supplied outside C/F';
    }
    this.inFlight = true;
    if (refusal) this._fail(refusal);
    try {
      const approved = await this.native.approve(this.cursor, refusal === null);
      if (approved !== 1)
        return this._fail('native source owner refused the one-use acknowledgement');
      if (this.failed) return false;
      if (this.mode !== 'legacy-v1') this.sourceFrame = result.source_frames_after;
      this.pending = false;
      ++this.cursor;
      if (this.cursor === (this.mode === 'legacy-v1' ? PLAN_ACTIONS : this.payload.schedule.length))
        this.complete = true;
      return true;
    } catch (error) {
      return this._fail(`native approval rejected: ${error?.message ?? String(error)}`);
    } finally { this.inFlight = false; }
  }

  status() {
    return {cursor: this.cursor, armed: this.armed, pending: this.pending, in_flight: this.inFlight,
      failed: this.failed, complete: this.complete, first_error: this.firstError};
  }

  _fail(error) {
    this.failed = true;
    this.pending = false;
    this.firstError ??= error;
    return false;
  }
}
