import {AXES, BUTTONS} from './controller-input.mjs';

// This panel is deliberately independent from the runtime. It only consumes
// the small controller-manager boundary, so it can also be used on a page that
// has no game or disc loaded.

const BUTTON_NAMES = Object.freeze(['A', 'B', 'X', 'Y', 'Z', 'Start', 'L', 'R', 'Up', 'Down', 'Left', 'Right']);
const AXIS_NAMES = Object.freeze([...AXES]);
const OPTIONAL_GENERIC = new Set(BUTTON_NAMES.filter(name => !['A', 'B', 'Start'].includes(name))
  .concat(AXIS_NAMES.filter(name => !['stickX', 'stickY'].includes(name))));
const CARDINAL_HATS = Object.freeze({Up: 0, Right: 2, Down: 4, Left: 6});
const AXIS_PROMPT = Object.freeze({
  stickX: 'Center the main stick, then move it fully right.',
  stickY: 'Center the main stick, then move it fully up.',
  cstickX: 'Center the C-stick, then move it fully right.',
  cstickY: 'Center the C-stick, then move it fully up.',
  triggerL: 'Release the L trigger, then squeeze it fully.',
  triggerR: 'Release the R trigger, then squeeze it fully.',
});
const BUTTON_PROMPT = Object.freeze({
  A: 'Press A.', B: 'Press B.', X: 'Press X.', Y: 'Press Y.', Z: 'Press Z.', Start: 'Press Start.',
  L: 'Press the L click.', R: 'Press the R click.', Up: 'Press D-pad up.', Down: 'Press D-pad down.',
  Left: 'Press D-pad left.', Right: 'Press D-pad right.',
});
const SAMPLE_INTERVAL_MS = 100;
const RAW_PRESSURE = 0.5;
const AXIS_MOVE = 0.28;
const STYLE_ID = 'melee-controller-panel-style-v1';

function deepCopy(value) {
  if (value == null) return value;
  if (typeof structuredClone === 'function') {
    try { return structuredClone(value); } catch { /* Fall through for older browsers. */ }
  }
  try { return JSON.parse(JSON.stringify(value)); } catch { return value; }
}

function finite(value) { return typeof value === 'number' && Number.isFinite(value); }

function rawButtonPressed(button) {
  return !!button && (button.pressed === true || (finite(button.value) && button.value > RAW_PRESSURE));
}

function hatDirection(value) {
  if (!finite(value) || value < -1.01 || value > 1.01) return null;
  const position = (value + 1) * 3.5;
  const nearest = Math.round(position);
  return Math.abs(position - nearest) <= 0.08 && nearest >= 0 && nearest <= 7 ? nearest : null;
}

function hatCardinals(direction) {
  if (!Number.isInteger(direction) || direction < 0 || direction > 7) return [];
  if (direction % 2 === 0) return [direction];
  return [direction - 1, (direction + 1) % 8];
}

function isDpadAction(name) { return Object.prototype.hasOwnProperty.call(CARDINAL_HATS, name); }

function emptyProfile(name, gamecube) {
  const buttons = Object.fromEntries(BUTTON_NAMES.map(action => [action, null]));
  const axes = Object.fromEntries(AXIS_NAMES.map(action => [action, null]));
  return {version: 1, name, gamecube, buttons, axes};
}

function rowRaw(row) {
  return row?.raw && Array.isArray(row.raw.buttons) && Array.isArray(row.raw.axes)
    ? row.raw : {buttons: [], axes: []};
}

function axisBaseline(raw, baseline) {
  return Array.isArray(baseline) ? baseline : raw.axes.map(value => finite(value) ? value : 0);
}

function isHatTransition(origin, value) {
  // GameCube adapters commonly report HID-hat neutral as a value outside
  // [-1,1] (for example 3.285714). This guard prevents a released trigger at
  // -1, or a centered stick at 0, from being mistaken for D-pad up.
  return finite(origin) && (origin < -1.01 || origin > 1.01) && finite(value) &&
    Math.abs(value - origin) > 0.15 && hatDirection(value) != null;
}

function actionBindingSection(action) { return AXIS_NAMES.includes(action) ? 'axes' : 'buttons'; }

function stableAxisBaseline(state, raw, counterName) {
  const values = raw.axes.map(value => finite(value) ? value : 0);
  const previous = state.baselineCandidate;
  if (!previous || previous.length !== values.length || values.some((value, index) => Math.abs(value - previous[index]) > 0.04)) {
    state.baselineCandidate = values;
    state[counterName] = 1;
    return false;
  }
  state[counterName] += 1;
  if (state[counterName] < 2) return false;
  state.baseline = state.baselineCandidate.slice();
  state.baselineCandidate = null;
  return true;
}

