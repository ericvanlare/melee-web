/** Browser hardware normalization. No simulation, network, or disc access. */
export const BUTTONS = Object.freeze({Left: 1, Right: 2, Down: 4, Up: 8,
  Z: 16, R: 32, L: 64, A: 256, B: 512, X: 1024, Y: 2048, Start: 4096});
export const AXES = Object.freeze(['stickX', 'stickY', 'cstickX', 'cstickY', 'triggerL', 'triggerR']);
const STORAGE = 'melee-controller-profiles-v1';
const TRIGGER_CLICK = 31150 / 32767;
const clamp = (n, lo, hi) => Math.max(lo, Math.min(hi, n));
const finite = n => typeof n === 'number' && Number.isFinite(n);
const empty = () => ({buttons: 0, stick: [0, 0], cstick: [0, 0], triggers: [0, 0]});
const button = index => ({kind: 'button', index});
const axis = (index, end = 1, rest = 0) => ({kind: 'axis', index, rest, end});
export const STANDARD_PROFILE = Object.freeze({version: 1, name: 'Browser standard', gamecube: false,
  buttons: {A: button(0), B: button(1), X: button(2), Y: button(3), Z: button(5), Start: button(9),
    L: null, R: null, Up: button(12), Down: button(13), Left: button(14), Right: button(15)},
  axes: {stickX: axis(0), stickY: axis(1, -1), cstickX: axis(2), cstickY: axis(3, -1),
    triggerL: button(6), triggerR: button(7)}});

// SDL's Mayflash face/stick definitions, adapted to Chromium/macOS HID usage
// indices (not SDL's native, interleaved collection indices). See CONTROLLERS.md.
// The digital L/R click indices are provisional until a physical sweep.
const MAYFLASH_MAC_PROFILE = {version: 1, name: 'Mayflash GameCube · Chrome on macOS', gamecube: true,
  buttons: {A: button(1), B: button(2), X: button(0), Y: button(3), Z: button(7), Start: button(9),
    L: button(4), R: button(5), ...Object.fromEntries(Object.entries({Up: 0, Right: 2, Down: 4, Left: 6})
      .map(([name, direction]) => [name, {kind: 'hat', index: 9, direction}]))},
  axes: {stickX: axis(0), stickY: axis(1, -1), cstickX: axis(5), cstickY: axis(2, -1),
    triggerL: axis(3, 1, -1), triggerR: axis(4, 1, -1)}};

function suggestedProfile(source, browser, platform) {
  if (browser !== 'Chromium' || !/^Mac/.test(platform) || source.mapping !== '' ||
      source.buttons.length !== 16 || source.axes.length !== 10 ||
      !/(?:Vendor: 0079 Product: 1843|0079-1843-)/i.test(source.id)) return null;
  return MAYFLASH_MAC_PROFILE;
}

export function copyRaw(pad) {
  return {buttons: Array.from(pad.buttons, b => ({pressed: !!b.pressed, value: b.value})),
    axes: Array.from(pad.axes)};
}

export function validateProfile(profile, raw) {
  if (!profile || profile.version !== 1 || typeof profile.gamecube !== 'boolean' ||
      typeof profile.name !== 'string' || profile.name.length > 100 || !profile.buttons || !profile.axes)
    throw Error('Invalid controller profile. Run controller setup again.');
  const usedButtons = new Set();
  const usedAxes = new Set();
  const usedAnalogButtons = new Set();
  const usedDirections = new Set();
  for (const [name, analog] of [...Object.keys(BUTTONS).map(n => [n, false]), ...AXES.map(n => [n, true])]) {
    const b = (analog ? profile.axes : profile.buttons)[name];
    if (b == null) {
      if (profile.gamecube || ['A', 'B', 'Start', 'stickX', 'stickY'].includes(name))
        throw Error(`Map ${name} before saving this controller.`);
      continue;
    }
    if (!Number.isInteger(b.index) || b.index < 0) throw Error(`Invalid ${name} input index.`);
    if (b.kind === 'button') {
      if (b.index >= raw.buttons.length || (analog && !name.startsWith('trigger')))
        throw Error(`${name} needs a ${analog ? 'stick axis' : 'valid button'}.`);
      if (!analog) {
        if (usedButtons.has(b.index)) throw Error('Two actions use the same button. Check the mapping.');
        usedButtons.add(b.index);
      } else {
        if (usedAnalogButtons.has(b.index)) throw Error('Two analog controls use the same button. Check the mapping.');
        usedAnalogButtons.add(b.index);
      }
    } else if (b.kind === 'axis') {
      if (b.index >= raw.axes.length || !finite(b.rest) || !finite(b.end) ||
          Math.abs(b.end - b.rest) < 0.15 || Math.abs(b.rest) > 1.1 || Math.abs(b.end) > 1.1)
        throw Error(`Invalid ${name} axis range.`);
      if (analog) {
        if (usedAxes.has(b.index)) throw Error('Two analog controls use the same axis. Check the mapping.');
        usedAxes.add(b.index);
      } else {
        const direction = `${b.index}:${Math.sign(b.end - b.rest)}`;
        if (usedDirections.has(direction)) throw Error('Two actions use the same axis direction. Check the mapping.');
        usedDirections.add(direction);
      }
    } else if (b.kind === 'hat') {
      if (analog || b.index >= raw.axes.length || !Number.isInteger(b.direction) ||
          b.direction < 0 || b.direction > 7 || !['Up', 'Right', 'Down', 'Left'].includes(name))
        throw Error(`Invalid ${name} directional pad.`);
      const direction = `hat:${b.index}:${b.direction}`;
      if (usedDirections.has(direction)) throw Error('Two actions use the same hat direction. Check the mapping.');
      usedDirections.add(direction);
    } else throw Error(`Unknown ${name} binding.`);
  }
  if (profile.gamecube) {
    for (const name of ['triggerL', 'triggerR']) {
      const pressure = profile.axes[name];
      if (Object.values(profile.buttons).some(click => click?.kind === pressure.kind && click.index === pressure.index))
        throw Error('GameCube trigger pressure must be independent of the digital buttons.');
    }
  }
  return profile;
}

