// SPDX-License-Identifier: GPL-3.0-or-later
// Diagnostic controls retain the shared runtime-owned cadence implementation.
import {createRuntimeMatchSessionLoop} from '../runtime_match_session_loop.mjs';
export {waitForLateRemotePairAndResume} from '../runtime_match_session_loop.mjs';
export function installRuntimePacedHistoryProbe(options = {}) {
  if(globalThis.__meleeRuntimePacedHistory)throw Error('Paced probe already installed');
  const api=createRuntimeMatchSessionLoop({...options,
    owner:globalThis.__meleeRuntimeMatchHistory,peer:globalThis.__meleeRuntimePeerHistoryProbe});
  globalThis.__meleeRuntimePacedHistory=api;
  return {role:options.role,scope:api.snapshot().scope};
}
