import path from 'node:path';
import {pathToFileURL} from 'node:url';

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

const handleUncaughtError = error => emit('runtime-uncaught-error', {error: serializeError(error)});
const handleStructuredLogs = log => emit('structured-log', {log});

async function start() {
  const root = process.env.MELEE_ROOM_RELAY_ROOT;
  const configPath = process.env.MELEE_ROOM_RELAY_CONFIG;
  const wranglerCli = process.env.MELEE_ROOM_RELAY_WRANGLER_CLI;
  const miniflareEntry = process.env.MELEE_ROOM_RELAY_MINIFLARE_ENTRY;
  const persistPath = process.env.MELEE_ROOM_RELAY_PERSIST;
  if (![root, configPath, wranglerCli, miniflareEntry, persistPath].every(Boolean))
    throw Error('direct Worker runtime is missing a required owned path');

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

  const options = miniflare.convertV4MiniflareOptions({
    host: '127.0.0.1',
    port: Number(process.env.MELEE_ROOM_RELAY_PORT),
    inspectorPort: Number(process.env.MELEE_ROOM_RELAY_INSPECTOR_PORT),
    rootPath: root,
    durableObjectsPersist: persistPath,
    handleUncaughtError,
    handleStructuredLogs,
    workers: [{
      name: config.name,
      modules: true,
      scriptPath: path.resolve(config.main),
      compatibilityDate: config.compatibility_date,
      vars: config.vars || {},
      durableObjects: {
        [binding.name]: {
          className: binding.class_name,
          useSQLite: exportConfig.storage === 'sqlite',
        },
      },
    }],
  });
  miniflare.MiniflareOptionsSchema.parse(options);
  runtime = new miniflare.Miniflare(options);
  await runtime.ready;
  emit('direct-miniflare-ready', {
    port: Number(process.env.MELEE_ROOM_RELAY_PORT),
    configPath,
    workerModule: config.main,
    workerName: config.name,
    compatibilityDate: config.compatibility_date,
    durableObjectBinding: binding.name,
    durableObjectClass: binding.class_name,
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
      emit('dispose-complete');
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
