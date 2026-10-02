/*
 * Copy this adapter to generated functions/api/diagnostics/[[report]].js.
 * The optional catch-all route is required for authenticated GET/DELETE by
 * report id; the sibling diagnostics.js adapter handles the collection root.
 */
import { fetch as diagnosticsFetch } from '../worker.mjs';

export async function onRequest(context) {
  return diagnosticsFetch(context.request, context.env, context);
}
