/** Functional byte equality does not admit the native replay performance gate. */
export function classifyStagingByteReplayCompletion(report) {
  const allowed = new Set(['livePipelinesCreated', 'preparationPauses', 'browserCallbackGaps']);
  if (!report || report.complete !== true || report.frames !== 600 ||
      report.metrics?.sourceFrames !== 600 || report.metrics?.sourceSteps !== 600 ||
      report.metrics?.sourceDraws !== 600 || report.metrics?.focusLost ||
      report.instrumented_timing_resumes !== 0 || !Array.isArray(report.failures)) {
    throw Error('Native replay did not complete the exact 600-input functional timeline');
  }
  const blocking = report.failures.filter(reason => !allowed.has(reason));
  if (blocking.length) throw Error(`Native replay has blocking failures: ${JSON.stringify(blocking)}`);
  return {scope: 'functional submitted-byte capture only; performance not admitted',
    excluded_performance_failures: report.failures.slice(), native_performance_pass: report.pass === true};
}
