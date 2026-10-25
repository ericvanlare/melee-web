import crypto from 'node:crypto';
import fs from 'node:fs/promises';
import path from 'node:path';

export const STADIUM_C3_REPORT_CHECKPOINT_SCHEMA =
  'melee-web-stadium-c3-css-sss-history-v1';
export const STADIUM_C3_FROZEN_SCENARIO_FIELDS = Object.freeze([
  'first_css_browser_stream',
  'first_css_final_pending_draw',
  'first_sss_constructor_pair',
  'first_sss_consumed_tick',
  'first_sss_draw',
  'first_sss_prefix',
  'first_sss_vs_entry',
]);
export const STADIUM_C3_DYNAMIC_RECORDS_SCHEMA =
  'melee-web-stadium-c3-append-only-records-v1';
const STADIUM_C3_DYNAMIC_ARRAYS = Object.freeze([
  'actions', 'runtime_diagnostics', 'host_comparisons',
]);
const STADIUM_C3_ZERO_HASH = '0'.repeat(64);

const hasOwn = (object, key) => Object.prototype.hasOwnProperty.call(object, key);
const exactKeys = (object, expected) => object !== null && typeof object === 'object' &&
  !Array.isArray(object) && Object.keys(object).length === expected.length &&
  expected.every(key => hasOwn(object, key));
const jsonClone = value => {
  const serialized = JSON.stringify(value);
  if (typeof serialized !== 'string')
    throw new Error('Stadium C3 frozen evidence is not JSON-serializable');
  return JSON.parse(serialized);
};
const freezeJsonValue = (value, seen = new WeakSet()) => {
  if (value === null || typeof value !== 'object' || seen.has(value)) return;
  const prototype = Object.getPrototypeOf(value);
  if (!Array.isArray(value) && prototype !== Object.prototype && prototype !== null)
    throw new Error('Stadium C3 frozen evidence contains a non-plain object');
  seen.add(value);
  for (const key of Object.keys(value)) freezeJsonValue(value[key], seen);
  Object.freeze(value);
};
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

export function isStadiumC3ReportCheckpointEligible(report) {
  const matched = report?.scenario?.matched_setup_cf;
  return (matched?.mode === 'run2-proper-prefix-v2' ||
    (matched?.mode === 'fresh132-full-a-v1' && matched.schedule_actions === 7168 &&
      matched.common_field_count === 15 && matched.seed_value_count === 1) ||
    ((matched?.mode === 'run2-full-schedule-v3' ||
      (matched?.mode === 'run2-full-common-fields-seed-v1' &&
       matched.common_field_count === 15 && matched.seed_value_count === 1)) &&
      matched.schedule_actions === 7167)) &&
    matched.explicit_step_mode === true;
}

