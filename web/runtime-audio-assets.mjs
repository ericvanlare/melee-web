/** Development audio inputs. This module and the coefficient generator are excluded from the silent public profile. */
import * as disc from './runtime-assets.mjs';
import {createAudioFilterTable, AUDIO_FILTER_SHA256} from './dsp-coefficients.mjs';
import {openDiscSession} from './disc-session.mjs';
export {RUNTIME_DISC_FILES, NATIVE_MENU_DISC_FILES, NATIVE_GAME_DISC_FILES, ORIGINAL_DOL_SHA1} from './runtime-assets.mjs';

// Private C1a importer additions. The ordinary/public NativeGame map above
// stays unchanged; this closure is reachable only through the diagnostic
// preparation scope after the compiled menu host confirms its armed request.
export const NATIVE_STADIUM_C1A_DISC_FILES = Object.freeze({
  'GrPs.usd':'GrPs.usd','GrPs1.dat':'GrPs1.dat','GrPs2.dat':'GrPs2.dat',
  'GrPs3.dat':'GrPs3.dat','GrPs4.dat':'GrPs4.dat',
  'pstadium.hps':'audio/pstadium.hps','pokesta.hps':'audio/pokesta.hps',
});

export function nativeStadiumC1aDiscPaths(names) {
  if (!Array.isArray(names)) throw Error('Stadium C1a requires its exact source manifest.');
  const seen = new Set(), paths = Object.create(null);
  for (const name of names) {
    if (typeof name !== 'string' || seen.has(name))
      throw Error('Invalid or duplicate native asset name.');
    seen.add(name);
    if (name === 'sislib_font.bin' || name === 'dsp_coef.bin') continue;
    const path = Object.hasOwn(NATIVE_STADIUM_C1A_DISC_FILES, name)
      ? NATIVE_STADIUM_C1A_DISC_FILES[name]
      : Object.hasOwn(disc.NATIVE_GAME_DISC_FILES, name)
        ? disc.NATIVE_GAME_DISC_FILES[name]
        : undefined;
    if (!path) throw Error('Unknown native scene asset: ' + name);
    paths[name] = path;
  }
  if (Object.keys(NATIVE_STADIUM_C1A_DISC_FILES).some(name => !seen.has(name)))
    throw Error('Stadium C1a requires its complete source stage closure.');
  return paths;
}

function nativeGameDiscPaths(names) {
  if (!Array.isArray(names)) throw Error('Invalid native asset scope.');
  const seen = new Set(), paths = Object.create(null);
  for (const name of names) {
    if (typeof name !== 'string' || seen.has(name))
      throw Error('Invalid or duplicate native asset name.');
    seen.add(name);
    if (name === 'sislib_font.bin' || name === 'dsp_coef.bin') continue;
    if (!Object.hasOwn(disc.NATIVE_GAME_DISC_FILES, name))
      throw Error('Unknown native scene asset: ' + name);
    paths[name] = disc.NATIVE_GAME_DISC_FILES[name];
  }
  return {paths, seen};
}

async function* streamNativeScope(session, names, report, stadiumC1a = false) {
  const {paths, seen} = stadiumC1a
    ? {paths: nativeStadiumC1aDiscPaths(names), seen: new Set(names)}
    : nativeGameDiscPaths(names);
  const total = names.length;
  report({phase: 'validate', complete: 0, total});
  yield* session.streamScope(paths, {
    beforeRead: ({name, index}) => report({phase: 'read', file: name, complete: index, total}),
  });
  if (seen.has('sislib_font.bin')) yield ['sislib_font.bin', session.fontBytes()];
  if (seen.has('dsp_coef.bin')) {
    const coefficients = createAudioFilterTable();
    const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', coefficients)),
      byte => byte.toString(16).padStart(2, '0')).join('');
    if (hash !== AUDIO_FILTER_SHA256) throw Error('Generated audio coefficients failed their integrity check.');
    session.metadata();
    yield ['dsp_coef.bin', coefficients];
  }
  report({phase: 'complete', complete: total, total});
  session.metadata();
}

async function withAudio(loader, file, report = () => {}) {
  let total = 0;
  const files = await loader(file, progress => {
    total = progress.total + 1;
    report({...progress, total, phase: progress.phase === 'complete' ? 'coefficients' : progress.phase});
  });
  const coefficients = createAudioFilterTable();
  const digest = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', coefficients)),
    byte => byte.toString(16).padStart(2, '0')).join('');
  if (digest !== AUDIO_FILTER_SHA256) throw Error('Generated audio coefficients failed their integrity check.');
  files.set('dsp_coef.bin', coefficients);
  report({phase: 'complete', complete: total, total});
  return files;
}
export const loadRuntimeDisc = (file, report) => withAudio(disc.loadRuntimeDisc, file, report);
export const loadNativeMenuDisc = (file, report) => withAudio(disc.loadNativeMenuDisc, file, report);
export const loadNativeGameDisc = (file, report) => withAudio(disc.loadNativeGameDisc, file, report);

/** One validated local File; native scenes can consume bounded payload batches. */
export async function openNativeGameSession(file) {
  const session = await openDiscSession(file);
  const adapter = {
    close: () => session.close(),
    identity: () => session.identity(),
    fileInfo: path => {
      const entry = session.fileInfo(path);
      return entry ? Object.freeze({name: entry.path, size: entry.size}) : null;
    },
    readFile: (path, offset, size) => session.readFile(path, offset, size),
    streamScope: (names, report = () => {}) => streamNativeScope(session, names, report),
    streamStadiumC1aScope: (names, report = () => {}) =>
      streamNativeScope(session, names, report, true),
    async readScope(names, report = () => {}) {
      const files = new Map();
      for await (const [name, bytes] of adapter.streamScope(names, report)) files.set(name, bytes);
      return files;
    },
  };
  return Object.freeze(adapter);
}
