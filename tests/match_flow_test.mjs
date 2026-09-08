import assert from 'node:assert/strict';
import {
  DEFAULT_STOCKS,
  DEFAULT_UNLOCKS,
  MATCH_CONTENT,
  MAX_STOCKS,
  MIN_STOCKS,
  PHASES,
  ROSTER,
  STAGES,
  MatchFlowError,
  createMatchFlow,
} from '../web/match-flow.mjs';

const sourceRoster = [
  ['captain-falcon', 0x00], ['donkey-kong', 0x01], ['fox', 0x02],
  ['mr-game-and-watch', 0x03], ['kirby', 0x04], ['bowser', 0x05],
  ['link', 0x06], ['luigi', 0x07], ['mario', 0x08], ['marth', 0x09],
  ['mewtwo', 0x0a], ['ness', 0x0b], ['peach', 0x0c], ['pikachu', 0x0d],
  ['ice-climbers', 0x0e], ['jigglypuff', 0x0f], ['samus', 0x10],
  ['yoshi', 0x11], ['zelda', 0x12], ['sheik', 0x13], ['falco', 0x14],
  ['young-link', 0x15], ['dr-mario', 0x16], ['roy', 0x17], ['pichu', 0x18],
  ['ganondorf', 0x19],
];

assert.deepEqual(ROSTER.map(({id, sourceId}) => [id, sourceId]), sourceRoster);
assert.equal(ROSTER.length, 26);
assert(ROSTER.every(character => DEFAULT_UNLOCKS[character.id] === true));
assert.deepEqual(ROSTER.filter(character => character.supported).map(character => character.id), ['mario']);
assert.deepEqual(MATCH_CONTENT.characters, ROSTER);
assert.deepEqual(STAGES.map(({id, sourceId, runtimeId}) => [id, sourceId, runtimeId]), [
  ['final-destination', 0x20, 0x25],
]);

function expectFlowError(action, code) {
  assert.throws(action, error => {
    assert(error instanceof MatchFlowError);
    assert.equal(error.code, code);
    return true;
  });
}

// CSS -> SSS -> loading -> playing -> CSS, with an immutable launch payload.
const flow = createMatchFlow();
assert.equal(flow.getState().phase, PHASES.CHARACTER_SELECT);
assert.equal(flow.getState().stocks, DEFAULT_STOCKS);
const cssState = flow.getState();
const sssState = flow.confirmCharacters();
assert.equal(sssState.phase, PHASES.STAGE_SELECT);
assert.equal(sssState.players[0].characterId, 'mario');
assert.equal(sssState.players[1].characterId, 'mario');
const loadingState = flow.beginLaunch();
assert.equal(loadingState.phase, PHASES.LOADING);
assert(Object.isFrozen(loadingState.launch));
assert.deepEqual(loadingState.launch.players.map(player => [
  player.slot, player.characterSourceId, player.stocks,
]), [[0, 0x08, 4], [1, 0x08, 4]]);
assert.deepEqual(loadingState.launch.stage, {
  id: 'final-destination',
  sourceName: 'St_Kind_Last',
  sourceId: 0x20,
  runtimeName: 'Gr_Kind_Last',
  runtimeId: 0x25,
});
assert.throws(() => { loadingState.launch.players[0].stocks = 1; }, TypeError);
assert.equal(flow.getState(), loadingState, 'launch snapshot remains the state-owned immutable request');
const playingState = flow.launchSucceeded();
assert.equal(playingState.phase, PHASES.PLAYING);
assert.equal(playingState.launch, loadingState.launch);
const returnedCss = flow.returnToCharacters();
assert.equal(returnedCss.phase, PHASES.CHARACTER_SELECT);
assert.equal(returnedCss.players[0].characterId, 'mario');
assert.equal(returnedCss.stage, 'final-destination');
assert.equal(returnedCss.stocks, 4);

