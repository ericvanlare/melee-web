/** Capture modes must be explicit, and foreground-only jobs require --headed. */
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const ROOT = fileURLToPath(new URL('..', import.meta.url));
const node = process.execPath;
const runHitch = path.join(ROOT, 'scripts/run_hitch_matrix.mjs');
const captureCpu = path.join(ROOT, 'scripts/capture_cpu_browser.mjs');

async function exists(filename) {
  try { await fs.access(filename); return true; }
  catch { return false; }
}

function run(script, args) {
  const result = spawnSync(node, [script, ...args], {
    cwd: ROOT, encoding: 'utf8', timeout: 30000,
  });
  assert.notEqual(result.error?.code, 'ETIMEDOUT', `${script} unexpectedly timed out`);
  return result;
}

const temporary = await fs.mkdtemp(path.join(os.tmpdir(), 'melee-foreground-guard-'));
try {
  const captureOut = path.join(temporary, 'capture-out');
  const captureResult = run(captureCpu, [
    '--url', 'http://127.0.0.1:8791/runtime.html',
    '--disc', path.join(temporary, 'missing-disc.gcm'),
    '--recipe', path.join(temporary, 'missing-recipe.mwrc'),
    '--out', captureOut,
    '--playwright', path.join(temporary, 'missing-playwright'),
  ]);
  assert.notEqual(captureResult.status, 0);
  assert.match(captureResult.stderr, /Choose exactly one browser mode/);
  assert.doesNotMatch(captureResult.stderr, /ENOENT|Playwright|missing-disc|missing-recipe/,
    'capture mode guard must run before disc/recipe hashing or Playwright resolution');
  assert.equal(await exists(captureOut), false,
    'capture mode guard must run before creating its output directory');

  const conflictingModeResult = run(captureCpu, ['--headed', '--headless']);
  assert.notEqual(conflictingModeResult.status, 0);
  assert.match(conflictingModeResult.stderr, /Choose exactly one browser mode/);
  assert.doesNotMatch(conflictingModeResult.stderr, /Playwright|Use --url/,
    'conflicting capture modes must fail before dependency or argument processing');

  const profileOut = path.join(temporary, 'profile-out');
  const profileResult = run(runHitch, [
    'profile', '--out', profileOut,
    '--build', path.join(temporary, 'missing-build'),
    '--browser-profile', path.join(temporary, 'missing-profile'),
  ]);
  assert.notEqual(profileResult.status, 0);
  assert.match(profileResult.stderr, /explicit --headed/);
  assert.doesNotMatch(profileResult.stderr, /ENOENT|Playwright|missing-build|missing-profile/,
    'hitch profile guard must run before browser launch or profile/build reads');
  assert.equal(await exists(profileOut), false,
    'hitch profile guard must run before creating its output directory');

  const captureArgumentResult = run(captureCpu, ['--headed']);
  assert.notEqual(captureArgumentResult.status, 0);
  assert.match(captureArgumentResult.stderr, /Use --url/);
  assert.doesNotMatch(captureArgumentResult.stderr, /Choose exactly one browser mode|Playwright/,
    'explicit --headed must pass the foreground guard before normal argument validation');

  const headlessArgumentResult = run(captureCpu, ['--headless']);
  assert.notEqual(headlessArgumentResult.status, 0);
  assert.match(headlessArgumentResult.stderr, /Use --url/);
  assert.doesNotMatch(headlessArgumentResult.stderr, /Choose exactly one browser mode|Playwright/,
    'explicit --headless must pass the mode guard before normal argument validation');

  const profileArgumentResult = run(runHitch, ['profile', '--headed']);
  assert.notEqual(profileArgumentResult.status, 0);
  assert.match(profileArgumentResult.stderr, /Missing required --out/);
  assert.doesNotMatch(profileArgumentResult.stderr, /explicit --headed|Playwright/,
    'explicit --headed must pass the foreground guard before normal profile validation');

  const runResult = run(runHitch, [
    'run', '--plan', path.join(temporary, 'missing-plan.json'),
    '--disc', path.join(temporary, 'missing-disc.gcm'),
  ]);
  assert.notEqual(runResult.status, 0);
  assert.match(runResult.stderr, /explicit --headed/);
  assert.doesNotMatch(runResult.stderr, /ENOENT|missing-plan|missing-disc/,
    'hitch run guard must run before reading a plan or disc');

  console.log('Capture mode guards reject implicit/conflicting modes; foreground jobs require explicit --headed');
} finally {
  await fs.rm(temporary, {recursive: true, force: true});
}