function bindingUsed(profile, candidate, action) {
  const section = actionBindingSection(action);
  if (!candidate) return false;
  for (const [name, binding] of Object.entries(profile[section] || {})) {
    if (name === action || !binding || binding.kind !== candidate.kind || binding.index !== candidate.index) continue;
    // Different cardinal directions on one HID hat are intentionally shared.
    if (candidate.kind === 'hat' && binding.direction !== candidate.direction) continue;
    if (section === 'buttons' && candidate.kind === 'axis' && candidate.rest === 0 && binding.rest === 0 &&
        Math.sign(candidate.end) !== Math.sign(binding.end)) continue;
    return true;
  }
  return false;
}

/**
 * Detect one binding from a raw Gamepad sample. This is exported so the
 * released-then-press and hat behavior can be tested without a browser UI.
 * `baseline` is the raw axis array captured while an analog control is at
 * rest. D-pad hats are converted to cardinal bindings while retaining the
 * neighboring diagonal directions in the manager's hat decoder.
 */
export function detectBindingCandidate(rawInput, action, baseline) {
  const raw = rawInput || {buttons: [], axes: []};
  const buttons = Array.isArray(raw.buttons) ? raw.buttons : [];
  const axes = Array.isArray(raw.axes) ? raw.axes : [];
  if (!BUTTON_NAMES.includes(action) && !AXIS_NAMES.includes(action)) return null;

  if (isDpadAction(action)) {
    // Prefer explicit browser d-pad buttons when one is pressed.
    const buttonIndex = buttons.findIndex(rawButtonPressed);
    if (buttonIndex >= 0) return {kind: 'button', index: buttonIndex};
    if (!Array.isArray(baseline)) return null;
    const rest = axisBaseline(raw, baseline);
    const expected = CARDINAL_HATS[action];
    for (let index = 0; index < axes.length; index += 1) {
      const direction = hatDirection(axes[index]);
      if (direction == null || !isHatTransition(rest[index], axes[index]) || !hatCardinals(direction).includes(expected)) continue;
      return {kind: 'hat', index, direction: expected};
    }
    // Some browser backends expose a D-pad as two centered signed axes.
    for (let index = 0; index < axes.length; index += 1) {
      if (Math.abs(rest[index]) < 0.1 && finite(axes[index]) && Math.abs(axes[index]) > 0.5 && Math.abs(axes[index]) <= 1.01)
        return {kind: 'axis', index, rest: 0, end: Math.sign(axes[index])};
    }
    return null;
  }

  if (!AXIS_NAMES.includes(action)) {
    const index = buttons.findIndex(rawButtonPressed);
    return index >= 0 ? {kind: 'button', index} : null;
  }

  const rest = axisBaseline(raw, baseline);
  let best = null;
  for (let index = 0; index < axes.length; index += 1) {
    const value = axes[index];
    const origin = finite(rest[index]) ? rest[index] : 0;
    if (!finite(value) || !finite(origin)) continue;
    const delta = value - origin;
    // Accept either hardware sign. The prompt names the physical direction,
    // while `end` records the observed sign so the manager maps that action to
    // positive logical output. Stick calibration deliberately keeps the
    // source's [-1,1] scale; a physical stick often tops out around .8.
    const directed = Math.abs(delta);
    if (directed < AXIS_MOVE) continue;
    if (!best || directed > best.score)
      best = {kind: 'axis', index, rest: origin, end: value, score: directed, delta};
  }
  if (best) {
    if (action === 'triggerL' || action === 'triggerR') {
      // Browser adapters expose released triggers as either -1 or 0. Preserve
      // that canonical origin and map the observed squeeze direction to the
      // corresponding full-scale endpoint; never use a partial squeeze as 1.
      best.rest = best.rest <= -0.5 ? -1 : 0;
      best.end = best.delta >= 0 ? 1 : -1;
    } else {
      best.rest = 0;
      best.end = best.delta >= 0 ? 1 : -1;
    }
    delete best.score; delete best.delta; return best;
  }

  // Some generic browsers expose triggers as analog button objects without a
  // corresponding axis. The engine accepts those as analog trigger bindings.
  if (action === 'triggerL' || action === 'triggerR') {
    const index = buttons.findIndex(button => finite(button?.value) && button.value > RAW_PRESSURE || button?.pressed === true);
    if (index >= 0) return {kind: 'button', index};
  }
  return null;
}

function rawIsReleased(rawInput) {
  const raw = rawInput || {buttons: [], axes: []};
  // Axes are intentionally omitted here. A number of GameCube adapters expose
  // a released trigger as -1, which is also a valid encoded HID-hat endpoint.
  // Analog prompts capture their own rest value, while a held d-pad axis is
  // rejected naturally when it does not match the next requested direction.
  return (raw.buttons || []).every(button => !rawButtonPressed(button));
}