function value(raw, binding, analog) {
  if (!binding) return 0;
  const i = binding.index;
  if (binding.kind === 'button') {
    const b = raw.buttons[i];
    if (!b || !finite(b.value) || b.value < 0 || b.value > 1) throw Error('Controller button layout changed.');
    return analog ? b.value : Number(b.pressed);
  }
  const v = raw.axes[i];
  if (!finite(v)) throw Error('Controller axis layout changed.');
  if (binding.kind === 'hat') {
    // HID hats have eight equally spaced values in [-1,1], with neutral outside
    // that range. Preserve the original float: narrowing it to int16 aliases
    // some neutral values with a diagonal.
    if (v < -1.01 || v > 1.01) return 0;
    const position = (v + 1) * 3.5, nearest = Math.round(position);
    if (Math.abs(position - nearest) > 0.08) return 0;
    const delta = (nearest - binding.direction + 8) % 8;
    return Number(delta === 0 || delta === 1 || delta === 7);
  }
  if (Math.abs(v) > 1.1) throw Error('Controller axis range changed. Run setup again.');
  const normalized = (v - binding.rest) / (binding.end - binding.rest);
  return analog ? normalized : Number(normalized > 0.5);
}

export function normalizeController(raw, profile) {
  const pad = empty();
  for (const [name, bit] of Object.entries(BUTTONS))
    if (value(raw, profile.buttons[name], false)) pad.buttons |= bit;
  // Original PAD units. The game's existing PADClamp remains the sole gameplay
  // clamp; no radial deadzone, smoothing, or extra simulation ticks are added.
  for (const [names, out] of [[['stickX', 'stickY'], pad.stick], [['cstickX', 'cstickY'], pad.cstick]])
    names.forEach((name, i) => { out[i] = clamp(Math.round(value(raw, profile.axes[name], true) * 128), -128, 127); });
  ['triggerL', 'triggerR'].forEach((name, i) => {
    const pressure = clamp(value(raw, profile.axes[name], true), 0, 1);
    pad.triggers[i] = Math.round(pressure * 255);
    if (!profile.gamecube && !profile.buttons[i ? 'R' : 'L'] && pressure > TRIGGER_CLICK)
      pad.buttons |= i ? BUTTONS.R : BUTTONS.L;
  });
  return pad;
}

function gamecubeIdentity(id) {
  return /game\s*cube|gamecube|wup-028/i.test(id) ||
    /(?:vendor: 057e product: (?:0337|2073)|057e-(?:0337|2073)-|vendor: 0079 product: 1843|0079-1843-)/i.test(id);
}

