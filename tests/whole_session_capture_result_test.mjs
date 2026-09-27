import assert from 'node:assert/strict';
import {finalizeSessionCapture, REQUIRED_SESSION_DOWNLOADS} from '../scripts/whole_session_capture_result.mjs';

const clean = () => ({
  result: 'fail', first_error: null, browser_errors: [], unexpected_requests: [],
  phases: [{name: 'whole-session-replay', result: 'pass'}],
  browser_report: {complete: true, pass: true, failures: []},
  saved_downloads: REQUIRED_SESSION_DOWNLOADS.map(name => ({name, bytes: 100, sha256: 'a'.repeat(64)})),
});
let checks = 0;
function rejects(mutate, message, result = 'fail') {
  const report = clean();
  // Even an incorrectly preassigned pass cannot bypass finalization.
  report.result = 'pass';
  mutate(report);
  assert.equal(finalizeSessionCapture(report), 1);
  assert.equal(report.result, result);
  assert(report.finalization_failures.some(reason => reason.includes(message)), JSON.stringify(report));
  checks++;
}
const success = clean();
assert.equal(finalizeSessionCapture(success), 0);
assert.equal(success.result, 'pass');
assert.deepEqual(success.finalization_failures, []);
for (const kind of ['pageerror', 'console', 'http', 'requestfailed'])
  rejects(r => { r.browser_errors.push({kind, message: 'fixture'}); }, 'Browser errors');
for (const key of ['first_error', 'failure', 'first_mismatch'])
  rejects(r => { r[key] = 'fixture'; }, 'fatal harness diagnostic');
for (const key of ['download_error', 'cpu_download_error', 'owner_trace_error',
  'source_allocation_trace_error', 'page_dump_error', 'screenshot_error', 'close_error'])
  rejects(r => { r[key] = 'fixture'; }, key);
rejects(r => { r.unexpected_requests.push({method: 'POST'}); }, 'non-GET');
rejects(r => { r.phases[0].result = 'fail'; }, 'did not complete');
for (const key of ['complete', 'pass'])
  rejects(r => { r.browser_report[key] = false; }, 'incomplete or failed');
for (const key of ['failures', 'errors'])
  rejects(r => { r.browser_report[key] = ['fixture']; }, 'incomplete or failed');
for (const key of ['failures', 'errors'])
  for (const value of [{}, null, ''])
    rejects(r => { r.browser_report[key] = value; }, 'incomplete or failed');
rejects(r => { delete r.browser_report.failures; }, 'incomplete or failed');
rejects(r => { r.saved_downloads = []; }, 'Required artifact');
for (const name of REQUIRED_SESSION_DOWNLOADS) {
  rejects(r => { r.saved_downloads = r.saved_downloads.filter(row => row.name !== name); }, name);
  rejects(r => { r.saved_downloads.push(r.saved_downloads.find(row => row.name === name)); }, name);
  for (const bytes of [0, -1, 0.5, undefined])
    rejects(r => { r.saved_downloads.find(row => row.name === name).bytes = bytes; }, name);
  rejects(r => { r.saved_downloads.find(row => row.name === name).sha256 = 'invalid'; }, name);
}
rejects(r => { r.deliberate_prefix_stop = {requested_cursor: 1}; }, 'Deliberate prefix', 'incomplete');
console.log(`Whole-session capture finalization: clean pass and ${checks} rejection controls passed`);
