import {createHash} from 'node:crypto';

const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');

function renderEvents(log) {
  const rows = [];
  for (const line of String(log || '').split(/\r?\n/)) {
    const match = line.match(/^(Native callback|Live render resource) (\{.*\})$/);
    if (!match) continue;
    try {
      const data = JSON.parse(match[2]);
      rows.push({kind: match[1], data, signature: `${match[1]} ${JSON.stringify(data)}`});
    } catch {}
  }
  return rows;
}

export function renderEventSignatures(log) {
  return new Set(renderEvents(log).map(row => row.signature));
}

export function readyRenderEvent(diagnostics, expectedPhase, priorSignatures = new Set()) {
  if (!diagnostics || diagnostics.unavailable || diagnostics.phase !== expectedPhase ||
      diagnostics.running !== 1 || /preparing original/i.test(diagnostics.status || '')) return null;
  const rows = renderEvents(diagnostics.log);
  for (let index = rows.length - 1; index >= 0; --index) {
    const row = rows[index], data = row.data;
    if (priorSignatures.has(row.signature) || data.began !== 1 || data.drawn !== 1 ||
        data.draw_suppressed !== 0 || data.preparation_ms !== 0 ||
        !(data.draw_calls > 0) || !(data.source_draws > 0) ||
        /preparing|ready:\s*0/i.test(data.source || '')) continue;
    return {phase: expectedPhase, kind: row.kind, frame: data.frame ?? null,
      draw_calls: data.draw_calls, source_draws: data.source_draws,
      draw_suppressed: data.draw_suppressed, source: data.source ?? null,
      signature: row.signature};
  }
  return null;
}

function validateTerminalStatus(status, expectedKind, role) {
  if (!status || status.active !== 1 || status.blocker !== 'terminal' ||
      status.terminal?.kind !== expectedKind)
    throw Error(`${role} native terminal hold is incomplete: ${JSON.stringify(status)}`);
}

export function verifyTerminalHold(before, after, expectedKind, {expectedTick, expectedChannel} = {}) {
  const result = {};
  for (const role of ['alpha', 'beta']) {
    const first = before?.[role], last = after?.[role];
    validateTerminalStatus(first, expectedKind, role);
    validateTerminalStatus(last, expectedKind, role);
    if (first.cursor !== last.cursor || first.pushed !== last.pushed)
      throw Error(`${role} native source advanced after terminal: ${first.cursor}/${first.pushed} -> ${last.cursor}/${last.pushed}`);
    if (expectedTick !== undefined && first.terminal.tick !== expectedTick)
      throw Error(`${role} native terminal tick ${first.terminal.tick} did not equal ${expectedTick}`);
    if (expectedChannel !== undefined && first.terminal.channel !== expectedChannel)
      throw Error(`${role} native terminal channel ${first.terminal.channel} did not equal ${expectedChannel}`);
    if (first.terminal.tick !== last.terminal.tick || first.terminal.channel !== last.terminal.channel)
      throw Error(`${role} native terminal identity changed during hold`);
    result[role] = {kind: first.terminal.kind, tick: first.terminal.tick,
      channel: first.terminal.channel, active: last.active, blocker: last.blocker,
      cursor_before: first.cursor, cursor_after: last.cursor,
      pushed_before: first.pushed, pushed_after: last.pushed};
  }
  return {minimum_hold_ms: 120, peers: result, no_fallback: true};
}

function mismatchChannel(left, right) {
  const fields = [[0, 16, 0], [24, 8, 1], [32, 8, 2], [16, 4, 3],
    [40, 8, 3], [56, 8, 4], [20, 4, 5], [48, 8, 5]];
  for (const [offset, size, channel] of fields)
    if (!left.subarray(offset, offset + size).equals(right.subarray(offset, offset + size))) return channel;
  return null;
}

export function verifyFirstChecksumMismatch(alpha, beta, expectedTick, expectedChannel) {
  const recordBytes = 64;
  if (!Buffer.isBuffer(alpha) || !Buffer.isBuffer(beta) ||
      alpha.length % recordBytes || beta.length % recordBytes)
    throw Error('Native checksum evidence must contain whole 64-byte records');
  for (let tick = 0; tick <= expectedTick; ++tick) {
    const start = tick * recordBytes;
    if (alpha.length < start + recordBytes || beta.length < start + recordBytes)
      throw Error(`Native checksum evidence is missing expected mismatch tick ${expectedTick}`);
    const left = alpha.subarray(start, start + recordBytes);
    const right = beta.subarray(start, start + recordBytes);
    if (left.readUInt32LE(0) !== tick || right.readUInt32LE(0) !== tick)
      throw Error(`Native checksum evidence skipped source tick ${tick}`);
    const channel = mismatchChannel(left, right);
    if (tick < expectedTick && channel !== null)
      throw Error(`Native checksum evidence diverged before expected tick ${expectedTick}, at tick ${tick}, channel ${channel}`);
    if (tick === expectedTick && channel !== expectedChannel)
      throw Error(`Native checksum evidence first diverged at tick ${tick}, channel ${channel}; expected channel ${expectedChannel}`);
  }
  return {equal_prefix_through_tick: expectedTick - 1, first_mismatch_tick: expectedTick,
    first_mismatch_channel: expectedChannel};
}

export class WasmResponseIdentityObserver {
  #initial = [];
  #later = [];
  #frozen = false;

  observe(response) {
    let url;
    try { url = new URL(response.url()); } catch { return false; }
    if (!url.pathname.endsWith('/gameplay_menu_browser.wasm')) return false;
    const row = {url: response.url(), status: response.status(), resource_type: null,
      sha256: null, byte_length: null, read_error: null};
    try { row.resource_type = response.request().resourceType(); } catch {}
    const target = this.#frozen ? this.#later : this.#initial;
    target.push(row);
    let bodyPromise;
    try { bodyPromise = response.body(); }
    catch (error) { bodyPromise = Promise.reject(error); }
    row.body = Promise.resolve(bodyPromise).then(bytes => {
      if (!(Buffer.isBuffer(bytes) || bytes instanceof Uint8Array)) throw Error('response body was not bytes');
      const body = Buffer.from(bytes);
      row.sha256 = sha256(body);
      row.byte_length = body.length;
    }).catch(error => { row.read_error = String(error?.message || error); });
    return true;
  }

  async freeze() {
    this.#frozen = true;
    const initial = this.#initial.slice();
    await Promise.all(initial.map(row => row.body));
    if (!initial.length) throw Error('No runtime Wasm load response was observed before the handshake fetch');
    const failed = initial.find(row => row.status !== 200 || row.read_error || !row.sha256);
    if (failed) throw Error(`Runtime Wasm load response was not usable: ${JSON.stringify({
      status: failed.status, url: failed.url, resource_type: failed.resource_type,
      read_error: failed.read_error})}`);
    const identities = new Set(initial.map(row => `${row.sha256}:${row.byte_length}`));
    if (identities.size !== 1) throw Error('Runtime Wasm load responses had conflicting body identities');
    const identity = initial[0];
    return {sha256: identity.sha256, byte_length: identity.byte_length,
      response_count: initial.length, response_hashes: initial.map(({status, url, resource_type, sha256: digest, byte_length}) =>
        ({status, url, resource_type, sha256: digest, byte_length})),
      response_set_frozen_before_handshake_fetch: true};
  }
}
