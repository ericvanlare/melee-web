-- v1 contains only normalized, allowlisted fields.  canonical_json is the
-- normalized wire report, never the unvalidated request body.
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS diagnostic_reports (
  report_id TEXT PRIMARY KEY NOT NULL CHECK (length(report_id) = 64),
  canonical_hash TEXT NOT NULL CHECK (length(canonical_hash) = 64),
  environment TEXT NOT NULL CHECK (environment IN ('staging', 'production')),
  origin TEXT NOT NULL,
  source_commit TEXT NOT NULL CHECK (length(source_commit) = 40),
  runtime_hash TEXT NOT NULL CHECK (length(runtime_hash) = 16),
  build_profile TEXT NOT NULL CHECK (build_profile IN ('player', 'audio-preview', 'audio-player')),
  session_id TEXT NOT NULL CHECK (length(session_id) BETWEEN 9 AND 72),
  incident_id TEXT NOT NULL CHECK (length(incident_id) BETWEEN 10 AND 96),
  reason TEXT NOT NULL CHECK (reason IN (
    'simulation_debt', 'audio_debt', 'nonfinite_clock', 'runtime_failure',
    'manual_pause', 'manual_resume', 'render_preparation', 'clock_regression',
    'scheduled_pause'
  )),
  received_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL,
  bytes INTEGER NOT NULL CHECK (bytes BETWEEN 1 AND 65536),
  canonical_json TEXT NOT NULL,
  UNIQUE (environment, session_id, incident_id)
);

CREATE INDEX IF NOT EXISTS diagnostic_reports_received_idx
  ON diagnostic_reports (environment, received_at DESC);
CREATE INDEX IF NOT EXISTS diagnostic_reports_build_idx
  ON diagnostic_reports (environment, source_commit, runtime_hash, build_profile, received_at DESC);
CREATE INDEX IF NOT EXISTS diagnostic_reports_reason_idx
  ON diagnostic_reports (reason, received_at DESC);
CREATE INDEX IF NOT EXISTS diagnostic_reports_expiry_idx
  ON diagnostic_reports (expires_at);

-- Protect the retained D1 volume independently of request rate.  The trigger
-- is evaluated inside SQLite's write transaction, so concurrent accepted
-- reports cannot pass this bound together.
CREATE TRIGGER IF NOT EXISTS diagnostic_reports_retention_guard
BEFORE INSERT ON diagnostic_reports
WHEN (SELECT COUNT(*) FROM diagnostic_reports WHERE environment = NEW.environment) >= 10000
  OR (SELECT COALESCE(SUM(bytes), 0) FROM diagnostic_reports WHERE environment = NEW.environment) + NEW.bytes > 536870912
BEGIN
  SELECT RAISE(ABORT, 'retention_budget');
END;

CREATE TABLE IF NOT EXISTS diagnostic_daily_usage (
  environment TEXT NOT NULL CHECK (environment IN ('staging', 'production')),
  day_start INTEGER NOT NULL,
  report_count INTEGER NOT NULL CHECK (report_count >= 0),
  byte_count INTEGER NOT NULL CHECK (byte_count >= 0),
  PRIMARY KEY (environment, day_start)
);

-- This is a coarse per-environment abuse cap.  D1 transactions make it
-- bounded and deterministic without storing client IPs or other metadata.
CREATE TABLE IF NOT EXISTS diagnostic_rate_buckets (
  bucket_key TEXT PRIMARY KEY NOT NULL,
  window_start INTEGER NOT NULL,
  count INTEGER NOT NULL CHECK (count >= 0)
);
