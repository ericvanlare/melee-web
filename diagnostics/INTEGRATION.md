# Pages integration contract

The current release tools produce a deliberately closed static inventory. The
packaging and staging tools make the following narrow additions for this
backend:

1. The Pages Functions tree must be at the Pages project root, alongside the
   static directory. For `pages dev public`, that means `functions/` is a
   sibling of `public/`; for direct upload, the temporary upload root contains
   the static files, `functions/`, and `_routes.json`. Do not place Functions
   under `public/dist` or another static asset directory. Copy
   `pages-function-adapter.mjs` to generated output as
   `functions/api/diagnostics.js`, and copy
   `pages-function-catchall-adapter.mjs` as
   `functions/api/diagnostics/[[report]].js`. Copy `worker.mjs` and `schema.mjs`
   to `functions/api/` so both adapters' relative imports resolve after upload.
   The exact root adapter and optional catch-all adapter are both required: the
   root handles `POST /api/diagnostics` and the catch-all handles
   `GET`/`DELETE /api/diagnostics/<64-hex-report-id>`. Do not copy
   `diagnostics/` itself or the Wrangler config into the public root.
2. Copy `_routes.json` to generated output root. Its only included routes are
   `/api/diagnostics` and `/api/diagnostics/*`; every other route stays on the
   existing Pages static path. Do not generate `_worker.js` advanced mode.
3. The backend sidecar inventory/audit allows exactly
   `functions/api/diagnostics.js`, `functions/api/worker.mjs`,
   `functions/api/schema.mjs`, `functions/api/diagnostics/[[report]].js`, and
   `_routes.json`, with byte hashes in the sidecar manifest. The Functions tree
   remains outside the public static directory; its helper sources cannot be
   downloaded as static assets. The adapter and
   helper files are source-owned and must be hashed like every other generated
   upload byte.
4. Keep `_headers` unchanged for static resources. Add API response security
   headers in `worker.mjs`; do not put the API route under immutable asset or
   runtime cache rules. Add API POST/GET/DELETE checks to HTTP verification,
   while continuing to require existing blocked/private paths to return 404.
5. `scripts/deploy_staging.py` checks the exact expected Pages Functions/D1
   configuration, retaining the
   fixed project name, staging branch, custom domains, analytics-disabled
   policy, immutable deployment URL, and Wrangler Direct Upload command.
6. Validate the generated Function graph with the pinned Wrangler before
   upload. For the generated `functions` tree, run `wrangler pages functions
   build <generated>/functions --outdir <generated>/.functions-build
   --output-routes-path <generated>/.functions-routes.json
   --output-config-path <generated>/.functions-routing-config.json` and require
   exit 0. The routes file must contain exactly `/api/diagnostics` and
   `/api/diagnostics/*`; the routing config must map the exact root module and
   `api/diagnostics/[[report]].js` catch-all module. This checks both adapters
   and their relative imports without contacting Cloudflare. Keep these files
   as validation scratch; do not add them to the public static inventory.
7. The current upload command runs from a temporary directory with
   `wrangler pages deploy ... --no-bundle`. Copy the generated function tree
   into that directory before the command. Pass this `wrangler.jsonc`
   explicitly from the repository (or an equivalent generated config) so the
   D1 binding and migration directory are not inferred from the temp directory.
   Never upload the config or secrets.

Before the first upload, configure the one admin secret in both Pages
environments. Use the Wrangler version pinned by `dependencies.lock.json`,
keep the value out of shell history, and do not put it in `wrangler.jsonc`:

```sh
WRANGLER_BIN=/path/to/wrangler-4.131.1/node_modules/.bin/wrangler
read -r -s DIAGNOSTICS_ADMIN_TOKEN
printf '%s' "$DIAGNOSTICS_ADMIN_TOKEN" | "$WRANGLER_BIN" pages secret put DIAGNOSTICS_ADMIN_TOKEN --project-name webmelee-staging --env preview
printf '%s' "$DIAGNOSTICS_ADMIN_TOKEN" | "$WRANGLER_BIN" pages secret put DIAGNOSTICS_ADMIN_TOKEN --project-name webmelee-staging --env production
unset DIAGNOSTICS_ADMIN_TOKEN
```

The Pages project read used by `scripts/deploy_staging.py` must report the
allowlisted name with `type: "secret_text"`; it may include an API-provided
value, but the validator never returns or copies that value. The generated
upload config contains only the four reviewed plain variables and therefore
preserves the existing Pages secret. Any other secret or binding fails closed.

Set `DIAGNOSTICS_ALLOWED_RELEASES` from the audited player manifest before
deploying. The source SHA is the explicit SHA passed to staging preparation;
the runtime hash and profile come from that same audited manifest. This
example produces the exact JSON value for a staging-only release without
including private manifest paths or credentials:

