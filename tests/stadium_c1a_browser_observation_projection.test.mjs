import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
import test from 'node:test';

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const RUNNER = process.env.MELEE_STADIUM_NATIVE_OBSERVATION_RUNNER_SOURCE ||
  path.join(REPO_ROOT, 'tests/stadium_c1a_browser_test.mjs');
const PATCH = process.env.MELEE_STADIUM_NATIVE_OBSERVATION_PATCH || null;

function extractFunction(source, name) {
  const marker = 'async function ' + name + '() {';
  const start = source.indexOf(marker);
  assert.notEqual(start, -1, 'missing actual source function: ' + name);
  const end = source.indexOf('\n}', start);
  assert.notEqual(end, -1, 'unterminated actual source function: ' + name);
  return source.slice(start, end + 2);
}

async function extractCandidateFunction(source, name) {
  if (!PATCH) return extractFunction(source, name);
  const patch = await fs.readFile(PATCH, 'utf8');
  const marker = 'async function ' + name + '() {';
  const lines = patch.split(/\r?\n/);
  const start = lines.findIndex(line => line === ' ' + marker || line === '+' + marker || line === marker);
  assert.notEqual(start, -1, 'candidate patch is missing source function: ' + name);
  const extracted = [];
  for (let index = start; index < lines.length; index += 1) {
    const line = lines[index];
    if (line.startsWith('@@') && extracted.length) break;
    let code;
    if (line.startsWith('+') && !line.startsWith('+++')) code = line.slice(1);
    else if (line.startsWith(' ')) code = line.slice(1);
    else continue;
    extracted.push(code);
    if (code === '}') break;
  }
  assert.equal(extracted.at(-1), '}', 'candidate patch function did not close');
  return extracted.join('\n');
}

function normalize(value) {
  return value === undefined ? undefined : JSON.parse(JSON.stringify(value));
}

function makeHostPayload() {
  const rows = Array.from({length: 374}, (_, index) => ({
    phase: 'synthetic-' + index,
    actual_json: 'row-' + index + ':' + 'h'.repeat(1024),
    approved: true,
  }));
  return {status: 'complete', hook_error: null, rows};
}

function makeContext({prefixSource, readerSource, mode, spec = {}}) {
  const events = [];
  const hostPayload = spec.hostPayload || makeHostPayload();
  let transferredBytes = null;
  let module = spec.module === 'absent' ? undefined : {};
  if (spec.module !== 'absent' && spec.module !== 'missing-observer') {
    module = {
      _melee_web_native_menu_stadium_first_css_draw_observe() {
        events.push('native_getter');
        if (spec.throwAt === 'native') throw new Error('native getter failure');
        return spec.pointer === undefined ? 17 : spec.pointer;
      },
      UTF8ToString(pointer) {
        events.push('utf8');
        assert.equal(pointer, 17);
        if (spec.throwAt === 'utf8') throw new Error('UTF8 conversion failure');
        if (spec.malformed) return '{not-json';
        return JSON.stringify(spec.native || {schema: 'synthetic-native-v1', nested: {marker: 73}});
      },
      _melee_web_native_menu_phase() {
        events.push('phase');
        if (spec.throwAt === 'phase') throw new Error('native phase failure');
        return 4;
      },
      _melee_web_native_menu_running() {
        events.push('running');
        if (spec.throwAt === 'running') throw new Error('native running failure');
        return 0;
      },
    };
  }
  const sandbox = {
    Module: module,
    JSON: {parse(text) { events.push('json_parse'); return JSON.parse(text); }},
    page: {
      async evaluate(callback) {
        const result = callback();
        const serialized = JSON.stringify(result);
        transferredBytes = Buffer.byteLength(serialized === undefined ? '' : serialized);
        return serialized === undefined ? undefined : JSON.parse(serialized);
      },
    },
  };
  if (spec.host !== 'absent') {
    sandbox.__meleeWebStadiumFirstSssPrefixRead = () => {
      events.push('host_callback');
      if (spec.throwAt === 'host') throw new Error('host callback failure');
      return hostPayload;
    };
  }
  const context = vm.createContext(sandbox);
  const declarations = [prefixSource];
  if (mode === 'legacy') {
    declarations.push('globalThis.__invoke = async () => (await readFirstSssPrefixObservation()).native;');
  } else if (mode === 'prefix') {
    declarations.push('globalThis.__invoke = readFirstSssPrefixObservation;');
  } else {
    declarations.push(readerSource);
    declarations.push('globalThis.__invoke = readFirstSssVsEntryObservation;');
  }
  new vm.Script(declarations.join('\n')).runInContext(context);
  return {context, events, hostPayload, get transferredBytes() { return transferredBytes; }};
}

