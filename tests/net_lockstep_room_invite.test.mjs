import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';
import {createRoomInvite, encodeRoomInvite, parseRoomInvite, ROOM_INVITE_MAX_CHARS,
  ROOM_INVITE_PREFIX} from '../scripts/net_lockstep_room_invite.mjs';
import {LOCKSTEP_DELAY, LOCKSTEP_MAX_SOURCE_TICKS} from '../scripts/net_lockstep_core.mjs';

const roomId = 'a'.repeat(32);
const ROOT = new URL('../', import.meta.url);
const runtimeSource = await readFile(new URL('web/runtime-development.mjs', ROOT), 'utf8');
const transferGuards = runtimeSource.split('\n').filter(line =>
  /^(?:const laterBReplayNativeSuspended=|function assertLaterBReplayNativeAccess\()/.test(line)).join('\n');
assert.equal(transferGuards.split('\n').length, 2, 'both actual native access guards are extracted');

const encodeObject = value => ROOM_INVITE_PREFIX + Buffer.from(JSON.stringify(value)).toString('base64url');
const valid = () => parseRoomInvite(encodeRoomInvite({url: 'wss://relay.example:9443', roomId, seed: 0xdeadbeef}));

function roomUiFixture() {
  const ids = ['lockstep-host', 'lockstep-join', 'lockstep-copy', 'lockstep-host-invite',
    'lockstep-join-invite', 'lockstep-relay-url', 'lockstep-copy-status', 'lockstep-status',
    'lockstep-leave', 'launch', 'unload', 'status'];
  const elements = Object.fromEntries(ids.map(id => [id, {value: '', disabled: false, textContent: '', dataset: {},
    focus() {}, select() {}}]));
  const calls = [], clipboard = [], clearedIntervals = [], intervals = new Map();
  let health = null, healthFailure = null, closeFailure = null, beginFailure = null, rejectBegin, healthCalls = 0, nextInterval = 0;
  const window = {
    meleeNetConfigureRuntimeLockstep(config) { calls.push(['configure', config]); return true; },
    meleeNetBeginLockstep(seed, sourceTicks) { calls.push(['begin', seed, sourceTicks]); if (beginFailure) throw beginFailure; return new Promise((resolve, reject) => { rejectBegin = reject; }); },
    meleeNetRuntimeLockstepSnapshot() { throw Error('the UI must not request a history snapshot'); },
    meleeNetRuntimeLockstepHealth() { ++healthCalls; if (healthFailure) throw healthFailure; return health; },
    meleeNetCloseRuntimeLockstep(mode) {
      calls.push(['close', mode]);
      return closeFailure ? Promise.reject(closeFailure) : Promise.resolve();
    },
  };
  const scope = {window, navigator: {clipboard: {async writeText(value) { clipboard.push(value); }}},
    LOCKSTEP_MAX_SOURCE_TICKS, createRoomInvite, parseRoomInvite,
    $(id) { return elements[id]; },
    setInterval(callback) { const id = ++nextInterval; intervals.set(id, callback); return id; },
    clearInterval(id) { if (intervals.has(id)) clearedIntervals.push(id); intervals.delete(id); },
    owner: {handle: {async unload() { calls.push(['unload']); }}},
    ready: true, fatal: false, bundle: true, retailRun: null, replayLoading: false, stockCheckActive: false};
  vm.createContext(scope);
  vm.runInContext(transferGuards, scope, {filename: 'runtime-development-native-access-guards.mjs'});
  return {scope, elements, calls, clipboard,
    setHealth(value) { health = value; }, setHealthFailure(value) { healthFailure = value; },
    setCloseFailure(value) { closeFailure = value; }, setBeginFailure(value) { beginFailure = value; },
    rejectBegin(error) { rejectBegin(error); }, get healthCalls() { return healthCalls; },
    get clearedIntervals() { return clearedIntervals; }};
}

test('invite creation uses secure room and seed values with the fixed shared profile', () => {
  const text = createRoomInvite({url: 'wss://relay.example:9443'});
  const invite = parseRoomInvite(text);
  assert.match(invite.roomId, /^[A-Za-z0-9_-]{22,64}$/);
  assert.ok(Number.isSafeInteger(invite.seed) && invite.seed >= 0 && invite.seed <= 0xffffffff);
  assert.deepEqual(invite, {schema: 'melee-web-runtime-room', version: 1,
    url: 'wss://relay.example:9443', roomId: invite.roomId, seed: invite.seed,
    sourceTicks: LOCKSTEP_MAX_SOURCE_TICKS, inputDelay: LOCKSTEP_DELAY});
  assert.equal(parseRoomInvite(text).seed, invite.seed);
});

test('invite encoder requires a verified relay base and the existing bounded token and seed domains', () => {
  for (const url of ['https://relay.example', 'ws://user:secret@relay.example',
    'wss://relay.example/path', 'wss://relay.example?room=1', 'wss://relay.example#fragment'])
    assert.throws(() => encodeRoomInvite({url, roomId, seed: 1}), /Room relay URL/);
  for (const badRoomId of ['short', 'x'.repeat(65), 'bad/token'])
    assert.throws(() => encodeRoomInvite({url: 'wss://relay.example', roomId: badRoomId, seed: 1}),
      /unsupported or invalid shared settings/);
  for (const seed of [-1, 0x100000000, 1.5, Number.NaN])
    assert.throws(() => encodeRoomInvite({url: 'wss://relay.example', roomId, seed}),
      /unsupported or invalid shared settings/);
});

test('invite parser rejects unsupported versions, shared settings, fields and malformed encodings', () => {
  const base = valid();
  const invalid = [
    {...base, version: 2},
    {...base, sourceTicks: LOCKSTEP_MAX_SOURCE_TICKS - 1},
    {...base, inputDelay: LOCKSTEP_DELAY + 1},
    {...base, seed: 0x100000000},
    {...base, roomId: 'short'},
    {...base, extra: true},
  ];
  for (const value of invalid) assert.throws(() => parseRoomInvite(encodeObject(value)));
  assert.throws(() => parseRoomInvite('MELEE-ROOM-V2:AAAA'), /supported bounded/);
  assert.throws(() => parseRoomInvite(ROOM_INVITE_PREFIX + '!!!'), /encoding is invalid/);
  assert.throws(() => parseRoomInvite(ROOM_INVITE_PREFIX + 'A'), /encoding is invalid/);
  assert.throws(() => parseRoomInvite('x'.repeat(ROOM_INVITE_MAX_CHARS + 1)), /supported bounded/);
});

test('invite parser requires canonical compact JSON before handing settings to the runtime', () => {
  const invite = valid();
  const reordered = {version: invite.version, schema: invite.schema, url: invite.url,
    roomId: invite.roomId, seed: invite.seed, sourceTicks: invite.sourceTicks, inputDelay: invite.inputDelay};
  assert.throws(() => parseRoomInvite(encodeObject(reordered)), /canonical form/);
});

test('private runtime build stages and permits the invite module as an existing browser artifact', async () => {
  const artifacts = JSON.parse(await readFile(new URL('tools/browser_build_artifacts.json', ROOT), 'utf8'));
  const cmake = await readFile(new URL('cmake/FighterRuntime.cmake', ROOT), 'utf8');
  const runtime = await readFile(new URL('web/runtime-development.mjs', ROOT), 'utf8');
  assert.ok(artifacts.includes('net_lockstep_room_invite.mjs'));
  assert.match(cmake, /configure_file\(scripts\/net_lockstep_room_invite\.mjs net_lockstep_room_invite\.mjs COPYONLY\)/);
  assert.match(runtime, /from '\.\/net_lockstep_room_invite\.mjs'/);
});

test('actual runtime Host/Join handlers pass the shared invite and keep Host copyable while waiting', async () => {
  const source = await readFile(new URL('web/runtime-development.mjs', ROOT), 'utf8');
  const start = source.indexOf('const LOCKSTEP_ROOM_TIMEOUT_MS=');
  const end = source.indexOf('// Diagnostic A/B only.', start);
  assert.ok(start >= 0 && end > start, 'room controls remain one bounded runtime-owned source seam');
  const host = roomUiFixture();
  vm.runInContext(source.slice(start, end), host.scope, {filename: 'runtime-development.mjs'});
  vm.runInContext('lockstepRoomCanStart=true;', host.scope);
  host.elements['lockstep-relay-url'].value = 'wss://relay.example:9443';
  host.elements['lockstep-host'].onclick();
  const hostInviteText = host.elements['lockstep-host-invite'].value;
  const hostInvite = parseRoomInvite(hostInviteText);
  assert.ok(hostInviteText, 'the invite is rendered before begin waits on its unresolved peer promise');
  assert.deepEqual([host.calls[0][0], JSON.parse(JSON.stringify(host.calls[0][1]))],
    ['configure', {role: 'alpha', roomId: hostInvite.roomId,
      url: hostInvite.url, timeoutMs: 120000, transport: 'relay'}]);
  assert.deepEqual(host.calls[1], ['begin', hostInvite.seed, LOCKSTEP_MAX_SOURCE_TICKS]);
  assert.equal(host.elements['lockstep-copy'].disabled, false);
  assert.match(host.elements['lockstep-status'].textContent, /Preparing Host session/);
  assert.equal(host.elements['lockstep-leave'].disabled, true,
    'the page cannot claim teardown during the owner’s unresolved identity fetch');
  await host.elements['lockstep-leave'].onclick();
  assert.deepEqual(host.calls.filter(row => row[0] === 'close'), []);
  host.setHealth({armed: true, closing: false, peer: null, failure: null});
  vm.runInContext('observeLockstepRoom();', host.scope);
  assert.equal(host.elements['lockstep-leave'].disabled, false,
    'health re-sync enables Leave as soon as the session is armed, even before peer creation');
  assert.match(host.elements['lockstep-status'].textContent, /Waiting for Player 2/);
  await host.elements['lockstep-copy'].onclick();
  assert.deepEqual(host.clipboard, [hostInviteText]);

  host.setHealth({armed: true, closing: false, failure: null, peer: {failure: null,
    endpointClosed: false, closed: false, protocol: {ready: true, terminal: null,
      local_input_ticks: 7, local_checksum_ticks: 9}}});
  vm.runInContext('observeLockstepRoom();', host.scope);
  assert.match(host.elements['lockstep-status'].textContent, /Connected/);
  assert.doesNotMatch(host.elements['lockstep-status'].textContent, /passed|success/i);
  host.setHealth({armed: true, closing: false, failure: null, peer: {failure: null,
    endpointClosed: false, closed: false, protocol: {ready: true, terminal: null,
      local_input_ticks: 0, local_checksum_ticks: LOCKSTEP_MAX_SOURCE_TICKS}}});
  vm.runInContext('observeLockstepRoom();', host.scope);
  assert.match(host.elements['lockstep-status'].textContent, /limit reached/i);
  assert.match(host.elements['lockstep-status'].textContent, /unverified/);
  assert.ok(host.clearedIntervals.length, 'session limit retires the polling interval');
  const callsAtLimit = host.healthCalls;
  vm.runInContext('observeLockstepRoom();', host.scope);
  assert.equal(host.healthCalls, callsAtLimit, 'a terminal UI state does not keep polling health');
  host.setCloseFailure(Error('close fence failed'));
  await host.elements['lockstep-leave'].onclick();
  assert.deepEqual(host.calls.filter(row => row[0] === 'close'), [['close', 'normal']]);
  assert.match(host.elements['lockstep-status'].textContent, /teardown was not confirmed/);
  assert.doesNotMatch(host.elements['lockstep-status'].textContent, /Disconnected by your choice/);

  const join = roomUiFixture();
  vm.runInContext(source.slice(start, end), join.scope, {filename: 'runtime-development.mjs'});
  vm.runInContext('lockstepRoomCanStart=true;', join.scope);
  join.elements['lockstep-join-invite'].value = hostInviteText;
  join.elements['lockstep-join'].onclick();
  assert.deepEqual([join.calls[0][0], JSON.parse(JSON.stringify(join.calls[0][1]))],
    ['configure', {role: 'beta', roomId: hostInvite.roomId,
      url: hostInvite.url, timeoutMs: 120000, transport: 'relay'}]);
  assert.deepEqual(join.calls[1], ['begin', hostInvite.seed, LOCKSTEP_MAX_SOURCE_TICKS]);
  assert.match(join.elements['lockstep-status'].textContent, /Preparing Join session/);
  assert.equal(join.elements['lockstep-leave'].disabled, true);
  join.setHealth({armed: true, closing: false, peer: null, failure: null});
  vm.runInContext('observeLockstepRoom();', join.scope);
  assert.equal(join.elements['lockstep-leave'].disabled, false);
  assert.match(join.elements['lockstep-status'].textContent, /Waiting for Player 1/);
  await join.elements['lockstep-leave'].onclick();
  assert.deepEqual(join.calls.filter(row => row[0] === 'close'), [['close', 'normal']],
    'a pending Join can stop without input or a second peer');
  assert.match(join.elements['lockstep-status'].textContent, /Disconnected by your choice/);
  assert.doesNotMatch(join.elements['lockstep-status'].textContent, /success|passed/i);

  const terminal = roomUiFixture();
  vm.runInContext(source.slice(start, end), terminal.scope, {filename: 'runtime-development.mjs'});
  vm.runInContext('lockstepRoomCanStart=true;', terminal.scope);
  terminal.elements['lockstep-join-invite'].value = hostInviteText;
  terminal.elements['lockstep-join'].onclick();
  terminal.setHealth({armed: true, closing: false, failure: null, peer: {failure: null,
    endpointClosed: true, closed: false, protocol: {ready: false,
      terminal: {kind: 'disconnect', reason: 'peer left'}}}});
  vm.runInContext('observeLockstepRoom();', terminal.scope);
  assert.match(terminal.elements['lockstep-status'].textContent, /Disconnected/);
  assert.ok(terminal.clearedIntervals.length, 'a terminal disconnect retires polling');

  const fatal = roomUiFixture();
  vm.runInContext(source.slice(start, end), fatal.scope, {filename: 'runtime-development.mjs'});
  vm.runInContext('lockstepRoomCanStart=true;', fatal.scope);
  fatal.elements['lockstep-join-invite'].value = hostInviteText;
  fatal.elements['lockstep-join'].onclick();
  fatal.setHealth({armed: true, closing: false, peer: null, failure: null});
  vm.runInContext('observeLockstepRoom();', fatal.scope);
  const fatalStart = source.indexOf('      if(fatal&&lockstepRoomConfigured)');
  const fatalEnd = source.indexOf('\n', fatalStart);
  assert.ok(fatalStart >= 0 && fatalEnd > fatalStart);
  fatal.scope.fatal = true;
  fatal.scope.state = {message: 'runtime stopped'};
  vm.runInContext(source.slice(fatalStart, fatalEnd), fatal.scope);
  assert.match(fatal.elements['lockstep-status'].textContent, /Error: runtime stopped/);
  assert.ok(fatal.clearedIntervals.length, 'fatal runtime state retires polling');

  const unload = roomUiFixture();
  vm.runInContext(source.slice(start, end), unload.scope, {filename: 'runtime-development.mjs'});
  vm.runInContext('lockstepRoomCanStart=true;', unload.scope);
  unload.elements['lockstep-join-invite'].value = hostInviteText;
  unload.elements['lockstep-join'].onclick();
  unload.setHealth({armed: true, closing: false, peer: null, failure: null});
  vm.runInContext('observeLockstepRoom();', unload.scope);
  const unloadStart = source.indexOf("$('unload').onclick=async()=>");
  const unloadEnd = source.indexOf('\ncontrollerSettings =', unloadStart);
  assert.ok(unloadStart >= 0 && unloadEnd > unloadStart);
  vm.runInContext(source.slice(unloadStart, unloadEnd), unload.scope, {filename: 'runtime-development.mjs'});
  await unload.elements.unload.onclick();
  assert.deepEqual(unload.calls.filter(row => row[0] === 'unload'), [['unload']]);
  assert.match(unload.elements['lockstep-status'].textContent, /Runtime unloaded/);
  assert.ok(unload.clearedIntervals.length, 'runtime unload retires polling');

  const failedHealth = roomUiFixture();
  vm.runInContext(source.slice(start, end), failedHealth.scope, {filename: 'runtime-development.mjs'});
  vm.runInContext('lockstepRoomCanStart=true;', failedHealth.scope);
  failedHealth.elements['lockstep-join-invite'].value = hostInviteText;
  failedHealth.elements['lockstep-join'].onclick();
  failedHealth.setHealthFailure(Error('health query failed'));
  vm.runInContext('observeLockstepRoom();', failedHealth.scope);
  assert.match(failedHealth.elements['lockstep-status'].textContent, /health unavailable/);
  assert.ok(failedHealth.clearedIntervals.length, 'health failure retires the polling interval');
});


test('late start rejection preserves Leave and unload outcomes; synchronous start failure retires polling', async () => {
  const source = await readFile(new URL('web/runtime-development.mjs', ROOT), 'utf8');
  const start = source.indexOf('const LOCKSTEP_ROOM_TIMEOUT_MS=');
  const end = source.indexOf('// Diagnostic A/B only.', start);
  const invite = encodeRoomInvite({url: 'wss://relay.example', roomId, seed: 1});
  for (const action of ['leave', 'unload', 'failed-leave']) {
    const run = roomUiFixture();
    vm.runInContext(source.slice(start, end), run.scope);
    vm.runInContext('lockstepRoomCanStart=true;', run.scope);
    run.elements['lockstep-join-invite'].value = invite;
    run.elements['lockstep-join'].onclick();
    run.setHealth({armed: true, closing: false, peer: null, failure: null});
    vm.runInContext('observeLockstepRoom();', run.scope);
    if (action === 'unload') {
      const unloadStart = source.indexOf("$('unload').onclick=async()=>");
      const unloadEnd = source.indexOf('\ncontrollerSettings =', unloadStart);
      vm.runInContext(source.slice(unloadStart, unloadEnd), run.scope);
      await run.elements.unload.onclick();
    } else {
      if (action === 'failed-leave') run.setCloseFailure(Error('close fence failed'));
      await run.elements['lockstep-leave'].onclick();
    }
    const outcome = run.elements['lockstep-status'].textContent;
    run.rejectBegin(Error('start canceled after teardown'));
    await Promise.resolve(); await Promise.resolve(); await Promise.resolve();
    assert.equal(run.elements['lockstep-status'].textContent, outcome,
      `${action} keeps its observed teardown outcome after the begin promise rejects`);
    assert.ok(run.clearedIntervals.length);
  }
  const failedStart = roomUiFixture();
  vm.runInContext(source.slice(start, end), failedStart.scope);
  vm.runInContext('lockstepRoomCanStart=true;', failedStart.scope);
  failedStart.setBeginFailure(Error('start refused synchronously'));
  failedStart.elements['lockstep-join-invite'].value = invite;
  failedStart.elements['lockstep-join'].onclick();
  assert.match(failedStart.elements['lockstep-status'].textContent, /start refused synchronously/);
  assert.ok(failedStart.clearedIntervals.length, 'a synchronous start refusal retires its allocated interval');
});


test('actual manual unload guard retains suspended room owner without native unload', async () => {
  const run = roomUiFixture();
  assert.equal(vm.runInContext('laterBReplayNativeSuspended()', run.scope), false);
  const finishes = [];
  run.scope.finishRetailReplay = async reason => { finishes.push(reason); };
  run.scope.owner.laterBReplayTransfer = {suspended: () => true};
  assert.throws(() => vm.runInContext('assertLaterBReplayNativeAccess()', run.scope),
    /Private replay continuation forbids ordinary native diagnostics/);
  const start = runtimeSource.indexOf("$('unload').onclick=async()=>");
  const end = runtimeSource.indexOf('\ncontrollerSettings =', start);
  assert.ok(start >= 0 && end > start, 'actual unload handler range is present');
  vm.runInContext(runtimeSource.slice(start, end), run.scope);
  await run.elements.unload.onclick();
  assert.deepEqual(finishes, ['Manual unload refused during private replay transfer']);
  assert.deepEqual(run.calls, [], 'guard performs no native unload, configure, begin or room-close call');
});
