const FULL_SCENE_ORDER = [1, 2, 3, 4, 1];

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

export const expectedFullSceneOrder = () => [...FULL_SCENE_ORDER];
