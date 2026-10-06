import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtemp, readFile, rm, writeFile, access} from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import test from 'node:test';
import {classifyRoute, validateFullRoute} from '../scripts/net_determinism_contract.mjs';

const scriptPath = fileURLToPath(new URL('../scripts/net_determinism_browser.mjs', import.meta.url));
const validMatch = {complete: true, rules: {stage: 0x20, player_stocks: [4, 4]},
  players: [{human: true}, {human: true}]};

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
