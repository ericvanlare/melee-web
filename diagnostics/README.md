# Diagnostics backend prerequisite

This directory is an isolated Pages/Worker backend prerequisite. It does not
fall back to static assets; the parent packaging change must expose
`worker.mjs` through the exact `/api/diagnostics` route and keep every other
request on the existing Pages static path.

`schema.mjs` owns the v1 wire allowlist. The report has fixed keys for a
release identity, exact staging/production origin, coarse client and
capability metadata, one structured incident, exactly 20 history columns with
at most 100 rows (missing cells remain `null`), at most 32 pre-events and 24
post-events, and two incomplete/persistence flags. Event types preserve the
safe local recorder fields for native, lifecycle, audio, and long-task events;
incident reasons and scene/clock-owner codes are enumerated. There are no
stack traces, file names, disc names, input values, memory values, raw
user-agent strings, IP addresses, or arbitrary strings.

`worker.mjs` exports a unit-testable `fetch(request, env, ctx)` function:

* Public `POST /api/diagnostics` requires JSON, an exact same-origin HTTPS
  `Origin`, an exact known host, a valid release from
  `DIAGNOSTICS_ALLOWED_RELEASES`, and a body no larger than 64 KiB of UTF-8.
* The D1 database stores canonical normalized JSON, metadata, and a 30-day
  expiry. The `(environment, session_id, incident_id)` key returns `409` when
  content changes and `200` for an exact retry. A bounded D1 bucket supplies a
  per-environment minute cap without storing client network metadata. A second
  transactional daily budget allows at most 1,000 new reports and 16 MiB per
  environment per UTC day; a SQLite trigger bounds retained rows to 10,000 and
  retained bytes to 512 MiB. Idempotent retries do not consume the daily
  budget.
* Authenticated `GET` lists or fetches reports and authenticated `DELETE`
  removes one report or a bounded time range. No unauthenticated listing is
  available. `DIAGNOSTICS_ADMIN_TOKEN` is a Wrangler secret and is never sent
  to the browser.

Pages packaging uses the exact root adapter plus an optional catch-all adapter:
`functions/api/diagnostics.js` handles the collection route and
`functions/api/diagnostics/[[report]].js` handles report-id GET/DELETE routes.
Both import the shared helper files from `functions/api/`; `_routes.json` keeps
all other requests on the existing static path.

Known hosts are `staging.webmelee.gg`, `webmelee-staging.pages.dev`,
`[8-hex].webmelee-staging.pages.dev`, `webmelee.gg`, `www.webmelee.gg`,
`webmelee.pages.dev`, and `[8-hex].webmelee.pages.dev`. The guessed
`staging.webmelee.pages.dev` alias is intentionally not accepted.

`wrangler.jsonc` is a configuration template only. Replace the D1 name/id and
configure the per-environment release allowlist during the parent deployment
integration. Put the admin token in Wrangler secrets; do not add it to this
file. Use an admin token with at least 16 characters. Redacted Pages metadata
may expose only `{"type":"secret_text"}`; operators must still configure a
token meeting that minimum. Apply `migrations/0001_diagnostics.sql` through the selected D1 binding
as a separate deployment operation.

The Pages request path performs bounded lazy expiry, but strict physical
deletion requires a separately deployed scheduled Worker. Reports become
unavailable to authenticated queries exactly at `expires_at`, even if physical
deletion is delayed. Substitute the D1 name/id in `purge-wrangler.jsonc` and
deploy `purge-worker.mjs` with the adjacent `purge-config.mjs` helper and its
every-30-minutes Cron Trigger. Each run removes at most 100 expired reports
and old daily-budget rows. Forty-eight
runs provide 4,800 deletion slots per UTC day, exceeding the configured
2,000-report combined daily intake cap by 2.4x. This is capacity headroom, not
an unconditional deletion SLA: missed Cron invocations, D1 errors, or a
backlog can extend physical-deletion lag while query expiry remains enforced.
Keep that Worker separate from the Pages upload and do not expose a public
route for it.

The scheduled Worker performs up to 48 invocations/day, each with one bounded
report-delete statement (up to 100 rows) and one daily-budget cleanup. The
retained database remains capped at 10,000 rows and 512 MiB per environment.
If the cadence causes an unexpected operational or cost issue, remove the Cron
Trigger and redeploy the Worker configuration; expired rows remain hidden by
the query boundary while physical deletion is paused. Restore the 30-minute
trigger after investigation. Exact D1 billing depends on the Cloudflare plan.

The focused test uses Node's built-in SQLite adapter to execute the migration
and exercises parser duplicate detection, host/origin/release checks, the
64 KiB and UTF-8 boundary, D1 dedup/conflict behavior, rate limiting, and
authenticated query/deletion:

```sh
python3 scripts/agent_workspace.py run -- node --test diagnostics/tests/worker_test.mjs
```

The retained local Pages/Miniflare integration also validates the real D1
migration, route/static isolation, authenticated administration, caps, malformed
and oversized bodies, the five-second total body deadline, and the scheduled Worker. Run it with:

```sh
python3 scripts/agent_workspace.py run -- python3 -m unittest diagnostics/tests/pages_dev_integration_test.py -v
```