function materializeFixedReportCheckpoint(checkpoint, sidecarBytes) {
  const marker = checkpoint?.stadium_c3_persistence_checkpoint;
  if (!exactKeys(marker, ['schema', 'sidecar', 'scenario_key_order']) ||
      marker.schema !== STADIUM_C3_REPORT_CHECKPOINT_SCHEMA)
    throw new Error('Stadium C3 checkpoint descriptor is missing or malformed');
  if (!exactKeys(marker.sidecar, ['basename', 'byte_length', 'sha256']) ||
      typeof marker.sidecar.basename !== 'string' ||
      !Number.isSafeInteger(marker.sidecar.byte_length) || marker.sidecar.byte_length < 1 ||
      !/^[0-9a-f]{64}$/.test(marker.sidecar.sha256))
    throw new Error('Stadium C3 sidecar descriptor is malformed');
  if (!Array.isArray(marker.scenario_key_order) ||
      marker.scenario_key_order.some(key => typeof key !== 'string') ||
      new Set(marker.scenario_key_order).size !== marker.scenario_key_order.length)
    throw new Error('Stadium C3 checkpoint scenario key order is malformed');
  if (!(sidecarBytes instanceof Uint8Array))
    throw new Error('Stadium C3 CSS/SSS sidecar bytes are missing or invalid');
  const verifiedSidecarBytes = Buffer.from(sidecarBytes);
  if (verifiedSidecarBytes.byteLength !== marker.sidecar.byte_length ||
      sha256(verifiedSidecarBytes) !== marker.sidecar.sha256)
    throw new Error('Stadium C3 CSS/SSS sidecar byte identity mismatch');
  let sidecarPayload;
  try { sidecarPayload = JSON.parse(verifiedSidecarBytes.toString('utf8')); }
  catch (error) { throw new Error(`Stadium C3 CSS/SSS sidecar JSON is invalid: ${error.message}`); }
  if (!exactKeys(sidecarPayload, ['schema', 'scenario_fields']) ||
      sidecarPayload.schema !== STADIUM_C3_REPORT_CHECKPOINT_SCHEMA ||
      !exactKeys(sidecarPayload.scenario_fields, STADIUM_C3_FROZEN_SCENARIO_FIELDS))
    throw new Error('Stadium C3 CSS/SSS sidecar is malformed');
  const scenario = checkpoint.scenario;
  if (!scenario || typeof scenario !== 'object' || Array.isArray(scenario))
    throw new Error('Stadium C3 checkpoint scenario is malformed');
  const frozen = new Set(STADIUM_C3_FROZEN_SCENARIO_FIELDS);
  if (STADIUM_C3_FROZEN_SCENARIO_FIELDS.some(key => hasOwn(scenario, key)))
    throw new Error('Stadium C3 checkpoint duplicates a frozen scenario field');
  const expectedScenarioKeys = new Set([...Object.keys(scenario), ...frozen]);
  if (expectedScenarioKeys.size !== marker.scenario_key_order.length ||
      marker.scenario_key_order.some(key => !expectedScenarioKeys.has(key)))
    throw new Error('Stadium C3 checkpoint key order does not cover its scenario fields');
  const materializedScenario = {};
  for (const key of marker.scenario_key_order) {
    if (frozen.has(key)) materializedScenario[key] = sidecarPayload.scenario_fields[key];
    else if (hasOwn(scenario, key)) materializedScenario[key] = scenario[key];
    else throw new Error(`Stadium C3 checkpoint is missing scenario field ${key}`);
  }
  const materialized = {...checkpoint};
  delete materialized.stadium_c3_persistence_checkpoint;
  materialized.scenario = materializedScenario;
  return materialized;
}

function validateDynamicDescriptor(value) {
  if (!exactKeys(value, ['schema', 'basename', 'byte_length', 'chain_sha256',
    'segment_count', 'record_counts', 'matched_setup_cf_key_order']) ||
      value.schema !== STADIUM_C3_DYNAMIC_RECORDS_SCHEMA ||
      typeof value.basename !== 'string' ||
      value.basename.length === 0 || path.basename(value.basename) !== value.basename ||
      !Number.isSafeInteger(value.byte_length) || value.byte_length < 0 ||
      !/^[0-9a-f]{64}$/.test(value.chain_sha256) ||
      !Number.isSafeInteger(value.segment_count) || value.segment_count < 0 ||
      !exactKeys(value.record_counts, STADIUM_C3_DYNAMIC_ARRAYS) ||
      STADIUM_C3_DYNAMIC_ARRAYS.some(key =>
        !Number.isSafeInteger(value.record_counts[key]) || value.record_counts[key] < 0) ||
      !Array.isArray(value.matched_setup_cf_key_order) ||
      value.matched_setup_cf_key_order.some(key => typeof key !== 'string') ||
      new Set(value.matched_setup_cf_key_order).size !== value.matched_setup_cf_key_order.length)
    throw new Error('Stadium C3 dynamic-record descriptor is malformed');
}

function appendChainHash(previousHash, segmentBytes) {
  return sha256(Buffer.concat([Buffer.from(previousHash, 'ascii'), Buffer.from(segmentBytes)]));
}

