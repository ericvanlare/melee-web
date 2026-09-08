/**
 * Browser-facing menu flow state.
 *
 * This is deliberately a small controller boundary. It carries the source
 * selection IDs into the existing match owner; it does not port the original
 * HSD CSS/SSS scenes.
 *
 * Character IDs and source IDs mirror CharacterKind in
 * .deps/melee/src/melee/ft/forward.h. Final Destination uses the stage-menu
 * StKind (0x20) and also records the runtime GrKind (0x25) separately.
 */

export const PHASES = Object.freeze({
  CHARACTER_SELECT: 'character-select',
  STAGE_SELECT: 'stage-select',
  LOADING: 'loading',
  PLAYING: 'playing',
});

export const DEFAULT_STOCKS = 4;
export const MIN_STOCKS = 1;
export const MAX_STOCKS = 99;

function deepFreeze(value) {
  if (value && typeof value === 'object' && !Object.isFrozen(value)) {
    Object.freeze(value);
    for (const child of Object.values(value)) deepFreeze(child);
  }
  return value;
}

// Values are kept explicit beside the source enum instead of being derived
// from a display-order array. Zelda and Sheik are separate CharacterKind IDs.
const CHARACTER_ROWS = [
  ['captain-falcon', 'Captain Falcon', 'CKIND_CAPTAIN', 0x00],
  ['donkey-kong', 'Donkey Kong', 'CKIND_DONKEY', 0x01],
  ['fox', 'Fox', 'CKIND_FOX', 0x02],
  ['mr-game-and-watch', 'Mr. Game & Watch', 'CKIND_GAMEWATCH', 0x03],
  ['kirby', 'Kirby', 'CKIND_KIRBY', 0x04],
  ['bowser', 'Bowser', 'CKIND_KOOPA', 0x05],
  ['link', 'Link', 'CKIND_LINK', 0x06],
  ['luigi', 'Luigi', 'CKIND_LUIGI', 0x07],
  ['mario', 'Mario', 'CKIND_MARIO', 0x08],
  ['marth', 'Marth', 'CKIND_MARS', 0x09],
  ['mewtwo', 'Mewtwo', 'CKIND_MEWTWO', 0x0a],
  ['ness', 'Ness', 'CKIND_NESS', 0x0b],
  ['peach', 'Peach', 'CKIND_PEACH', 0x0c],
  ['pikachu', 'Pikachu', 'CKIND_PIKACHU', 0x0d],
  ['ice-climbers', 'Ice Climbers', 'CKIND_POPONANA', 0x0e],
  ['jigglypuff', 'Jigglypuff', 'CKIND_PURIN', 0x0f],
  ['samus', 'Samus', 'CKIND_SAMUS', 0x10],
  ['yoshi', 'Yoshi', 'CKIND_YOSHI', 0x11],
  ['zelda', 'Zelda', 'CKIND_ZELDA', 0x12],
  ['sheik', 'Sheik', 'CKIND_SEAK', 0x13],
  ['falco', 'Falco', 'CKIND_FALCO', 0x14],
  ['young-link', 'Young Link', 'CKIND_CLINK', 0x15],
  ['dr-mario', 'Dr. Mario', 'CKIND_DRMARIO', 0x16],
  ['roy', 'Roy', 'CKIND_EMBLEM', 0x17],
  ['pichu', 'Pichu', 'CKIND_PICHU', 0x18],
  ['ganondorf', 'Ganondorf', 'CKIND_GANON', 0x19],
];

export const ROSTER = deepFreeze(CHARACTER_ROWS.map(([id, name, sourceName, sourceId]) => ({
  id,
  name,
  sourceName,
  sourceId,
  supported: id === 'mario',
})));

export const STAGES = deepFreeze([
  {
    id: 'final-destination',
    name: 'Final Destination',
    sourceName: 'St_Kind_Last',
    sourceId: 0x20,
    runtimeName: 'Gr_Kind_Last',
    runtimeId: 0x25,
    supported: true,
  },
]);

export const MATCH_CONTENT = deepFreeze({characters: ROSTER, stages: STAGES});
export const DEFAULT_UNLOCKS = deepFreeze(
  Object.fromEntries(ROSTER.map(character => [character.id, true])),
);

