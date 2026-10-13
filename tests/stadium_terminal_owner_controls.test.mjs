import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const harness = path.join(here, 'melee_runtime_owner_test.mjs');
const expectSettled = process.argv.includes('--expect-stale-settled');
const cases = ['clean', 'stale-native-preparation', 'busy-asset-transfer', 'disc-loading', 'handoff-loading', 'fatal-owner'];
const results = [];
for (const name of cases) {
  const args = [harness, '--silent', `--stadium-retirement-case=${name}`];
  if (expectSettled && ['stale-native-preparation', 'disc-loading'].includes(name))
    args.push('--stadium-retirement-expect-settled');
  const child = spawnSync(process.execPath, args, {encoding: 'utf8', timeout: 20000});
  assert.equal(child.error, undefined, `${name}: child process starts and exits within 20 seconds`);
  assert.equal(child.status, 0, `${name}: actual runtime owner control exits cleanly\n${child.stdout}\n${child.stderr}`);
  const marker = child.stdout.split('\n').findLast(line => line.startsWith('STADIUM_RETIREMENT_RESULT '));
  assert.ok(marker, `${name}: actual runtime owner emits its observed terminal state\n${child.stdout}`);
  results.push(JSON.parse(marker.slice('STADIUM_RETIREMENT_RESULT '.length)));
}
console.log(JSON.stringify({schema: 'stadium-terminal-owner-actual-mount-controls-v1', expect_stale_settled: expectSettled,
  cases: results}, null, 2));
