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

export function compareOriginalIdleSnapshot({label, expected, expectedGo, expectedSeedHex, actual}) {
  if (!['C', 'F'].includes(label)) throw new TypeError('only C and F are comparable boundaries');
  if (!expected || expected.label !== label || !expectedGo || expectedGo.label !== 'GO' ||
      !actual || typeof actual !== 'object')
    throw new TypeError(`${label} comparison lacks its exact host-only source records`);
  const ordinal = label === 'C' ? 84 : 85;
  const goSequence = requiredInteger(expectedGo.sequence, 'original GO event sequence');
  const rowSequence = requiredInteger(expected.sequence, `${label} original state sequence`);
  if (goSequence !== 2925 || rowSequence !== (label === 'C' ? 2926 : 2931) ||
      expectedGo.source_tick !== 84 || expectedGo.draw_ordinal !== 84 || expected.source_tick !== ordinal ||
      expected.draw_ordinal !== ordinal || actual.ordinal !== ordinal || actual.label !== label)
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
      go.go_after?.world_ticks !== 84 || idle.world_ticks !== ordinal + 1 ||
      [go.stage_before, go.stage_after, go.go_after].some(event =>
        event?.world_ticks !== 84 || event?.generation !== idle.world_generation) ||
      go.next_sequence !== 3 ||
      go.stage_before?.sequence >= go.go_after?.sequence ||
      go.stage_after?.sequence >= go.go_after?.sequence)
    mismatch ??= {path: 'go_trace.membership', expected: 'one ordered GO event in tick 84',
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

export function createHostCallbacks({setupApproved, originalSnapshots, originalSeeds}) {
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
          expectedSeedHex: seeds.get(phase), actual});
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

export function hostEvidenceFromPacket(packet) {
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

function bundleBytes(bundle) {
  const hex = bundle.ports.map(port => port.status_hex).join('');
  return Uint8Array.from(hex.match(/.{2}/g), pair => Number.parseInt(pair, 16));
}

export class StadiumSetupCfActionDriver {
  constructor({runtimePayload, native}) {
    this.payload = validateRuntimePayload(runtimePayload);
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
  }

  async arm(setupApproved) {
    if (this.inFlight || this.armed || this.failed || this.complete)
      return this._fail('duplicate or terminal arm');
    if (setupApproved !== true)
      return this._fail('fresh completed Setup owner/approval was refused');
    this.inFlight = true;
    try {
      if (await this.native.arm() !== 1)
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
    const result = observation?.last_action;
    if (observation?.failed || !result || result.complete !== true ||
        result.schedule_index !== this.cursor || result.action !== action.action ||
        result.ordinal !== action.ordinal ||
        (action.action === 'tick' && result.bundle !== action.bundle))
      return this._fail(observation?.error || 'native completion receipt differs from the pending action');
    const isBoundary = action.action === 'tick' && (action.ordinal === 84 || action.ordinal === 85);
    let refusal = null;
    if (isBoundary) {
      const expectedLabel = action.ordinal === 84 ? 'C' : 'F';
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
      this.pending = false;
      ++this.cursor;
      if (this.cursor === PLAN_ACTIONS) this.complete = true;
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
