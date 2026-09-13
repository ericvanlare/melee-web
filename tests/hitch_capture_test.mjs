import assert from 'node:assert/strict';
import fs from 'node:fs';
import {
  BROWSER_HARD_GAP_MS,
  NATIVE_DEADLINE_MS,
  createHitchCapture,
} from '../web/hitch-capture.mjs';

const runtime = fs.readFileSync(new URL('../web/runtime.html', import.meta.url), 'utf8');
assert.match(runtime, /await window\.meleeHitchCaptureLoading/);
assert.match(runtime, /diagnostic_capture:diagnosticCapture/);
assert.match(runtime, /nativeCallbacksOverBudget/);
assert.match(runtime, /hitchCausalFromUrl/);
assert.match(runtime, /syncDiagnostics:hitchCausalFromUrl/);

let clock = 100;
const marks = [];
const perf = {
  timeOrigin: 1700000000000,
  now: () => clock,
  mark(name, options) { marks.push({name, options}); },
  clearMarks() {},
};

const native = (frame, total, preparation = 0) => ({
  frame,
  started: clock - 1,
  total_ms: total,
  preparation_ms: preparation,
  end_phases: {last_frame: frame, staging_writes_ms: frame + 0.25},
});

// Boundary values are strict: exactly one budget is not a deadline miss.
{
  const capture = createHitchCapture({enabled: true, performance: perf, now: () => clock, cap: 8, contextCap: 2, userTiming: true});
  const first = native(1, NATIVE_DEADLINE_MS);
  assert.equal(capture.observeNativeTiming({current: first, totalMs: first.total_ms, preparationMs: 0, timestamp: clock}).recorded, false);
  const previous = native(2, 7);
  const current = native(3, NATIVE_DEADLINE_MS + 1);
  assert.equal(capture.observeNativeTiming({current: previous, totalMs: 7, timestamp: ++clock}).recorded, false);
  assert.equal(capture.observeNativeTiming({current, previous, totalMs: current.total_ms, timestamp: ++clock}).recorded, true);
  const browser = capture.observeBrowserGap({started: 200, ended: 240, intervalMs: BROWSER_HARD_GAP_MS, timestamp: 200});
  assert.equal(browser.recorded, false);
  const gap = capture.observeBrowserGap({started: 200, ended: 240, timestamp: 240, currentNative: current, previousNative: previous});
  assert.equal(gap.recorded, true);
  const report = capture.report();
  assert.equal(report.valid, true);
  assert.equal(report.event_count, 2);
  assert.equal(report.events[0].kind, 'native_deadline');
  assert.equal(report.events[0].native.current.end_phases.staging_writes_ms, 3.25);
  assert.equal(report.events[0].native.previous.end_phases.staging_writes_ms, 2.25);
  assert.equal(report.events[0].native_started, current.started);
  assert.equal(report.events[0].timeOrigin, perf.timeOrigin);
  assert.equal(report.events[0].context.pre_event_native.length, 2);
  assert.equal(report.events[1].native.current.end_phases.staging_writes_ms, 3.25);
  assert.equal(report.events[1].browser.started, 200);
  assert.equal(report.events[1].browser.ended, 240);
  assert.equal(marks.length, 2);
  assert.equal(marks[1].options.startTime, 200);

  // Event data is an immutable snapshot, independent from native object reuse.
  current.end_phases.staging_writes_ms = 999;
  assert.equal(report.events[0].native.current.end_phases.staging_writes_ms, 3.25);
  assert.throws(() => { report.events[0].native.current.frame = 99; }, TypeError);

  capture.reset();
  assert.equal(capture.report().event_count, 0);
  assert.equal(capture.report().overflowed, false);
}

// Preparation-only work is excluded from live deadline diagnostics.
{
  const capture = createHitchCapture({enabled: true, performance: perf, now: () => clock});
  const prepared = native(10, 80, 80);
  assert.equal(capture.observeNativeTiming({current: prepared, totalMs: 80, preparationMs: 80, timestamp: clock}).recorded, false);
  assert.equal(capture.report().event_count, 0);
}

// The cap never overwrites an earlier failure; overflow explicitly invalidates the report.
{
  const initialMarks = marks.length;
  const capture = createHitchCapture({enabled: true, performance: perf, now: () => clock, cap: 1, userTiming: true});
  assert.equal(capture.observeBrowserGap({started: 0, ended: 40}).recorded, true);
  const overflow = capture.observeBrowserGap({started: 50, ended: 100});
  assert.equal(overflow.recorded, false);
  const report = capture.report();
  assert.equal(report.valid, false);
  assert.equal(report.invalid, true);
  assert.equal(report.overflowed, true);
  assert.equal(report.overflow_count, 1);
  assert.equal(report.event_count, 1);
  assert.equal(report.events[0].browser.started, 0);
  for (let i = 0; i < 100; i++) capture.observeBrowserGap({started: i, ended: i + 40});
  assert.equal(marks.length - initialMarks, 1, 'Overflow must not leak User Timing marks');
}

// Preparation context has a separate finite bound, including ordinary UI use.
{
  const capture = createHitchCapture({enabled: false, performance: perf, cap: 1});
  for (let i = 0; i < 10; i++) {capture.setPreparation(true, i); capture.setPreparation(false, i + 1);}
  assert.equal(capture.isInvalid(), false, 'Disabled capture must not retain preparation history');
  capture.setEnabled(true);
  capture.setPreparation(true, 1);capture.setPreparation(false, 2);
  capture.setPreparation(true, 3);capture.setPreparation(false, 4);
  assert.equal(capture.isOverflowed(), true);
}

