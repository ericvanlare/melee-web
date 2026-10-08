/** Bounded observation of existing native callback counters; no source mutation. */
export async function installNetSourceAccounting(page, capacity = 32768) {
  if (!Number.isSafeInteger(capacity) || capacity < 1 || capacity > 32768)
    throw Error('Invalid network accounting capacity');
  return page.evaluate(capacity => {
    if (window.__netSourceAccounting) throw Error('Network accounting is already installed');
    const original = window.menuRuntimeTiming;
    if (typeof original !== 'function') throw Error('Native timing observer is unavailable');
    const initial = window.__net.status();
    if (initial.active !== 1 || initial.cursor !== 0 || initial.blocker !== 'start_identity')
      throw Error('Network accounting must start at the unconsumed identity barrier');
    let subscriber = null;
    const state = {capacity, initial, rows: [], overflow: 0, errors: [], frozen: false};
    const fail = message => { if (state.errors.length < 32) state.errors.push(message); };
    const observer = function(data) {
      let originalError = null;
      try { return original.apply(this, arguments); }
      catch (error) { originalError = error; fail(`Original timing observer failed: ${String(error)}`); throw error; }
      finally {
        if (state.rows.length >= capacity) ++state.overflow;
        else state.rows.push({frame: data?.frame, valid: data?.valid,
          source_steps: data?.source_steps, source_draws: data?.source_draws});
        if (subscriber) {
          const observationError = originalError || (state.overflow || data?.valid !== 1 ||
            !Number.isSafeInteger(data?.frame) || data.frame < 1 ||
            !Number.isSafeInteger(data?.source_steps) || data.source_steps < 0 ||
            data.source_steps !== data.source_draws
              ? Error('Diagnostic native progress callback has invalid source accounting') : null);
          if (observationError && !originalError) fail(observationError.message);
          try { subscriber(observationError); }
          catch (error) {
            fail(`Native progress subscriber failed: ${String(error)}`);
            if (!originalError) throw error;
          }
        }
      }
    };
    window.menuRuntimeTiming = observer;
    window.__netSourceAccounting = {
      subscribeProgress(callback) {
        if (state.frozen || window.menuRuntimeTiming !== observer || state.errors.length ||
            subscriber || typeof callback !== 'function') {
          const error = Error('Diagnostic native progress observer is frozen, changed, failed or already owned');
          fail(error.message);
          throw error;
        }
        subscriber = callback;
        return () => {
          if (subscriber !== callback) throw Error('Diagnostic native progress subscription ownership changed');
          subscriber = null;
          if (window.menuRuntimeTiming !== observer || state.frozen) {
            const error = Error('Diagnostic native progress observer changed before unsubscribe');
            fail(error.message);
            throw error;
          }
        };
      },
      read(freeze) {
        if (freeze && subscriber) {
          const error = Error('Cannot freeze source accounting with an active native progress subscriber');
          fail(error.message);
          throw error;
        }
        if (window.menuRuntimeTiming !== observer && !state.frozen)
          fail('Timing observer changed during network accounting');
        if (freeze && !state.frozen) {
          if (window.menuRuntimeTiming === observer) window.menuRuntimeTiming = original;
          state.frozen = true;
        }
        return {...state, final: window.__net.status(), rows: state.rows.slice(),
          errors: state.errors.slice()};
      },
    };
    return {capacity, initial};
  }, capacity);
}

export async function readNetSourceAccounting(page, {freeze = false} = {}) {
  return page.evaluate(freeze => {
    if (!window.__netSourceAccounting) throw Error('Network accounting is not installed');
    return window.__netSourceAccounting.read(freeze);
  }, freeze);
}

export function verifyNetSourceAccounting(capture, expectedCursor) {
  if (!Number.isSafeInteger(expectedCursor) || expectedCursor < 0)
    throw Error('Invalid expected network cursor');
  if (!capture?.frozen || capture.initial?.active !== 1 || capture.initial?.cursor !== 0 ||
      capture.initial?.blocker !== 'start_identity' || capture.final?.active !== 1 ||
      capture.final?.cursor !== expectedCursor || capture.overflow !== 0 ||
      !Array.isArray(capture.errors) || capture.errors.length ||
      !Array.isArray(capture.rows) || !capture.rows.length)
    throw Error('Network source accounting is incomplete');
  let steps = 0, draws = 0, prior = null;
  for (const row of capture.rows) {
    if (row.valid !== 1 || !Number.isSafeInteger(row.frame) || row.frame < 1 ||
        !Number.isSafeInteger(row.source_steps) || row.source_steps < 0 ||
        !Number.isSafeInteger(row.source_draws) || row.source_draws < 0)
      throw Error('Invalid native callback in network source accounting');
    if (prior !== null && row.frame !== prior + 1)
      throw Error('Missing, repeated or reordered native accounting callback');
    if (row.source_steps !== row.source_draws)
      throw Error(`Source step/draw mismatch at native callback ${row.frame}`);
    prior = row.frame;
    steps += row.source_steps;
    draws += row.source_draws;
  }
  if (steps !== expectedCursor || draws !== expectedCursor)
    throw Error(`Network cursor ${expectedCursor} disagrees with ${steps} steps and ${draws} draws`);
  return {source_cursor: expectedCursor, source_steps: steps, source_draws: draws,
    callback_count: capture.rows.length, first_callback: capture.rows[0].frame,
    last_callback: prior, callback_ids_contiguous: true, each_step_drawn: true,
    scope: 'Source draw completion accounting; no pixel, GPU completion or timing equivalence claim'};
}
