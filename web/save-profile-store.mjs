const DATABASE = 'webmelee-save-profiles-v1';
const VERSION = 1;
const PROFILE_STORE = 'profiles';
const SETTINGS_STORE = 'settings';
const PROFILE_KEY = 'personal';
const MODE_KEY = 'save-mode';
const PROFILE_BYTES = 0x1790 + 7 * 0x1F2C;

const cloneBytes = value => new Uint8Array(value);
const requestValue = request => new Promise((resolve, reject) => {
  request.onsuccess = () => resolve(request.result);
  request.onerror = () => reject(request.error || Error('Browser storage read failed.'));
});
const transactionDone = transaction => new Promise((resolve, reject) => {
  transaction.oncomplete = () => resolve();
  transaction.onabort = transaction.onerror = () => reject(transaction.error || Error('Browser storage transaction failed.'));
});
const validMode = mode => mode === 'everything' || mode === 'personal';

export class SaveProfileStorageError extends Error {
  constructor(message, {cause, recoveryAvailable = false, revision = null} = {}) {
    super(message, {cause});
    this.name = 'SaveProfileStorageError';
    this.recoveryAvailable = recoveryAvailable;
    this.revision = revision;
  }
}

export class SaveProfileConflictError extends SaveProfileStorageError {
  constructor(message = 'Another tab changed save data. Reload this page before continuing.') {
    super(message);
    this.name = 'SaveProfileConflictError';
  }
}

async function checksum(bytes) {
  if (!globalThis.crypto?.subtle) throw new SaveProfileStorageError('Secure browser storage is unavailable in this context.');
  const digest = await crypto.subtle.digest('SHA-256', bytes);
  return [...new Uint8Array(digest)].map(value => value.toString(16).padStart(2, '0')).join('');
}

function structurallyValid(envelope) {
  return !!envelope && envelope.version === VERSION && Number.isSafeInteger(envelope.revision) &&
    envelope.revision > 0 && typeof envelope.generation === 'string' &&
    typeof envelope.committedAt === 'string' && envelope.data instanceof Uint8Array &&
    envelope.data.byteLength === PROFILE_BYTES && /^[a-f0-9]{64}$/.test(envelope.sha256);
}

async function verified(envelope) {
  if (!structurallyValid(envelope)) return false;
  try { return await checksum(envelope.data) === envelope.sha256; }
  catch { return false; }
}

function randomGeneration() {
  if (crypto.randomUUID) return crypto.randomUUID();
  const bytes = crypto.getRandomValues(new Uint8Array(16));
  return [...bytes].map(value => value.toString(16).padStart(2, '0')).join('');
}

export class SaveProfileStore {
  constructor(db) { this.db = db; }

  static open({indexedDB = globalThis.indexedDB} = {}) {
    return new Promise((resolve, reject) => {
      if (!indexedDB) return reject(new SaveProfileStorageError('IndexedDB is unavailable; local save progress cannot be stored.'));
      let request;
      try { request = indexedDB.open(DATABASE, VERSION); }
      catch (cause) { reject(new SaveProfileStorageError('Browser save storage could not be opened.', {cause})); return; }
      request.onupgradeneeded = () => {
        const db = request.result;
        if (!db.objectStoreNames.contains(PROFILE_STORE)) db.createObjectStore(PROFILE_STORE);
        if (!db.objectStoreNames.contains(SETTINGS_STORE)) db.createObjectStore(SETTINGS_STORE);
      };
      request.onsuccess = () => resolve(new SaveProfileStore(request.result));
      request.onerror = () => reject(new SaveProfileStorageError('Browser save storage could not be opened.', {cause: request.error}));
      request.onblocked = () => reject(new SaveProfileStorageError('Close older WebMelee tabs to enable browser save storage.'));
    });
  }

  async getMode() {
    const tx = this.db.transaction(SETTINGS_STORE, 'readonly');
    const done = transactionDone(tx);
    let record;
    try { record = await requestValue(tx.objectStore(SETTINGS_STORE).get(MODE_KEY)); await done; }
    catch (cause) { throw new SaveProfileStorageError('Save mode could not be read from this browser.', {cause}); }
    if (!record) return {mode: 'everything', revision: 0};
    if (!validMode(record.mode) || !Number.isSafeInteger(record.revision) || record.revision < 1)
      return {mode: 'everything', revision: 0, damaged: true};
    return {mode: record.mode, revision: record.revision};
  }

  async setMode(mode, expectedRevision) {
    if (!validMode(mode)) throw new TypeError('Unknown save mode.');
    return this.#commit({mode, expectedModeRevision: expectedRevision});
  }