async function run(mode, sources, spec = {}) {
  const fixture = makeContext({...sources, mode, spec});
  try {
    const value = await fixture.context.__invoke();
    return {ok: true, value: normalize(value), events: fixture.events,
      transferredBytes: fixture.transferredBytes, hostPayload: fixture.hostPayload};
  } catch (error) {
    return {ok: false, errorName: error.name, errorMessage: String(error.message),
      events: fixture.events, transferredBytes: fixture.transferredBytes,
      hostPayload: fixture.hostPayload};
  }
}

function assertSameOutcome(left, right, expectedEvents) {
  assert.equal(right.ok, left.ok);
  if (left.ok) assert.deepEqual(right.value, left.value);
  else {
    assert.equal(right.errorName, left.errorName);
    assert.equal(right.errorMessage, left.errorMessage);
  }
  assert.deepEqual(left.events, expectedEvents);
  assert.deepEqual(right.events, expectedEvents);
}

test('the actual native-only reader retains native semantics and omits unused host rows from its result', async t => {
  const runnerSource = await fs.readFile(RUNNER, 'utf8');
  const prefixSource = extractFunction(runnerSource, 'readFirstSssPrefixObservation');
  const readerSource = await extractCandidateFunction(runnerSource,
    'readFirstSssVsEntryObservation');
  assert.match(readerSource, /__meleeWebStadiumFirstSssPrefixRead\?\.\(\)/,
    'candidate must still invoke the existing host callback');
  assert.match(readerSource, /return native;/,
    'candidate must return the complete native value only');

  const expectedNative = {schema: 'synthetic-native-v1', nested: {marker: 73},
    native_phase: 4, running: 0};
  const expectedNormalOrder = ['native_getter', 'utf8', 'json_parse', 'phase', 'running', 'host_callback'];
  const legacy = await run('legacy', {prefixSource}, {});
  const current = await run('candidate', {prefixSource, readerSource}, {});
  assertSameOutcome(legacy, current, expectedNormalOrder);
  assert.deepEqual(current.value, expectedNative);
  assert.equal(Object.hasOwn(current.value, 'host'), false);
  assert.equal(JSON.stringify(current.value).includes('synthetic-0'), false);
  assert.ok(legacy.transferredBytes > 300000, 'legacy mock boundary should include all synthetic rows');
  assert.ok(current.transferredBytes < 3000, 'candidate mock boundary should return only native data');

  const fullPrefix = await run('prefix', {prefixSource}, {});
  assert.equal(fullPrefix.ok, true);
  assert.equal(fullPrefix.value.host.rows.length, 374,
    'the evidence reader must continue to expose all host rows');
  assert.equal(fullPrefix.value.host.rows[0].actual_json.length > 1000, true);
  assert.deepEqual(fullPrefix.events, expectedNormalOrder);

  const cases = [
    {name: 'absent Module and absent helper', spec: {module: 'absent', host: 'absent'},
      events: []},
    {name: 'absent Module still invokes installed helper', spec: {module: 'absent'},
      events: ['host_callback']},
    {name: 'missing optional native observer and helper',
      spec: {module: 'missing-observer', host: 'absent'}, events: []},
    {name: 'null native pointer still invokes helper', spec: {pointer: 0},
      events: ['native_getter', 'host_callback']},
    {name: 'malformed native JSON rejects before helper', spec: {malformed: true},
      events: ['native_getter', 'utf8', 'json_parse']},
    {name: 'native getter throw rejects before helper', spec: {throwAt: 'native'},
      events: ['native_getter']},
    {name: 'UTF8 conversion throw rejects before helper', spec: {throwAt: 'utf8'},
      events: ['native_getter', 'utf8']},
    {name: 'phase getter throw rejects before later calls', spec: {throwAt: 'phase'},
      events: ['native_getter', 'utf8', 'json_parse', 'phase']},
    {name: 'running getter throw rejects before helper', spec: {throwAt: 'running'},
      events: ['native_getter', 'utf8', 'json_parse', 'phase', 'running']},
    {name: 'host callback throw is preserved', spec: {throwAt: 'host'},
      events: expectedNormalOrder},
  ];
  for (const entry of cases) {
    await t.test(entry.name, async () => {
      const oldPath = await run('legacy', {prefixSource}, entry.spec);
      const newPath = await run('candidate', {prefixSource, readerSource}, entry.spec);
      assertSameOutcome(oldPath, newPath, entry.events);
      if (!oldPath.ok) assert.match(oldPath.errorMessage, /failure|JSON|position|property|undefined/i);
      else assert.equal(oldPath.value, null);
    });
  }
});
