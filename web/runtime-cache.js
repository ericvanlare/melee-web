// Optional browser persistence for Aurora's shader/pipeline cache.
//
// runtime.html loads this classic script before its inline Module setup.  The
// inline setup then calls installRuntimeCache(Module, report) before loading
// gameplay_browser.js.  Only /melee-render-cache is mounted here; imported
// game archives stay in the page and are never placed in IDBFS.
(function () {
  const path = "/melee-render-cache";
  const dependency = "melee-render-cache-populate";

  function installRuntimeCache(module, report) {
    if (!module || typeof module !== "object") {
      throw new TypeError("installRuntimeCache requires the Emscripten Module object");
    }
    const notify = typeof report === "function" ? report : function () {};
    const state = {
      state: "initializing",
      mounted: false,
      populated: false,
      saves: 0,
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
      };
      try {
        notify(payload);
      } catch (error) {
        console.warn(`[Melee render cache] Status reporter failed: ${describe(error)}`);
      }
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
          try {
            if (error) {
              unavailable("Render cache storage could not be populated", error);
            } else {
              state.populated = true;
              setState("ready", "Optional render cache storage is ready.");
            }
          } finally {
            if (dependencyAdded) {
              dependencyAdded = false;
              removeDependency(dependency);
            }
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
      });
    } catch (error) {
      console.warn(`[Melee render cache] Status reporter failed: ${describe(error)}`);
    }
    return state;
  }

  globalThis.installRuntimeCache = installRuntimeCache;
})();