const CHARACTERS_BY_ID = new Map(ROSTER.map(character => [character.id, character]));
const STAGES_BY_ID = new Map(STAGES.map(stage => [stage.id, stage]));

export class MatchFlowError extends Error {
  constructor(code, message, action, phase) {
    super(message);
    this.name = 'MatchFlowError';
    this.code = code;
    this.action = action;
    this.phase = phase;
  }
}

function normalizeUnlocks(unlocks) {
  if (unlocks === undefined) return DEFAULT_UNLOCKS;
  if (!unlocks || typeof unlocks !== 'object' || Array.isArray(unlocks)) {
    throw new TypeError('unlocks must be an object keyed by character ID');
  }
  const result = {};
  for (const character of ROSTER) result[character.id] = unlocks[character.id] === true;
  return deepFreeze(result);
}

function requireStocks(stocks) {
  if (!Number.isInteger(stocks) || stocks < MIN_STOCKS || stocks > MAX_STOCKS) {
    throw new RangeError(`stocks must be an integer from ${MIN_STOCKS} to ${MAX_STOCKS}`);
  }
}

function cloneSelection(selection) {
  return {
    players: selection.players.map(player => ({...player})),
    stageId: selection.stageId,
    stocks: selection.stocks,
  };
}

function invalidPhase(action, phase, expected) {
  const expectedText = Array.isArray(expected) ? expected.join(' or ') : expected;
  return new MatchFlowError(
    'INVALID_PHASE',
    `${action} is only valid during ${expectedText}; current phase is ${phase}`,
    action,
    phase,
  );
}

function launchSnapshot(selection) {
  const stage = STAGES_BY_ID.get(selection.stageId);
  return deepFreeze({
    kind: 'stock',
    stocks: selection.stocks,
    players: selection.players.map(player => {
      const character = CHARACTERS_BY_ID.get(player.characterId);
      return {
        slot: player.slot,
        characterId: character.id,
        characterSourceName: character.sourceName,
        characterSourceId: character.sourceId,
        costume: 0,
        stocks: selection.stocks,
      };
    }),
    stage: {
      id: stage.id,
      sourceName: stage.sourceName,
      sourceId: stage.sourceId,
      runtimeName: stage.runtimeName,
      runtimeId: stage.runtimeId,
    },
  });
}

/**
 * Create the CSS -> SSS -> loading -> playing flow.
 *
 * All returned states and the loading launch snapshot are deeply immutable.
 * `selectCharacter` uses source player slots 0 and 1. `confirmCharacters()`
 * advances CSS to stage-select. `beginLaunch()` creates `state.launch`, which
 * the runtime may pass to its asynchronous launcher before calling
 * `launchSucceeded()` or `launchFailed()`.
 */