function previousBindingReleased(state, raw) {
  const previous = state.lastBinding;
  if (!previous) return true;
  const binding = previous.binding;
  if (binding.kind === 'button') return !rawButtonPressed(raw.buttons?.[binding.index]);
  const value = raw.axes?.[binding.index];
  if (!finite(value)) return false;
  if (binding.kind === 'hat') {
    // The captured neutral value for a HID hat is outside [-1,1]. A pressed
    // cardinal or diagonal remains an encoded value inside that range.
    return finite(previous.rest) && Math.abs(previous.rest) > 1.01 && Math.abs(value) > 1.01 &&
      Math.abs(value - previous.rest) < 0.15;
  }
  const denominator = binding.end - previous.rest;
  if (!finite(denominator) || Math.abs(denominator) < 0.01) return false;
  return Math.abs((value - previous.rest) / denominator) < 0.15;
}

function setAttributes(node, attrs = {}) {
  for (const [name, value] of Object.entries(attrs)) {
    if (value == null || value === false) continue;
    if (name === 'className') node.className = value;
    else if (name === 'textContent') node.textContent = value;
    else if (name === 'checked' || name === 'disabled' || name === 'hidden') node[name] = !!value;
    else node.setAttribute(name, value === true ? '' : String(value));
  }
  return node;
}

function element(tag, attrs, ...children) {
  const node = document.createElement(tag);
  setAttributes(node, attrs);
  node.append(...children.filter(child => child != null));
  return node;
}

function text(value) { return document.createTextNode(String(value)); }

function installStyle() {
  if (document.getElementById(STYLE_ID) || document.querySelector?.('link[data-melee-controller-panel-style]')) return;
  const link = element('link', {id: STYLE_ID, 'data-melee-controller-panel-style': '', rel: 'stylesheet', href: new URL('./controller-panel.css', import.meta.url).href});
  document.head?.append(link);
}

function profileFromRow(row, gamecube) {
  const previous = row?.profileConfig;
  const name = typeof previous?.name === 'string' && previous.name ? previous.name : `${gamecube ? 'GameCube' : 'Controller'} profile`;
  return emptyProfile(name, gamecube);
}

function profileLabel(row) {
  if (!row) return 'No controller selected';
  const profile = typeof row.profile === 'string' ? row.profile : row.profile?.name;
  return profile || 'Unconfigured';
}

function padButtonState(pad, name) {
  const bit = BUTTONS[name];
  return Number.isInteger(bit) && !!(Number(pad?.buttons || 0) & bit);
}

function numberPair(value, fallback = 0) {
  return Array.isArray(value) ? [finite(value[0]) ? value[0] : fallback, finite(value[1]) ? value[1] : fallback] : [fallback, fallback];
}

function meter(container, value, min, max) {
  const safe = finite(value) ? value : min;
  const ratio = Math.max(0, Math.min(1, (safe - min) / (max - min || 1)));
  const fill = container.matches('progress') ? container : container.querySelector('progress');
  if (fill) fill.value = Math.round(ratio * 100);
}

function renderLogicalBlock(root, row) {
  if (!root) return;
  const pad = row?.pad || {};
  root.replaceChildren();
  const status = row ? `${row.status === 'ready' ? 'Ready' : 'Needs setup'}${row.active ? ' · output active' : ' · output muted'}` : 'No controller selected';
  root.append(element('p', {className: 'mcp-caption', textContent: status}));
  if (row?.reason) root.append(element('p', {className: 'mcp-warning', textContent: row.reason}));

  const buttonBlock = element('div', {className: 'mcp-live-block'});
  buttonBlock.append(element('h3', {textContent: 'Buttons'}));
  const buttonGrid = element('div', {className: 'mcp-button-grid', role: 'group', 'aria-label': 'Logical controller buttons'});
  for (const name of BUTTON_NAMES) {
    const pressed = padButtonState(pad, name);
    buttonGrid.append(element('span', {
      className: 'mcp-logical-button', 'data-pressed': String(pressed), 'aria-label': `${name}${pressed ? ' pressed' : ' released'}`,
    }, text(name)));
  }
  buttonBlock.append(buttonGrid);

  const [sx, sy] = numberPair(pad.stick), [cx, cy] = numberPair(pad.cstick), [lt, rt] = numberPair(pad.triggers);
  const analogBlock = element('div', {className: 'mcp-live-block'});
  analogBlock.append(element('h3', {textContent: 'Analog'}));
  const analogValues = [
    ['Main stick', `${Math.round(sx)}, ${Math.round(sy)}`, sx, sy, -128, 127],
    ['C-stick', `${Math.round(cx)}, ${Math.round(cy)}`, cx, cy, -128, 127],
    ['L trigger', `${Math.round(lt)}`, lt, lt, 0, 255],
    ['R trigger', `${Math.round(rt)}`, rt, rt, 0, 255],
  ];
  for (const [label, value, first, second, min, max] of analogValues) {
    const line = element('div');
    line.append(element('div', {className: 'mcp-value'}, text(`${label}: ${value}`)));
    const bar = element('progress', {className: 'mcp-meter', min: 0, max: 100, value: 0, 'aria-label': `${label} ${value}`});
    meter(bar, Math.max(first, second), min, max);
    line.append(bar); analogBlock.append(line);
  }
  root.append(element('div', {className: 'mcp-live'}, buttonBlock, analogBlock));
}

