/** Check native network status and public-owner state before timing recovery. */
export function evaluateNetworkTimingPause({networkStatus, nativeRunning, message, ownerState} = {}) {
  const reasons = [];
  if (networkStatus?.active !== 1) reasons.push('network-inactive');
  if (nativeRunning !== 0) reasons.push('native-running');
  if (typeof message !== 'string' || !message.startsWith('Paused after a timing disruption'))
    reasons.push('message-not-timing-pause');
  if (ownerState?.state !== 'paused' || ownerState.paused !== true)
    reasons.push('owner-not-paused');
  if (ownerState?.canPause !== true) reasons.push('owner-cannot-pause');
  return {eligible: reasons.length === 0, reasons};
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