export function createMatchFlow({stocks = DEFAULT_STOCKS, unlocks} = {}) {
  requireStocks(stocks);
  const unlockState = normalizeUnlocks(unlocks);
  let state = deepFreeze({
    phase: PHASES.CHARACTER_SELECT,
    revision: 0,
    unlocks: unlockState,
    players: [
      {slot: 0, characterId: 'mario'},
      {slot: 1, characterId: 'mario'},
    ],
    stage: 'final-destination',
    stocks,
    launch: null,
  });

  function current() {
    return state;
  }

  function selectionFromState() {
    return {
      players: state.players.map(player => ({...player})),
      stageId: state.stage,
      stocks: state.stocks,
    };
  }

  function commit(phase, selection, launch = null) {
    state = deepFreeze({
      phase,
      revision: state.revision + 1,
      unlocks: unlockState,
      players: selection.players,
      stage: selection.stageId,
      stocks: selection.stocks,
      launch,
    });
    return state;
  }

  function selectCharacter(slot, characterId) {
    if (state.phase !== PHASES.CHARACTER_SELECT) {
      throw invalidPhase('selectCharacter', state.phase, PHASES.CHARACTER_SELECT);
    }
    if (!Number.isInteger(slot) || slot < 0 || slot > 1) {
      throw new MatchFlowError('INVALID_PLAYER', 'character selection requires player slot 0 or 1', 'selectCharacter', state.phase);
    }
    const character = CHARACTERS_BY_ID.get(characterId);
    if (!character) {
      throw new MatchFlowError('UNKNOWN_CHARACTER', `unknown character ID: ${String(characterId)}`, 'selectCharacter', state.phase);
    }
    if (!unlockState[character.id]) {
      throw new MatchFlowError('LOCKED_CHARACTER', `character is locked: ${character.id}`, 'selectCharacter', state.phase);
    }
    if (!character.supported) {
      throw new MatchFlowError('UNSUPPORTED_CHARACTER', `character is not supported by this slice: ${character.id}`, 'selectCharacter', state.phase);
    }
    const selection = selectionFromState();
    selection.players[slot].characterId = character.id;
    return commit(state.phase, selection);
  }

  function setStocks(nextStocks) {
    if (state.phase !== PHASES.CHARACTER_SELECT) {
      throw invalidPhase('setStocks', state.phase, PHASES.CHARACTER_SELECT);
    }
    requireStocks(nextStocks);
    const selection = selectionFromState();
    selection.stocks = nextStocks;
    return commit(state.phase, selection);
  }

  function selectStage(stageId) {
    if (state.phase !== PHASES.STAGE_SELECT) {
      throw invalidPhase('selectStage', state.phase, PHASES.STAGE_SELECT);
    }
    const stage = STAGES_BY_ID.get(stageId);
    if (!stage) {
      throw new MatchFlowError('UNKNOWN_STAGE', `unknown stage ID: ${String(stageId)}`, 'selectStage', state.phase);
    }
    if (!stage.supported) {
      throw new MatchFlowError('UNSUPPORTED_STAGE', `stage is not supported by this slice: ${stage.id}`, 'selectStage', state.phase);
    }
    const selection = selectionFromState();
    selection.stageId = stage.id;
    return commit(state.phase, selection);
  }

  function beginLaunch() {
    if (state.phase !== PHASES.STAGE_SELECT) {
      throw invalidPhase('beginLaunch', state.phase, PHASES.STAGE_SELECT);
    }
    const stage = STAGES_BY_ID.get(state.stage);
    if (!stage || !stage.supported) {
      throw new MatchFlowError('INVALID_SELECTION', 'a supported stage must be selected', 'beginLaunch', state.phase);
    }
    const selection = selectionFromState();
    return commit(PHASES.LOADING, selection, launchSnapshot(selection));
  }

  function confirmCharacters() {
    if (state.phase !== PHASES.CHARACTER_SELECT) {
      throw invalidPhase('confirmCharacters', state.phase, PHASES.CHARACTER_SELECT);
    }
    for (const player of state.players) {
      const character = CHARACTERS_BY_ID.get(player.characterId);
      if (!character || !character.supported || !unlockState[character.id]) {
        throw new MatchFlowError('INVALID_SELECTION', 'both players must have supported unlocked characters', 'confirmCharacters', state.phase);
      }
    }
    return commit(PHASES.STAGE_SELECT, selectionFromState());
  }

  function back() {
    if (state.phase !== PHASES.STAGE_SELECT) {
      throw invalidPhase('back', state.phase, PHASES.STAGE_SELECT);
    }
    return commit(PHASES.CHARACTER_SELECT, selectionFromState());
  }

  function launchSucceeded() {
    if (state.phase !== PHASES.LOADING) {
      throw invalidPhase('launchSucceeded', state.phase, PHASES.LOADING);
    }
    return commit(PHASES.PLAYING, selectionFromState(), state.launch);
  }

  function launchFailed() {
    if (state.phase !== PHASES.LOADING) {
      throw invalidPhase('launchFailed', state.phase, PHASES.LOADING);
    }
    return commit(PHASES.STAGE_SELECT, selectionFromState());
  }

  function returnToCharacters() {
    if (state.phase !== PHASES.PLAYING) {
      throw invalidPhase('returnToCharacters', state.phase, PHASES.PLAYING);
    }
    return commit(PHASES.CHARACTER_SELECT, selectionFromState());
  }

  return Object.freeze({
    getState: current,
    selectCharacter,
    setStocks,
    selectStage,
    confirmCharacters,
    beginLaunch,
    back,
    launchSucceeded,
    launchFailed,
    returnToCharacters,
  });
}
