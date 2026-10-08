import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtemp, readFile, rm, writeFile, access} from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import test from 'node:test';
import {classifyRoute, collapseConsecutiveScenes, validateActiveMatchRoute, validateFullRoute} from '../scripts/net_determinism_contract.mjs';

const scriptPath = fileURLToPath(new URL('../scripts/net_determinism_browser.mjs', import.meta.url));
const validMatch = {complete: true, rules: {stage: 0x20, player_stocks: [4, 4]},
  players: [{human: true}, {human: true}]};
const validActiveMatch = {ready: true, paused: false, ending: false, complete: false, frame: 1,
  rules: {stage: 0x20, player_stocks: [4, 4]},
  players: [{fighter: 0, human: true, stocks: 4}, {fighter: 0, human: true, stocks: 4}]};

async function withTemp(run) {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'melee-net-determinism-cli-'));
  try { await run(dir); }
  finally { await rm(dir, {recursive: true, force: true}); }
}

function callHarness(args) {
  return spawnSync(process.execPath, [scriptPath, ...args], {encoding: 'utf8', timeout: 5000});
}

function baseArgs(dir, seed = '7') {
  return ['--url', 'http://127.0.0.1:8787/runtime.html', '--disc', '/unused/disc.ciso',
    '--script', path.join(dir, 'tiny.mwni'), `--seed=${seed}`, '--out', path.join(dir, 'out')];
}

async function writeScript(dir, frameCount = 2) {
  const bytes = Buffer.alloc(16 + frameCount * 44);
  bytes.write('MWNI', 0, 'ascii');
  bytes.writeUInt32BE(1, 4);
  bytes.writeUInt32BE(frameCount, 8);
  await writeFile(path.join(dir, 'tiny.mwni'), bytes);
  return bytes;
}

test('prefix workloads are classified without asserting full-route acceptance', () => {
  assert.equal(classifyRoute(2, 2), 'full');
  assert.equal(classifyRoute(1, 2), 'prefix-only');
  assert.deepEqual(validateFullRoute([1, 2, 3, 4, 1], validMatch), {
    scope: 'full', status: 'passed', observed_scenes: [1, 2, 3, 4, 1],
    selection: {stage: 0x20, player_stocks: [4, 4], humans: [true, true]},
  });
  assert.throws(() => validateFullRoute([1, 2], validMatch), /scene order mismatch/);
  assert.throws(() => validateFullRoute([1, 2, 3, 4, 1], {...validMatch, players: [{human: true}, {human: false}]}), /two-human/);
  assert.throws(() => validateFullRoute([1, 2, 3, 4, 1], {...validMatch, rules: {...validMatch.rules, stage: 0}}), /Final Destination/);
});

test('active-match validator requires only the declared CSS/SSS/match prefix and exact live selection', () => {
  assert.deepEqual(validateActiveMatchRoute([1, 2, 3], validActiveMatch), {
    scope: 'active-match-prefix', status: 'passed', observed_scenes: [1, 2, 3], active_frame: 1,
    selection: {stage: 0x20, player_stocks: [4, 4], fighters: [0, 0], humans: [true, true], current_stocks: [4, 4]},
  });
  for (const scenes of [[1, 2], [1, 2, 3, 4], [1, 3, 2]])
    assert.throws(() => validateActiveMatchRoute(scenes, validActiveMatch), /scene order mismatch/);
  for (const match of [
    {}, {...validActiveMatch, frame: 0}, {...validActiveMatch, ready: false},
    {...validActiveMatch, paused: true}, {...validActiveMatch, ending: true},
    {...validActiveMatch, complete: true}, {...validActiveMatch, observer_error: true},
    {...validActiveMatch, rules: {...validActiveMatch.rules, stage: 0}},
    {...validActiveMatch, rules: {...validActiveMatch.rules, player_stocks: [4, 3]}},
    {...validActiveMatch, players: [{...validActiveMatch.players[0], fighter: 1}, validActiveMatch.players[1]]},
    {...validActiveMatch, players: [{...validActiveMatch.players[0], human: false}, validActiveMatch.players[1]]},
    {...validActiveMatch, players: [{...validActiveMatch.players[0], stocks: 3}, validActiveMatch.players[1]]},
    {...validActiveMatch, players: [validActiveMatch.players[0]]},
  ]) assert.throws(() => validateActiveMatchRoute([1, 2, 3], match), /did not retain two human Mario/);
});

test('A2 per-tick checksum scenes collapse only consecutive repeats before route validation', () => {
  const perTickScenes = [
    ...Array(154).fill(1),
    ...Array(152).fill(2),
    ...Array(3986).fill(3),
    ...Array(612).fill(4),
    ...Array(180).fill(1),
  ];
  assert.equal(perTickScenes.length, 5084);
  assert.deepEqual(collapseConsecutiveScenes(perTickScenes), [1, 2, 3, 4, 1]);
  assert.equal(perTickScenes.length, 5084, 'scene compression must not remove per-tick samples');
  assert.deepEqual(validateFullRoute(collapseConsecutiveScenes(perTickScenes), validMatch).observed_scenes,
    [1, 2, 3, 4, 1]);

  const repeatedScene = [...perTickScenes];
  repeatedScene.splice(155, 0, 1);
  assert.deepEqual(collapseConsecutiveScenes(repeatedScene), [1, 2, 1, 2, 3, 4, 1],
    'a later return to a scene remains visible');
  assert.throws(() => validateFullRoute(collapseConsecutiveScenes(repeatedScene), validMatch),
    /scene order mismatch/);

  const outOfOrder = [...perTickScenes];
  outOfOrder[200] = 3;
  assert.deepEqual(collapseConsecutiveScenes(outOfOrder), [1, 2, 3, 2, 3, 4, 1]);
  assert.throws(() => validateFullRoute(collapseConsecutiveScenes(outOfOrder), validMatch),
    /scene order mismatch/);
});

test('invalid global and per-instance seeds fail before browser startup', async () => {
  await withTemp(async dir => {
    await writeScript(dir);
    const profile = path.join(dir, 'profile');
    for (const seed of ['NaN', '-1', '1.5', '4294967296']) {
      const result = callHarness([...baseArgs(dir, seed), '--instance', `label=a,profile=${profile}`]);
      assert.notEqual(result.status, 0, `global seed ${seed} unexpectedly accepted`);
      assert.match(result.stderr, /unsigned 32-bit integer/);
    }
    for (const seed of ['NaN', '-1', '1.5', '4294967296']) {
      const result = callHarness([...baseArgs(dir), '--instance', `label=a,profile=${profile},seed=${seed}`]);
      assert.notEqual(result.status, 0, `instance seed ${seed} unexpectedly accepted`);
      assert.match(result.stderr, /unsigned 32-bit integer/);
    }
    await assert.rejects(access(path.join(dir, 'out')));
  });
});

test('out-of-workload flip is refused before output creation or Chrome startup', async () => {
  await withTemp(async dir => {
    const bytes = await writeScript(dir, 2);
    const saved = await readFile(path.join(dir, 'tiny.mwni'));
    assert.deepEqual(saved, bytes);
    const result = callHarness([...baseArgs(dir), '--stop-after-ticks', '1', '--instance',
      `label=a,profile=${path.join(dir, 'profile')},flip=1:0:0`]);
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /outside the 1-tick workload/);
    await assert.rejects(access(path.join(dir, 'out')));
  });
});
