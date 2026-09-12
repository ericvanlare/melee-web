// Optional browser persistence for Aurora's shader/pipeline cache.
//
// runtime.html loads this classic script before its inline Module setup.  The
// inline setup then calls installRuntimeCache(Module, report) before loading
// gameplay_browser.js.  Only /melee-render-cache is mounted here; imported
// game archives stay in the page and are never placed in IDBFS.
(function () {
  const path = "/melee-render-cache";
  const dependency = "melee-render-cache-populate";

  function installRuntimeCache(module, report, options = {}) {
    if (!module || typeof module !== "object") {
      throw new TypeError("installRuntimeCache requires the Emscripten Module object");
    }
    const notify = typeof report === "function" ? report : function () {};
    const clearOnLoad = options.clearOnLoad === true;
    const syncClock = typeof options.now === "function"
      ? options.now
      : () => (typeof performance !== "undefined" && typeof performance.now === "function"
        ? performance.now() : null);
    const syncEventReport = typeof options.onSync === "function" ? options.onSync : null;
    const state = {
      state: "initializing",
      mounted: false,
      populated: false,
      saves: 0,
      clears: 0,
      dirty: false,
      lastSaveMs: null,
      fileBytes: 0,
      message: "Preparing optional render cache storage.",
      syncDiagnostics: {
        enabled: options.syncDiagnostics === true,
        installed: false,
        calls: 0,
        pending: 0,
        errors: 0,
      },
    };
    let saveQueue = Promise.resolve(false);
    let dependencyAdded = false;
    let syncIntent = null;
    let syncMountType = null;
    let syncTargetMount = null;
    let syncOriginalType = null;
    let syncOriginal = null;
    let syncWrapped = null;
    let nextSyncId = 1;

    function syncNow() {
      try {
        const raw = syncClock();
        const value = raw === null || raw === undefined ? NaN : Number(raw);
        return Number.isFinite(value) ? value : null;
      } catch (_) {
        return null;
      }
    }

    function reportSync(event) {
      if (!syncEventReport) return;
      try {
        syncEventReport(event);
      } catch (error) {
        // Diagnostic reporting must never change the cache call's behavior.
        console.warn(`[Melee render cache] Sync diagnostic reporter failed: ${describe(error)}`);
      }
    }

    function describeSyncError(error) {
      if (!error) return null;
      const value = {
        name: error && error.name ? String(error.name) : null,
        message: describe(error),
      };
      return value;
    }

    function finishSync(event, error) {
      if (!event || event.finished) return;
      event.finished = true;
      const ended = syncNow();
      const failed = !!error;
      state.syncDiagnostics.pending = Math.max(0, state.syncDiagnostics.pending - 1);
      if (failed) ++state.syncDiagnostics.errors;
      reportSync({
        id: event.id,
        phase: "completion",
        status: failed ? "error" : "completed",
        source: event.source,
        operation: event.operation,
        started: event.started,
        ended,
        duration_ms: event.started !== null && ended !== null ? Math.max(0, ended - event.started) : null,
        error: describeSyncError(error),
        clock: "performance.now",
        native_context: "unknown",
        stack: event.stack,
      });
    }

    function resolveSyncType(fs, mounted) {
      const mount = mounted?.mount || (mounted?.type ? mounted
        : (typeof fs?.lookupPath === "function" ? fs.lookupPath(path)?.node?.mount : null));
      const type = mounted && typeof mounted.syncfs === "function" ? mounted : (mount?.type || null);
      return type && typeof type.syncfs === "function" ? {type, mount} : null;
    }

    function unwrapSyncInstrumentation() {
      if (syncTargetMount && syncTargetMount.type === syncMountType) {
        try { syncTargetMount.type = syncOriginalType; } catch (_) { /* optional diagnostics */ }
      }
      syncMountType = null;
      syncTargetMount = null;
      syncOriginalType = null;
      syncOriginal = null;
      syncWrapped = null;
      state.syncDiagnostics.installed = false;
    }

    function installSyncInstrumentation(fs, mounted) {
      if (!state.syncDiagnostics.enabled || state.syncDiagnostics.installed) return false;
      const resolved = resolveSyncType(fs, mounted);
      if (!resolved?.mount || resolved.mount.type !== resolved.type) return false;
      const {type, mount} = resolved;
      const original = type.syncfs;
      if (original.__meleeRenderCacheSyncInstrumentation) return false;
      const wrapped = function (...args) {
        // Keep the disabled path as the original call, including its receiver,
        // argument list, return value, and synchronous errors.
        // Only this mount receives an adapter. Preserve the receiver the
        // original type would have received; custom call receivers pass through.
        const receiver = this === syncMountType ? type : this;
        if (!state.syncDiagnostics.enabled || args[0] !== syncTargetMount) {
          return Reflect.apply(original, receiver, args);
        }
        const operation = syncIntent;
        const event = {
          id: `cache-sync-${nextSyncId++}`,
          source: operation ? "explicit" : "unknown",
          operation: operation || "unknown",
          started: syncNow(),
          stack: (() => {
            try {
              const value = new Error().stack;
              return value ? String(value).slice(0, 4096) : null;
            } catch (_) {
              return null;
            }
          })(),
          finished: false,
        };
        ++state.syncDiagnostics.calls;
        ++state.syncDiagnostics.pending;
        reportSync({
          id: event.id,
          phase: "start",
          status: "pending",
          source: event.source,
          operation: event.operation,
          started: event.started,
          ended: null,
          duration_ms: null,
          error: null,
          clock: "performance.now",
          native_context: "unknown",
          stack: event.stack,
        });

        const callbackIndex = args.length - 1;
        const callback = callbackIndex >= 0 && typeof args[callbackIndex] === "function"
          ? args[callbackIndex] : null;
        const forwarded = callback ? args.slice() : args;
        if (callback) {
          forwarded[callbackIndex] = function (...callbackArgs) {
            // Finish before forwarding so callback observers see a completed
            // record, while callback receiver/arguments/errors stay intact.
            try { finishSync(event, callbackArgs[0]); } catch (_) { /* diagnostics only */ }
            return Reflect.apply(callback, this, callbackArgs);
          };
        }
        try {
          const result = Reflect.apply(original, receiver, forwarded);
          // A syncfs implementation without a callback is still a completed
          // call from the instrumenter's perspective.
          if (!callback) finishSync(event, null);
          return result;
        } catch (error) {
          try { finishSync(event, error); } catch (_) { /* diagnostics only */ }
          throw error;
        }
      };
      try {
        Object.defineProperty(wrapped, "__meleeRenderCacheSyncInstrumentation", {value: true});
        const adapter = Object.create(type);
        Object.defineProperty(adapter, "syncfs", {value: wrapped, configurable: true});
        mount.type = adapter;
        if (mount.type !== adapter) return false;
        syncMountType = adapter;
      } catch (_) {
        return false;
      }
      syncTargetMount = mount;
      syncOriginalType = type;
      syncOriginal = original;
      syncWrapped = wrapped;
      state.syncDiagnostics.installed = true;
      return true;
    }

    function withSyncIntent(operation, invoke) {
      const previous = syncIntent;
      syncIntent = operation;
      try {
        return invoke();
      } finally {
        syncIntent = previous;
      }
    }

    function describe(error) {
      if (error instanceof Error && error.message) return error.message;
      return String(error && error.message ? error.message : error);
    }

    function setState(next, message) {
      state.state = next;
      state.message = message;
      const payload = {
        state: next,
        message,
        mounted: state.mounted,
        populated: state.populated,
        saves: state.saves,
        clears: state.clears,
        dirty: state.dirty,
        lastSaveMs: state.lastSaveMs,
        fileBytes: state.fileBytes,
        sync_diagnostics: {...state.syncDiagnostics},
      };
      try {
        notify(payload);
      } catch (error) {
        console.warn(`[Melee render cache] Status reporter failed: ${describe(error)}`);
      }
    }

    function refreshFileBytes(fs) {
      state.fileBytes = 0;
      try {
        for (const name of fs.readdir(path)) {
          if (name === "." || name === "..") continue;
          const entry = `${path}/${name}`;
          const stat = fs.stat(entry);
          if (!fs.isDir(stat.mode)) state.fileBytes += Number(stat.size) || 0;
        }
      } catch (_) {
        // Size is diagnostic only; mounting and cache use may continue.
      }
    }

    function clearMountedFiles(fs) {
      for (const name of fs.readdir(path)) {
        if (name === "." || name === "..") continue;
        const entry = `${path}/${name}`;
        const stat = fs.stat(entry);
        if (fs.isDir(stat.mode)) throw new Error(`Unexpected render-cache directory: ${name}`);
        fs.unlink(entry);
      }
      state.fileBytes = 0;
    }

    function unavailable(message, error) {
      state.mounted = false;
      state.populated = false;
      unwrapSyncInstrumentation();
      const detail = error ? `${message} (${describe(error)})` : message;
      // Cache persistence is optional.  Keep the runtime usable when storage
      // is blocked by privacy mode, quota, or an older browser.
      setState("unavailable", detail);
      console.warn(`[Melee render cache] ${detail}; continuing uncached.`);
    }

    function saveOnce() {
      return new Promise((resolve) => {
        if (!state.mounted || !state.populated) {
          resolve(false);
          return;
        }
        const startedAt = Date.now();
        setState("saving", "Persisting optional render cache storage.");
        try {
          const fs = module.FS || (typeof FS !== "undefined" ? FS : null);
          if (!fs || typeof fs.syncfs !== "function") {
            unavailable("IDBFS runtime support disappeared before save");
            resolve(false);
            return;
          }
          withSyncIntent("save", () => fs.syncfs(false, (error) => {
            if (error) {
              unavailable("Render cache save failed", error);
              resolve(false);
              return;
            }
            ++state.saves;
            state.dirty = false;
            state.lastSaveMs = Date.now() - startedAt;
            refreshFileBytes(fs);
            setState("saved", "Optional render cache persisted.");
            resolve(true);
          }));
        } catch (error) {
          unavailable("Render cache save failed", error);
          resolve(false);
        }
      });
    }

    // Calls are queued instead of overlapping IDBFS transactions.  The root
    // page calls this only at a paused/unloaded lifetime boundary, but the
    // queue also makes repeated UI requests safe.
    module.saveRuntimeCache = function () {
      saveQueue = saveQueue.catch(() => false).then(saveOnce);
      return saveQueue;
    };

    module.setRuntimeCacheSyncDiagnostics = function (enabled) {
      state.syncDiagnostics.enabled = enabled === true;
      if (!state.syncDiagnostics.enabled) {
        unwrapSyncInstrumentation();
      } else if (state.mounted) {
        const fs = module.FS || (typeof FS !== "undefined" ? FS : null);
        installSyncInstrumentation(fs, null);
      }
      setState(state.state, state.message);
      return state.syncDiagnostics.enabled;
    };

    // Pipeline discovery happens during scene preparation and live first use.
    // IDBFS sync serializes the SQLite database on the browser thread, so even
    // an idle callback with a timeout can interrupt a later gameplay frame.
    // Record the dirty state here and flush only after native scene ownership
    // has been torn down by unloadAndSave().
    module.markRuntimeCacheDirty = function () {
      if (!state.mounted || !state.populated) return false;
      state.dirty = true;
      setState("dirty", "Optional render cache has pending changes; it will persist at unload.");
      return true;
    };

    function populate() {
      const fs = module.FS || (typeof FS !== "undefined" ? FS : null);
      const idbfs = module.IDBFS || (typeof IDBFS !== "undefined" ? IDBFS : null);
      const addDependency = module.addRunDependency ||
        (typeof addRunDependency === "function" ? addRunDependency : null);
      const removeDependency = module.removeRunDependency ||
        (typeof removeRunDependency === "function" ? removeRunDependency : null);
      if (!fs || !idbfs || !addDependency || !removeDependency) {
        unavailable("IDBFS runtime support is unavailable");
        return;
      }
      try {
        fs.mkdirTree(path);
        const mounted = fs.mount(idbfs, {}, path);
        state.mounted = true;
        installSyncInstrumentation(fs, mounted);
        addDependency(dependency);
        dependencyAdded = true;
        withSyncIntent("populate", () => fs.syncfs(true, (error) => {
          const finish = () => {
            if (dependencyAdded) {
              dependencyAdded = false;
              removeDependency(dependency);
            }
          };
          if (error) {
            unavailable("Render cache storage could not be populated", error);
            finish();
            return;
          }
          state.populated = true;
          if (!clearOnLoad) {
            refreshFileBytes(fs);
            setState("ready", "Optional render cache storage is ready.");
            finish();
            return;
          }
          // This happens while the run dependency still prevents Aurora from
          // opening its SQLite cache. Removing an open database later would
          // race its writer and would not evict in-memory pipelines.
          try {
            clearMountedFiles(fs);
            withSyncIntent("clear", () => fs.syncfs(false, (clearError) => {
              if (clearError) unavailable("Render cache reset failed", clearError);
              else {
                ++state.clears;
                setState("cleared", "Optional render cache cleared before renderer startup; browser driver cache unchanged.");
              }
              finish();
            }));
          } catch (clearError) {
            unavailable("Render cache reset failed", clearError);
            finish();
          }
        }));
      } catch (error) {
        unavailable("Render cache storage could not be mounted", error);
        if (dependencyAdded) {
          dependencyAdded = false;
          removeDependency(dependency);
        }
      }
    }

    const preRun = module.preRun;
    if (preRun === undefined) module.preRun = [populate];
    else if (Array.isArray(preRun)) preRun.push(populate);
    else module.preRun = [preRun, populate];
    module.runtimeCacheState = state;
    try {
      notify({
        state: state.state,
        message: state.message,
        mounted: state.mounted,
        populated: state.populated,
        saves: state.saves,
        clears: state.clears,
        dirty: state.dirty,
        lastSaveMs: state.lastSaveMs,
        fileBytes: state.fileBytes,
        sync_diagnostics: {...state.syncDiagnostics},
      });
    } catch (error) {
      console.warn(`[Melee render cache] Status reporter failed: ${describe(error)}`);
    }
    return state;
  }

  globalThis.installRuntimeCache = installRuntimeCache;
})();
