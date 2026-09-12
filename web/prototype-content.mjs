import {ROSTER, STAGES, DEFAULT_UNLOCKS} from './match-flow.mjs';

/**
 * Transitional catalog, not a new selection flow. The old HTML menu's supported
 * flags predate the native four-by-four slice. Availability is generated from
 * gameplay_content.h; names/identities/unlocks are reused from match-flow.
 */
export function resolvePrototypeContent(manifest) {
  if (manifest?.schema !== 'melee-web-prototype-content-v1' ||
      !Array.isArray(manifest.fighters) || !manifest.fighters.length ||
      !Array.isArray(manifest.stages) || !manifest.stages.length) {
    throw Error('Content inventory is unavailable. Rebuild the prototype preview.');
  }
  const fighterNames = new Set();
  for (const row of manifest.fighters) {
    if (!ROSTER.some(item => item.sourceName === row.sourceName && item.name === row.name) || fighterNames.has(row.sourceName)) {
      throw Error('Content inventory does not match the shared roster.');
    }
    fighterNames.add(row.sourceName);
  }
  const stageNames = new Set();
  const stages = manifest.stages.map(row => {
    if (typeof row.sourceName !== 'string' || typeof row.name !== 'string' || !row.name || stageNames.has(row.sourceName)) {
      throw Error('Content inventory contains an invalid stage.');
    }
    stageNames.add(row.sourceName);
    const shared = STAGES.find(item => item.sourceName === row.sourceName);
    if (shared && shared.name !== row.name) throw Error('Content inventory disagrees with the shared stage.');
    return Object.freeze({...row, ...shared, supported: true});
  });
  return Object.freeze({
    characters: Object.freeze(ROSTER.map(row => Object.freeze({
      ...row, unlocked: DEFAULT_UNLOCKS[row.id], supported: fighterNames.has(row.sourceName),
    }))),
    stages: Object.freeze(stages),
  });
}
