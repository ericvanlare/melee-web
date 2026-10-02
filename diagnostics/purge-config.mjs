// Retention Worker constants live in a helper module so the Worker entrypoint
// exports only its scheduled handler, as required by the Workers runtime.
export const PURGE_BATCH_SIZE = 100;
export const PURGE_CRON = '*/30 * * * *';
export const PURGE_RUNS_PER_DAY = 48;
export const PURGE_DAILY_CAPACITY = PURGE_BATCH_SIZE * PURGE_RUNS_PER_DAY;
