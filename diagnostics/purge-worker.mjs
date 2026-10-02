/*
 * Optional separate Worker for bounded physical deletion.
 * Pages Functions handle requests; this scheduled Worker owns maintenance.
 * purge-wrangler.jsonc invokes this every 30 minutes. Each invocation is
 * capped at 100 rows, so 48 invocations provide 4,800 deletion slots/day.
 */
import { purgeExpiredReports } from './worker.mjs';
import { PURGE_BATCH_SIZE } from './purge-config.mjs';

export default {
  async scheduled(controller, env) {
    const scheduledTime = Number.isSafeInteger(controller?.scheduledTime) ? controller.scheduledTime : Date.now();
    await purgeExpiredReports(env, scheduledTime, PURGE_BATCH_SIZE);
  },
};
