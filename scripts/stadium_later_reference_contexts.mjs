// Fixed Node-side context identities, shared with the generated private header.
// No live game state or browser module graph imports this data module.
import table from '../tools/stadium_later_reference_contexts.json' with {type:'json'};
import {isDeepStrictEqual} from 'node:util';
const freeze = value => {
  if (value && typeof value === 'object') {
    Object.values(value).forEach(freeze);
    Object.freeze(value);
  }
  return value;
};
export const LATER_REFERENCE_CONTEXTS = freeze(table);
export const LEGACY_LATER_REFERENCE = table.contexts[0];
export const OBSERVED_B_REFERENCE = table.contexts[1];
export const OBSERVED_B_SCHEMA = 'melee-web-stock4-none-later-sss-reference-expectation-v1';
export function laterReferenceContext(expected) {
  if (expected?.schema === OBSERVED_B_SCHEMA) {
    if (!isDeepStrictEqual(expected.reference_context, OBSERVED_B_REFERENCE))
      throw Error('Later reference descriptor is stale, foreign or mixed');
    return OBSERVED_B_REFERENCE;
  }
  if (expected?.schema !== 'melee-web-stock4-none-later-sss-exit-host-expectation-v1')
    throw Error('Later reference schema is unknown');
  return LEGACY_LATER_REFERENCE;
}
