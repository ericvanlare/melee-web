// SPDX-License-Identifier: GPL-3.0-or-later
// Diagnostic adapter for the same page-owned bridge used by the staging player.
import {createRuntimeMatchSessionBridge} from '../runtime_match_session_bridge.mjs';
export function installRuntimePeerHistoryProbe(options = {}) {
  if (globalThis.__meleeRuntimePeerHistoryProbe)
    throw Error('Runtime peer-history bridge is already installed');
  const bridge = createRuntimeMatchSessionBridge({...options,
    owner:globalThis.__meleeRuntimeMatchHistory});
  globalThis.__meleeRuntimePeerHistoryProbe = bridge;
  return {role:options.role,peer_owner:'browser-page',native_game_input_calls:0,
    peer_snapshot:bridge.snapshot()};
}