function describeRow(row) {
  const port = Number.isInteger(row?.port) && row.port >= 0 ? `Port ${row.port + 1}` : 'Unassigned';
  const status = row?.status === 'ready' ? 'ready' : 'needs setup';
  return `${port} · ${row?.id || 'Unnamed controller'} · ${status}`;
}

function rowSignature(rows) {
  return rows.map(row => [row.key, row.index, row.port, row.id, row.status, profileLabel(row), row.profileSource,
    JSON.stringify(row.profileConfig), row.reason, row.storageWarning].join('\u0001')).join('\u0002');
}

function describeBinding(binding) {
  if (!binding) return 'Unmapped';
  if (binding.kind === 'button') return `Button ${binding.index}`;
  if (binding.kind === 'hat') return `Hat axis ${binding.index} · ${['up', 'up/right', 'right', 'down/right', 'down', 'down/left', 'left', 'up/left'][binding.direction]}`;
  return `Axis ${binding.index} · ${binding.rest} → ${binding.end}`;
}

function safeRows(manager) {
  try {
    const rows = manager?.inspect?.();
    if (manager.error) return {error: Error(manager.error)};
    return Array.isArray(rows) ? rows : [];
  } catch (error) {
    return {error};
  }
}

function promptFor(action) {
  return AXIS_NAMES.includes(action) ? AXIS_PROMPT[action] : BUTTON_PROMPT[action];
}

