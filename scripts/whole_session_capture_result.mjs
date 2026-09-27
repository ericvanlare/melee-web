/** Final transport gates only; exact game-state comparison is a separate step. */
export const REQUIRED_SESSION_DOWNLOADS = Object.freeze([
  'retail-port.jsonl', 'retail-browser-report.json',
]);

export function finalizeSessionCapture(report) {
  const failures = [];
  if (!report.phases?.some(row => row.name === 'whole-session-replay' && row.result === 'pass'))
    failures.push('Whole-session replay did not complete successfully');
  if (report.browser_report?.complete !== true || report.browser_report?.pass !== true ||
      !Array.isArray(report.browser_report.failures) || report.browser_report.failures.length ||
      (report.browser_report.errors !== undefined &&
       (!Array.isArray(report.browser_report.errors) || report.browser_report.errors.length)))
    failures.push('Browser replay report is incomplete or failed');
  if (report.first_error || report.failure || report.first_mismatch)
    failures.push('A fatal harness diagnostic was recorded');
  if (report.browser_errors?.length)
    failures.push('Browser errors were recorded');
  if (report.unexpected_requests?.length)
    failures.push('Unexpected non-GET requests were recorded');
  for (const key of ['download_error', 'cpu_download_error', 'owner_trace_error',
    'source_allocation_trace_error', 'page_dump_error', 'screenshot_error', 'close_error']) {
    if (report[key]) failures.push(`${key}: ${report[key]}`);
  }
  for (const name of REQUIRED_SESSION_DOWNLOADS) {
    const rows = (report.saved_downloads || []).filter(row => row.name === name);
    if (rows.length !== 1 || !Number.isSafeInteger(rows[0].bytes) || rows[0].bytes <= 0 ||
        !/^[a-f0-9]{64}$/.test(rows[0].sha256 || ''))
      failures.push(`Required artifact was not exported exactly once with nonempty hashed bytes: ${name}`);
  }
  if (report.deliberate_prefix_stop)
    failures.push('Deliberate prefix stop is incomplete session evidence');
  report.finalization_failures = failures;
  report.result = report.deliberate_prefix_stop ? 'incomplete' : failures.length ? 'fail' : 'pass';
  return report.result === 'pass' ? 0 : 1;
}
