// Liveness checks for the test driver only; never alter game scheduling/state.
export function requireConnectedPage(browser, page) {
  if (!browser?.isConnected()) throw Error('Headless browser disconnected; replay cannot advance');
  if (!page || page.isClosed()) throw Error('Headless browser page closed; replay cannot advance');
}

export function requireReplaySnapshot(snapshot) {
  if (!snapshot) throw Error('Replay snapshot is unavailable');
  if (snapshot.snapshot_error) throw Error(snapshot.snapshot_error);
  if (snapshot.runtime_error) throw Error(snapshot.runtime_error);
  if (!Number.isInteger(snapshot.phase) || !Number.isInteger(snapshot.source_cursor))
    throw Error('Replay source phase/cursor observation is unavailable');
}