/** Mount the compact controller status/setup panel and return its cleanup. */
export function mountControllerPanel(container, manager) {
  if (!container || typeof container.append !== 'function') throw Error('A controller panel container is required.');
  if (!manager || typeof manager.inspect !== 'function') throw Error('A controller manager is required.');
  installStyle();

  const root = element('section', {className: 'melee-controller-panel', 'data-controller-panel': '', 'aria-label': 'Controller setup and test'});
  const heading = element('header');
  heading.append(element('h2', {textContent: 'Controller setup'}));
  heading.append(element('p', {textContent: 'Connect a controller and press a button. Recognized layouts work immediately; check the live display and change any binding that is wrong. Settings and recordings stay local to this browser.'}));
  const status = element('p', {'role': 'status', 'aria-live': 'polite'});
  heading.append(status); root.append(heading);

  const deviceSelect = element('select', {'aria-label': 'Controller device'});
  const kindSelect = element('select', {'aria-label': 'Controller kind'},
    element('option', {value: 'generic', textContent: 'Other controller'}),
    element('option', {value: 'gamecube', textContent: 'GameCube controller'}));
  const configureButton = element('button', {type: 'button', textContent: 'Set up selected'});
  const refreshButton = element('button', {type: 'button', textContent: 'Refresh'});
  const toolbar = element('div', {className: 'mcp-toolbar'},
    element('label', {className: 'mcp-field'}, element('span', {textContent: 'Device'}), deviceSelect),
    element('label', {className: 'mcp-field'}, element('span', {textContent: 'Kind'}), kindSelect),
    configureButton, refreshButton);
  root.append(toolbar);

  const devicesCard = element('section', {className: 'mcp-card', 'aria-labelledby': 'mcp-devices-title'});
  devicesCard.append(element('h3', {id: 'mcp-devices-title', textContent: 'Detected devices'}));
  const devices = element('div', {className: 'mcp-device-list', 'aria-live': 'polite'});
  devicesCard.append(devices); root.append(devicesCard);

  const liveCard = element('section', {className: 'mcp-card', 'aria-labelledby': 'mcp-live-title'});
  liveCard.append(element('h3', {id: 'mcp-live-title', textContent: 'Live logical input'}));
  const live = element('div'); liveCard.append(live); root.append(liveCard);

  const bindingsCard = element('section', {className: 'mcp-card', 'aria-label': 'Controller bindings'});
  const bindings = element('div'); bindingsCard.append(bindings); root.append(bindingsCard);

  const recordingCard = element('section', {className: 'mcp-card', 'aria-labelledby': 'mcp-recording-title'});
  recordingCard.append(element('h3', {id: 'mcp-recording-title', textContent: 'Local recording'}));
  recordingCard.append(element('p', {className: 'mcp-muted', textContent: 'Record raw samples and the setup prompts for debugging. The file contains no game, disc, network, or account data.'}));
  const recordStart = element('button', {type: 'button', textContent: 'Start recording'});
  const recordStop = element('button', {type: 'button', textContent: 'Stop and prepare download', disabled: true});
  const recordLink = element('a', {hidden: true, download: 'melee-controller-check.json'}, text('Download recording'));
  recordingCard.append(element('div', {className: 'mcp-recording'}, recordStart, recordStop, recordLink)); root.append(recordingCard);

  const wizard = element('section', {className: 'mcp-dialog', role: 'dialog', 'aria-labelledby': 'mcp-wizard-title', hidden: true});
  wizard.append(element('h3', {id: 'mcp-wizard-title', textContent: 'Controller mapping'}));
  const wizardName = element('input', {type: 'text', maxlength: 100, autocomplete: 'off', 'aria-label': 'Profile name'});
  wizard.append(element('label', {className: 'mcp-field'}, element('span', {textContent: 'Profile name'}), wizardName));
  const wizardProgress = element('p', {className: 'mcp-caption'});
  const wizardPrompt = element('p', {'aria-live': 'assertive'});
  const wizardHint = element('p', {className: 'mcp-muted'});
  const wizardPrompts = element('ol', {className: 'mcp-prompt-list', 'aria-label': 'Mapping progress'});
  const skipButton = element('button', {type: 'button', textContent: 'Skip optional control', hidden: true});
  const cancelButton = element('button', {type: 'button', textContent: 'Cancel'});
  wizard.append(wizardProgress, wizardPrompt, wizardHint, wizardPrompts,
    element('div', {className: 'mcp-dialog-actions'}, skipButton, cancelButton));
  root.append(wizard);
  container.append(root);

  let currentRows = [];
  let selectedKey = '';
  let renderedSignature = null;
  let bindingsSignature = null;
  let wizardState = null;
  let timer = null;
  let destroyed = false;
  let recording = null;
  let recordingUrl = '';
  let lastPollError = '';

  function setStatus(message, className = '') {
    status.textContent = message || '';
    status.className = className;
  }

  function selectedRow(rows = currentRows) {
    return rows.find(row => row.key === selectedKey) || rows[0] || null;
  }

  function renderDeviceList(rows) {
    const signature = rowSignature(rows);
    if (signature === renderedSignature) return;
    renderedSignature = signature;
    const keep = selectedKey;
    deviceSelect.replaceChildren();
    if (!rows.length) deviceSelect.append(element('option', {value: '', textContent: 'No controller detected'}));
    for (const row of rows) deviceSelect.append(element('option', {value: row.key, textContent: describeRow(row)}));
    selectedKey = rows.some(row => row.key === keep) ? keep : rows[0]?.key || '';
    deviceSelect.value = selectedKey;
    if (selectedKey !== keep) kindSelect.value = selectedRow(rows)?.gamecube ? 'gamecube' : 'generic';
    deviceSelect.disabled = !rows.length;
    kindSelect.disabled = !rows.length;
    configureButton.disabled = !rows.length;

    devices.replaceChildren();
    if (!rows.length) {
      devices.append(element('p', {className: 'mcp-muted', textContent: 'No controller detected. Connect it and press a button to let the browser detect it, then refresh.'}));
      return;
    }
    for (const row of rows) {
      const portSelect = element('select', {'aria-label': `Player port for ${row.id || row.key}`},
        element('option', {value: '-1', textContent: 'Unassigned'}));
      for (let port = 0; port < 4; port += 1)
        portSelect.append(element('option', {value: String(port), textContent: `Port ${port + 1}`}));
      if (Number.isInteger(row.port) && row.port >= -1 && row.port < 4) portSelect.value = String(row.port);
      portSelect.addEventListener('change', () => {
        try {
          manager.assign(row.key, Number(portSelect.value));
          setStatus(Number(portSelect.value) < 0 ? 'Controller left unassigned.' : `Assigned ${row.id || 'controller'} to Port ${Number(portSelect.value) + 1}.`);
          poll();
        } catch (error) { setStatus(error.message || String(error), 'mcp-danger'); }
      });
      const configure = element('button', {type: 'button', textContent: 'Set up'});
      configure.addEventListener('click', () => openWizard(row.key));
      const clear = element('button', {type: 'button', textContent: row.hasSuggestion ? 'Use suggested mapping' : 'Clear profile', disabled: row.profileSource !== 'saved'});
      clear.addEventListener('click', () => {
        try { manager.clearProfile(row.key); setStatus('Saved profile cleared.'); poll(); }
        catch (error) { setStatus(error.message || String(error), 'mcp-danger'); }
      });
      const article = element('article');
      article.append(element('h3', {textContent: `${row.id || 'Unnamed controller'} · ${row.status === 'ready' ? 'Ready' : 'Needs setup'}`}));
      article.append(element('div', {className: 'mcp-device-meta', textContent: `${Number.isInteger(row.index) ? `Browser index ${row.index}` : 'Browser index unknown'} · ${row.profileSource === 'suggested' ? 'Suggested: ' : ''}${profileLabel(row)}`}));
      if (row.reason) article.append(element('div', {className: 'mcp-device-meta mcp-warning', textContent: row.reason}));
      article.append(element('div', {className: 'mcp-actions'}, element('label', {className: 'mcp-field'}, element('span', {textContent: 'Player port'}), portSelect), configure, clear));
      devices.append(article);
    }
  }

  function renderBindings(row) {
    const signature = row ? `${row.key}:${row.profileSource}:${JSON.stringify(row.profileConfig)}` : '';
    if (signature === bindingsSignature) return;
    bindingsSignature = signature;
    bindings.replaceChildren(); bindingsCard.hidden = !row?.profileConfig;
    if (!row?.profileConfig) return;
    bindings.append(element('h3', {textContent: row.profileSource === 'suggested' ? 'Suggested Mayflash mapping · active' : 'Current bindings'}));
    if (row.profileSource === 'suggested') {
      bindings.append(element('p', {textContent: 'This starting layout uses SDL’s Mayflash definitions, adapted to Chrome on macOS and this adapter’s USB layout. No setup is needed to try it.'}));
      bindings.append(element('p', {className: 'mcp-warning', textContent: 'L/R click bindings are provisional. Check light pressure and each full click separately. This mapping has not yet been verified with a physical controller sweep.'}));
    }
    bindings.append(element('p', {className: 'mcp-caption', textContent: 'Input numbers are browser indices, starting at 0. Change only a control that needs correction; assigning an occupied input swaps the two bindings.'}));
    const grid = element('div', {className: 'mcp-bindings'});
    for (const action of [...BUTTON_NAMES, ...AXIS_NAMES]) {
      const label = ({L: 'L click', R: 'R click', stickX: 'Main stick right', stickY: 'Main stick up',
        cstickX: 'C-stick right', cstickY: 'C-stick up', triggerL: 'L pressure', triggerR: 'R pressure'})[action] || action;
      const change = element('button', {type: 'button', textContent: 'Change', 'aria-label': `Change ${label} binding`});
      change.addEventListener('click', () => openWizard(row.key, action));
      grid.append(element('div', {className: 'mcp-binding', 'data-binding': action},
        element('div', {}, element('strong', {textContent: label}), element('div', {className: 'mcp-caption', textContent: describeBinding(row.profileConfig[actionBindingSection(action)][action])})), change));
    }
    bindings.append(grid);
  }

  function renderWizard() {
    if (!wizardState) { wizard.hidden = true; return; }
    wizard.hidden = false;
    const current = wizardState.actions[wizardState.stepIndex];
    wizardName.value = wizardState.profile.name;
    wizardProgress.textContent = current ? `Step ${wizardState.stepIndex + 1} of ${wizardState.actions.length}` : 'Mapping complete';
    wizardPrompt.textContent = current ? `${current}: ${promptFor(current)}` : 'All required controls are mapped. Save this profile to use it.';
    wizardHint.textContent = current
      ? AXIS_NAMES.includes(current)
        ? (wizardState.phase === 'rest' ? 'Keep every button released and center the requested control.' : 'Hold the requested direction until it is detected.')
        : (wizardState.phase === 'release' ? 'Release every control first. This prevents a held button from being mapped accidentally.' : 'The first pressed control is recorded for this action. D-pad diagonals stay usable.')
      : '';
    skipButton.hidden = !current || wizardState.gamecube || !OPTIONAL_GENERIC.has(current);
    wizardPrompts.replaceChildren();
    for (const action of wizardState.actions) {
      const mapped = wizardState.profile[actionBindingSection(action)]?.[action] != null;
      wizardPrompts.append(element('li', {'data-current': String(action === current), 'data-done': String(mapped)}, text(action)));
    }
  }

  function openWizard(key, action = null) {
    const row = currentRows.find(candidate => candidate.key === key);
    if (!row) { setStatus('Select a connected controller before setup.', 'mcp-warning'); return; }
    selectedKey = key; deviceSelect.value = key;
    const gamecube = action ? row.profileConfig.gamecube : kindSelect.value === 'gamecube';
    const profile = action ? deepCopy(row.profileConfig) : profileFromRow(row, gamecube);
    if (action) profile.name = `${gamecube ? 'GameCube' : 'Controller'} custom mapping`;
    wizardState = {
      key, gamecube, profile, actions: action ? [action] : [...BUTTON_NAMES, ...AXIS_NAMES], stepIndex: 0,
      phase: AXIS_NAMES.includes(action) ? 'rest' : 'release', baseline: null,
      baselineCandidate: null, lastBinding: null, releaseSamples: 0, restSamples: 0,
    };
    wizardName.value = profile.name; renderWizard();
    wizardName.focus();
    setStatus(`Setting up ${row.id || 'controller'}.`);
  }

  function closeWizard(message = 'Controller setup cancelled.') {
    wizardState = null; renderWizard(); setStatus(message);
  }

  function nextWizardStep(message) {
    if (!wizardState) return;
    wizardState.stepIndex += 1;
    wizardState.phase = wizardState.stepIndex < wizardState.actions.length && AXIS_NAMES.includes(wizardState.actions[wizardState.stepIndex]) ? 'rest' : 'release';
    wizardState.baseline = null; wizardState.baselineCandidate = null;
    wizardState.releaseSamples = 0; wizardState.restSamples = 0;
    renderWizard();
    if (message) setStatus(message);
  }

  function finishWizard() {
    if (!wizardState) return;
    const done = wizardState;
    const name = wizardName.value.trim().slice(0, 100) || `${done.gamecube ? 'GameCube' : 'Controller'} profile`;
    done.profile.name = name;
    try {
      manager.saveProfile(done.key, deepCopy(done.profile));
      wizardState = null; renderWizard(); setStatus(`Saved ${name}.${done.swapped ? ` Swapped ${done.swapped}.` : ''} Hold the controller neutral before playing.`); poll();
    } catch (error) { setStatus(error.message || String(error), 'mcp-danger'); }
  }

  function processWizard(rows) {
    if (!wizardState) return;
    const row = rows.find(candidate => candidate.key === wizardState.key);
    if (!row) { wizardPrompt.textContent = 'Controller disconnected. Reconnect it to continue setup.'; return; }
    const action = wizardState.actions[wizardState.stepIndex];
    if (!action) { finishWizard(); return; }
    const raw = rowRaw(row);
    if (wizardState.phase === 'release') {
      if (!rawIsReleased(raw)) {
        wizardState.releaseSamples = 0; wizardState.baselineCandidate = null;
        wizardHint.textContent = 'Release every button and D-pad direction to arm this step.'; return;
      }
      if (!previousBindingReleased(wizardState, raw)) {
        wizardState.releaseSamples = 0; wizardState.baselineCandidate = null;
        wizardHint.textContent = 'Release the control just mapped before continuing.'; return;
      }
      if (!stableAxisBaseline(wizardState, raw, 'releaseSamples')) return;
      wizardState.phase = 'press'; renderWizard(); return;
    }
    if (wizardState.phase === 'rest') {
      if (!rawIsReleased(raw)) { wizardState.restSamples = 0; wizardState.baselineCandidate = null; return; }
      if (!previousBindingReleased(wizardState, raw)) {
        wizardState.restSamples = 0; wizardState.baselineCandidate = null;
        wizardHint.textContent = 'Release the control just mapped before centering this one.'; return;
      }
      if (!stableAxisBaseline(wizardState, raw, 'restSamples')) return;
      wizardState.phase = 'move'; renderWizard(); return;
    }
    const candidate = detectBindingCandidate(raw, action, wizardState.baseline);
    if (!candidate) return;
    if (bindingUsed(wizardState.profile, candidate, action)) {
      const section = actionBindingSection(action), entries = wizardState.profile[section];
      const conflict = Object.keys(entries).filter(name => name !== action && bindingUsed({[section]: {[name]: entries[name]}}, candidate, action));
      if (wizardState.actions.length !== 1 || conflict.length !== 1 || !entries[action]) {
        wizardHint.textContent = 'That control is already mapped. Release it and use a different control.';
        return;
      }
      entries[conflict[0]] = deepCopy(entries[action]);
      wizardState.swapped = `${action} and ${conflict[0]}`;
    }
    wizardState.profile[actionBindingSection(action)][action] = candidate;
    wizardState.lastBinding = {
      binding: deepCopy(candidate),
      rest: wizardState.baseline?.[candidate.index] ?? null,
    };
    nextWizardStep(`${action} mapped. Release the control for the next step.`);
  }

  function recordingSample(rows) {
    if (!recording?.active || !rows.length) return;
    const row = selectedRow(rows);
    if (!row) return;
    if (recording.samples.length >= recording.limit) {
      recording.truncated = true;
      return;
    }
    recording.samples.push({
      sample: recording.samples.length,
      deviceKey: row.key,
      index: row.index,
      port: row.port,
      raw: deepCopy(row.raw || {buttons: [], axes: []}),
      pad: deepCopy(row.pad || null),
      output: deepCopy(row.output || null),
      status: row.status,
      active: !!row.active,
      prompt: wizardState ? {
        action: wizardState.actions[wizardState.stepIndex] || null,
        phase: wizardState.phase,
      } : null,
    });
  }

  function recordingPrompts() {
    return [...BUTTON_NAMES, ...AXIS_NAMES].map(action => ({
      action, kind: AXIS_NAMES.includes(action) ? 'axis' : 'button',
      required: !OPTIONAL_GENERIC.has(action), text: promptFor(action),
    }));
  }

  function downloadRecording() {
  if (!recording || !recording.samples.length || typeof Blob === 'undefined' ||
      typeof URL === 'undefined' || typeof URL.createObjectURL !== 'function') {
      setStatus('Record at least one sample before downloading.', 'mcp-warning'); return;
    }
    if (recordingUrl && typeof URL !== 'undefined' && typeof URL.revokeObjectURL === 'function') URL.revokeObjectURL(recordingUrl);
    const row = selectedRow();
    const data = {
      version: 1, kind: 'melee-controller-check',
      device: row ? {key: row.key, index: row.index, port: row.port, id: row.id || ''} : null,
      prompts: recording.prompts, samples: recording.samples,
      truncated: !!recording.truncated,
    };
    recordingUrl = URL.createObjectURL(new Blob([JSON.stringify(data, null, 2)], {type: 'application/json'}));
    recordLink.href = recordingUrl; recordLink.hidden = false;
    setStatus(`Prepared a local recording with ${recording.samples.length} samples.`);
  }

  function startRecording() {
    recording = {active: true, limit: 20000, prompts: recordingPrompts(), samples: [], truncated: false};
    recordLink.hidden = true; recordStart.disabled = true; recordStop.disabled = false;
    setStatus('Recording raw controller samples locally.');
  }

  function stopRecording() {
    if (recording) recording.active = false;
    recordStop.disabled = true; recordStart.disabled = false; downloadRecording();
  }

  function poll() {
    if (destroyed) return;
    const result = safeRows(manager);
    if (result?.error) {
      currentRows = []; renderDeviceList([]); renderLogicalBlock(live, null); renderBindings(null);
      const message = result.error.message || String(result.error);
      if (message !== lastPollError) setStatus(message, 'mcp-danger');
      lastPollError = message;
    } else {
      currentRows = result; lastPollError = '';
      renderDeviceList(currentRows);
      const row = selectedRow(currentRows);
      renderLogicalBlock(live, row);
      renderBindings(row);
      if (row?.storageWarning) setStatus(row.storageWarning, 'mcp-warning');
      recordingSample(currentRows); processWizard(currentRows);
    }
  }

  deviceSelect.addEventListener('change', () => { selectedKey = deviceSelect.value; kindSelect.value = selectedRow()?.gamecube ? 'gamecube' : 'generic'; renderLogicalBlock(live, selectedRow()); renderBindings(selectedRow()); });
  configureButton.addEventListener('click', () => openWizard(deviceSelect.value || selectedKey));
  refreshButton.addEventListener('click', () => { poll(); setStatus('Controller list refreshed.'); });
  cancelButton.addEventListener('click', () => closeWizard());
  wizard.addEventListener('keydown', event => {
    if (event.key === 'Escape') { event.preventDefault(); closeWizard(); }
  });
  skipButton.addEventListener('click', () => {
    if (!wizardState) return;
    const action = wizardState.actions[wizardState.stepIndex];
    if (wizardState.gamecube || !OPTIONAL_GENERIC.has(action)) return;
    wizardState.profile[actionBindingSection(action)][action] = null;
    nextWizardStep(`${action} skipped.`);
  });
  wizardName.addEventListener('input', () => { if (wizardState) wizardState.profile.name = wizardName.value.slice(0, 100); });
  recordStart.addEventListener('click', startRecording);
  recordStop.addEventListener('click', stopRecording);

  poll();
  timer = setInterval(poll, SAMPLE_INTERVAL_MS);
  function cleanup() {
    if (destroyed) return;
    destroyed = true; if (timer != null) clearInterval(timer);
    if (recordingUrl && typeof URL !== 'undefined' && typeof URL.revokeObjectURL === 'function') URL.revokeObjectURL(recordingUrl);
    root.remove();
  }
  return cleanup;
}

export const controllerPanelActions = Object.freeze({BUTTON_NAMES, AXIS_NAMES, CARDINAL_HATS});
