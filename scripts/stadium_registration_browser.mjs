// Read the diagnostic source observer only; these functions never advance play.
export function armStadiumRegistrationInPage() {
  const m = globalThis.Module;
  if (!m || typeof m._melee_web_stadium_registration_arm !== 'function' ||
      typeof m._melee_web_stadium_registration_status_json !== 'function' ||
      typeof m._melee_web_stadium_registration_read_json !== 'function' ||
      typeof m._melee_web_stadium_registration_seal !== 'function' ||
      typeof m.UTF8ToString !== 'function')
    throw Error('Stadium registration observer is unavailable');
  if (m.stadiumMatchedSetupNativeIdle?.() !== true)
    throw Error('Stadium registration native owner is busy or unavailable; no native reads attempted');
  if (m._melee_web_stadium_registration_arm() !== 1)
    throw Error('Stadium registration must arm before the first source world');
  const status = JSON.parse(m.UTF8ToString(m._melee_web_stadium_registration_status_json()));
  if (status.schema !== 'melee-web-source-registration-status-v1' ||
      status.armed !== true || status.sealed !== false || status.invalid !== false ||
      status.overflow !== false || status.active_world !== '0' || status.worlds !== 0 ||
      status.count !== 0 || status.pending_setup !== false)
    throw Error('Stadium registration initial state is not empty and healthy');
  return status;
}

export function collectStadiumRegistrationInPage({seal}) {
  const m = globalThis.Module;
  if (!m || typeof m._melee_web_stadium_registration_status_json !== 'function')
    throw Error('Stadium registration observer was lost');
  if (m.stadiumMatchedSetupNativeIdle?.() !== true)
    throw Error('Stadium registration native owner is busy or unavailable; no native reads attempted');
  if (seal && m._melee_web_stadium_registration_seal() !== 1)
    throw Error('Stadium registration cannot seal an incomplete source lifetime');
  const status = JSON.parse(m.UTF8ToString(m._melee_web_stadium_registration_status_json()));
  if (!Number.isSafeInteger(status.count) || status.count < 0 || status.count > 65536)
    throw Error('Stadium registration row count exceeds its fixed bound');
  const rows = [];
  for (let i = 0; i < status.count; ++i) {
    const pointer = m._melee_web_stadium_registration_read_json(i);
    if (!pointer) throw Error('Stadium registration row is missing');
    rows.push(m.UTF8ToString(pointer));
  }
  return {status, rows};
}

export function validateStadiumRegistrationSnapshot(snapshot) {
  const s = snapshot?.status;
  if (!s || s.schema !== 'melee-web-source-registration-status-v1' ||
      s.armed !== true || s.sealed !== true || s.invalid !== false || s.overflow !== false ||
      s.active_world !== '0' || s.pending_setup !== false ||
      !Number.isSafeInteger(s.worlds) || s.worlds < 2 ||
      s.row_capacity !== 65536 || s.slot_capacity !== 8192 ||
      !Number.isSafeInteger(s.storage_bytes) || s.storage_bytes <= 0 || s.storage_bytes > 16 * 1024 * 1024 ||
      !Number.isSafeInteger(s.count) || s.count < 1 || s.count > s.row_capacity ||
      !Array.isArray(snapshot.rows) || snapshot.rows.length !== s.count)
    throw Error('Stadium registration snapshot is incomplete or unhealthy');
  for (let i = 0; i < snapshot.rows.length; ++i) {
    const row = JSON.parse(snapshot.rows[i]);
    if (row.schema !== 'melee-web-source-registration-row-v1' || row.sequence !== i ||
        !Number.isInteger(row.kind) || row.kind < 1 || row.kind > 15)
      throw Error('Stadium registration row framing differs');
  }
  return {complete: true, rows: s.count, worlds: s.worlds,
    scope: 'Complete diagnostic stream framing only; original correspondence is checked separately.',
    original_lifecycle_parity: false, all_rng_writers_observed: false, C3: false};
}
