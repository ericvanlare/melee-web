// SPDX-License-Identifier: GPL-3.0-or-later
/** Small executable controls shared by the live synctest runner. */

export function validateAuroraFrame(status, previousBeginFrameId, previousEndFrameId) {
  if (!status || !Number.isSafeInteger(status.drawCallCount) || status.drawCallCount <= 0) {
    throw new Error('Actual source draw submitted no Aurora draw calls');
  }
  if (!Number.isSafeInteger(status.lastBeginFrameId) ||
      status.lastBeginFrameId <= previousBeginFrameId) {
    throw new Error('Aurora begin-frame identity did not advance');
  }
  if (!Number.isSafeInteger(status.lastEndFrameId) ||
      status.lastEndFrameId < status.lastBeginFrameId ||
      status.lastEndFrameId <= previousEndFrameId) {
    throw new Error('Aurora end-frame identity did not advance after begin-frame');
  }
  return {beginFrameId: status.lastBeginFrameId, endFrameId: status.lastEndFrameId};
}

export async function checkedAsyncCcall(module, label, identifier, argumentTypes = [], arguments_ = []) {
  const result = await module.ccall(identifier, 'number', argumentTypes, arguments_, {async: true});
  if (result !== 1) {
    const error = module.UTF8ToString(module._melee_web_snapshot_error());
    throw new Error(`${label}: ${error}`);
  }
  return result;
}

export function chromeUserDataDirectoryFromSpawnArgs(spawnargs) {
  if (!Array.isArray(spawnargs)) throw new Error('Owned Chrome process did not expose spawn arguments');
  for (let index = 0; index < spawnargs.length; index++) {
    const argument = spawnargs[index];
    if (typeof argument !== 'string') continue;
    if (argument.startsWith('--user-data-dir=')) {
      const directory = argument.slice('--user-data-dir='.length);
      if (directory) return directory;
    }
    if (argument === '--user-data-dir') {
      const directory = spawnargs[index + 1];
      if (typeof directory === 'string' && directory) return directory;
    }
  }
  throw new Error('Owned Chrome command line has no --user-data-dir');
}

export function recordBrowserProcessInfo(report, processInfo) {
  if (!Array.isArray(processInfo) || processInfo.length === 0) {
    throw new Error('Chrome process inventory is empty or malformed');
  }
  const malformed = processInfo.find(row => !row || !Number.isSafeInteger(row.id) ||
    row.id <= 0 || typeof row.type !== 'string' || row.type.length === 0);
  if (malformed) throw new Error(`Chrome process inventory contains a malformed row: ${JSON.stringify(malformed)}`);
  const browser = processInfo.find(row => row.type === 'browser');
  if (!browser) throw new Error('Chrome process inventory has no browser process row');
  report.browser_process_info = processInfo.map(row => ({...row}));
  report.browser_process_info_source = 'Chrome CDP SystemInfo.getProcessInfo';
  return {browserPid: browser.id, processIds: [...new Set(processInfo.map(row => row.id))]};
}

export async function launchOwnedChromeServer(chromium, launchOptions) {
  if (!chromium || typeof chromium.launchServer !== 'function') {
    throw new Error('Installed Playwright Chromium does not expose launchServer');
  }
  if ((launchOptions?.args || []).some(argument => typeof argument === 'string' &&
      (argument === '--user-data-dir' || argument.startsWith('--user-data-dir=')))) {
    throw new Error('Do not pass --user-data-dir to launchServer; record Playwright’s owned profile from spawnargs');
  }
  const server = await chromium.launchServer(launchOptions);
  const child = server.process();
  if (!child || !Number.isSafeInteger(child.pid) || child.pid <= 0) {
    try { await server.close(); } catch {}
    throw new Error('Playwright did not expose the owned headless Chrome child process');
  }
  return {server, child};
}

export function ownedProcessPresent(pid) {
  if (!Number.isInteger(pid) || pid <= 0) throw new Error(`Invalid owned process PID: ${pid}`);
  try {
    process.kill(pid, 0);
    return true;
  } catch (error) {
    if (error?.code === 'ESRCH') return false;
    throw error;
  }
}

export function applyCleanupVerdict(report, cleanupFailures) {
  report.cleanup = {...(report.cleanup || {}), failures: cleanupFailures};
  if (cleanupFailures.length) {
    report.result = 'failed';
    report.failure ??= {message: 'Owned cleanup failed', cleanupFailures};
  }
  return report.result;
}
