import assert from 'node:assert/strict';
import {installPlayerMetrics, samplePlayerMetrics} from '../scripts/player_metrics.mjs';

// A browser without PerformanceObserver must report long-task metrics as unavailable,
// rather than claiming that the unsupported API observed zero work.
const page={
  async addInitScript(script){script();},
  async evaluate(script, sinceMs){return script(sinceMs);},
};
const previous=globalThis.PerformanceObserver;
try {
  delete globalThis.PerformanceObserver;
  await installPlayerMetrics(page);
  const report=await samplePlayerMetrics(page);
  assert.equal(report.longtask_supported,false);
  assert.equal(report.longtask_observed,false);
  assert.equal(report.longtask_count,null);
  assert.equal(report.longtask_total_ms,null);
  assert.equal(report.longest_task_ms,null);
} finally {
  if(previous===undefined)delete globalThis.PerformanceObserver;
  else globalThis.PerformanceObserver=previous;
}
console.log('Player metrics unavailable long-task fields stay null');
