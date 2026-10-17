/* Private same-instance identity accessor for the Stadium C1A recreation canary.
 * Linked only by MELEE_WEB_STADIUM_C1A_DIAGNOSTIC. It retains actual Emscripten
 * closure references and emits scalar comparisons; no identity tokens are
 * synthesized or serialized. */
addToLibrary({
  $MeleeWebStadiumRecreationIdentityV1: {},
  $MeleeWebStadiumRecreationIdentityV1__postset: `
    MeleeWebStadiumRecreationIdentityV1 = (() => {
      const propertyName = "__meleeWebStadiumRecreationIdentityV1";
      const schema = "melee-web-stadium-instance-identity-v1";
      const exportNames = Object.freeze([
        "_main",
        "_melee_web_native_menu_stadium_recreation_status",
        "_melee_web_native_menu_stadium_recreation_close",
        "_melee_web_native_menu_stadium_recreation_restart",
        "_melee_web_native_menu_stadium_recreation_admit_b",
      ]);
      let baseline = null;
      function liveBindings() {
        if (typeof Module !== "object" || Module === null || globalThis.Module !== Module ||
            typeof wasmExports !== "object" || wasmExports === null ||
            typeof WebAssembly !== "object" ||
            !(wasmMemory instanceof WebAssembly.Memory) ||
            !(Module.HEAPU8 instanceof Uint8Array) ||
            Module.HEAPU8.buffer !== wasmMemory.buffer) {
          throw new Error("Stadium same-instance accessor lacks a live Emscripten instance");
        }
        const functions = Object.create(null);
        for (const name of exportNames) {
          if (typeof Module[name] !== "function") {
            throw new Error("Stadium same-instance export is unavailable: " + name);
          }
          functions[name] = Module[name];
        }
        return {module: Module, exports: wasmExports, memory: wasmMemory,
          heapBuffer: Module.HEAPU8.buffer, memoryBuffer: wasmMemory.buffer,
          memoryBytes: wasmMemory.buffer.byteLength, functions};
      }
      const api = Object.freeze({
        schema,
        capture() {
          if (baseline) throw new Error("Stadium instance identity was already captured");
          baseline = liveBindings();
          return Object.freeze({schema, captured: true,
            export_count: Object.keys(baseline.exports).length,
            memory_bytes: baseline.memoryBytes,
            heap_bytes: baseline.heapBuffer.byteLength});
        },
        compare() {
          if (!baseline) throw new Error("Stadium instance identity has not been captured");
          const current = liveBindings();
          const moduleSame = current.module === baseline.module;
          const exportsSame = current.exports === baseline.exports;
          const memorySame = current.memory === baseline.memory;
          const wrappersSame = exportNames.every(name =>
            current.functions[name] === baseline.functions[name]);
          const heapBufferSame = current.heapBuffer === baseline.heapBuffer;
          const memoryBufferSame = current.memoryBuffer === baseline.memoryBuffer;
          return Object.freeze({schema, module_same: moduleSame,
            wasm_exports_same: exportsSame, wasm_memory_same: memorySame,
            exported_wrappers_same: wrappersSame,
            heap_buffer_same: heapBufferSame,
            memory_buffer_same: memoryBufferSame,
            memory_buffer_changed: !memoryBufferSame,
            memory_bytes_before: baseline.memoryBytes,
            memory_bytes_now: current.memoryBytes,
            same_instance: moduleSame && exportsSame && memorySame && wrappersSame});
        },
      });
      if (Object.prototype.hasOwnProperty.call(Module, propertyName))
        throw new Error("Stadium instance identity property already exists");
      Object.defineProperty(Module, propertyName, {
        value: api, enumerable: false, writable: false, configurable: false,
      });
      return api;
    })();
  `,
});
