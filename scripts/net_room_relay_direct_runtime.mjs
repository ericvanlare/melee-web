import os from 'node:os';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {readdir, realpath} from 'node:fs/promises';
import {buildRoomRelayMiniflareOptions} from './net_room_relay_miniflare_options.mjs';

const startedNs = process.hrtime.bigint();
let sequence = 0;
let runtime = null;
let stopping = false;

function serializeError(error) {
  return {
    name: error?.name || typeof error,
    message: error?.message || String(error),
    stack: error?.stack || null,
    cause: error?.cause ? String(error.cause?.stack || error.cause) : null,
  };
}

function emit(event, details = {}) {
  const now = process.hrtime.bigint();
  process.stdout.write('MELEE_ROOM_RELAY_RUNTIME ' + JSON.stringify({
    sequence: ++sequence,
    event,
    monotonicNs: now.toString(),
    elapsedNs: (now - startedNs).toString(),
    wallTime: new Date().toISOString(),
    ...details,
  }) + '\n');
}

function assertOwnedPath(label, value, scratch) {
  const resolved = path.resolve(value);
  const relative = path.relative(scratch, resolved);
  if (!relative || relative === '..' || relative.startsWith(`..${path.sep}`) || path.isAbsolute(relative))
    throw Error(`${label} must be a child of the per-run Worker scratch`);
  return resolved;
}

async function listRuntimeTempDirectories(runtimeTmpPath, scratch) {
  const entries = await readdir(runtimeTmpPath, {withFileTypes: true});
  const result = [];
  for (const entry of entries) {
    if (!entry.isDirectory() || !/^miniflare-[0-9a-f]{32}$/.test(entry.name))
      throw Error(`Unexpected entry in owned Miniflare runtime temp root: ${entry.name}`);
    const fullPath = assertOwnedPath('Miniflare runtime temp directory', path.join(runtimeTmpPath, entry.name), scratch);
    result.push({name: entry.name, path: fullPath});
  }
  return result;
}

async function waitForDirectoryEmpty(directory, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  let entries = await readdir(directory);
  while (entries.length && Date.now() < deadline) {
    await new Promise(resolve => setTimeout(resolve, 25));
    entries = await readdir(directory);
  }
  return {empty: entries.length === 0, entries};
}

const handleUncaughtError = error => emit('runtime-uncaught-error', {error: serializeError(error)});
const handleStructuredLogs = log => emit('structured-log', {log});

