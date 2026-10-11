/* Original source CSS door input choreography from reviewed R292 cce75b33.
 * The caller owns page/report lifetime and supplies the same checked observations. */
import assert from 'node:assert/strict';

export function createCssHumanJoinDriver({page,report,shot,observeCssSetup,
    sourcePadSample,sourcePadTap,resumeTimingPause,ensureNoError}) {
const moveCssCursor = async (label, isInside, directionFor) => {
  for (let step = 0; step < 240; step++) {
    const setup = await observeCssSetup();
    assert(setup?.cursors?.length === 16 && setup?.doors?.length === 40 &&
      setup?.geometry?.length === 48, 'Live original CSS cursor geometry is unavailable');
    const [x, y] = setup.geometry;
    if (isInside(x, y, setup)) return setup;
    const [stickX, stickY] = directionFor(x, y, setup);
    assert(stickX || stickY, `${label} cursor movement made no progress at (${x}, ${y})`);
    await sourcePadSample(0, stickX, stickY, `${label} source cursor step ${step + 1}`);
  }
  const setup = await observeCssSetup();
  throw Error(`${label} cursor did not reach its authored source bounds: ${JSON.stringify({
    cursor: setup?.geometry?.slice(0, 2), door1: setup?.geometry?.slice(12, 24)})}`);
};
const cssDoor = (setup, port) => ({
  p_kind: setup.doors[port * 10],
  slot_type: setup.doors[port * 10 + 4],
  character: setup.doors[port * 10 + 3],
  slot: setup.doors[port * 10 + 6],
  source_port: setup.doors[port * 10 + 6]
    ? setup.doors[port * 10 + 6] - 1 : port,
});
const waitCssDoor = async (port, predicate, label) => {
  const deadline = Date.now() + 15000;
  let setup;
  while (Date.now() < deadline) {
    await resumeTimingPause(label);
    setup = await observeCssSetup();
    if (setup?.doors?.length === 40 && setup?.geometry?.length === 48) {
      const door = cssDoor(setup, port);
      if (predicate(door)) return {setup, door};
    }
    await ensureNoError(label);
    await page.waitForTimeout(40);
  }
  throw Error(`${label}: source CSS door ${port} did not reach the required state: ${JSON.stringify({
    door: setup?.doors?.length === 40 ? cssDoor(setup, port) : null,
    geometry: setup?.geometry?.slice(port * 12, port * 12 + 12)})}`);
};
const configureCompetitiveSecondHuman = async () => {
  await page.locator('#controls-open').click();
  await page.locator('#keyboard-layout').selectOption('two');
  await page.locator('#player-two-source').selectOption('keyboard');
  await page.waitForFunction(() => {
    const layout = document.querySelector('#keyboard-layout')?.value;
    const source = document.querySelector('#player-two-source')?.value;
    const status = document.querySelector('#player-two-source-status')?.textContent?.trim();
    return layout === 'two' && source === 'keyboard' && status === 'Keyboard';
  }, null, {timeout: 5000});
  const controlsChange = await page.evaluate(() => ({
    layout: document.querySelector('#keyboard-layout')?.value,
    playerOneSource: document.querySelector('#player-one-source')?.value,
    playerTwoSource: document.querySelector('#player-two-source')?.value,
    playerTwoStatus: document.querySelector('#player-two-source-status')?.textContent?.trim(),
  }));
  assert.deepEqual(controlsChange, {
    layout: 'two', playerOneSource: 'keyboard', playerTwoSource: 'keyboard',
    playerTwoStatus: 'Keyboard',
  }, 'Controls must enable the actual two-player keyboard source before CSS joins P2');
  await page.locator('#controls-close').click();
  report.inputConfiguration.playerTwoAfterProfile = 'keyboard';
  report.inputConfiguration.layoutAfterProfile = 'two';
  report.competitiveMatchStart.controls_change = controlsChange;

  let setup = await observeCssSetup();
  assert(setup?.cursors?.length === 16 && setup?.doors?.length === 40 &&
    setup?.geometry?.length === 48, 'Live CSS source roster is unavailable after Controls change');
  const initialRoster = [0, 1, 2, 3].map(port => cssDoor(setup, port));
  assert.deepEqual(initialRoster.map(door => [door.p_kind, door.slot_type]),
    [[0, 0], [1, 1], [3, 3], [3, 3]],
    'Original CSS must expose P1 Human, P2 CPU, and two empty doors before the source join');
  assert.deepEqual(initialRoster.slice(0, 2).map(door => door.character), [8, 8],
    'The source CSS roster must already contain Mario on both active doors');
  assert.deepEqual(initialRoster.slice(0, 2).map(door => door.slot), [0, 0],
    'Original CSS StartMeleeData keeps its authored zero-valued raw slot fields');
  assert.deepEqual(initialRoster.slice(0, 2).map(door => door.source_port), [0, 1],
    'When raw slot fields are zero, source player order resolves the P1/P2 ports');
  report.competitiveMatchStart.css_roster.push({label: 'CSS after P2 keyboard selection', doors: initialRoster});
  await shot('07-css-before-p2-source-join');

  const bounds = setup.geometry.slice(12, 24);
  const left = bounds[4], right = bounds[5];
  assert(Number.isFinite(left) && Number.isFinite(right) && right > left,
    `Original P2 CPU/Human toggle bounds are invalid: ${JSON.stringify(bounds)}`);
  await moveCssCursor('original P2 CPU/Human toggle',
    (x, y, current) => {
      const doorBounds = current.geometry.slice(12, 24);
      return x > doorBounds[4] + 0.2 && x < doorBounds[5] - 0.2 && y > -4.4 && y < 0;
    },
    (x, y, current) => {
      const doorBounds = current.geometry.slice(12, 24);
      const centerX = (doorBounds[4] + doorBounds[5]) / 2;
      return [x < centerX - 0.5 ? 80 : x > centerX + 0.5 ? -80 : 0,
        y < -2.2 ? 80 : y > -2.2 ? -80 : 0];
    });
  await sourcePadTap(0x0100, 'original CSS P2 CPU to empty');
  const empty = await waitCssDoor(1,
    door => door.p_kind === 3 && door.slot_type === 3,
    'original CSS P2 CPU-to-empty source transition');
  report.competitiveMatchStart.css_roster.push({label: 'original CSS P2 empty transition', door: empty.door});
  await sourcePadTap(0x0100, 'original CSS P2 empty to Human');
  const human = await waitCssDoor(1,
    door => door.p_kind === 0 && door.slot_type === 0,
    'original CSS P2 empty-to-Human source transition');
  assert.equal(human.door.character, 8,
    'Original CSS P2 Human transition must retain Mario without selecting another character');
  assert.deepEqual([0, 1, 2, 3].map(port => {
    const door = cssDoor(human.setup, port);
    return [door.p_kind, door.slot_type];
  }), [[0, 0], [0, 0], [3, 3], [3, 3]],
  'Original CSS must retain both Human doors and both empty doors after P2 joins');
  assert.deepEqual([0, 1].map(port => cssDoor(human.setup, port).source_port), [0, 1],
    'The two active source doors must retain their resolved controller ports');
  report.competitiveMatchStart.css_roster.push({label: 'original CSS P2 Human transition', door: human.door});
  await shot('08-css-two-human-mario');
  report.checks.push('Controls enables P2 keyboard; original CSS confirms door 1 CPU -> empty -> Human, raw slot fields remain zero, source-order fallback resolves ports 0/1, and doors 2/3 remain empty');
};
  return {configureSecondHuman: configureCompetitiveSecondHuman, cssDoor, moveCssCursor, waitCssDoor};
}
