/** Player-experience metrics for the smoke harness. See docs/PLAYER_EXPERIENCE_METRICS.md.
 * Boundary wall times and main-thread long tasks observed by the page. These
 * describe what a player waits through on one machine; they are never accuracy,
 * source-timing or performance-admission evidence.
 */
export const PLAYER_METRICS_SCHEMA='melee-web-player-metrics-v1';

/** Install before the first navigation; the init script reruns on every load. */
export async function installPlayerMetrics(page) {
  await page.addInitScript(()=>{
    const tasks=[];
    Object.defineProperty(globalThis,'__meleePlayerMetrics',{value:{tasks},configurable:false});
    try {
      if(PerformanceObserver.supportedEntryTypes?.includes('longtask')) {
        new PerformanceObserver(list=>{
          for(const entry of list.getEntries())if(tasks.length<10000)tasks.push([entry.startTime,entry.duration]);
        }).observe({type:'longtask',buffered:true});
      }
    } catch {}
  });
}

/** Summarize page-side observations since the current document loaded. */
export async function samplePlayerMetrics(page, sinceMs=0) {
  return page.evaluate(sinceMs=>{
    const state=globalThis.__meleePlayerMetrics;
    const tasks=state?(state.tasks.filter(([start])=>start>=sinceMs)):null;
    let memory=null;
    try {
      const module=globalThis.Module, ptr=module?._melee_web_native_menu_memory?.();
      if(ptr&&module.UTF8ToString)memory=JSON.parse(module.UTF8ToString(ptr));
    } catch {}
    return {
      page_now_ms:performance.now(),
      longtask_supported:!!state&&PerformanceObserver.supportedEntryTypes?.includes('longtask'),
      longtask_count:tasks?tasks.length:null,
      longtask_total_ms:tasks?Math.round(tasks.reduce((sum,[,duration])=>sum+duration,0)):null,
      longest_task_ms:tasks?Math.round(tasks.reduce((max,[,duration])=>Math.max(max,duration),0)):null,
      wasm_heap_bytes:memory?.wasm_heap_bytes??null,
      allocator_live_bytes:memory?.allocator_live_bytes??null,
      js_heap_used_bytes:performance.memory?.usedJSHeapSize??null,
    };
  },sinceMs);
}

/** Time one disc import from the file choice to running original CSS. */
export async function measureDiscToCss(page, enterCss) {
  const before=await page.evaluate(()=>performance.now());
  const started=Date.now();
  const entry=await enterCss();
  const disc_to_css_ms=Date.now()-started;
  return {entry,disc_to_css_ms,...await samplePlayerMetrics(page,before)};
}