```sh
SOURCE_SHA=0123456789abcdef0123456789abcdef01234567
AUDITED_MANIFEST=/path/to/audited-player.manifest.json
RELEASES=$(python3 - "$AUDITED_MANIFEST" "$SOURCE_SHA" <<'PY'
import json, pathlib, re, sys
manifest = json.loads(pathlib.Path(sys.argv[1]).read_text())
source = sys.argv[2]
runtime = manifest.get("runtime", {})
if (manifest.get("profile") != "player" or manifest.get("mode") != "production"
        or not re.fullmatch(r"[0-9a-f]{40}", source)
        or not re.fullmatch(r"[0-9a-f]{16}", runtime.get("hash", ""))):
    raise SystemExit("audited production/player manifest identity is invalid")
print(json.dumps({"staging": [{"source_commit": source,
    "runtime_hash": runtime["hash"], "build_profile": "player"}],
    "production": []}, separators=(",", ":"), sort_keys=True))
PY
)
printf '%s\n' "$RELEASES"
```

Apply that value as the Pages plain environment variable through the reviewed
Pages project configuration workflow, then re-read the project and run the
staging wrapper. Do not add it to a static file or browser bundle. The
production list stays empty until the separately audited production manifest
is promoted; a production identity is never inferred from a staging build.

The wrapper's exact rollback command uses the prior deployment receipt and
the same frozen candidate identities. It validates the target before calling
the Pages rollback endpoint:

```sh
python3 scripts/deploy_staging.py rollback \
  --source-root "$CANDIDATE" --sha "$SOURCE_SHA" \
  --base-output "$BASE_OUTPUT" --base-manifest "$BASE_MANIFEST" \
  --base-manifest-sha256 "$BASE_MANIFEST_SHA256" \
  --output "$OUTPUT" --manifest "$MANIFEST" \
  --manifest-sha256 "$MANIFEST_SHA256" --report-dir "$ROLLBACK_REPORT" \
  --wrangler "$WRANGLER_BIN" --account-id "$CLOUDFLARE_ACCOUNT_ID" \
  --deployment-id "$ROLLBACK_DEPLOYMENT_ID"
```

Rollback never accepts a preview deployment or an arbitrary project route. Keep
the failed report directory and its immutable URL for investigation; remove a
disposable report only after the operation has been independently verified.

The static audit still rejects function/config sources inside the public
asset directory. The separate backend graph audit checks exact adapter/helper
bytes and routes. Non-API browser requests remain GET-only; the diagnostics
network validator permits only the intended known-host same-origin JSON POST. The browser contract remains same-origin POST;
bearer-secret GET/DELETE is private CLI only. No browser credential or admin
token belongs in generated player files.

Keep these finite backend caps in deployment config: 60 accepted POST attempts
per environment per minute, 1,000 new reports and 16 MiB of new canonical
bytes per environment per UTC day, 10,000 retained rows, and 512 MiB retained
bytes. Known-origin browser requests are not authenticated.

The adapters use the file-based `onRequest` contract and intentionally do not
implement static fallback; Pages serves static requests outside the two
`_routes.json` includes. Deployment preparation validates the generated
upload through the pinned Wrangler version and then the immutable staging URL.

Pages request handling performs a bounded lazy expiry pass, but that request
path alone is not a physical-deletion guarantee. Queries filter `expires_at >
now`, so expired reports disappear from authenticated reads on time even while
their rows await deletion. Deploy `purge-worker.mjs` separately with
`purge-wrangler.jsonc` after substituting the real D1 name and id. Its Cron
Trigger runs every 30 minutes (`*/30 * * * *` UTC) and removes at most 100
expired reports (plus old daily-budget rows) per invocation. Forty-eight runs
provide 4,800 deletion slots/day against the configured 2,000-report combined
daily intake cap. That is operational headroom, not an unconditional physical
deletion SLA: missed invocations, D1 errors, and existing backlog extend
physical-deletion lag. Keep this Worker off the Pages upload and do not expose
it through a public route; it uses the same D1 binding and migration in
`migrations/`. The separate Worker is a setup prerequisite for strict
physical retention and has not been deployed by this backend prerequisite. The
Worker upload must include the adjacent `purge-config.mjs` helper; the entry
point deliberately exports only its scheduled handler for Workers runtime
compatibility.

The scheduled resource bound is at most 48 invocations/day, one delete of up
to 100 report rows and one daily-budget cleanup per invocation. The database
guard remains 10,000 rows and 512 MiB per environment. If emergency rollback
is needed, remove the Cron Trigger and redeploy the Worker configuration;
query-time expiry continues to hide expired rows while physical deletion is
paused. Restore the 30-minute trigger after investigation. Billing depends on
the Cloudflare plan.

A planning example, not observed traffic: 100 reports/day across both
environments at 20 KiB/report would store about 1.95 MiB/day and 58.6 MiB
over 30 days, before SQLite/index overhead. Each new intake performs bounded
rate/budget bookkeeping and one report insert; retries still use request-rate
capacity but do not add retained reports or daily intake bytes. Use the
authenticated CLI to check actual intake before changing these caps or
selecting a paid plan.

The reusable local integration test applies the real migration, starts Pages
Functions over HTTPS, and starts the scheduled Worker with Wrangler's local
`--test-scheduled` endpoint. The pinned local runtime uses compatibility date
`2026-09-18` because Wrangler 4.131.1 rejects the later production template
date; the production templates remain `2026-10-01`. Local Pages dev may SPA
fallback unknown non-Function paths, so production blocked/private route
status still belongs to the staging HTTP verifier.
