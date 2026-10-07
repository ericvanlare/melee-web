import path from 'node:path';

/** Build the pinned Miniflare V4 input for the room relay Worker. */
export function buildRoomRelayMiniflareOptions({config, root, port, inspectorPort,
  resourcePersistencePath, resourceTmpPath, handleUncaughtError, handleStructuredLogs}) {
  const binding = config.durable_objects.bindings[0];
  const exportConfig = config.exports[binding.class_name];
  return {
    host: '127.0.0.1',
    port,
    inspectorPort,
    rootPath: root,
    resourcePersistencePath,
    resourceTmpPath,
    handleUncaughtError,
    handleStructuredLogs,
    workers: [{
      name: config.name,
      modules: true,
      scriptPath: path.resolve(root, config.main),
      compatibilityDate: config.compatibility_date,
      bindings: {...(config.vars || {})},
      durableObjects: {
        [binding.name]: {
          className: binding.class_name,
          useSQLite: exportConfig.storage === 'sqlite',
        },
      },
    }],
  };
}
