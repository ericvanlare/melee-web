/*
 * Copy this adapter, worker.mjs, and schema.mjs together to the generated
 * Pages output at functions/api/diagnostics.js, functions/api/worker.mjs, and
 * functions/api/schema.mjs. The parent package owns that copy operation.
 * Keeping imports relative makes the generated Function self-contained.
 */
import { fetch as diagnosticsFetch } from './worker.mjs';

export async function onRequest(context) {
  return diagnosticsFetch(context.request, context.env, context);
}