function decodeDynamicRecords(descriptor, bytes) {
  validateDynamicDescriptor(descriptor);
  if (!(bytes instanceof Uint8Array))
    throw new Error('Stadium C3 dynamic-record sidecar bytes are missing or invalid');
  const source = Buffer.from(bytes);
  if (source.byteLength !== descriptor.byte_length)
    throw new Error(source.byteLength > descriptor.byte_length
      ? 'Stadium C3 dynamic-record sidecar has an uncommitted tail or changed length'
      : 'Stadium C3 dynamic-record sidecar is truncated');
  const records = Object.fromEntries(STADIUM_C3_DYNAMIC_ARRAYS.map(key => [key, []]));
  let previousHash = STADIUM_C3_ZERO_HASH;
  let offset = 0;
  let segments = 0;
  while (offset < source.byteLength) {
    const newline = source.indexOf(0x0a, offset);
    if (newline < 0) throw new Error('Stadium C3 dynamic-record sidecar has a truncated segment');
    const segmentBytes = source.subarray(offset, newline + 1);
    let segment;
    try { segment = JSON.parse(segmentBytes.toString('utf8')); }
    catch (error) { throw new Error(`Stadium C3 dynamic-record segment JSON is invalid: ${error.message}`); }
    if (!exactKeys(segment, ['schema', 'sequence', 'previous_sha256', 'records']) ||
        segment.schema !== STADIUM_C3_DYNAMIC_RECORDS_SCHEMA ||
        segment.sequence !== segments || segment.previous_sha256 !== previousHash ||
        !exactKeys(segment.records, STADIUM_C3_DYNAMIC_ARRAYS) ||
        STADIUM_C3_DYNAMIC_ARRAYS.some(key => !Array.isArray(segment.records[key])))
      throw new Error('Stadium C3 dynamic-record segment order or shape is invalid');
    for (const key of STADIUM_C3_DYNAMIC_ARRAYS) {
      for (const record of segment.records[key]) records[key].push(record);
    }
    previousHash = appendChainHash(previousHash, segmentBytes);
    ++segments;
    offset = newline + 1;
  }
  const actualCounts = Object.fromEntries(STADIUM_C3_DYNAMIC_ARRAYS.map(key => [key, records[key].length]));
  if (segments !== descriptor.segment_count ||
      STADIUM_C3_DYNAMIC_ARRAYS.some(key => actualCounts[key] !== descriptor.record_counts[key]) ||
      previousHash !== descriptor.chain_sha256)
    throw new Error('Stadium C3 dynamic-record count or hash-chain identity mismatch');
  return records;
}

export function materializeStadiumC3ReportCheckpoint(checkpoint, sidecarBytes, dynamicRecordBytes = null) {
  const marker = checkpoint?.stadium_c3_persistence_checkpoint;
  if (!marker || !hasOwn(marker, 'dynamic_records')) {
    const matched = checkpoint?.scenario?.matched_setup_cf;
    if (matched && STADIUM_C3_DYNAMIC_ARRAYS.some(key => !hasOwn(matched, key)))
      throw new Error('Stadium C3 dynamic-record descriptor is missing from a compact checkpoint');
    return materializeFixedReportCheckpoint(checkpoint, sidecarBytes);
  }
  if (!exactKeys(marker, ['schema', 'sidecar', 'scenario_key_order', 'dynamic_records']))
    throw new Error('Stadium C3 checkpoint descriptor is missing or malformed');
  const records = decodeDynamicRecords(marker.dynamic_records, dynamicRecordBytes);
  const fixedCheckpoint = {...checkpoint,
    stadium_c3_persistence_checkpoint: {
      schema: marker.schema, sidecar: marker.sidecar, scenario_key_order: marker.scenario_key_order,
    }};
  const materialized = materializeFixedReportCheckpoint(fixedCheckpoint, sidecarBytes);
  const matched = materialized.scenario?.matched_setup_cf;
  if (!matched || typeof matched !== 'object' || Array.isArray(matched))
    throw new Error('Stadium C3 dynamic-record checkpoint has no matched Setup C/F object');
  const keyOrder = marker.dynamic_records.matched_setup_cf_key_order;
  const arrayNames = new Set(STADIUM_C3_DYNAMIC_ARRAYS);
  const expected = new Set([...Object.keys(matched), ...STADIUM_C3_DYNAMIC_ARRAYS]);
  if (expected.size !== keyOrder.length || keyOrder.some(key => !expected.has(key)) ||
      STADIUM_C3_DYNAMIC_ARRAYS.some(key => hasOwn(matched, key)))
    throw new Error('Stadium C3 matched Setup C/F dynamic-record key order is invalid');
  const restored = {};
  for (const key of keyOrder) {
    if (arrayNames.has(key)) restored[key] = records[key];
    else if (hasOwn(matched, key)) restored[key] = matched[key];
    else throw new Error(`Stadium C3 matched Setup C/F checkpoint is missing ${key}`);
  }
  materialized.scenario.matched_setup_cf = restored;
  return materialized;
}