async function start() {
  const root = process.env.MELEE_ROOM_RELAY_ROOT;
  const configPath = process.env.MELEE_ROOM_RELAY_CONFIG;
  const wranglerCli = process.env.MELEE_ROOM_RELAY_WRANGLER_CLI;
  const miniflareEntry = process.env.MELEE_ROOM_RELAY_MINIFLARE_ENTRY;
  const persistPath = process.env.MELEE_ROOM_RELAY_PERSIST;
  const ownerScratch = process.env.MELEE_ROOM_RELAY_SCRATCH;
  const resourceTmpPath = process.env.MELEE_ROOM_RELAY_RESOURCE_TMP;
  const runtimeTmpPath = process.env.MELEE_ROOM_RELAY_RUNTIME_TMP;
  if (![root, configPath, wranglerCli, miniflareEntry, ownerScratch, persistPath,
    resourceTmpPath, runtimeTmpPath].every(Boolean))
    throw Error('direct Worker runtime is missing a required owned path');

  const scratchRealPath = await realpath(ownerScratch);
  const ownedPaths = {};
  for (const [label, key, value] of [
    ['Miniflare persistence path', 'resourcePersistencePath', persistPath],
    ['Miniflare plugin temp path', 'resourceTmpPath', resourceTmpPath],
    ['Miniflare process temp root', 'nodeTmpPath', runtimeTmpPath],
  ]) {
    const resolved = assertOwnedPath(label, value, scratchRealPath);
    const actual = await realpath(resolved);
    ownedPaths[key] = assertOwnedPath(label, actual, scratchRealPath);
  }
  if (path.resolve(os.tmpdir()) !== ownedPaths.nodeTmpPath ||
      path.resolve(process.env.TMPDIR || '') !== ownedPaths.nodeTmpPath)
    throw Error('Node and Miniflare must use the per-run owned runtime temp root');

  const [{unstable_readConfig}, miniflare] = await Promise.all([
    import(pathToFileURL(wranglerCli).href),
    import(pathToFileURL(miniflareEntry).href),
  ]);
  if (typeof unstable_readConfig !== 'function')
    throw Error('pinned Wrangler does not export unstable_readConfig');
  const config = unstable_readConfig({config: configPath});
  const bindings = config.durable_objects?.bindings || [];
  if (bindings.length !== 1) throw Error('room relay config must declare exactly one Durable Object binding');
  const binding = bindings[0];
  const exportConfig = config.exports?.[binding.class_name];
  if (binding.name !== 'ROOMS' || exportConfig?.type !== 'durable-object' || exportConfig.storage !== 'sqlite')
    throw Error('room relay config binding and SQLite Durable Object export do not match');
  if (!config.main || !config.compatibility_date || !config.name)
    throw Error('room relay config is missing its module entry, name, or compatibility date');

  const v4Options = buildRoomRelayMiniflareOptions({
    config, root,
    port: Number(process.env.MELEE_ROOM_RELAY_PORT),
    inspectorPort: Number(process.env.MELEE_ROOM_RELAY_INSPECTOR_PORT),
    resourcePersistencePath: ownedPaths.resourcePersistencePath,
    resourceTmpPath: ownedPaths.resourceTmpPath,
    handleUncaughtError,
    handleStructuredLogs,
  });
  const convertedOptions = miniflare.convertV4MiniflareOptions(v4Options);
  const options = miniflare.MiniflareOptionsSchema.parse(convertedOptions);
  const originBinding = options.workers?.[0]?.config?.env?.RELAY_ALLOWED_ORIGINS;
  if (originBinding?.type !== 'text' || originBinding.value !== config.vars?.RELAY_ALLOWED_ORIGINS)
    throw Error('Pinned Miniflare conversion did not retain the configured relay Origin allowlist');
  const effectiveOptions = {
    workerEnv: {RELAY_ALLOWED_ORIGINS: originBinding.value},
    resourcePersistencePath: options.resourcePersistencePath,
    isolatedResourcePersistencePath: options.isolatedResourcePersistencePath,
    resourceTmpPath: options.resourceTmpPath,
    nodeTmpPath: path.resolve(os.tmpdir()),
    ownerScratch: scratchRealPath,
  };
  runtime = new miniflare.Miniflare(options);
  await runtime.ready;
  const runtimeTemporaryDirectories = await listRuntimeTempDirectories(ownedPaths.nodeTmpPath, scratchRealPath);
  emit('direct-miniflare-ready', {
    port: Number(process.env.MELEE_ROOM_RELAY_PORT),
    configPath,
    workerModule: config.main,
    workerName: config.name,
    compatibilityDate: config.compatibility_date,
    durableObjectBinding: binding.name,
    durableObjectClass: binding.class_name,
    effectiveOptions,
    runtimeTemporaryDirectories,
  });
}

process.on('message', message => {
  if (!message || typeof message !== 'object' || message.type !== 'dispose' || stopping) return;
  stopping = true;
  void (async () => {
    emit('dispose-begin');
    let exitCode = 0;
    try {
      if (!runtime) throw Error('Miniflare runtime is unavailable during disposal');
      await runtime.dispose();
      const [runtimeTmpState, resourceTmpEntries] = await Promise.all([
        waitForDirectoryEmpty(process.env.MELEE_ROOM_RELAY_RUNTIME_TMP, 1500),
        readdir(process.env.MELEE_ROOM_RELAY_RESOURCE_TMP),
      ]);
      emit('dispose-complete', {
        runtimeTmpEmpty: runtimeTmpState.empty,
        runtimeTmpRemaining: runtimeTmpState.entries,
        resourceTmpEmpty: resourceTmpEntries.length === 0,
        resourceTmpRemaining: resourceTmpEntries,
      });
      if (!runtimeTmpState.empty || resourceTmpEntries.length)
        throw Error('Miniflare left temporary data outside its disposal boundary');
    } catch (error) {
      emit('dispose-error', {error: serializeError(error)});
      exitCode = 1;
    }
    process.stdout.write('', () => process.exit(exitCode));
  })();
});

for (const event of ['unhandledRejection', 'uncaughtException'])
  process.on(event, error => emit(`host-${event}`, {error: serializeError(error)}));
process.on('SIGTERM', () => emit('fallback-sigterm-received'));

start().catch(error => {
  emit('bootstrap-error', {error: serializeError(error)});
  process.exitCode = 1;
});
