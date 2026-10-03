import assert from 'node:assert/strict';
import {mountDiagnosticsSettings, readDiagnosticsPreference, writeDiagnosticsPreference,
  DIAGNOSTICS_PREFERENCE_KEY} from '../web/diagnostics-settings.mjs';

const stored = new Map();
const preferenceRoot = {localStorage: {getItem: key => stored.get(key), setItem: (key, value) => stored.set(key, value)}};
assert.equal(readDiagnosticsPreference(preferenceRoot), true);
writeDiagnosticsPreference(false, preferenceRoot);
assert.equal(stored.get(DIAGNOSTICS_PREFERENCE_KEY), 'off');
assert.equal(readDiagnosticsPreference(preferenceRoot), false);
writeDiagnosticsPreference(true, preferenceRoot);
assert.equal(readDiagnosticsPreference(preferenceRoot), true);
const denied = {get localStorage() { throw Error('private message must not escape'); }};
assert.equal(readDiagnosticsPreference(denied), true);
assert.doesNotThrow(() => writeDiagnosticsPreference(false, denied));

const nodes = new Map(['automatic-diagnostics', 'diagnostics-description', 'export-diagnostics',
  'diagnostics-export-status'].map(id => [id, {disabled: false, checked: false, textContent: '', title: ''}]));
const downloads = [], revoked = [], timers = [];
const document = {getElementById: id => nodes.get(id), createElement: () => {
  const link = {click() { downloads.push({name: this.download, url: this.href}); }}; return link;
}};
const root = {Blob, URL: {createObjectURL: () => 'blob:local-report', revokeObjectURL: value => revoked.push(value)},
  setTimeout: callback => {timers.push(callback);}};
let running = true, automatic = true, eligible = true, exports = 0;
const player = {getState: () => ({running, busy: false}),
  getDiagnosticsSettings: () => ({eligible, automatic}),
  setAutomaticDiagnostics: value => { automatic = value; },
  exportDiagnostics: async () => { exports++; return {current: {incidents: []}, retained: {records: []}}; }};
const settings = mountDiagnosticsSettings({document, root});
assert.equal(nodes.get('export-diagnostics').disabled, true);
settings.bindPlayer(player);
assert.equal(nodes.get('automatic-diagnostics').checked, true);
assert.equal(nodes.get('export-diagnostics').disabled, true, 'active frame work blocks export');
nodes.get('automatic-diagnostics').checked = false;
nodes.get('automatic-diagnostics').onchange();
assert.equal(automatic, false);
assert.match(nodes.get('diagnostics-description').textContent, /Local diagnostics remain/);
running = false; settings.setState({running: false, busy: true});
assert.equal(nodes.get('export-diagnostics').disabled, true, 'preparation/saves also block export');
settings.setState({running: false, busy: false, loading: {phase: 'catalog'}});
assert.equal(nodes.get('export-diagnostics').disabled, true, 'startup graphics preparation also blocks export');
settings.setState({running: false, busy: false});
await nodes.get('export-diagnostics').onclick();
assert.equal(exports, 1);
assert.deepEqual(downloads, [{name: 'webmelee-diagnostics.json', url: 'blob:local-report'}]);
assert.equal(nodes.get('diagnostics-export-status').textContent, 'Diagnostics exported.');
timers.forEach(callback => callback()); assert.deepEqual(revoked, ['blob:local-report']);
eligible = false; settings.setState({running: false, busy: false});
assert.equal(nodes.get('automatic-diagnostics').disabled, true);
assert.equal(nodes.get('automatic-diagnostics').checked, false);
assert.match(nodes.get('diagnostics-description').textContent, /stay on this device/);
player.exportDiagnostics = async () => { throw Error('private.invalid/user-file'); };
await nodes.get('export-diagnostics').onclick();
assert.equal(nodes.get('diagnostics-export-status').textContent, 'Diagnostics are unavailable for this visit.');
console.log('Diagnostics Settings preference, paused export, local-only and denial checks passed');
