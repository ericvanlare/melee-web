import assert from 'node:assert/strict';
import {requireConnectedPage, requireReplaySnapshot} from './character_reference_health.mjs';

const liveBrowser = {isConnected: () => true};
const livePage = {isClosed: () => false};
assert.doesNotThrow(() => requireConnectedPage(liveBrowser, livePage));
assert.throws(() => requireConnectedPage({isConnected: () => false}, livePage), /disconnected/);
assert.throws(() => requireConnectedPage(liveBrowser, {isClosed: () => true}), /page closed/);
assert.throws(() => requireReplaySnapshot(null), /unavailable/);
assert.throws(() => requireReplaySnapshot({snapshot_error:'CDP failed'}), /CDP failed/);
assert.throws(() => requireReplaySnapshot({runtime_error:'Native assertion'}), /Native assertion/);
assert.throws(() => requireReplaySnapshot({phase:7, source_cursor:null}), /phase\/cursor/);
assert.doesNotThrow(() => requireReplaySnapshot({phase:7, source_cursor:22560}));
console.log('Character replay rejects disconnected, closed, missing and failed observations');