export function recoverStadiumC3ReportCheckpoint(checkpoint, sidecarBytes, dynamicRecordBytes = null) {
  const marker = checkpoint?.stadium_c3_persistence_checkpoint;
  if (!marker || !hasOwn(marker, 'dynamic_records'))
    return {report: materializeStadiumC3ReportCheckpoint(checkpoint, sidecarBytes, dynamicRecordBytes),
      uncommittedDynamicTail: null};
  validateDynamicDescriptor(marker.dynamic_records);
  if (!(dynamicRecordBytes instanceof Uint8Array))
    throw new Error('Stadium C3 dynamic-record sidecar bytes are missing or invalid');
  const source = Buffer.from(dynamicRecordBytes);
  const committedLength = marker.dynamic_records.byte_length;
  if (source.byteLength < committedLength)
    throw new Error('Stadium C3 dynamic-record sidecar is truncated before the committed checkpoint');
  const committedBytes = source.subarray(0, committedLength);
  const report = materializeStadiumC3ReportCheckpoint(checkpoint, sidecarBytes, committedBytes);
  const tail = source.subarray(committedLength);
  return {
    report,
    uncommittedDynamicTail: tail.byteLength === 0 ? null : {
      basename: marker.dynamic_records.basename,
      byte_length: tail.byteLength,
      sha256: sha256(tail),
      preserved_in_source_sidecar: true,
    },
    committedRecordCounts: {...marker.dynamic_records.record_counts},
    committedSegmentCount: marker.dynamic_records.segment_count,
  };
}

