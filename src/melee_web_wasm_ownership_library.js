/* Private first-pair Emscripten instance bridge.
 * Linked only by MELEE_WEB_FIRST_PAIR_AUDIO_FILE_DIAGNOSTIC. The host supplies
 * the reviewed JS accessor through mountRuntime's configureModule callback. */
addToLibrary({
  $MeleeWebWasmOwnershipPrivateV1__deps: ['$Asyncify', '$wasmTable'],
  $MeleeWebWasmOwnershipPrivateV1: {},
  $MeleeWebWasmOwnershipPrivateV1__postset: `
    MeleeWebWasmOwnershipPrivateV1 = (() => {
      const accessorPropertyName = "__meleeWebWasmOwnershipPrivateV1";
      const installerKey = "__meleeWebWasmOwnershipInstallV1";
      const schema = "melee-web-private-wasm-instance-v1";
      let state = "waiting_for_prerun";
      let failure = null;
      let writerCounter = null;
      let bindings = null;
      let installedBy = null;

      const refuse = (message) => {
        failure = String(message);
        state = "refused";
        throw new Error("Private Wasm ownership setup refused: " + failure);
      };

      function requirePreRunInstaller() {
        if (Module.instantiateWasm) {
          refuse("custom Module.instantiateWasm is unsupported");
        }
        const installer = Module[installerKey];
        if (typeof installer !== "function") {
          refuse("configureModule did not provide the reviewed accessor installer");
        }
        if (installedBy && installedBy !== installer) {
          refuse("configureModule accessor installer identity changed");
        }
        installedBy = installer;
        return installer;
      }

      function installAfterReceiveInstance() {
        if (state !== "waiting_for_prerun") refuse("preRun hook ran more than once");
        const installer = requirePreRunInstaller();
        if (typeof wasmExports !== "object" || wasmExports === null ||
            typeof Asyncify !== "object" || Asyncify === null ||
            typeof WebAssembly === "undefined" ||
            !(wasmMemory instanceof WebAssembly.Memory) ||
            !(wasmTable instanceof WebAssembly.Table)) {
          refuse("preRun did not receive the expected live Emscripten instance");
        }
        if (!(Module.HEAPU8 instanceof Uint8Array) ||
            Module.HEAPU8.buffer !== wasmMemory.buffer) {
          refuse("Module.HEAPU8 is not backed by this instance's WebAssembly.Memory");
        }
        for (const name of ["stackSave", "stackRestore", "getTempRet0", "setTempRet0"]) {
          if (typeof Module[name] !== "function") {
            refuse("private runtime helper is missing: " + name);
          }
        }
        const aliases = {
          start_unwind: { get: () => _asyncify_start_unwind, set: (value) => { _asyncify_start_unwind = value; } },
          stop_unwind: { get: () => _asyncify_stop_unwind, set: (value) => { _asyncify_stop_unwind = value; } },
          start_rewind: { get: () => _asyncify_start_rewind, set: (value) => { _asyncify_start_rewind = value; } },
          stop_rewind: { get: () => _asyncify_stop_rewind, set: (value) => { _asyncify_stop_rewind = value; } },
        };
        const getWasmExports = () => wasmExports;
        const getAsyncify = () => Asyncify;
        const getMemory = () => wasmMemory;
        const getTable = () => wasmTable;
        const result = Reflect.apply(installer, Module, [{
          module: Module, wasmExports, getWasmExports,
          asyncify: Asyncify, getAsyncify, aliases,
          isAborted: () => ABORT,
        }]);
        if (!result || typeof result.readCount !== "function" ||
            typeof result.validateBindings !== "function") {
          refuse("reviewed accessor returned an incomplete writer counter");
        }
        result.validateBindings();
        writerCounter = result;
        bindings = Object.freeze({
          schema, module: Module, wasmExports, getWasmExports,
          asyncify: Asyncify, getAsyncify,
          aliases, memory: wasmMemory, getMemory,
          table: wasmTable, getTable,
          writerCounter, isAborted: () => ABORT,
          actualTableLength: wasmTable.length,
        });
        state = "ready";
      }

      function addPreRun(callback) {
        const descriptor = Object.getOwnPropertyDescriptor(Module, "preRun");
        if (descriptor && (!Object.prototype.hasOwnProperty.call(descriptor, "value") ||
            descriptor.writable !== true)) {
          refuse("Module.preRun is not a writable data property");
        }
        const previous = Module.preRun;
        if (previous === undefined || previous === null) {
          Module.preRun = [callback];
          return;
        }
        if (typeof previous === "function") {
          Module.preRun = [previous, callback];
          return;
        }
        if (Array.isArray(previous)) {
          if (!Object.isExtensible(previous)) refuse("Module.preRun array is not extensible");
          for (let index = 0; index < previous.length; index += 1) {
            if (!Object.prototype.hasOwnProperty.call(previous, index) ||
                typeof previous[index] !== "function") {
              refuse("Module.preRun array contains an unclassified callback");
            }
          }
          previous.push(callback);
          return;
        }
        refuse("Module.preRun has an unsupported shape");
      }

      const api = Object.freeze({
        schema,
        status() { return Object.freeze({ state, failure }); },
        getBindings() {
          if (state !== "ready" || !bindings) {
            throw new Error("Private Wasm ownership bindings are unavailable: " + (failure || state));
          }
          return bindings;
        },
      });
      if (Object.prototype.hasOwnProperty.call(Module, accessorPropertyName)) {
        refuse("private Module accessor property already exists");
      }
      Object.defineProperty(Module, accessorPropertyName, {
        value: api, enumerable: false, writable: false, configurable: false,
      });
      requirePreRunInstaller();
      addPreRun(installAfterReceiveInstance);
      return api;
    })();
  `,
});
