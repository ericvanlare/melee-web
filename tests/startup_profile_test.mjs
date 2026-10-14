import assert from 'node:assert/strict';
import {createStartupProfile, STARTUP_PROFILE_SCHEMA} from '../web/startup-profile.mjs';

let clock = 100;
const resources = [
  {name: 'https://cdn.test/runtime/gameplay_public.js?build=secret', startTime: 1, duration: 12,
    transferSize: 100, encodedBodySize: 90, decodedBodySize: 80, initiatorType: 'script'},
  {name: 'https://cdn.test/runtime/gameplay_audio_preview.wasm', startTime: 2, duration: 34,
    transferSize: 200, encodedBodySize: 190, decodedBodySize: 180, initiatorType: 'fetch'},
  {name: 'https://cdn.test/elsewhere/gameplay_public.data', startTime: 3, duration: 900,
    transferSize: 0, encodedBodySize: 0, decodedBodySize: 0, initiatorType: 'other'},
  {name: 'https://other.test/runtime/gameplay_public.wasm', startTime: 4, duration: 900,
    transferSize: 0, encodedBodySize: 0, decodedBodySize: 0, initiatorType: 'other'},
];
const pendingLongtasks = [{startTime: 96, duration: 7}, {startTime: 103, duration: 11}];
let observer;
class FakePerformanceObserver {
  constructor(callback) { this.callback = callback; observer = this; }
  observe(options) { assert.deepEqual(options, {type: 'longtask', buffered: true}); }
  takeRecords() { return pendingLongtasks.splice(0); }
  disconnect() { this.disconnected = true; }
}
const perf = {
  now: () => clock,
  mark() {}, measure() {},
  getEntriesByType(type) {
    if (type === 'navigation') return [{startTime: 0}];
    if (type === 'resource') return resources;
    return [];
  },
};
const root = {performance: perf, PerformanceObserver: FakePerformanceObserver,
  location: {href: 'https://cdn.test/runtime/index.html'},
  setInterval() { throw Error('profile test must not start an interval'); },
  clearInterval() {}};
const updates = [];
const profile = createStartupProfile({root, autoTick: false,
  identity: {schema_version: 1, source_commit: 'a'.repeat(40), runtime_hash: 'b'.repeat(16),
    build_profile: 'audio-player', ignored: 'private'},
  onUpdate: value => updates.push(value)});
profile.setAssetBase(new URL('https://cdn.test/runtime/'));
let state = profile.snapshot();
assert.equal(state.schema, STARTUP_PROFILE_SCHEMA);
assert.deepEqual(state.resources.map(row => row.name), ['gameplay_public.js', 'gameplay_audio_preview.wasm']);
assert.equal(state.identity.build_profile, 'audio-player');
assert.equal(JSON.stringify(state).includes('secret'), false);
assert.equal(state.coverage.resource_scope, 'exact_loader_assets');

profile.observePipeline({ready: false, selected: 2, pending: 2, union_ready_ms: 0, bootstrap_total_ms: 0});
assert.equal(profile.snapshot().native_pipeline.union_ready_ms, null,
  'unfinished zero-duration fields are unavailable, not completed measurements');
assert.equal(profile.snapshot().native_pipeline.bootstrap_total_ms, null);

const graphics = {renderer_init_ms: 0, queued_pipelines: 9, created_pipelines: 2,
  cache_idle: 0, first_native_callback_ms: null, elapsed_ms: 0, terminal: false};
profile.observeGraphicsStartup(graphics);
clock = 1100;
graphics.elapsed_ms = 1000;
graphics.queued_pipelines = 7;
profile.observeGraphicsStartup(graphics);
clock = 2100;
graphics.elapsed_ms = 2000;
graphics.terminal = true;
profile.observeGraphicsStartup(graphics);
state = profile.snapshot();
assert.equal(state.graphics_samples.length, 3);
assert.equal(state.graphics_samples.at(-1).terminal, true);
assert.equal(state.timeline.filter(row => row.event === 'graphics_startup_terminal').length, 1,
  'the first pending to terminal observation is recorded');

profile.fail('startup_timeout');
state = profile.snapshot();
assert.equal(observer.disconnected, true);
assert.equal(state.terminal, true);
assert.equal(state.terminal_reason, 'startup_timeout');
assert.equal(state.longtasks.before_profile_count, 1);
assert.equal(state.longtasks.before_profile_total_ms, 7);
assert.equal(state.longtasks.count, 2, 'takeRecords includes long tasks queued before disconnect');
const frozenElapsed = state.elapsed_ms;
const frozenRows = state.resources;
clock = 90000;
resources.push({name: 'https://cdn.test/runtime/gameplay_public.data', startTime: 90001,
  duration: 500, transferSize: 500, encodedBodySize: 500, decodedBodySize: 500, initiatorType: 'fetch'});
profile.loaderLoaded();
profile.observeGraphicsStartup({...graphics, elapsed_ms: 90000, queued_pipelines: 0});
profile.destroy();
assert.equal(profile.snapshot().elapsed_ms, frozenElapsed, 'terminal reads preserve the completion timestamp');
assert.deepEqual(profile.snapshot().resources, frozenRows, 'terminal reads retain the startup-only resource snapshot');
assert.equal(profile.snapshot().timeline.some(row => row.event === 'loader_loaded'), false,
  'late callbacks cannot add post-terminal events');
assert.ok(updates.length > 1);
console.log('Startup profile schema, source scoping, bounded native samples, terminal freeze and observer cleanup pass.');
