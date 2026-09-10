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
    };
    let saveQueue = Promise.resolve(false);
    let dependencyAdded = false;

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
          fs.syncfs(false, (error) => {
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
          });
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
        fs.mount(idbfs, {}, path);
        state.mounted = true;
        addDependency(dependency);
        dependencyAdded = true;
        fs.syncfs(true, (error) => {
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
            fs.syncfs(false, (clearError) => {
              if (clearError) unavailable("Render cache reset failed", clearError);
              else {
                ++state.clears;
                setState("cleared", "Optional render cache cleared before renderer startup; browser driver cache unchanged.");
              }
              finish();
            });
          } catch (clearError) {
            unavailable("Render cache reset failed", clearError);
            finish();
          }
        });
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
      });
    } catch (error) {
      console.warn(`[Melee render cache] Status reporter failed: ${describe(error)}`);
    }
    return state;
  }

  globalThis.installRuntimeCache = installRuntimeCache;
})();