// Platform observers are capability-reported and flushed without attribution claims.
{
  const instances = [];
  class FakeObserver {
    static supportedEntryTypes = ['longtask', 'long-animation-frame'];
    constructor(callback) { this.callback = callback; this.records = []; instances.push(this); }
    observe(options) { this.type = options.type; }
    takeRecords() { return this.records.splice(0); }
    disconnect() { this.disconnected = true; }
  }
  clock = 100;
  const capture = createHitchCapture({enabled: true, performance: perf, PerformanceObserver: FakeObserver});
  assert.equal(instances.length, 2);
  instances.find(observer => observer.type === 'longtask').records.push({entryType: 'longtask', name: 'before', startTime: 90, duration: 5});
  instances.find(observer => observer.type === 'longtask').records.push({entryType: 'longtask', name: 'task', startTime: 103, duration: 41});
  instances.find(observer => observer.type === 'long-animation-frame').records.push({entryType: 'long-animation-frame', name: 'frame', startTime: 104, duration: 52});
  capture.setPreparation(true, 105);
  capture.setPreparation(false, 110);
  clock = 200;
  const report = capture.stop();
  assert.equal(report.capabilities.longtask.supported, true);
  assert.equal(report.capabilities.longtask.observed, true);
  assert.equal(report.capabilities.loaf.supported, true);
  assert.equal(report.observer_count, 2);
  assert.equal(report.capture_interval.started, 100);
  assert.equal(report.capture_interval.ended, 200);
  assert.equal(report.observers[0].observer.attribution, null);
  assert.equal(report.observers[1].observer.attribution, null);
  assert.equal(report.observers[0].context.preparation_overlap.start, 105);
  assert.equal(capture.captureObserverEntry('longtask', {startTime: 250, duration: 10}).recorded, false);
  capture.reset();
  assert.equal(capture.isEnabled(), true);
  assert.equal(instances.length, 4, 'Reset reinstalls optional observers for the next run');
}

// Render-cache syncs have a separate bounded lifecycle stream. Their clock is
// shared with native timing, while source callback identity stays unknown until
// an offline correlation step.
{
  clock = 100;
  const capture = createHitchCapture({
    enabled: true,
    performance: perf,
    now: () => clock,
    userTiming: true,
    cacheSyncDiagnostics: true,
    cacheSyncCap: 2,
  });
  const nativeTiming = native(20, 20);
  nativeTiming.started = 100;
  capture.observeNativeTiming({current: nativeTiming, totalMs: 20, timestamp: 100});
  capture.observeCacheSync({id: 'cache-sync-test', phase: 'start', source: 'explicit', operation: 'save', started: 105, stack: 'at fd_sync (libwasi.js:561)'});
  clock = 115;
  capture.observeCacheSync({id: 'cache-sync-test', phase: 'completion', source: 'explicit', operation: 'save', ended: 115});
  const report = capture.report();
  assert.equal(report.cache_sync_count, 1);
  assert.equal(report.cache_syncs[0].status, 'completed');
  assert.equal(report.cache_syncs[0].source, 'explicit');
  assert.equal(report.cache_syncs[0].operation, 'save');
  assert.equal(report.cache_syncs[0].duration_ms, 10);
  assert.equal(report.cache_syncs[0].native_context, 'unknown');
  assert.equal('native_overlap_ids' in report.cache_syncs[0], false, 'Interval joins belong offline');
  assert.match(report.cache_syncs[0].stack, /fd_sync/);
  assert.equal(report.capabilities.cache_sync.requested, true);
  assert.equal(report.capabilities.cache_sync.observed, true);
  assert.equal(marks.filter(mark => mark.name.startsWith('melee-cache-sync-')).length, 2);
  capture.reset();
  assert.equal(capture.report().cache_sync_count, 0);
  assert.equal(capture.report().cache_sync_overflow_count, 0);
}

// The sync stream's own cap invalidates the report without changing native or
// browser event counts.
{
  const capture = createHitchCapture({enabled: true, performance: perf, cacheSyncDiagnostics: true, cacheSyncCap: 1});
  assert.equal(capture.observeCacheSync({id: 'one', started: 1}).recorded, true);
  assert.equal(capture.observeCacheSync({id: 'two', started: 2}).recorded, false);
  const report = capture.report();
  assert.equal(report.event_count, 0);
  assert.equal(report.cache_sync_count, 1);
  assert.equal(report.cache_sync_overflow_count, 1);
  assert.equal(report.valid, false);
}

// A startup queue loss or duplicate id cannot silently become a clean capture.
{
  const capture = createHitchCapture({enabled: true, performance: perf, cacheSyncDiagnostics: true});
  capture.observeCacheSync({id: 'same', started: 1});
  capture.observeCacheSync({id: 'same', started: 2});
  assert.equal(capture.report().cache_syncs[0].started, 1);
  assert.equal(capture.report().cache_sync_count, 1);
  capture.noteCacheSyncLoss(3);
  assert.equal(capture.report().cache_sync_overflow_count, 4);
  assert.equal(capture.report().valid, false);
}

console.log('Bounded hitch capture boundaries, snapshots, reset, preparation exclusion, observers and overflow passed.');
