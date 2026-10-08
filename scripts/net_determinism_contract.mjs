const FULL_SCENE_ORDER = [1, 2, 3, 4, 1];
const ACTIVE_MATCH_SCENE_ORDER = [1, 2, 3];
const MARIO_FIGHTER_KIND = 0;
const FINAL_DESTINATION_KIND = 0x20;

export function classifyRoute(ticks, frameCount) {
  return ticks === frameCount ? 'full' : 'prefix-only';
}

export function collapseConsecutiveScenes(perTickScenes) {
  const transitions = [];
  for (const scene of perTickScenes) {
    if (transitions.at(-1) !== scene) transitions.push(scene);
  }
  return transitions;
}

export function validateFullRoute(scenes, match) {
  if (JSON.stringify(scenes) !== JSON.stringify(FULL_SCENE_ORDER))
    throw Error(`Full route scene order mismatch: ${JSON.stringify(scenes)}`);
  if (!match || match.complete !== true || match.rules?.stage !== 0x20 ||
      JSON.stringify(match.rules?.player_stocks) !== JSON.stringify([4, 4]) ||
      !Array.isArray(match.players) || match.players.length < 2 ||
      match.players[0]?.human !== true || match.players[1]?.human !== true)
    throw Error(`Full route did not retain a complete two-human, four-stock Final Destination selection: ${JSON.stringify(match)}`);
  return {scope: 'full', status: 'passed', observed_scenes: scenes,
    selection: {stage: match.rules.stage, player_stocks: match.rules.player_stocks,
      humans: match.players.slice(0, 2).map(player => player.human)}};
}

/** Validate the declared CSS/SSS/active-match boundary without implying Results or CSS return. */
export function validateActiveMatchRoute(scenes, match) {
  if (JSON.stringify(scenes) !== JSON.stringify(ACTIVE_MATCH_SCENE_ORDER))
    throw Error(`Active-match route scene order mismatch: ${JSON.stringify(scenes)}`);
  const players = match?.players;
  if (!match || match.observer_error === true || match.ready !== true ||
      !Number.isSafeInteger(match.frame) || match.frame <= 0 || match.paused !== false ||
      match.ending !== false || match.complete !== false ||
      match.rules?.stage !== FINAL_DESTINATION_KIND ||
      JSON.stringify(match.rules?.player_stocks) !== '[4,4]' ||
      !Array.isArray(players) || players.length !== 2 ||
      players.some(player => player?.fighter !== MARIO_FIGHTER_KIND || player?.human !== true || player?.stocks !== 4))
    throw Error(`Active-match observation did not retain two human Mario fighters, four stocks and Final Destination: ${JSON.stringify(match)}`);
  return {scope: 'active-match-prefix', status: 'passed', observed_scenes: scenes,
    active_frame: match.frame,
    selection: {stage: match.rules.stage, player_stocks: match.rules.player_stocks,
      fighters: players.map(player => player.fighter), humans: players.map(player => player.human),
      current_stocks: players.map(player => player.stocks)}};
}

export const expectedFullSceneOrder = () => [...FULL_SCENE_ORDER];
