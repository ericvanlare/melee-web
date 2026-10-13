import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source = fs.readFileSync(new URL('../web/melee-runtime.mjs', import.meta.url), 'utf8');

function actualFunction(signature, name) {
  const start = source.indexOf(signature);
  assert.notEqual(start, -1, `source must contain ${signature}`);
  assert.equal(source.indexOf(signature, start + signature.length), -1,
    `source must contain one ${signature}`);
  const open = source.indexOf('{', start + signature.length);
  assert.notEqual(open, -1, `${signature} must have a body`);

  // Find the matching body brace while ignoring braces inside comments and
  // quoted strings. These selected functions contain no template literals.
  let depth = 0, quote = '', lineComment = false, blockComment = false;
  for (let i = open; i < source.length; i++) {
    const ch = source[i], next = source[i + 1];
    if (lineComment) {
      if (ch === '\n') lineComment = false;
      continue;
    }
    if (blockComment) {
      if (ch === '*' && next === '/') { blockComment = false; i++; }
      continue;
    }
    if (quote) {
      if (ch === '\\') { i++; continue; }
      if (ch === quote) quote = '';
      continue;
    }
    if (ch === '/' && next === '/') { lineComment = true; i++; continue; }
    if (ch === '/' && next === '*') { blockComment = true; i++; continue; }
    if (ch === '"' || ch === "'") { quote = ch; continue; }
    if (ch === '{') depth++;
    if (ch === '}' && --depth === 0) {
      return `function ${name}() ${source.slice(open, i + 1)}`;
    }
  }
  assert.fail(`could not find the end of ${signature}`);
}

const publish = actualFunction('function publish()', 'publish');
const completion = actualFunction('menuMatchDrawDiscardRetired()', 'completion');
const exportDiagnostics = actualFunction('async exportDiagnostics()', 'exportDiagnostics')
  .replace(/^function /, 'async function ');

function harness({retired = false, privateExport = true, phase = 0, running = 0,
  busy = false, fatal = false, destroyed = false, malformed = false} = {}) {
  const counts = {status: 0, phase: 0, running: 0, snapshots: 0,
    published: 0, sceneExit: 0, persist: 0};
  const context = {
    counts,
    ready: true,
    fatal,
    destroyed,
    terminalDrawDiscardRetirementConsumed: false,
    diagnosticActive: true,
    lastState: '',
    preparationLabel: '',
    document: {hidden: false},
    Module: {
      UTF8ToString: value => value,
      _melee_web_native_menu_phase: () => { counts.phase++; return phase; },
      _melee_web_native_menu_running: () => { counts.running++; return running; },
    },
    snapshot: () => { counts.snapshots++; return {running: !!running, busy, loading: null}; },
    diagnosticActivity: value => { context.diagnosticActive = value; },
    onState: () => { counts.published++; },
    diagnosticLifecycle: type => {
      assert.equal(type, 'scene_exit');
      counts.sceneExit++;
    },
    diagnostics: {
      persist: async () => { counts.persist++; },
      exportReports: () => ({schema: 'melee-web-runtime-diagnostics'}),
      exportRetained: async () => ({records: []}),
    },
  };
  if (privateExport) {
    context.Module._melee_web_native_menu_match_draw_discard_status = () => {
      counts.status++;
      return malformed ? 'not-json' : JSON.stringify({native_retired: retired});
    };
  }
  vm.createContext(context);
  vm.runInContext(`${publish}\n${completion}\n${exportDiagnostics}\n` +
    'globalThis.complete = completion; globalThis.exported = exportDiagnostics;', context);
  return {context, counts};
}

test('a retired private owner publishes inactivity once and permits the existing export', async () => {
  const {context, counts} = harness({retired: true});
  assert.equal(context.complete(), true);
  assert.equal(context.terminalDrawDiscardRetirementConsumed, true);
  assert.equal(context.diagnosticActive, false);
  assert.deepEqual([counts.status, counts.phase, counts.running, counts.snapshots,
    counts.published, counts.sceneExit], [1, 1, 1, 1, 1, 1]);

  const result = await context.exported();
  assert.equal(result.current.schema, 'melee-web-runtime-diagnostics');
  assert.deepEqual(result.retained.records, []);
  assert.equal(counts.persist, 1);

  assert.equal(context.complete(), false);
  assert.equal(counts.status, 1);
  assert.equal(counts.published, 1);
  assert.equal(counts.sceneExit, 1);
});

test('ordinary builds without the private status export cannot publish retirement or export while active', async () => {
  const {context, counts} = harness({privateExport: false});
  assert.equal(context.complete(), false);
  assert.equal(await context.exported(), null);
  assert.equal(context.terminalDrawDiscardRetirementConsumed, false);
  assert.deepEqual([counts.phase, counts.running, counts.published, counts.persist], [0, 0, 0, 0]);
});

test('a private status that is not retired refuses before reading the native owner state', async () => {
  const {context, counts} = harness();
  assert.equal(context.complete(), false);
  assert.equal(await context.exported(), null);
  assert.equal(context.terminalDrawDiscardRetirementConsumed, false);
  assert.deepEqual([counts.status, counts.phase, counts.running, counts.published, counts.persist],
    [1, 0, 0, 0, 0]);
});

for (const options of [
  {phase: 7},
  {running: 1},
  {fatal: true},
  {destroyed: true},
  {malformed: true},
]) {
  test(`ineligible private owner refuses retirement: ${JSON.stringify(options)}`, async () => {
    const {context, counts} = harness({retired: true, ...options});
    assert.equal(context.complete(), false);
    assert.equal(await context.exported(), null);
    assert.equal(context.terminalDrawDiscardRetirementConsumed, false);
    assert.equal(counts.published, 0);
    assert.equal(counts.sceneExit, 0);
    assert.equal(counts.persist, 0);
  });
}

test('other busy work keeps the existing diagnostics export guard active after publication', async () => {
  const {context, counts} = harness({retired: true, busy: true});
  assert.equal(context.complete(), false);
  assert.equal(context.terminalDrawDiscardRetirementConsumed, true);
  assert.equal(context.diagnosticActive, true);
  assert.equal(counts.sceneExit, 1);
  assert.equal(await context.exported(), null);
  assert.equal(counts.persist, 0);
});
