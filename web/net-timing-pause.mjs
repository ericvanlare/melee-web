/** True only for an active, source-paused network session paused by timing. */
export function canResumeNetworkTimingPause({networkActive, nativeRunning, message, ownerState} = {}) {
  return networkActive === true && nativeRunning === false &&
    typeof message === 'string' && message.startsWith('Paused after a timing disruption') &&
    ownerState?.state === 'paused' && ownerState.paused === true && ownerState.canPause === true;
}

/** Delegate through the shared lifecycle API, then verify native unpause. */
export async function attemptNetworkTimingPauseResume({canResume, resume, isRunning}) {
  if (!canResume()) return false;
  try { await resume(); }
  catch (error) {
    if (!canResume()) return false;
    throw error;
  }
  return isRunning() === true;
}
