import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {resolvePrototypeContent} from '../web/prototype-content.mjs';
import {ROSTER, STAGES, DEFAULT_UNLOCKS} from '../web/match-flow.mjs';
const manifest = JSON.parse(readFileSync(0, 'utf8'));
const content = resolvePrototypeContent(manifest);
assert.equal(content.characters.length, ROSTER.length);
assert.equal(content.characters.filter(c => c.supported).length, 4);
assert.equal(content.characters.filter(c => !c.supported).length, 22);
for (const row of content.characters) {
  assert.equal(row.sourceId, ROSTER.find(c => c.id === row.id).sourceId);
  assert.equal(row.unlocked, DEFAULT_UNLOCKS[row.id]);
}
assert.equal(content.stages[0].sourceId, STAGES[0].sourceId);
assert.throws(() => resolvePrototypeContent({}), /inventory/);
assert.throws(() => resolvePrototypeContent({...manifest, fighters:[...manifest.fighters,manifest.fighters[0]]}), /roster/);
assert.throws(() => resolvePrototypeContent({...manifest, fighters:[{sourceName:'CKIND_UNKNOWN'}]}), /roster/);
assert.throws(() => resolvePrototypeContent({...manifest, stages:[...manifest.stages,manifest.stages[0]]}), /stage/);
// Availability follows native rows; it must never fall back to stale Mario-only flags.
const changed = resolvePrototypeContent({...manifest, fighters:manifest.fighters.filter(c => c.sourceName !== 'CKIND_MARIO')});
assert.equal(changed.characters.find(c => c.id === 'mario').supported, false);
assert.equal(changed.characters.find(c => c.id === 'fox').supported, true);
assert.equal(ROSTER.filter(c => c.supported).length, 1);