export function createStadiumC3ReportCheckpoint({reportPath, fileSystem = fs}) {
  if (typeof reportPath !== 'string' || reportPath.length === 0)
    throw new TypeError('Stadium C3 report path is required');
  const sidecarPath = `${reportPath}.stadium-c3-css-sss-history-v1.json`;
  const sidecarTemporary = `${sidecarPath}.tmp`;
  const dynamicRecordPath = `${reportPath}.stadium-c3-action-records-v1.jsonl`;
  const dynamicRecordTemporary = `${dynamicRecordPath}.tmp`;
  let frozenState = null;
  let dynamicState = null;
  let pendingPreparation = null;
  let dynamicAppendError = null;
  let initializationError = null;
  let checkpointSequence = 0;

  const shouldUse = report => frozenState !== null ||
    initializationError !== null || isStadiumC3ReportCheckpointEligible(report);

  async function initialize(report) {
    const scenario = report?.scenario;
    if (!scenario || typeof scenario !== 'object' || Array.isArray(scenario))
      throw new Error('Stadium C3 report scenario is missing at the pre-action checkpoint');
    const scenarioFields = {};
    for (const key of STADIUM_C3_FROZEN_SCENARIO_FIELDS) {
      if (!hasOwn(scenario, key))
        throw new Error(`Stadium C3 pre-action checkpoint is missing ${key}`);
      const serialized = JSON.stringify(scenario[key]);
      if (typeof serialized !== 'string')
        throw new Error(`Stadium C3 pre-action field ${key} is not JSON-serializable`);
      scenarioFields[key] = JSON.parse(serialized);
    }
    const sidecarPayload = {
      schema: STADIUM_C3_REPORT_CHECKPOINT_SCHEMA,
      scenario_fields: scenarioFields,
    };
    const sidecarBytes = Buffer.from(`${JSON.stringify(sidecarPayload)}\n`);
    try {
      await fileSystem.access(sidecarPath);
      throw new Error(`Refusing to overwrite existing Stadium C3 sidecar ${path.basename(sidecarPath)}`);
    } catch (error) {
      if (error?.code !== 'ENOENT') throw error;
    }
    try {
      await fileSystem.writeFile(sidecarTemporary, sidecarBytes);
      await fileSystem.rename(sidecarTemporary, sidecarPath);
    } catch (error) {
      initializationError = error;
      throw error;
    }
    const matched = scenario.matched_setup_cf;
    if (!matched || typeof matched !== 'object' || Array.isArray(matched) ||
        STADIUM_C3_DYNAMIC_ARRAYS.some(key => !Array.isArray(matched[key])))
      throw new Error('Stadium C3 pre-action report is missing append-only matched Setup C/F arrays');
    try {
      await fileSystem.access(dynamicRecordPath);
      throw new Error(`Refusing to overwrite existing Stadium C3 dynamic-record sidecar ${path.basename(dynamicRecordPath)}`);
    } catch (error) {
      if (error?.code !== 'ENOENT') throw error;
    }
    try {
      await fileSystem.writeFile(dynamicRecordTemporary, Buffer.alloc(0));
      await fileSystem.rename(dynamicRecordTemporary, dynamicRecordPath);
    } catch (error) {
      initializationError = error;
      throw error;
    }
    dynamicState = {
      matched,
      arrays: Object.fromEntries(STADIUM_C3_DYNAMIC_ARRAYS.map(key => [key, matched[key]])),
      recordCounts: Object.fromEntries(STADIUM_C3_DYNAMIC_ARRAYS.map(key => [key, 0])),
      byteLength: 0,
      chainSha256: STADIUM_C3_ZERO_HASH,
      segmentCount: 0,
      matchedKeyOrder: Object.keys(matched),
    };
    for (const key of STADIUM_C3_FROZEN_SCENARIO_FIELDS) {
      freezeJsonValue(scenario[key]);
      Object.defineProperty(scenario, key, {writable: false, configurable: false});
    }
    frozenState = {
      payload: sidecarPayload,
      scenario,
      matchedMode: matched.mode,
      frozenRoots: Object.fromEntries(STADIUM_C3_FROZEN_SCENARIO_FIELDS.map(
        key => [key, scenario[key]])),
      sidecar: {
        basename: path.basename(sidecarPath),
        byte_length: sidecarBytes.byteLength,
        sha256: sha256(sidecarBytes),
      },
    };
  }

  function assertFrozenUnchanged(report) {
    const scenario = report?.scenario;
    if (!scenario || typeof scenario !== 'object' || Array.isArray(scenario))
      throw new Error('Stadium C3 report scenario disappeared after pre-action freeze');
    if (scenario !== frozenState.scenario)
      throw new Error('Stadium C3 report scenario root identity changed after pre-action freeze');
    for (const key of STADIUM_C3_FROZEN_SCENARIO_FIELDS) {
      if (!Object.is(scenario[key], frozenState.frozenRoots[key]))
        throw new Error(`Stadium C3 frozen pre-action root identity changed: ${key}`);
      const descriptor = Object.getOwnPropertyDescriptor(scenario, key);
      if (!descriptor || descriptor.writable !== false || descriptor.configurable !== false ||
          (scenario[key] && typeof scenario[key] === 'object' && !Object.isFrozen(scenario[key])))
        throw new Error(`Stadium C3 frozen pre-action field changed: ${key}`);
    }
    const matched = scenario.matched_setup_cf;
    if (matched?.mode !== frozenState.matchedMode ||
        !isStadiumC3ReportCheckpointEligible(report))
      throw new Error('Stadium C3 explicit run2 checkpoint identity changed after freeze');
  }

  function assertMatchedKeyOrderCanAdvance(matched) {
    const current = Object.keys(matched);
    const previous = dynamicState.matchedKeyOrder;
    if (current.length < previous.length || previous.some((key, index) => current[index] !== key))
      throw new Error('Stadium C3 matched Setup C/F existing metadata key order changed');
    return current;
  }

  function freezeNewDynamicRecords(matched) {
    for (const key of STADIUM_C3_DYNAMIC_ARRAYS) {
      const records = matched[key];
      for (let index = dynamicState.recordCounts[key]; index < records.length; ++index) {
        const descriptor = Object.getOwnPropertyDescriptor(records, String(index));
        if (!descriptor || descriptor.writable !== true || descriptor.configurable !== true)
          throw new Error(`Stadium C3 newly appended ${key} record ${index} is not an ordinary writable array entry`);
        freezeJsonValue(records[index]);
        Object.defineProperty(records, String(index), {writable: false, configurable: false});
      }
    }
  }

  async function prepare(report, {terminal = false} = {}) {
    if (!shouldUse(report)) return report;
    if (initializationError) throw initializationError;
    if (!frozenState) {
      if (!isStadiumC3ReportCheckpointEligible(report))
        throw new Error('Stadium C3 checkpoint state became active outside the explicit run2 prefix');
      try { await initialize(report); }
      catch (error) {
        initializationError ??= error;
        throw error;
      }
    }
    assertFrozenUnchanged(report);
    const scenarioKeyOrder = Object.keys(report.scenario);
    const matched = report.scenario.matched_setup_cf;
    if (!dynamicState || matched !== dynamicState.matched ||
        STADIUM_C3_DYNAMIC_ARRAYS.some(key => matched[key] !== dynamicState.arrays[key]))
      throw new Error('Stadium C3 append-only report arrays were replaced');
    const matchedKeyOrder = assertMatchedKeyOrderCanAdvance(matched);
    if (STADIUM_C3_DYNAMIC_ARRAYS.some(key =>
      matched[key].length < dynamicState.recordCounts[key]))
      throw new Error('Stadium C3 append-only report records were removed');
    if (terminal) {
      const terminalCheckpoint = {...report, scenario: {...report.scenario}};
      for (const key of STADIUM_C3_FROZEN_SCENARIO_FIELDS) delete terminalCheckpoint.scenario[key];
      terminalCheckpoint.stadium_c3_persistence_checkpoint = {
        schema: STADIUM_C3_REPORT_CHECKPOINT_SCHEMA,
        sidecar: {...frozenState.sidecar},
        scenario_key_order: scenarioKeyOrder,
      };
      const sidecarBytes = Buffer.from(await fileSystem.readFile(sidecarPath));
      return materializeFixedReportCheckpoint(terminalCheckpoint, sidecarBytes);
    }
    if (pendingPreparation)
      throw new Error('Stadium C3 previous report checkpoint has not committed');
    if (dynamicAppendError) throw dynamicAppendError;
    freezeNewDynamicRecords(matched);
    const deltas = Object.fromEntries(STADIUM_C3_DYNAMIC_ARRAYS.map(key =>
      [key, matched[key].slice(dynamicState.recordCounts[key])]));
    const hasDelta = STADIUM_C3_DYNAMIC_ARRAYS.some(key => deltas[key].length > 0);
    let stagedState = {...dynamicState, matchedKeyOrder};
    let segmentBytes = null;
    if (hasDelta) {
      const segment = {
        schema: STADIUM_C3_DYNAMIC_RECORDS_SCHEMA,
        sequence: dynamicState.segmentCount,
        previous_sha256: dynamicState.chainSha256,
        records: deltas,
      };
      const serializedSegment = JSON.stringify(segment);
      if (typeof serializedSegment !== 'string')
        throw new Error('Stadium C3 dynamic-record segment is not JSON-serializable');
      segmentBytes = Buffer.from(`${serializedSegment}\n`);
      stagedState = {
        ...stagedState,
        recordCounts: Object.fromEntries(STADIUM_C3_DYNAMIC_ARRAYS.map(key =>
          [key, matched[key].length])),
        byteLength: dynamicState.byteLength + segmentBytes.byteLength,
        chainSha256: appendChainHash(dynamicState.chainSha256, segmentBytes),
        segmentCount: dynamicState.segmentCount + 1,
      };
    }
    const scenarioCheckpoint = {...report.scenario};
    const matchedCheckpoint = {...matched};
    for (const key of STADIUM_C3_DYNAMIC_ARRAYS) delete matchedCheckpoint[key];
    scenarioCheckpoint.matched_setup_cf = matchedCheckpoint;
    for (const key of STADIUM_C3_FROZEN_SCENARIO_FIELDS) delete scenarioCheckpoint[key];
    const checkpoint = {...report, scenario: scenarioCheckpoint};
    checkpoint.stadium_c3_persistence_checkpoint = {
      schema: STADIUM_C3_REPORT_CHECKPOINT_SCHEMA,
      sidecar: {...frozenState.sidecar},
      scenario_key_order: scenarioKeyOrder,
      dynamic_records: {
        schema: STADIUM_C3_DYNAMIC_RECORDS_SCHEMA,
        basename: path.basename(dynamicRecordPath),
        byte_length: stagedState.byteLength,
        chain_sha256: stagedState.chainSha256,
        segment_count: stagedState.segmentCount,
        record_counts: {...stagedState.recordCounts},
        matched_setup_cf_key_order: stagedState.matchedKeyOrder,
      },
    };
    if (segmentBytes) {
      try { await fileSystem.appendFile(dynamicRecordPath, segmentBytes); }
      catch (error) {
        dynamicAppendError ??= error;
        throw error;
      }
    }
    pendingPreparation = {checkpoint, state: stagedState};
    return checkpoint;
  }

  function commitPrepared(checkpoint) {
    if (!pendingPreparation || pendingPreparation.checkpoint !== checkpoint)
      throw new Error('Stadium C3 report checkpoint commit does not match the prepared checkpoint');
    dynamicState = pendingPreparation.state;
    pendingPreparation = null;
  }

  return Object.freeze({
    shouldUse,
    prepare,
    commitPrepared,
    nextReportTemporaryPath() {
      if (!frozenState && !initializationError)
        throw new Error('Stadium C3 report checkpoint is not active');
      checkpointSequence += 1;
      return `${reportPath}.stadium-c3-checkpoint-${String(checkpointSequence).padStart(6, '0')}.tmp`;
    },
    get sidecarPath() { return sidecarPath; },
    get dynamicRecordPath() { return dynamicRecordPath; },
  });
}
