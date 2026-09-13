/**
 * Install a bounded observer at the Emscripten Module print boundary.
 *
 * This function is passed directly to Playwright's addInitScript, so it must
 * not close over module state outside its body. The runtime publishes Module
 * immediately before loading the generated Emscripten script; the setter
 * wraps the callbacks before that script snapshots them.
 */
export function installCpuBrowserModuleCapture() {
  const root = globalThis;
  const captureKey = '__meleeCpuModuleOutputCapture';
  if (root[captureKey]?.version === 1) return;

  const state = {
    version: 1,
    core_limit: 36004,
    timer_limit: 36003,
    core: [],
    timer: [],
    core_overflow: 0,
    timer_overflow: 0,
    capture_errors: 0,
    wrapped_modules: 0,
  };
  const wrappedModules = new WeakSet();
  const capture = (kind, value) => {
    const text = String(value);
    if (kind === 'core') {
      if (!text.startsWith('{"record":')) return;
      if (state.core.length >= state.core_limit) {
        state.core_overflow++;
        return;
      }
      state.core.push(text);
      return;
    }
    if (!text.startsWith('TIMER_AUDIT ')) return;
    if (state.timer.length >= state.timer_limit) {
      state.timer_overflow++;
      return;
    }
    state.timer.push(text.slice('TIMER_AUDIT '.length));
  };
  const wrap = (module, name, kind) => {
    const original = module[name];
    if (typeof original !== 'function') return false;
    const descriptor = Object.getOwnPropertyDescriptor(module, name);
    if (descriptor && !descriptor.configurable && !descriptor.writable) return false;
    const wrapped = function (...args) {
      try {
        capture(kind, args[0]);
      } catch {
        // A diagnostic observer cannot change the native callback's behavior.
        state.capture_errors++;
      }
      return Reflect.apply(original, this, args);
    };
    Object.defineProperty(module, name, {
      ...(descriptor || {}),
      value: wrapped,
      writable: descriptor?.writable ?? true,
      configurable: descriptor?.configurable ?? true,
      enumerable: descriptor?.enumerable ?? true,
    });
    return true;
  };
  const installModule = module => {
    if (!module || (typeof module !== 'object' && typeof module !== 'function') ||
        wrappedModules.has(module)) return;
    const wrappedPrint = wrap(module, 'print', 'core');
    const wrappedPrintErr = wrap(module, 'printErr', 'timer');
    if (wrappedPrint || wrappedPrintErr) {
      wrappedModules.add(module);
      state.wrapped_modules++;
    }
  };

  Object.defineProperty(root, captureKey, {
    configurable: false,
    enumerable: false,
    value: state,
  });

  const previous = Object.getOwnPropertyDescriptor(root, 'Module');
  let current = previous && 'value' in previous ? previous.value :
    (previous?.get ? root.Module : undefined);
  const setModule = value => {
    current = value;
    installModule(value);
  };
  if (!previous || previous.configurable) {
    Object.defineProperty(root, 'Module', {
      configurable: true,
      enumerable: previous?.enumerable ?? true,
      get: () => current,
      set: setModule,
    });
  }
  installModule(current);
}
