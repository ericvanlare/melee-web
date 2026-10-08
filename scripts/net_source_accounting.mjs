/** Bounded observation of existing native callback counters; no source mutation. */
export async function installNetSourceAccounting(page, capacity = 32768,
  {retainRenderReadiness = false, awaitStartIdentity = false} = {}) {
  if (!Number.isSafeInteger(capacity) || capacity < 1 || capacity > 32768)
    throw Error('Invalid network accounting capacity');
  if (typeof retainRenderReadiness !== 'boolean') throw Error('Invalid render-readiness option');
  if (typeof awaitStartIdentity !== 'boolean') throw Error('Invalid start-identity wait option');
  return page.evaluate(({capacity, retainRenderReadiness, awaitStartIdentity}) => {
    if (window.__netSourceAccounting) throw Error('Network accounting is already installed');
    const original = window.menuRuntimeTiming;
    if (typeof original !== 'function') throw Error('Native timing observer is unavailable');
    const initialAtInstall = window.__net.status();
    const atStartIdentity = status => status?.active === 1 && status.cursor === 0 &&
      status.blocker === 'start_identity';
    if (!awaitStartIdentity && !atStartIdentity(initialAtInstall))
      throw Error('Network accounting must start at the unconsumed identity barrier');
    let subscriber = null;
    const state = {capacity, initial: atStartIdentity(initialAtInstall) ? initialAtInstall : null,
      awaitingStartIdentity: awaitStartIdentity, rows: [], overflow: 0, errors: [], frozen: false,
      ...(retainRenderReadiness ? {render_readiness_enabled: true, render_readiness: null} : {})};
    const fail = message => { if (state.errors.length < 32) state.errors.push(message); };
    const observer = function(data) {
      let originalError = null;
      try { return original.apply(this, arguments); }
      catch (error) { originalError = error; fail(`Original timing observer failed: ${String(error)}`); throw error; }
      finally {
        if (state.awaitingStartIdentity && !state.initial) {
          try {
            const status = window.__net.status();
            if (atStartIdentity(status)) state.initial = status;
            else if (status?.active === 1 && Number.isSafeInteger(status.cursor) && status.cursor > 0)
              fail('Lockstep source progress passed the start-identity barrier before accounting observed it');
          } catch (error) { fail(`Start-identity accounting observation failed: ${String(error)}`); }
        }
        const accountingActive = !state.awaitingStartIdentity || Boolean(state.initial);
        if (accountingActive) {
          if (state.rows.length >= capacity) ++state.overflow;
          else state.rows.push({frame: data?.frame, valid: data?.valid,
            source_steps: data?.source_steps, source_draws: data?.source_draws});
        }
        let renderError = null;
        if (accountingActive && retainRenderReadiness && !originalError && !state.overflow &&
            (data?.source_steps > 0 || data?.source_draws > 0)) {
          try {
            const status = window.__net.status(), native = window.__net.native();
            if (native.error !== null) throw Error('Structured render native observer has an error');
            const source = window.__net.renderSource();
            if (typeof source !== 'string' || !source.length || source.length > 4096)
              throw Error('Structured render source is missing or exceeds 4096 characters');
            state.render_readiness = {row_index: state.rows.length - 1,
              frame: data.frame, valid: data.valid, source_steps: data.source_steps,
              source_draws: data.source_draws, began: data.began, drawn: data.drawn,
              preparation_ms: data.preparation_ms, draw_suppressed: data.draw_suppressed,
              draw_calls: data.draw_calls, source_cursor: status.cursor,
              phase: native.phase, running: native.running, error: native.error,
              status: native.status ?? null, source};
          } catch (error) {
            renderError = error;
            fail(`Structured render observation failed: ${String(error)}`);
          }
        }
        if (subscriber && accountingActive) {
          const observationError = originalError || renderError || (state.overflow || data?.valid !== 1 ||
            !Number.isSafeInteger(data?.frame) || data.frame < 1 ||
            !Number.isSafeInteger(data?.source_steps) || data.source_steps < 0 ||
            data.source_steps !== data.source_draws
              ? Error('Diagnostic native progress callback has invalid source accounting') : null);
          if (observationError && !originalError && !renderError) fail(observationError.message);
          try { subscriber(observationError); }
          catch (error) {
            fail(`Native progress subscriber failed: ${String(error)}`);
            if (!originalError && !renderError) throw error;
          }
        }
        if (renderError && !originalError) throw renderError;
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
          ...(retainRenderReadiness ? {render_readiness: state.render_readiness && {...state.render_readiness}} : {}),
          errors: state.errors.slice()};
      },
    };
    return {capacity, initial: state.initial, awaiting_start_identity: state.awaitingStartIdentity};
    }, {capacity, retainRenderReadiness, awaitStartIdentity});
}

export async function readNetSourceAccounting(page, {freeze = false} = {}) {
  return page.evaluate(freeze => {
    if (!window.__netSourceAccounting) throw Error('Network accounting is not installed');
    return window.__netSourceAccounting.read(freeze);
  }, freeze);
}

export function verifyNetSourceAccounting(capture, expectedCursor) {
  return verifySourceAccounting(capture, expectedCursor, true);
}

// A read-only snapshot at a complete source boundary preserves the active
// progress subscription. Final exported accounting still requires its freeze.
export function verifyHeldNetSourceAccounting(capture, expectedCursor) {
  if (capture?.frozen !== false || capture.final?.blocker !== 'complete' ||
      capture.final?.terminal?.kind !== 0)
    throw Error('Network source accounting snapshot is not held at a complete boundary');
  return verifySourceAccounting(capture, expectedCursor, false);
}

function verifySourceAccounting(capture, expectedCursor, requireFrozen) {
  if (!Number.isSafeInteger(expectedCursor) || expectedCursor < 0)
    throw Error('Invalid expected network cursor');
  if ((requireFrozen && !capture?.frozen) || capture.initial?.active !== 1 || capture.initial?.cursor !== 0 ||
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
