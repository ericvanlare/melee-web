/*
 * Opt-in, bounded hitch diagnostics.
 *
 * This module deliberately does not format native timing data on every
 * callback.  It keeps a small ring of references and snapshots only when an
 * abnormal event is admitted to the capture.  The native callback remains the
 * owner of timing and ordering; this module only observes its already
 * published values.
 */

export const NATIVE_DEADLINE_MS = 1000 / 60;
export const BROWSER_HARD_GAP_MS = 1000 / 30;
export const DEFAULT_EVENT_CAP = 128;
export const DEFAULT_CONTEXT_CAP = 8;

const finite = value => value === null || value === undefined || !Number.isFinite(Number(value))
  ? null : Number(value);

function clone(value, seen = new WeakMap()) {
  if (value === null || typeof value !== 'object') return value;
  if (seen.has(value)) return seen.get(value);
  if (Array.isArray(value)) {
    const result = [];
    seen.set(value, result);
    for (const item of value) result.push(clone(item, seen));
    return result;
  }
  const result = {};
  seen.set(value, result);
  for (const [key, item] of Object.entries(value)) result[key] = clone(item, seen);
  return result;
}

function freeze(value, seen = new WeakSet()) {
  if (value === null || typeof value !== 'object' || seen.has(value)) return value;
  seen.add(value);
  for (const item of Object.values(value)) freeze(item, seen);
  return Object.freeze(value);
}

function immutable(value) {
  return freeze(clone(value));
}

function capability(Ctor, supported, reason) {
  return {supported: !!supported, observed: false, reason: reason || null};
}

function entryValue(entry, key, fallback = null) {
  const value = entry?.[key];
  return value === undefined ? fallback : value;
}

/**
 * Make an opt-in diagnostic collector.
 *
 * `performance`, `PerformanceObserver`, and `globalThis` are injectable so
 * the bounded behavior can be tested without a browser.  A runtime may call
 * `setEnabled(true)` from an explicit UI action or construct the collector
 * with `enabled: true` from an explicit URL query parameter.
 */
