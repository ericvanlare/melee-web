import {parseMeleeGCI, downloadMeleeGCI, MELEE_GCI_FILE_BYTES,
  MELEE_GCI_PROFILE_BYTES} from './gamecube-save.mjs';
import {SaveProfileStore, SaveProfileStorageError} from './save-profile-store.mjs';

const $ = id => document.getElementById(id);
const PROFILE_SCENES = new Set(['css', 'sss', 'title', 'main', 'opening', 'opening-vs',
  'match', 'results', 'prize']);
const descriptions = {
  everything: 'Everything is unlocked. Changes aren’t saved.',
  personal: 'Progress saves automatically on this device.',
};
const modeLabel = mode => mode === 'personal' ? 'Personal progress' : 'Everything unlocked';
const modeBytes = bytes => bytes?.byteLength === MELEE_GCI_PROFILE_BYTES ? new Uint8Array(bytes) : null;
const sameBytes = (left, right) => !!left && !!right && left.length === right.length &&
  left.every((value, index) => value === right[index]);

export function mountSaveProfileSettings({onError = () => {}, onBusy = () => {}} = {}) {
  const dialog = $('settings-dialog'), confirmDialog = $('save-confirm-dialog');
  const modeSelect = $('save-mode'), statusElement = $('save-status');
  const description = $('save-mode-description');
  const confirmTitle = $('save-confirm-title'), confirmBody = $('save-confirm-body');
  const acceptButton = $('save-confirm-accept');
  if (!dialog || !confirmDialog || !modeSelect || !statusElement)
    throw Error('Save Settings controls are unavailable.');

  let player = null, store = null, state = null;
  let activeMode = 'everything', modeRevision = 0, profileRevision = null;
  let profileData = null, recovered = false, blocked = false;
  let storageFailure = null, persistentGranted = false, settingsDamaged = false;
  let timer = null, snapshotBusy = false, frozen = false, epoch = 0;
  let writeQueue = Promise.resolve(), confirmation = null, changing = false;

  function status(message) { statusElement.textContent = message; }
  function showFailure(error) {
    let cause = error, quotaFailure = false;
    for (let depth = 0; cause && depth < 4; depth++, cause = cause.cause) {
      if (cause.name === 'QuotaExceededError') { quotaFailure = true; break; }
    }
    if (quotaFailure) {
      error = new SaveProfileStorageError(
        'Browser storage is full. Previously committed progress remains available. Free space in this browser profile, then reload to resume autosaving.',
        {cause: error, recoveryAvailable: error?.recoveryAvailable, revision: error?.revision});
    }
    if (error instanceof SaveProfileStorageError) { storageFailure = error; cancelTimer(); }
    status(error?.message || String(error));
    onError(error);
  }
  function describe() { description.textContent = descriptions[modeSelect.value || activeMode]; }
  function updateModeUI() {
    modeSelect.value = activeMode;
    describe();
  }
  function isLoaded() { return !!state?.bundle; }
  function hasScene() { return PROFILE_SCENES.has(state?.scene); }
  function canSnapshot() { return !!player && (isLoaded() || hasScene()); }
  function restartOnTransition() { return isLoaded() || hasScene(); }
  async function snapshotPersonalProfile() {
    // The native source owner restores only preferences temporarily replaced
    // by its supported startup configuration. Keep browser storage agnostic
    // to SaveData offsets so live progress remains owned by the source snapshot.
    return player.snapshotSaveProfile();
  }
  function displaySavedState() {
    if (storageFailure) return;
    if (settingsDamaged) {
      status('Save mode settings were damaged. Choose a mode or load a save to repair them.');
      return;
    }
    if (activeMode === 'everything') {
      status('');
    } else if (recovered) {
      status('Using the previous verified progress copy. The newer stored copy failed its checksum.');
    } else if (profileRevision !== null && profileData) {
      const saved = new Date(profileData.committedAt);
      status(`Personal progress saved on this device${Number.isFinite(saved.valueOf()) ? ` at ${saved.toLocaleTimeString()}` : ''}.` +
        (persistentGranted ? '' : ' Browser storage can still be cleared or evicted.'));
    } else {
      status(`Personal progress will start from the original fresh profile and save automatically${
        isLoaded() ? ' after play begins.' : ' on the next launch after play begins.'}`);
    }
  }

  function cancelTimer() {
    if (timer !== null) clearInterval(timer);
    timer = null;
  }
  function reconcileTimer() {
    if (timer !== null) return;
    if (frozen || blocked || storageFailure || activeMode !== 'personal' || !hasScene()) return;
    timer = setInterval(() => { void autosaveTick(); }, 1500);
  }
  async function persistSnapshot(bytes, {force = false, snapshotEpoch = epoch} = {}) {
    if (!store || activeMode !== 'personal') return false;
    const nextWrite = writeQueue.then(async () => {
      if (!force && (frozen || snapshotEpoch !== epoch || activeMode !== 'personal')) return false;
      if (sameBytes(bytes, profileData?.data)) return false;
      const committed = await store.commitProfile(bytes, profileRevision);
      profileRevision = committed.revision;
      profileData = {data: new Uint8Array(bytes), committedAt: committed.committedAt,
        generation: committed.generation};
      recovered = false;
      storageFailure = null;
      displaySavedState();
      return true;
    });
    writeQueue = nextWrite.catch(error => {
      storageFailure = error;
      status(error?.message || String(error));
    });
    return nextWrite;
  }
  async function captureAndPersist({force = false} = {}) {
    if (!canSnapshot() || activeMode !== 'personal') return false;
    const snapshotEpoch = epoch;
    const bytes = await snapshotPersonalProfile();
    return persistSnapshot(bytes, {force, snapshotEpoch});
  }
  async function autosaveTick() {
    if (snapshotBusy || frozen || blocked || storageFailure || activeMode !== 'personal' || !hasScene()) return;
    snapshotBusy = true;
    try {
      const changed = await captureAndPersist();
      if (changed) status('Personal progress saved on this device.');
    } catch (error) { showFailure(error); cancelTimer(); }
    finally { snapshotBusy = false; }
  }
  async function waitForWrites() {
    try { await writeQueue; } catch { /* writeQueue records failures and remains drainable */ }
    if (storageFailure) throw storageFailure;
  }
  async function freezeAndFlush({flush = true} = {}) {
    frozen = true; epoch++; cancelTimer();
    try { await waitForWrites(); }
    catch (error) { if (!storageFailure) throw error; }
    if (flush && !storageFailure && activeMode === 'personal' && hasScene()) {
      await captureAndPersist({force: true});
      try { await waitForWrites(); }
      catch (error) { if (!storageFailure) throw error; }
    }
  }
  function unfreeze() { frozen = false; reconcileTimer(); }

  function setChanging(value) {
    changing = value;
    for (const id of ['settings-open', 'save-mode', 'export-save', 'load-save', 'settings-close',
      'save-confirm-cancel', 'save-confirm-accept']) $(id).disabled = value;
    onBusy();
  }

  async function configureAndLaunch(mode, data, restart) {
    if (isLoaded()) await player.unload();
    await player.configureSaveProfile(mode, mode === 'personal' ? modeBytes(data) : null);
    if (restart) await player.start();
  }
  async function restoreRuntime(mode, data, restart) {
    try {
      if (isLoaded() || hasScene()) await player.unload();
      await player.configureSaveProfile(mode, mode === 'personal' ? modeBytes(data) : null);
      if (restart) await player.start();
    } catch (error) {
      throw Error(`The change failed and runtime rollback also failed: ${error?.message || error}`);
    }
  }

  async function switchMode(target) {
    if (!store) throw Error('Browser save storage is unavailable; the save mode was not changed.');
    if (blocked && target === 'personal') throw Error('Resolve the stored progress error before starting in Personal progress.');
    const oldMode = activeMode, oldData = profileData?.data || null;
    const restart = restartOnTransition();
    await freezeAndFlush({flush: !storageFailure && !blocked});
    const latestData = profileData?.data || oldData;
    let runtimeChanged = false;
    try {
      await configureAndLaunch(target, latestData, restart);
      runtimeChanged = true;
      const savedMode = await store.setMode(target, modeRevision);
      modeRevision = savedMode.revision;
      activeMode = target; settingsDamaged = false;
      storageFailure = null;
    } catch (error) {
      if (runtimeChanged || restart) await restoreRuntime(oldMode, latestData, restart);
      activeMode = oldMode;
      updateModeUI();
      unfreeze();
      throw error;
    }
    updateModeUI();
    unfreeze();
    displaySavedState();
    status(restart ? `${modeLabel(target)} is active; the source session restarted.` :
      `${modeLabel(target)} applies to the next launch. No game was running.`);
  }

  async function importProfile(candidate) {
    if (!store) throw Error('Browser save storage is unavailable; the save was not imported.');
    const oldMode = activeMode, oldData = profileData?.data || null;
    const restart = restartOnTransition();
    await freezeAndFlush({flush: !storageFailure && !blocked});
    const latestData = profileData?.data || oldData;
    let expectedRevision = profileRevision;
    if (expectedRevision === null) expectedRevision = await store.getProfileRevision();
    let runtimeChanged = false;
    try {
      await configureAndLaunch('personal', candidate, restart);
      runtimeChanged = true;
      const committed = await store.commitProfile(candidate, expectedRevision,
        {mode: 'personal', expectedModeRevision: modeRevision});
      profileRevision = committed.revision;
      profileData = {data: new Uint8Array(candidate), committedAt: committed.committedAt,
        generation: committed.generation};
      const mode = await store.getMode();
      modeRevision = mode.revision;
      activeMode = 'personal'; recovered = false; blocked = false;
      storageFailure = null; settingsDamaged = false;
    } catch (error) {
      if (runtimeChanged || restart) await restoreRuntime(oldMode, latestData, restart);
      activeMode = oldMode;
      updateModeUI();
      unfreeze();
      throw error;
    }
    updateModeUI();
    unfreeze();
    displaySavedState();
    status(restart ? 'Save loaded. Personal progress is active and the game restarted.' :
      'Save loaded. Personal progress will be used on the next launch; no game was running.');
  }

  function openConfirmation({title, body, action, run}) {
    confirmation = run;
    confirmTitle.textContent = title;
    confirmBody.textContent = body;
    acceptButton.textContent = action;
    if (!confirmDialog.open) confirmDialog.showModal();
    $('save-confirm-cancel').focus();
  }
  function closeConfirmation() {
    if (confirmDialog.open) confirmDialog.close();
    confirmation = null;
    modeSelect.focus();
  }

  $('settings-open').addEventListener('click', () => {
    if (!dialog.open) dialog.showModal();
    modeSelect.focus();
  });
  $('settings-close').addEventListener('click', () => dialog.close());
  modeSelect.addEventListener('change', () => {
    const target = modeSelect.value;
    updateModeUI();
    modeSelect.value = activeMode;
    if (target === activeMode) return;
    const restart = restartOnTransition();
    const keep = target === 'everything' && activeMode === 'personal' ?
      ' Existing Personal progress is retained.' :
      target === 'personal' ? ' Existing Personal progress is kept unless you load a replacement save.' : '';
    const next = restart ? 'Switch and restart the game?' : 'This choice applies to the next launch; no game will restart now.';
    openConfirmation({
      title: restart ? `Switch to ${modeLabel(target)} and restart the game?` :
        `Switch to ${modeLabel(target)} for the next launch?`,
      body: `${next}${keep}`,
      action: restart ? 'Switch and restart' : 'Switch for next launch',
      run: () => switchMode(target),
    });
  });
  $('save-confirm-cancel').addEventListener('click', closeConfirmation);
  confirmDialog.addEventListener('cancel', event => { event.preventDefault(); closeConfirmation(); });
  acceptButton.addEventListener('click', async () => {
    const run = confirmation;
    confirmDialog.close(); confirmation = null;
    if (!run) return;
    setChanging(true);
    try { await run(); }
    catch (error) { showFailure(error); }
    finally { setChanging(false); }
  });
  $('load-save').addEventListener('click', () => $('save-file').click());
  $('save-file').addEventListener('change', async event => {
    const file = event.currentTarget.files?.[0];
    event.currentTarget.value = '';
    if (!file) return;
    if (file.size !== MELEE_GCI_FILE_BYTES) {
      showFailure(new Error(
        `Melee GCI must be exactly ${MELEE_GCI_FILE_BYTES} bytes; selected file has ${file.size} bytes.`));
      return;
    }
    let candidate;
    try { candidate = parseMeleeGCI(new Uint8Array(await file.arrayBuffer())); }
    catch (error) { showFailure(error); return; }
    const restart = restartOnTransition();
    const oldSave = profileRevision !== null || !!profileData ?
      'This replaces your current Personal progress save if one exists. ' :
      'This becomes your Personal progress save. ';
    const restartText = restart ? 'The game will restart now.' :
      'It will be used on the next launch; no game will restart now.';
    openConfirmation({
      title: 'Load this GameCube save file?',
      body: `${oldSave}The import switches Save mode to Personal progress. ${restartText}`,
      action: restart ? 'Load save and restart' : 'Load save for next launch',
      run: () => importProfile(candidate),
    });
  });
  $('export-save').addEventListener('click', async () => {
    try {
      let bytes;
      if (activeMode === 'everything') {
        bytes = await player.snapshotSaveProfile({baseline: true});
      } else if (canSnapshot()) {
        bytes = await snapshotPersonalProfile();
      } else if (profileData?.data) {
        bytes = new Uint8Array(profileData.data);
      } else {
        throw Error('There is no Personal progress save to export yet. Start the game to initialize one.');
      }
      const result = downloadMeleeGCI(bytes, activeMode);
      status(`${modeLabel(activeMode)} save downloaded as ${result.fileName}.`);
    } catch (error) { showFailure(error); }
  });
  confirmDialog.addEventListener('close', () => { if (dialog.open && !confirmation) modeSelect.focus(); });

  return Object.freeze({
    async bindPlayer(boundPlayer) {
      player = boundPlayer;
      try {
        store = await SaveProfileStore.open();
        persistentGranted = await store.requestPersistence();
        const mode = await store.getMode();
        activeMode = mode.mode; modeRevision = mode.revision;
        settingsDamaged = !!mode.damaged;
        try {
          const profile = await store.getProfile();
          if (profile) {
            profileRevision = profile.revision;
            profileData = {data: profile.data, committedAt: profile.committedAt,
              generation: profile.generation};
            recovered = profile.recovered;
          }
        } catch (error) {
          if (error instanceof Error && 'revision' in error) profileRevision = error.revision;
          blocked = true;
          storageFailure = error;
          await player.configureSaveProfile('everything', null);
          updateModeUI();
          status(error.message);
          onError(error);
          return;
        }
        await player.configureSaveProfile(activeMode,
          activeMode === 'personal' ? profileData?.data || null : null);
        updateModeUI();
        displaySavedState();
      } catch (error) {
        blocked = true;
        storageFailure = error;
        await player.configureSaveProfile('everything', null).catch(() => {});
        status(error?.message || String(error));
        onError(error);
      }
    },
    setState(next) { state = next; reconcileTimer(); },
    get blocked() { return blocked && activeMode === 'personal'; },
    get busy() { return changing; },
    async flushBeforeTeardown() {
      await freezeAndFlush();
      if (activeMode === 'personal' && canSnapshot() && storageFailure) throw storageFailure;
    },
    async close() { cancelTimer(); await waitForWrites().catch(() => {}); store?.close(); },
  });
}
