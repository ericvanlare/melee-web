#!/usr/bin/env node
/*
 * Diagnostic-only source fixture probe for the typed gameplay bootstrap owner.
 * It runs the existing synchronous initialization/Ready path and reads the
 * 128-byte producer twice at one quiescent boundary. It never restores state.
 */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';

const usage = 'Usage: gameplay_bootstrap_state_probe.mjs --runtime FILE --assets DIR --probe-source FILE --source-c FILE --source-h FILE --out ABSENT_DIR';
const args = process.argv.slice(2);
const options = {};
for (let i = 0; i < args.length; i += 2) {
  const key = args[i];
  const value = args[i + 1];
  if (!['--runtime', '--assets', '--probe-source', '--source-c', '--source-h', '--out'].includes(key) || !value || options[key]) {
    throw new Error(usage);
  }
  options[key] = path.resolve(value);
}
for (const key of ['--runtime', '--assets', '--probe-source', '--source-c', '--source-h', '--out']) {
  if (!options[key]) throw new Error(usage);
}
const regular = (file, label) => {
  const stat = fs.lstatSync(file);
  if (!stat.isFile() || stat.isSymbolicLink()) throw new Error(`${label} must be a regular file: ${file}`);
};
for (const [key, label] of [['--runtime', 'runtime'], ['--probe-source', 'probe source'], ['--source-c', 'producer C source'], ['--source-h', 'producer header']]) regular(options[key], label);
if (!fs.lstatSync(options['--assets']).isDirectory() || fs.lstatSync(options['--assets']).isSymbolicLink()) throw new Error(`assets must be a local directory: ${options['--assets']}`);
if (fs.existsSync(options['--out']) || fs.lstatSync(options['--out'], {throwIfNoEntry: false})) throw new Error(`output must be absent: ${options['--out']}`);
fs.mkdirSync(options['--out']);
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const fileSha256 = file => sha256(fs.readFileSync(file));
const report = {
  schema: 'source-gameplay-bootstrap-state-observation-v2', result: 'incomplete',
  evidence_label: 'Source identified', runtime_admission: false,
  execution_scope: 'compiled Wasm diagnostic fixture; no original native/Dolphin trace',
  source_drawing: false, browser_gameplay: false,
  runtime: null, identities: null, ready_steps: null,
  captures: [], refusals: [], quiescence: null, cleanup: null,
  runtime_log: [], runtime_log_bytes: 0,
};
const write = () => fs.writeFileSync(path.join(options['--out'], 'bootstrap-state-report.json'), JSON.stringify(report, null, 2) + '\n');
write();
let module = null;
let opened = false;
const readState = (bytes) => {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const u32 = offset => view.getUint32(offset, true);
  const u64 = offset => view.getBigUint64(offset, true).toString();
  return {
    abi_version: u32(0), abi_size: u32(4), schema: u32(8), reserved0: u32(12),
    ticks: u64(16), disabled_links: u64(24), generation: u64(32), allocation_generation: u64(40),
    arena_bytes: u64(48), session_bytes: u64(56), arena_identity: u32(64), session_identity: u32(68),
    heap_handle: view.getInt32(72, true), object_kind_count: u32(76),
    stepping: u32(80), shutting_down: u32(84), tables_live: u32(88), vs_startup_pending: u32(92),
    startup_in_progress: u32(96), vs_sis_live: u32(100), vs_dynamics_ready: u32(104), vs_manager_ready: u32(108),
    vs_startup_callback: u32(112), vs_shutdown_callback: u32(116), finish_hsd_objects: u32(120), reserved1: u32(124),
    raw_length: bytes.length, raw_hex: Buffer.from(bytes).toString('hex'),
  };
};
const checked = (fn, label) => {
  const result = fn();
  if (result !== 1) throw new Error(`${label} failed: ${module ? module.UTF8ToString(module._melee_web_snapshot_error()) : 'no module error'}`);
  return result;
};
const requireRefusal = (label, capture) => {
  const ptr = module._malloc(128);
  if (!ptr) throw new Error('could not allocate the diagnostic output');
  try {
    module.HEAPU8.fill(0xa5, ptr, ptr + 128);
    if (capture(ptr) !== 0) throw new Error(`${label} unexpectedly accepted`);
    if (!module.HEAPU8.slice(ptr, ptr + 128).every(byte => byte === 0xa5)) {
      throw new Error(`${label} modified its refused output`);
    }
    report.refusals.push({label, returned_zero: true, output_unchanged: true});
  } finally { module._free(ptr); }
};
const recordLog = (kind, message) => {
  const text = String(message);
  report.runtime_log_bytes += Buffer.byteLength(text);
  // Diagnostic retention cap; this is not an authored source table bound.
  if (report.runtime_log_bytes > 65536) throw new Error('runtime diagnostic log exceeds 64 KiB');
  report.runtime_log.push({kind, text});
};
const requireState = state => {
  if (state.abi_version !== 1 || state.abi_size !== 128 || state.schema !== 0x47504253 || state.reserved0 !== 0 || state.reserved1 !== 0) throw new Error('bootstrap ABI/schema/padding mismatch');
  for (const key of ['stepping', 'shutting_down', 'tables_live', 'vs_startup_pending', 'startup_in_progress', 'vs_sis_live', 'vs_dynamics_ready', 'vs_manager_ready']) if (![0, 1].includes(state[key])) throw new Error(`bootstrap flag is not 0/1: ${key}`);
  if (state.stepping || state.shutting_down || state.startup_in_progress || state.tables_live !== 1) throw new Error('bootstrap observation is not a published quiescent world');
  if (!state.arena_identity || state.heap_handle < 0 || state.generation === '0' || state.allocation_generation === '0') throw new Error('bootstrap arena/generation ownership is not live');
  for (const key of ['arena_bytes', 'session_bytes']) if (state[key] !== '0' && (BigInt(state[key]) < 65536n || BigInt(state[key]) > 64n * 1024n * 1024n)) throw new Error(`bootstrap ${key} outside authored bounds`);
  if (state.session_identity === 0 && state.session_bytes !== '0') throw new Error('session bytes lack identity');
  if (state.vs_sis_live && (!state.vs_manager_ready || !state.tables_live)) throw new Error('VS SIS/manager phase mismatch');
  if (state.vs_dynamics_ready && !state.vs_sis_live) throw new Error('VS dynamics phase mismatch');
  if ((state.vs_startup_callback === 0) !== (state.vs_shutdown_callback === 0)) throw new Error('VS callback identities are unpaired');
};
try {
  const wasmPath = options['--runtime'].replace(/\.js$/, '.wasm');
  regular(wasmPath, 'runtime Wasm');
  report.runtime = {js_sha256: fileSha256(options['--runtime']), wasm_sha256: fileSha256(wasmPath)};
  report.identities = {
    probe_source_sha256: fileSha256(options['--probe-source']),
    source_c_sha256: fileSha256(options['--source-c']),
    source_h_sha256: fileSha256(options['--source-h']),
  };
  const factory = createRequire(import.meta.url)(options['--runtime']);
  module = await factory({print: message => recordLog('stdout', message), printErr: message => recordLog('stderr', message)});
  const probeIdentity = module.UTF8ToString(module._melee_web_gameplay_bootstrap_state_probe_identity());
  const sourceCIdentity = module.UTF8ToString(module._melee_web_gameplay_bootstrap_state_source_c_identity());
  const sourceHIdentity = module.UTF8ToString(module._melee_web_gameplay_bootstrap_state_source_h_identity());
  if (probeIdentity !== report.identities.probe_source_sha256 ||
      sourceCIdentity !== report.identities.source_c_sha256 ||
      sourceHIdentity !== report.identities.source_h_sha256) throw new Error('compiled probe/source identity mismatch');
  if (module._melee_web_gameplay_bootstrap_state_abi_size() !== 128 || module._melee_web_gameplay_bootstrap_state_abi_version() !== 1 || module._melee_web_gameplay_bootstrap_state_schema() !== 0x47504253) throw new Error('compiled bootstrap ABI exports mismatch');
  requireRefusal('before world initialization', ptr => module._melee_web_gameplay_bootstrap_state_capture(ptr, 128));
  checked(() => module.ccall('melee_web_snapshot_init', 'number', ['string'], [options['--assets']]), 'source fixture init');
  opened = true;
  const ready = () => new DataView(module.HEAPU8.buffer).getUint32(module._melee_web_snapshot_observation() + 8, true);
  let steps = 0;
  while (!ready() && steps < 600) { checked(() => module._melee_web_snapshot_step(steps), 'ordinary source step'); steps += 1; }
  if (!ready()) throw new Error('source fixture did not reach Ready within 600 ordinary steps');
  report.ready_steps = steps;
  checked(() => module._melee_web_snapshot_quiescent(), 'source quiescence');
  requireRefusal('wrong output size', ptr => module._melee_web_gameplay_bootstrap_state_capture(ptr, 127));
  requireRefusal('null output', () => module._melee_web_gameplay_bootstrap_state_capture(0, 128));
  const size = module._melee_web_gameplay_bootstrap_state_abi_size();
  const ptr = module._malloc(size);
  if (!ptr) throw new Error('could not allocate the diagnostic output');
  try {
    const captures = [];
    for (let i = 0; i < 2; i += 1) {
      checked(() => module._melee_web_gameplay_bootstrap_state_capture(ptr, size), `bootstrap capture ${i}`);
      const copy = Buffer.from(module.HEAPU8.slice(ptr, ptr + size));
      const state = readState(copy);
      requireState(state); captures.push({state, bytes: copy});
    }
    if (!captures[0].bytes.equals(captures[1].bytes)) throw new Error('consecutive bootstrap captures differ');
    report.captures = captures.map(item => item.state);
    report.quiescence = {stable_bytes: true, generation: captures[0].state.generation, ticks: captures[0].state.ticks};
  } finally { module._free(ptr); }
  checked(() => module._melee_web_snapshot_close(), 'source close'); opened = false;
  requireRefusal('after world close', ptr => module._melee_web_gameplay_bootstrap_state_capture(ptr, 128));
  report.cleanup = 'source match/audio/world closed'; report.result = 'passed';
} catch (error) {
  report.result = 'failed'; report.failure = String(error.stack || error);
  if (module && opened) { try { report.cleanup = module._melee_web_snapshot_close() === 1 ? 'source match/audio/world closed after failure' : module.UTF8ToString(module._melee_web_snapshot_error()); } catch (cleanupError) { report.cleanup = String(cleanupError.stack || cleanupError); } }
  process.exitCode = 1;
} finally { write(); console.log(JSON.stringify({result: report.result, output: options['--out'], failure: report.failure})); }
