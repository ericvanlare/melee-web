function select(object, keys) {
  if (!object || typeof object !== 'object') return null;
  return Object.fromEntries(keys.map(key => [key, object[key] ?? null]));
}

export function checksumCompareReached(row, target, phase = 1) {
  const protocol = row?.owner_health?.peer?.protocol;
  return (phase === null || row?.menu_phase === phase) && row?.menu_running === 1 &&
    protocol?.ready === true && protocol.next_checksum_compare >= target &&
    protocol.checksum_mismatch_count === 0;
}

export function checksumWaitDiagnostic(row, role) {
  const status = row?.native_status;
  const owner = row?.owner_health;
  const peer = owner?.peer;
  const protocol = peer?.protocol;
  const networkWait = status?.network_wait;
  const indexed = status?.indexed;
  const match = row?.match_summary;
  return {
    role,
    phase: row?.menu_phase ?? null,
    running: row?.menu_running ?? null,
    source_tick: status?.cursor ?? null,
    menu_message: row?.menu_message ?? null,
    runtime_error: row?.runtime_error ?? null,
    owner_failure: owner?.failure ?? null,
    peer_failure: peer?.failure ?? null,
    endpoint_error_count: peer?.endpoint_error_count ?? null,
    native_status: {
      active: status?.active ?? null,
      cursor: status?.cursor ?? null,
      pushed: status?.pushed ?? null,
      blocker: status?.blocker ?? null,
      terminal: status?.terminal ?? null,
      local_capture_failure: status?.local_capture_failure ?? null,
      network_wait: select(networkWait,
        ['active', 'callbacks', 'episodes', 'start_tick', 'last_tick', 'resume_count']),
      indexed: select(indexed, ['duplicates', 'conflicts', 'gaps', 'invalid']),
    },
    protocol: select(protocol, [
      'role', 'local_port', 'remote_port', 'ready', 'terminal',
      'local_input_ticks', 'remote_input_ticks', 'remote_contiguous_input', 'remote_ack_input',
      'local_checksum_ticks', 'remote_checksum_ticks', 'remote_contiguous_checksum',
      'local_contiguous_input', 'local_contiguous_checksum', 'next_checksum_compare',
      'remote_ack_checksum', 'acknowledged_inputs', 'acknowledged_checksums',
      'checksum_mismatch_count', 'native_local_divergences',
    ]),
    match_summary: match ? {
      leg: match.leg ?? null,
      frame: match.frame ?? null,
      ready: match.ready ?? null,
      paused: match.paused ?? null,
      ending: match.ending ?? null,
      complete: match.complete ?? null,
      outcome: match.outcome ?? null,
      winner: match.winner ?? null,
      rules: select(match.rules, ['stage', 'is_stock', 'is_vs', 'is_teams', 'player_stocks']),
      players: Array.isArray(match.players) ? match.players.map(player => select(player,
        ['source_port', 'source_character', 'stocks', 'damage_percent', 'x', 'y', 'motion', 'groundAir'])) : null,
    } : row?.match_observation_error ? {observer_error: row.match_observation_error} : null,
  };
}