export function createControllerManager({getGamepads = () => navigator.getGamepads?.() || [],
  storage, platform = globalThis.navigator?.platform || '', userAgent = globalThis.navigator?.userAgent || ''} = {}) {
  if (storage === undefined) { try { storage = globalThis.localStorage; } catch { storage = null; } }
  const browser = /Firefox\//.test(userAgent) ? 'Firefox' : /(?:Chrome|Chromium|Edg)\//.test(userAgent) ? 'Chromium' : 'Other';
  const records = new Map(), profiles = new Map();
  const disconnected = [];
  function remember(record) {
    if (record.port >= 0) disconnected.push({layout: record.layout, port: record.port});
    if (disconnected.length > 64) disconnected.shift();
  }
  let testing = false, storageWarning = '', inputError = '', lastRows = [];
  try {
    const saved = JSON.parse(storage?.getItem(STORAGE) || '{}');
    if (saved.version === 1 && Array.isArray(saved.profiles))
      for (const pair of saved.profiles.slice(0, 64))
        if (Array.isArray(pair) && typeof pair[0] === 'string') profiles.set(pair[0], pair[1]);
  } catch { storageWarning = 'Saved controller settings could not be read. Setup remains available.'; }
  const persist = () => {
    try {
      if (!storage) throw Error('Storage unavailable');
      storage.setItem(STORAGE, JSON.stringify({version: 1, profiles: [...profiles]}));
      storageWarning = '';
    } catch { storageWarning = 'Controller settings apply to this tab only; browser storage is unavailable.'; }
  };
  function sample() {
    let pads; inputError = '';
    try { pads = Array.from(getGamepads()).filter(p => p?.connected !== false && p); }
    catch { pads = []; inputError = 'The browser could not read controllers. Check browser permissions or reconnect the adapter.'; }
    const seen = new Set(pads.map(p => p.index));
    for (const [index, record] of records) if (!seen.has(index)) { remember(record); records.delete(index); }
    const rows = [];
    for (const source of pads) {
      const layout = JSON.stringify([1, browser, platform, source.id, source.mapping, source.buttons.length, source.axes.length]);
      let record = records.get(source.index);
      if (!record || record.layout !== layout) {
        if (record) remember(record);
        const occupied = new Set([...records.values()].filter(r => r !== record).map(r => r.port));
        const previous = disconnected.filter(r => r.layout === layout);
        // The API has no serial identity for identical controllers. After an
        // ambiguous reconnect, leave assignment visible instead of guessing.
        let port = previous.length ? -1 : [0, 1, 2, 3].find(p => !occupied.has(p)) ?? -1;
        if (previous.length === 1 && !occupied.has(previous[0].port)) {
          port = previous[0].port; disconnected.splice(disconnected.indexOf(previous[0]), 1);
        }
        record = {layout, key: `${source.index}:${layout}`, port,
          neutralRequired: true};
        records.set(source.index, record);
      }
      const raw = copyRaw(source);
      const suggestion = suggestedProfile(source, browser, platform);
      let profile = profiles.get(layout), pad = empty(), reason = '', status = 'ready';
      let profileSource = profile ? 'saved' : suggestion ? 'suggested' : 'standard';
      if (!profile && suggestion) profile = suggestion;
      if (!profile && source.mapping === 'standard' && !gamecubeIdentity(source.id)) profile = STANDARD_PROFILE;
      if (!profile) { record.neutralRequired = true; status = 'needs-setup'; reason = 'Set up this controller before playing; its button layout is not verified.'; }
      else {
        try { validateProfile(profile, raw); pad = normalizeController(raw, profile); }
        catch (error) { record.neutralRequired = true; status = 'needs-setup'; reason = error.message; }
      }
      if (testing) record.neutralRequired = true;
      if (!testing && status === 'ready' && pad.buttons === 0 &&
          [...pad.stick, ...pad.cstick, ...pad.triggers].every(n => Math.abs(n) < 15)) record.neutralRequired = false;
      const active = record.port >= 0 && status === 'ready' && !testing && !record.neutralRequired;
      if (record.port < 0) reason = 'Choose a player port for this controller in Controls.' + (reason ? ` ${reason}` : '');
      rows.push({key: record.key, index: source.index, port: record.port, id: source.id, layout,
        gamecube: profile?.gamecube ?? gamecubeIdentity(source.id), profile: profile?.name || 'Unconfigured', status, reason, raw, pad,
        profileSource: profile ? profileSource : 'none', profileConfig: profile || null, hasSuggestion: !!suggestion,
        output: active ? pad : empty(), active, storageWarning});
    }
    lastRows = rows;
    return rows;
  }
  return Object.freeze({sample, inspect: sample,
    saveProfile(key, profile) {
      const row = sample().find(r => r.key === key);
      if (!row) throw Error('Controller disconnected. Connect it and restart setup.');
      validateProfile(profile, row.raw);
      profiles.set(row.layout, JSON.parse(JSON.stringify(profile)));
      for (const record of records.values()) if (record.layout === row.layout) record.neutralRequired = true;
      persist();
    },
    clearProfile(key) {
      const row = sample().find(r => r.key === key);
      if (!row) return;
      profiles.delete(row.layout); records.get(row.index).neutralRequired = true; persist();
    },
    assign(key, port) {
      if (!Number.isInteger(port) || port < -1 || port > 3) throw Error('Choose player 1–4 or leave this controller unassigned.');
      const row = sample().find(r => r.key === key);
      if (!row) throw Error('Controller disconnected.');
      for (let i = disconnected.length - 1; i >= 0; --i) if (disconnected[i].layout === row.layout) disconnected.splice(i, 1);
      const record = records.get(row.index), previous = record.port;
      for (const other of records.values()) if (port >= 0 && other !== record && other.port === port) {
        other.port = previous; other.neutralRequired = true;
      }
      record.port = port; record.neutralRequired = true;
    },
    setTesting(value) { testing = !!value; for (const record of records.values()) record.neutralRequired = true; },
    // Narrow synchronous ABI, called once at the existing source PAD boundary.
    // Eight int32 values per port: connected, buttons, LX, LY, CX, CY, LT, RT.
    writeSamples(heap, pointer) {
      heap.fill(0, pointer >> 2, (pointer >> 2) + 32);
      for (const row of sample()) {
        if (row.port < 0) continue;
        const p = row.output, base = (pointer >> 2) + row.port * 8;
        heap.set([1, p.buttons, ...p.stick, ...p.cstick, ...p.triggers], base);
      }
    },
    get error() { return inputError; },
    get lastSample() { return lastRows; },
  });
}
