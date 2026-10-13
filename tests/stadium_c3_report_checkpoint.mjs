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
  return matched?.mode === 'run2-proper-prefix-v2' &&
    matched.explicit_step_mode === true;
}

export function materializeStadiumC3ReportCheckpoint(checkpoint, sidecarBytes) {
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

export function createStadiumC3ReportCheckpoint({reportPath, fileSystem = fs}) {
  if (typeof reportPath !== 'string' || reportPath.length === 0)
    throw new TypeError('Stadium C3 report path is required');
  const sidecarPath = `${reportPath}.stadium-c3-css-sss-history-v1.json`;
  const sidecarTemporary = `${sidecarPath}.tmp`;
  let frozenState = null;
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
    for (const key of STADIUM_C3_FROZEN_SCENARIO_FIELDS) {
      freezeJsonValue(scenario[key]);
      Object.defineProperty(scenario, key, {writable: false, configurable: false});
    }
    frozenState = {
      payload: sidecarPayload,
      scenario,
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
    if (matched?.mode !== 'run2-proper-prefix-v2' || matched.explicit_step_mode !== true)
      throw new Error('Stadium C3 explicit run2 checkpoint identity changed after freeze');
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
    const checkpoint = {...report, scenario: {...report.scenario}};
    for (const key of STADIUM_C3_FROZEN_SCENARIO_FIELDS) delete checkpoint.scenario[key];
    checkpoint.stadium_c3_persistence_checkpoint = {
      schema: STADIUM_C3_REPORT_CHECKPOINT_SCHEMA,
      sidecar: {...frozenState.sidecar},
      scenario_key_order: scenarioKeyOrder,
    };
    if (!terminal) return checkpoint;

    const sidecarBytes = Buffer.from(await fileSystem.readFile(sidecarPath));
    return materializeStadiumC3ReportCheckpoint(checkpoint, sidecarBytes);
  }

  return Object.freeze({
    shouldUse,
    prepare,
    nextReportTemporaryPath() {
      if (!frozenState && !initializationError)
        throw new Error('Stadium C3 report checkpoint is not active');
      checkpointSequence += 1;
      return `${reportPath}.stadium-c3-checkpoint-${String(checkpointSequence).padStart(6, '0')}.tmp`;
    },
    get sidecarPath() { return sidecarPath; },
  });
}
