/* Pair actual per-peer native observations by their shared source cursor. */
const PEERS = ['alpha', 'beta'];

export function createSourceCursorJoin({retainedCursorsPerPeer = 8} = {}) {
  if (!Number.isSafeInteger(retainedCursorsPerPeer) || retainedCursorsPerPeer < 1 ||
      retainedCursorsPerPeer > 64) {
    throw Error('retainedCursorsPerPeer must be an integer from 1 through 64');
  }
  const peers = Object.fromEntries(PEERS.map(role => [role, {
    rows: new Map(), latestCursor: null, sequence: 0,
  }]));
  let lastMatchedCursor = -1;
  let lastPairVersion = null;

  function observe(role, row, observedAt) {
    const state = peers[role];
    if (!state) throw Error(`Unknown source-cursor peer ${role}`);
    const cursor = row?.source_tick;
    if (!Number.isSafeInteger(cursor) || cursor < 0)
      throw Error(`${role} observation has no nonnegative integer source_tick`);
    if (typeof observedAt !== 'string' || !Number.isFinite(Date.parse(observedAt)))
      throw Error(`${role} observation has no valid observedAt timestamp`);
    if (state.latestCursor !== null && cursor < state.latestCursor)
      throw Error(`${role} observed source_tick moved backward (${state.latestCursor} -> ${cursor})`);
    state.latestCursor = cursor;
    state.sequence++;
    state.rows.set(cursor, {row, observedAt, sequence: state.sequence});
    while (state.rows.size > retainedCursorsPerPeer) {
      const oldestCursor = Math.min(...state.rows.keys());
      state.rows.delete(oldestCursor);
    }
  }

  function takePair() {
    const alpha = peers.alpha;
    const beta = peers.beta;
    if (alpha.latestCursor === null || beta.latestCursor === null) return null;
    const cursor = Math.max(alpha.latestCursor, beta.latestCursor);
    if (cursor < lastMatchedCursor || !alpha.rows.has(cursor) || !beta.rows.has(cursor)) return null;
    const alphaEntry = alpha.rows.get(cursor);
    const betaEntry = beta.rows.get(cursor);
    const version = [cursor, alphaEntry.sequence, betaEntry.sequence].join(':');
    if (cursor === lastMatchedCursor && version === lastPairVersion) return null;
    lastMatchedCursor = cursor;
    lastPairVersion = version;
    return {source_tick: cursor, rows: [alphaEntry.row, betaEntry.row],
      observed_at_by_peer: {alpha: alphaEntry.observedAt, beta: betaEntry.observedAt},
      observation_sequence_by_peer: {alpha: alphaEntry.sequence, beta: betaEntry.sequence}};
  }

  function laggingPeer() {
    const alphaCursor = peers.alpha.latestCursor;
    const betaCursor = peers.beta.latestCursor;
    if (alphaCursor === null || betaCursor === null) return null;
    if (alphaCursor === betaCursor) return null;
    return alphaCursor < betaCursor ? 'alpha' : 'beta';
  }

  function status() {
    return {
      latest_cursor_by_peer: Object.fromEntries(PEERS.map(role =>
        [role, peers[role].latestCursor])),
      retained_rows_by_peer: Object.fromEntries(PEERS.map(role =>
        [role, peers[role].rows.size])),
      last_matched_cursor: lastMatchedCursor < 0 ? null : lastMatchedCursor,
    };
  }

  return {observe, takePair, laggingPeer, status};
}