// A failed launch returns to SSS, and the second cycle can change rules before
// launching again. The returned selection is retained across both boundaries.
const secondFlow = createMatchFlow();
secondFlow.confirmCharacters();
secondFlow.selectStage('final-destination');
secondFlow.beginLaunch();
const failedLoading = secondFlow.getState();
secondFlow.launchFailed();
assert.equal(secondFlow.getState().phase, PHASES.STAGE_SELECT);
assert.equal(secondFlow.getState().stage, 'final-destination');
secondFlow.back();
assert.equal(secondFlow.getState().phase, PHASES.CHARACTER_SELECT);
secondFlow.setStocks(7);
secondFlow.confirmCharacters();
secondFlow.beginLaunch();
assert.equal(secondFlow.getState().launch.stocks, 7);
assert.notEqual(secondFlow.getState().launch, failedLoading.launch);
secondFlow.launchSucceeded();
assert.equal(secondFlow.getState().phase, PHASES.PLAYING);
secondFlow.returnToCharacters();
assert.equal(secondFlow.getState().stocks, 7);

// Invalid actions reject without changing the state object or revision.
const invalidFlow = createMatchFlow();
let before = invalidFlow.getState();
expectFlowError(() => invalidFlow.back(), 'INVALID_PHASE');
assert.equal(invalidFlow.getState(), before);
expectFlowError(() => invalidFlow.launchSucceeded(), 'INVALID_PHASE');
assert.equal(invalidFlow.getState(), before);
expectFlowError(() => invalidFlow.selectCharacter(0, 'fox'), 'UNSUPPORTED_CHARACTER');
assert.equal(invalidFlow.getState(), before);
expectFlowError(() => invalidFlow.selectCharacter(0, 'not-a-character'), 'UNKNOWN_CHARACTER');
assert.equal(invalidFlow.getState(), before);
expectFlowError(() => invalidFlow.selectCharacter(2, 'mario'), 'INVALID_PLAYER');
assert.equal(invalidFlow.getState(), before);
for (const stocks of [0, -1, MAX_STOCKS + 1, 1.5, '4']) {
  assert.throws(() => invalidFlow.setStocks(stocks), /stocks must be an integer/);
  assert.equal(invalidFlow.getState(), before);
}
invalidFlow.confirmCharacters();
before = invalidFlow.getState();
expectFlowError(() => invalidFlow.confirmCharacters(), 'INVALID_PHASE');
assert.equal(invalidFlow.getState(), before);
expectFlowError(() => invalidFlow.selectStage('battlefield'), 'UNKNOWN_STAGE');
assert.equal(invalidFlow.getState(), before);
invalidFlow.beginLaunch();
before = invalidFlow.getState();
expectFlowError(() => invalidFlow.beginLaunch(), 'INVALID_PHASE');
assert.equal(invalidFlow.getState(), before);
invalidFlow.launchFailed();
before = invalidFlow.getState();
expectFlowError(() => invalidFlow.launchFailed(), 'INVALID_PHASE');
assert.equal(invalidFlow.getState(), before);
invalidFlow.beginLaunch();
invalidFlow.launchSucceeded();
before = invalidFlow.getState();
expectFlowError(() => invalidFlow.launchSucceeded(), 'INVALID_PHASE');
assert.equal(invalidFlow.getState(), before);

// Unlock state is separate from implementation support. A locked Mario is a
// different rejection from an unlocked but unsupported vanilla character.
const locked = createMatchFlow({unlocks: {...DEFAULT_UNLOCKS, mario: false}});
before = locked.getState();
expectFlowError(() => locked.selectCharacter(0, 'mario'), 'LOCKED_CHARACTER');
assert.equal(locked.getState(), before);
expectFlowError(() => locked.confirmCharacters(), 'INVALID_SELECTION');
assert.equal(locked.getState(), before);
assert.equal(MIN_STOCKS, 1);
console.log('Menu flow registry, immutable two-cycle lifecycle, source IDs, stock validation and rejection paths passed');
