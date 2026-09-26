import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {pathToFileURL} from 'node:url';

const root = new URL('../', import.meta.url);
const sourceUrl = new URL('web/runtime-assets.mjs', root);
const source = await fs.readFile(sourceUrl, 'utf8');
assert.doesNotMatch(source, /runtime-audio(?:-assets)?\.mjs/,
  'public disc loader must not import development audio modules');
assert.match(source, /openNativeGameDiscSession/);

// Substitute only the shared session opener so this test exercises the real
// public wrapper without requiring a multi-gigabyte disc image.
const importLines = [
  "import {openDiscImage,fontFileRange} from './disc-image.mjs';",
  "import {openDiscSession} from './disc-session.mjs';",
].join('\n');
assert.ok(source.includes(importLines));
const substituted = source.replace(importLines, [
  'const openDiscImage = globalThis.testOpenDiscImage;',
  'const fontFileRange = globalThis.testFontFileRange;',
  'const openDiscSession = globalThis.testOpenDiscSession;',
].join('\n'));
const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'melee-public-assets-'));
const modulePath = path.join(directory, 'runtime-assets.mjs');
await fs.writeFile(modulePath, substituted);

let readCalls = 0;
let metadataCalls = 0;
let closeCalls = 0;
let closed = false;
const scopeCalls = [];
globalThis.testOpenDiscImage = () => { throw Error('public wrapper must use DiscAssetSession'); };
globalThis.testFontFileRange = () => { throw Error('public wrapper must use session font bytes'); };
globalThis.testOpenDiscSession = async file => {
  assert.equal(file, 'owned-disc');
  return {
    async readScope(scope, {beforeRead}) {
      if (closed) throw Error('DiscAssetSession is closed');
      ++readCalls;
      scopeCalls.push(Object.entries(scope));
      const result = new Map();
      for (const [name] of Object.entries(scope)) {
        const index = result.size;
        beforeRead({name, index});
        result.set(name, new Uint8Array([index + 1]));
      }
      return result;
    },
    fontBytes() {
      if (closed) throw Error('DiscAssetSession is closed');
      return new Uint8Array([0x51, 0x52]);
    },
    metadata() {
      if (closed) throw Error('DiscAssetSession is closed');
      ++metadataCalls;
    },
    close() {
      if (closed) return;
      closed = true;
      ++closeCalls;
    },
  };
};

try {
  const {NATIVE_GAME_DISC_FILES, openNativeGameDiscSession} =
    await import(`${pathToFileURL(modulePath).href}?public-test`);
  assert.equal(NATIVE_GAME_DISC_FILES['PlCo.dat'], 'PlCo.dat');
  assert.equal(NATIVE_GAME_DISC_FILES['LbRf.dat'], 'LbRf.dat');
  assert.equal(NATIVE_GAME_DISC_FILES['main.ssm'], 'audio/us/main.ssm');
  assert.equal(Object.hasOwn(NATIVE_GAME_DISC_FILES, 'dsp_coef.bin'), false);

  const session = await openNativeGameDiscSession('owned-disc');
  const progress = [];
  const files = await session.readScope(['PlCo.dat', 'LbRf.dat', 'sislib_font.bin'],
    event => progress.push(event));
  assert.deepEqual(scopeCalls, [[['PlCo.dat', 'PlCo.dat'], ['LbRf.dat', 'LbRf.dat']]],
    'logical names map to the exact native FST paths');
  assert.deepEqual([...files.keys()], ['PlCo.dat', 'LbRf.dat', 'sislib_font.bin']);
  assert.deepEqual([...files.get('sislib_font.bin')], [0x51, 0x52]);
  assert.deepEqual(progress.map(event => event.phase), ['validate', 'read', 'read', 'complete']);
  assert.equal(progress.at(-1).complete, 3);
  assert.equal(readCalls, 1);
  assert.equal(metadataCalls, 1);

  for (const names of [['PlCo.dat', 'PlCo.dat'], ['unknown-native-name'], ['dsp_coef.bin']]) {
    await assert.rejects(session.readScope(names), names[0] === 'dsp_coef.bin'
      ? /DSP coefficients/ : names[0] === 'unknown-native-name'
        ? /Unknown native scene asset/ : /duplicate/);
  }
  assert.equal(readCalls, 1, 'invalid public names fail before any payload read');

  await assert.rejects(
    session.readScope(['PlCo.dat'], event => {
      if (event.phase === 'complete') session.close();
    }),
    /DiscAssetSession is closed/,
    'closing during completion reporting must reject before returning the scope',
  );
  session.close();
  session.close();
  assert.equal(closeCalls, 1, 'session close remains idempotent');
  await assert.rejects(session.readScope(['PlCo.dat']), /DiscAssetSession is closed/);
} finally {
  delete globalThis.testOpenDiscImage;
  delete globalThis.testFontFileRange;
  delete globalThis.testOpenDiscSession;
  await fs.rm(directory, {recursive: true, force: true});
}

const shell = await fs.readFile(new URL('web/player/player-shell.mjs', root), 'utf8');
assert.match(shell, /openDisc:\s*openNativeGameDiscSession/,
  'public shell must hand the neutral session to the shared owner');
console.log('Public scene disc scope maps exact files, stays audio-free, preflights names, and closes cleanly.');
