/* Read-only workload observation: no await between native owner getters.
 * Keep this function standalone so Playwright serializes the actual reader. */
export function readRuntimeOwnerSnapshot({includeMatch=true,includeResultsTrace=false}={}) {
  const module=globalThis.Module;
  const state={
    message:module?._melee_web_native_menu_message?module.UTF8ToString(module._melee_web_native_menu_message()):null,
    phase:module?._melee_web_native_menu_phase?.()??null,
    running:module?._melee_web_native_menu_running?.()??null,
    status:document.querySelector('#status')?.textContent||'',
    pause_present:!!document.querySelector('#pause'),
    pause_disabled:document.querySelector('#pause')?.disabled??null,
    error:document.querySelector('#status')?.dataset.runtimeError||
      (document.querySelector('#error-dialog[open]')?
        document.querySelector('#error')?.textContent||'Application error':null),
    diagnostics:module?._melee_web_native_menu_diagnostics?
      module.UTF8ToString(module._melee_web_native_menu_diagnostics()):'',
  };
  const snapshot={state};
  if(includeMatch)snapshot.match=JSON.parse(module.UTF8ToString(module._melee_web_native_menu_match_observe()));
  if(includeResultsTrace){
    const pointer=typeof module?._melee_web_native_menu_results_pad_trace==='function'?
      module._melee_web_native_menu_results_pad_trace():0;
    snapshot.trace=pointer?JSON.parse(module.UTF8ToString(pointer)):null;
  }
  return snapshot;
}

export async function observeRuntimeOwner(page,retention,label,options) {
  const snapshot=await page.evaluate(readRuntimeOwnerSnapshot,options);
  // Keep the exact failing owner pair, not a later failure-handler re-read.
  retention.latestOwnerObservation={label,...snapshot};
  return snapshot;
}