  async getProfile() {
    const tx = this.db.transaction(PROFILE_STORE, 'readonly');
    const done = transactionDone(tx);
    let record;
    try { record = await requestValue(tx.objectStore(PROFILE_STORE).get(PROFILE_KEY)); await done; }
    catch (cause) { throw new SaveProfileStorageError('Personal progress could not be read from this browser.', {cause}); }
    if (!record) return null;
    const current = record.active;
    const recordRevision = Number.isSafeInteger(record.revision) && record.revision > 0 ? record.revision : null;
    if (await verified(current)) return {data: cloneBytes(current.data), revision: recordRevision,
      generation: current.generation, committedAt: current.committedAt, recovered: false};
    const previous = record.previous;
    if (await verified(previous)) return {data: cloneBytes(previous.data), revision: recordRevision,
      generation: previous.generation, committedAt: previous.committedAt, recovered: true};
    throw new SaveProfileStorageError('Personal progress is damaged and no verified recovery generation remains. Stored data was left untouched.',
      {recoveryAvailable: false, revision: record.revision});
  }

  async getProfileRevision() {
    const tx = this.db.transaction(PROFILE_STORE, 'readonly');
    const done = transactionDone(tx);
    let record;
    try { record = await requestValue(tx.objectStore(PROFILE_STORE).get(PROFILE_KEY)); await done; }
    catch (cause) { throw new SaveProfileStorageError('Personal progress version could not be read.', {cause}); }
    return Number.isSafeInteger(record?.revision) && record.revision > 0 ? record.revision : null;
  }

  async commitProfile(data, expectedRevision, {mode, expectedModeRevision} = {}) {
    if (!(data instanceof Uint8Array) || data.byteLength !== PROFILE_BYTES)
      throw new TypeError('Personal progress must match the original GameCube save manifest.');
    if (mode !== undefined && !validMode(mode)) throw new TypeError('Unknown save mode.');
    const bytes = cloneBytes(data);
    const sha256 = await checksum(bytes);
    const old = await this.#readProfileRecord();
    let previous = null;
    if (old && await verified(old.active)) previous = old.active;
    else if (old && await verified(old.previous)) previous = old.previous;
    const active = {version: VERSION, revision: (expectedRevision ?? 0) + 1,
      generation: randomGeneration(), committedAt: new Date().toISOString(), data: bytes, sha256};
    return this.#commit({active, expectedProfileRevision: expectedRevision,
      previous, mode, expectedModeRevision});
  }

  async #readProfileRecord() {
    const tx = this.db.transaction(PROFILE_STORE, 'readonly');
    const done = transactionDone(tx);
    let record;
    try { record = await requestValue(tx.objectStore(PROFILE_STORE).get(PROFILE_KEY)); await done; }
    catch (cause) { throw new SaveProfileStorageError('Personal progress could not be read before a write.', {cause}); }
    return record;
  }

  async #commit({active, previous, mode, expectedProfileRevision, expectedModeRevision}) {
    const names = [SETTINGS_STORE];
    if (active) names.push(PROFILE_STORE);
    let tx;
    try { tx = this.db.transaction(names, 'readwrite', {durability: 'strict'}); }
    catch (strictError) {
      try { tx = this.db.transaction(names, 'readwrite'); }
      catch (cause) { throw new SaveProfileStorageError('Browser save storage refused a write transaction.', {cause: cause || strictError}); }
    }
    const done = transactionDone(tx);
    let failure;
    const settings = tx.objectStore(SETTINGS_STORE);
    const settingsRequest = settings.get(MODE_KEY);
    settingsRequest.onsuccess = () => {
      const storedMode = settingsRequest.result;
      const oldMode = storedMode && validMode(storedMode.mode) &&
        Number.isSafeInteger(storedMode.revision) && storedMode.revision > 0 ? storedMode :
        {mode: 'everything', revision: 0};
      if (expectedModeRevision !== undefined && oldMode.revision !== expectedModeRevision) {
        failure = new SaveProfileConflictError('Another tab changed the save mode. Reload this page before continuing.');
        tx.abort(); return;
      }
      if (active) {
        const profiles = tx.objectStore(PROFILE_STORE);
        const profileRequest = profiles.get(PROFILE_KEY);
        profileRequest.onsuccess = () => {
          const old = profileRequest.result;
          const revision = Number.isSafeInteger(old?.revision) && old.revision > 0 ? old.revision : null;
          if (revision !== expectedProfileRevision) {
            failure = new SaveProfileConflictError(); tx.abort(); return;
          }
          profiles.put({revision: active.revision, active, previous: previous || null}, PROFILE_KEY);
          if (mode !== undefined)
            settings.put({mode, revision: oldMode.revision + 1}, MODE_KEY);
        };
        profileRequest.onerror = () => { failure = profileRequest.error; tx.abort(); };
      } else {
        settings.put({mode, revision: oldMode.revision + 1}, MODE_KEY);
      }
    };
    settingsRequest.onerror = () => { failure = settingsRequest.error; tx.abort(); };
    try { await done; }
    catch (cause) {
      if (failure instanceof Error) throw failure;
      throw new SaveProfileStorageError('Browser save transaction did not commit. Previous committed progress remains available.', {cause});
    }
    return active ? {revision: active.revision, generation: active.generation, committedAt: active.committedAt} :
      {mode, revision: (await this.getMode()).revision};
  }

  async requestPersistence() {
    try { return await navigator.storage?.persist?.() ?? false; }
    catch { return false; }
  }

  close() { this.db.close(); }
}

export const SAVE_PROFILE_BYTES = PROFILE_BYTES;
