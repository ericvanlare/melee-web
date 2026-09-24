import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';

const root = new URL('..', import.meta.url);
const runtime = await fs.readFile(new URL('web/runtime-development.mjs', root), 'utf8');
const wallDeclarations = runtime.match(/const RETAIL_REPLAY_(?:LEGACY_WALL_TIME_MS|WHOLE_SESSION_WALL_TIME_MS)[^;]*;/g);
assert(wallDeclarations?.length === 2, 'runtime must declare legacy and whole-session wall-time bounds');
const wallPolicy = runtime.slice(runtime.indexOf('function retailReplayWallTimeMs'), runtime.indexOf('\n// Keep the upload ceiling'));
const wallScope = {};
vm.createContext(wallScope);
vm.runInContext(`${wallDeclarations.join('\n')}\n${wallPolicy}\nlegacy=retailReplayWallTimeMs(false); whole=retailReplayWallTimeMs(true);`, wallScope);
assert.equal(wallScope.legacy, 900000);
assert.equal(wallScope.whole, 2700000);
assert.equal(wallScope.whole, wallScope.legacy * 3);
const declarations = runtime.match(/const RETAIL_REPLAY_(?:LEGACY_MAX_FRAMES|WHOLE_SESSION_MAX_FRAMES|STATE_RECORD_OVERHEAD|TIMER_RECORD_OVERHEAD|MAX_BYTES)[^;]*;/g);
assert(declarations?.length === 5, 'runtime must declare all bounded MWRC transport constants');
const scope = {};
vm.createContext(scope);
vm.runInContext(`${declarations.join('\n')}\nresult=RETAIL_REPLAY_MAX_BYTES;`, scope);

// This is the native v8 envelope at 108,000 frames and all 32 admitted spans.
// The 32-span table is 372 bytes larger than the one-span minimum.
const expected = 16 + 4 + 8 + (0x18 + 0x55E8 + 0x148 + 6) +
  0x138 + 822 + 108000 * 44 + 2 + 32 * 12;
assert.equal(expected, 4_775_898);
assert.equal(scope.result, expected);
assert.equal(scope.result - (expected - 31 * 12), 372);
console.log('Whole-session browser bound admits the exact 32-span v8 envelope');