export function createHitchCapture(options = {}) {
  const root = options.globalThis || globalThis;
  const perf = options.performance || root.performance || null;
  const Observer = options.PerformanceObserver || root.PerformanceObserver;
  const configuredCap = Number(options.cap ?? DEFAULT_EVENT_CAP);
  const cap = Number.isInteger(configuredCap) && configuredCap > 0
    ? configuredCap : DEFAULT_EVENT_CAP;
  const configuredContextCap = Number(options.contextCap ?? DEFAULT_CONTEXT_CAP);
  const contextCap = Number.isInteger(configuredContextCap) && configuredContextCap > 0
    ? configuredContextCap : DEFAULT_CONTEXT_CAP;
  const useUserTiming = options.userTiming === true;
  const requestedObservers = options.observeObservers !== false;
  const timeOrigin = finite(options.timeOrigin ?? perf?.timeOrigin);
  const now = () => finite(options.now?.() ?? perf?.now?.()) ?? 0;

  const supportedTypes = Array.isArray(Observer?.supportedEntryTypes)
    ? new Set(Observer.supportedEntryTypes) : new Set();
  const capabilities = {
    longtask: capability(Observer, supportedTypes.has('longtask'),
      Observer ? (supportedTypes.has('longtask') ? null : 'entry type unsupported') : 'PerformanceObserver unavailable'),
    loaf: capability(Observer, supportedTypes.has('long-animation-frame'),
      Observer ? (supportedTypes.has('long-animation-frame') ? null : 'entry type unsupported') : 'PerformanceObserver unavailable'),
    user_timing: {requested: useUserTiming, supported: !!perf?.mark, observed: false, reason: perf?.mark ? null : 'performance.mark unavailable'},
  };

  let enabled = options.enabled === true;
  let stopped = false;
  let invalid = false;
  let overflowed = false;
  let overflowCount = 0;
  let nextId = 1;
  let events = [];
  let observers = [];
  let marks = [];
  let recentNative = [];
  let observerHandles = [];
  let observing = false;
  let captureStart = enabled ? now() : null;
  let captureEnd = null;
  let preparationSince = null;
  let preparationWindows = [];

  const mark = (eventId, timestamp) => {
    if (!enabled || !useUserTiming || !perf?.mark || events.length + observers.length >= cap) return null;
    const name = `melee-hitch-${eventId}`;
    try {
      // `startTime` uses the same performance clock as native `started` and
      // browser callback timestamps.  Browsers that reject it still get a
      // useful mark at the observation point.
      perf.mark(name, {startTime: finite(timestamp) ?? now()});
      marks.push(name);
      capabilities.user_timing.observed = true;
      return {name, clock: 'performance.now', start_time: finite(timestamp) ?? null};
    } catch {
      try {
        perf.mark(name);
        marks.push(name);
        capabilities.user_timing.observed = true;
        return {name, clock: 'performance.now', start_time: null, requested_start_time: finite(timestamp) ?? null};
      } catch {
        return null;
      }
    }
  };

  const failOverflow = () => {
    overflowed = true;
    invalid = true;
    overflowCount++;
  };

  const append = (target, value) => {
    if (events.length + observers.length >= cap) {
      failOverflow();
      return false;
    }
    target.push(immutable(value));
    return true;
  };

  const context = (extra, preEvent = recentNative) => immutable({
    ...extra,
    pre_event_native: preEvent.map(item => ({
      id: item.id,
      started: item.started,
      timestamp: item.timestamp,
      active_ms: item.active_ms,
      preparation_ms: item.preparation_ms,
      current: immutable(item.current),
    })),
  });

  const nativePair = (current, previous) => ({
    current: current === undefined || current === null ? null : current,
    previous: previous === undefined || previous === null ? null : previous,
  });

  function observeNativeTiming({
    current,
    previous = null,
    activeMs,
    totalMs,
    preparationMs = 0,
    timestamp,
    context: extraContext = {},
  } = {}) {
    const total = finite(totalMs ?? current?.total_ms) ?? 0;
    const prep = Math.max(0, finite(preparationMs ?? current?.preparation_ms) ?? 0);
    const active = Math.max(0, finite(activeMs) ?? Math.max(0, total - prep));
    if (!enabled || stopped) return {recorded: false, overflowed, invalid, active_ms: active};
    const observedAt = finite(timestamp) ?? now();
    const currentStarted = finite(current?.started);
    const previousStarted = finite(previous?.started);
    const callbackId = `native-${nextId++}`;
    const recentItem = {
      id: callbackId,
      started: currentStarted,
      timestamp: observedAt,
      active_ms: active,
      preparation_ms: prep,
      current,
    };

    // A fully preparation-only callback is excluded.  A callback with some
    // live work is judged by its already preparation-subtracted active time.
    const abnormal = active > NATIVE_DEADLINE_MS && active > 0;
    // Snapshot the ring only for an abnormal event.  Keeping references and
    // scalar metadata on the normal path avoids per-tick state formatting.
    const preEvent = abnormal ? recentNative.slice() : null;
    if (enabled) {
      recentNative.push(recentItem);
      if (recentNative.length > contextCap) recentNative.shift();
    }
    if (!abnormal) {
      return {recorded: false, overflowed, invalid, active_ms: active};
    }

    const id = callbackId;
    const event = {
      id,
      kind: 'native_deadline',
      timestamp: observedAt,
      timeOrigin,
      duration_ms: active,
      threshold_ms: NATIVE_DEADLINE_MS,
      preparation: false,
      native: nativePair(current, previous),
      native_started: currentStarted,
      previous_native_started: previousStarted,
      context: context({
        ...extraContext,
        preparation_ms: prep,
        native_clock: 'performance.now',
      }, preEvent),
    };
    event.user_timing = mark(id, currentStarted ?? observedAt);
    const recorded = append(events, event);
    return {recorded, overflowed, invalid, active_ms: active, id: recorded ? id : null};
  }

  function observeBrowserGap({
    started,
    ended,
    intervalMs,
    currentNative = null,
    previousNative = null,
    timestamp,
    preparation = false,
    context: extraContext = {},
  } = {}) {
    const start = finite(started);
    const end = finite(ended);
    const interval = finite(intervalMs) ?? (start !== null && end !== null ? end - start : 0);
    const observedAt = finite(timestamp) ?? (end ?? now());
    // Browser gaps use the existing active-callback gate; preparation is
    // reported as context only when the caller supplies it.  Native deadline
    // accounting performs the explicit preparation subtraction.
    if (!enabled || stopped || !(interval > BROWSER_HARD_GAP_MS)) {
      return {recorded: false, overflowed, invalid, interval_ms: interval};
    }
    const id = `browser-${nextId++}`;
    const event = {
      id,
      kind: 'browser_gap',
      timestamp: observedAt,
      timeOrigin,
      duration_ms: interval,
      threshold_ms: BROWSER_HARD_GAP_MS,
      preparation: !!preparation,
      browser: {started: start, ended: end, clock: 'performance.now'},
      native: nativePair(currentNative, previousNative),
      native_started: finite(currentNative?.started),
      previous_native_started: finite(previousNative?.started),
      context: context({
        ...extraContext,
        preparation: !!preparation,
      }),
    };
    event.user_timing = mark(id, start ?? observedAt);
    const recorded = append(events, event);
    return {recorded, overflowed, invalid, interval_ms: interval, id: recorded ? id : null};
  }

  function captureObserverEntry(type, entry) {
    if (!enabled || stopped) return {recorded: false, overflowed, invalid};
    const started = finite(entry?.startTime);
    const duration = Math.max(0, finite(entry?.duration) ?? 0);
    const ended = started === null ? null : started + duration;
    if (captureStart !== null && ended !== null && ended <= captureStart) {
      return {recorded: false, ignored: 'before_capture', overflowed, invalid};
    }
    if (captureEnd !== null && started !== null && started >= captureEnd) {
      return {recorded: false, ignored: 'after_capture', overflowed, invalid};
    }
    const preparationOverlap = preparationWindows.find(window =>
      started !== null && ended !== null && started < window.end && ended > window.start) ||
      (preparationSince !== null && started !== null && ended !== null && ended > preparationSince
        ? {start: preparationSince, end: null} : null);
    let attribution = null;
    if (Array.isArray(entry?.scripts)) {
      attribution = entry.scripts.map(script => {
        try {
          if (typeof script?.toJSON === 'function') return script.toJSON();
        } catch { /* optional browser attribution */ }
        const fields = ['name', 'sourceURL', 'sourceFunctionName', 'sourceCharPosition',
          'duration', 'startTime', 'invoker', 'executionType'];
        const value = {};
        for (const field of fields) {
          try { if (script?.[field] !== undefined) value[field] = script[field]; } catch { /* optional field */ }
        }
        return value;
      });
    }
    const event = {
      id: `observer-${nextId++}`,
      kind: type === 'long-animation-frame' ? 'long_animation_frame' : 'long_task',
      timestamp: started,
      timeOrigin,
      duration_ms: finite(entry?.duration),
      threshold_ms: null,
      preparation: false,
      observer: {
        type,
        name: entryValue(entry, 'name'),
        start_time: started,
        duration_ms: finite(entry?.duration),
        // Attribution is intentionally retained only when the platform gave
        // us a serializable field.  No source/native attribution is inferred.
        attribution,
      },
      context: context({
        observer_attribution_supported: attribution !== null,
        preparation_overlap: preparationOverlap,
      }),
    };
    const recorded = append(observers, event);
    return {recorded, overflowed, invalid, id: recorded ? event.id : null};
  }

  function installObservers() {
    if (!enabled || stopped || !requestedObservers || !Observer || observing) return;
    for (const type of ['longtask', 'long-animation-frame']) {
      if (!supportedTypes.has(type)) continue;
      try {
        const observer = new Observer(list => {
          for (const entry of list.getEntries()) captureObserverEntry(type, entry);
        });
        observer.observe({type, buffered: true});
        observerHandles.push(observer);
        capabilities[type === 'longtask' ? 'longtask' : 'loaf'].observed = true;
      } catch (error) {
        capabilities[type === 'longtask' ? 'longtask' : 'loaf'].reason = String(error?.message || error);
      }
    }
    observing = observerHandles.length > 0;
  }

  function flushObservers() {
    for (const observer of observerHandles) {
      try {
        if (typeof observer.takeRecords === 'function') {
          for (const entry of observer.takeRecords()) {
            const type = entry?.entryType === 'long-animation-frame' ? 'long-animation-frame' : 'longtask';
            captureObserverEntry(type, entry);
          }
        }
      } catch {
        // A failing optional observer must not compromise native teardown.
      }
    }
  }

  function clearMarks() {
    if (!perf?.clearMarks) return;
    for (const name of marks.splice(0)) {
      try { perf.clearMarks(name); } catch { /* optional User Timing */ }
    }
  }

  function reset() {
    captureEnd = now();
    flushObservers();
    clearMarks();
    events = [];
    observers = [];
    recentNative = [];
    invalid = false;
    overflowed = false;
    overflowCount = 0;
    nextId = 1;
    stopped = false;
    captureStart = enabled ? now() : null;
    captureEnd = null;
    preparationSince = null;
    preparationWindows = [];
    if (enabled) installObservers();
  }

  function setEnabled(value) {
    enabled = value === true;
    stopped = false;
    captureStart = enabled ? now() : null;
    captureEnd = null;
    if (enabled) installObservers();
    else {
      for (const observer of observerHandles.splice(0)) {
        try { observer.disconnect(); } catch { /* optional observer */ }
      }
      observing = false;
    }
    return enabled;
  }

  function stop() {
    captureEnd = now();
    flushObservers();
    for (const observer of observerHandles.splice(0)) {
      try { observer.disconnect(); } catch { /* optional observer */ }
    }
    observing = false;
    stopped = true;
    return report();
  }

  function setPreparation(active, timestamp) {
    if (!enabled || stopped) return;
    const at = finite(timestamp) ?? now();
    if (active) {
      if (preparationSince === null) preparationSince = at;
    } else if (preparationSince !== null) {
      if (preparationWindows.length >= cap) failOverflow();
      else preparationWindows.push({start: preparationSince, end: at});
      preparationSince = null;
    }
  }

  function report() {
    flushObservers();
    return immutable({
      schema: 'melee-web-diagnostic-hitch-capture',
      version: 1,
      enabled,
      stopped,
      valid: !invalid,
      invalid,
      overflowed,
      overflow_count: overflowCount,
      cap,
      context_cap: contextCap,
      event_count: events.length,
      observer_count: observers.length,
      events,
      observers,
      capabilities,
      clock: {source: 'performance.now', time_origin: timeOrigin},
      capture_interval: {started: captureStart, ended: captureEnd},
    });
  }

  if (enabled) installObservers();

  return Object.freeze({
    setEnabled,
    isEnabled: () => enabled && !stopped,
    setPreparation,
    reset,
    stop,
    report,
    isInvalid: () => invalid,
    isOverflowed: () => overflowed,
    observeNativeTiming,
    observeBrowserGap,
    captureObserverEntry,
    constants: Object.freeze({native_deadline_ms: NATIVE_DEADLINE_MS, browser_hard_gap_ms: BROWSER_HARD_GAP_MS}),
  });
}
