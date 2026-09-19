# Public player deployment runbook

This is a Cloudflare Pages **Direct Upload** release. The player at `/` keeps
the original prototype's black canvas and small bottom toolbar. Disc, Play,
Pause, Controls, Fullscreen and Eject call the shared player owner directly.
About and legal information lives on linked document pages. There is no landing
page, iframe, developer host, account, analytics or upload endpoint.

The `player` profile packages a Release native build with a fixed public export
surface. The `maintenance` profile packages the small nonplayable fallback.
Never upload the repository, `web/`, a native build directory, an iframe staging
package or accumulated `work/` files. Do not change repository visibility.
See `PUBLIC_RELEASE_REVIEW.md` for the executable/source rights assessment,
the accepted alpha risk posture and the artifact/contact requirements.

## Current launch state

The silent alpha is live at **[webmelee.gg](https://webmelee.gg/)**.
The operator is **NaiadAI, LLC**; the public contact is **legal@webmelee.gg**.
Actual Google Workspace alias delivery was verified before the nameserver
change and again after migration, before custom-domain activation. The reviewed
PR #38 pipeline-preparation fix (`b3b9da4`, merged as `90ff371`) was deployed on
September 19, 2026 to staging at
[236f2265.webmelee-staging.pages.dev](https://236f2265.webmelee-staging.pages.dev)
and production at
[24f92d33.webmelee.pages.dev](https://24f92d33.webmelee.pages.dev).
The exact audited bytes passed HTTP verification and all ten public browser
checks on both production origins. The hosted Link/Young Link route also passes
cold and warm without timing resumes or live pipeline creation.
The [current release receipt](evidence/public-link-pipeline-release-v1.json)
records identity, rollback and scoped verification. These short silent-player
checks do not admit full-match performance. The preceding
[PR #32 release receipt](evidence/public-link-release-v1.json) and initial launch
record below remain historical.

Namecheap saved `alan.ns.cloudflare.com` and `hadlee.ns.cloudflare.com` as the
custom nameservers after the mail and artifact gates passed. Cloudflare serves
the Google MX/SPF/verification records and proxied apex/www CNAME records to
`webmelee.pages.dev`. Both queried `.gg` authorities now publish that delegation;
1.1.1.1 and 8.8.8.8 resolve the apex to Cloudflare. Both Pages custom domains
are active with SSL enabled. The two-entry `webmelee_canonical_hosts` list is
attached to the **enabled** rule `WebMelee canonical hostnames`. The apex,
redirects, missing routes and unchanged preview hosts passed external checks.
Always Use HTTPS is enabled and SSL mode remains Full. See the
[final alpha evidence](PUBLIC_ALPHA_VALIDATION.md) for the exact runtime,
manifest, limitations and measured resolver coverage.

## Build, audit and preview

Freeze a reviewed pushed source checkpoint in an isolated checkout. Install its
pinned dependencies independently with `scripts/bootstrap.py`; do not borrow
another task's modified sources or build. Build and audit with the checked-in
scripts, using a fresh ignored output directory for each candidate:

```sh
python3 scripts/build.py --target runtime-public --configuration Release
python3 scripts/build_public.py --profile player --runtime-dir build/browser-public-release --output build/player-preview --mode preview
python3 scripts/audit_public.py --output build/player-preview --manifest build/player-preview.manifest.json
```

`runtime-public` builds the shared native engine, input and renderer with a
Release-only export list and an explicitly audio-disabled public native closure.
Its separate `build/browser-public-release` directory prevents the alpha flags
from changing the normal development build in `build/browser-release`. It does
not use a copied development executable.
`build/runtime-public-identity.json` records native file bytes/hashes, actual
Wasm exports and JS bindings, source trees, prepared pinned-source identity,
toolchain, pipeline seed, disabled-audio policy and compile/link exclusion proof. The public builder verifies that record against
this checkout and selects only the three named native files. It copies the
reviewed player, disc and input modules directly from source and hashes the complete runtime
graph into `runtime/<hash>/`. Relative imports and the Wasm/data paths
remain within that immutable directory.

The sidecar release manifest inventories every output path, byte size and
SHA-256, including hosting configuration and full third-party notices. It stays
outside the upload. Audit regenerates expected output from reviewed sources and
compares the complete inventory. A native or source mismatch fails; a manually
rewritten manifest does not authorize extra files. The source commit and the
local operator configuration must be retained beside the candidate.

Preview mode has draft legal placeholders and is for local review only. Build
the production candidate with the operator-approved facts below, then verify
forwarding independently before activating the public domain:

```sh
python3 scripts/build_public.py --profile player --runtime-dir build/browser-public-release --output build/player-candidate --mode production --operator 'NaiadAI, LLC' --contact legal@webmelee.gg
python3 scripts/audit_public.py --output build/player-candidate --manifest build/player-candidate.manifest.json
```

The operator accepts the unresolved recovered-code risk for this alpha; that
is recorded in `PUBLIC_RELEASE_REVIEW.md`, without a claim of legal clearance.
The public artifact must exclude the GPL-derived audio implementations and pass
its final audit. Legacy audio-enabled/v1 identities are rejected. The public
page must retain both the audio-disabled disclosure and opcode-63 limitation. Production stays
non-indexed unless `--index-production` is deliberately chosen. A preview is
marked in its browser title; it does not add a banner to the player.

Build a maintenance fallback with `--profile maintenance` and the same operator
facts. It has disabled gameplay and no native files. Retain its audited output
alongside the first playable production candidate for incident response.

Run the repository's full unittest discovery and the actual affected native
target. Real-browser checks below are additional evidence, not substitutes for
the separate accuracy, PCM, physical-controller or performance admission gates.

The launch uses Wrangler 4.131.1 from the npm registry as a local development
and deployment tool; `WRANGLER_SEND_METRICS=false` disables its optional metrics.
Install tools under ignored `work/`, not the public directory. Do not put tokens,
account IDs, private contact details or local paths into Git, published manifests,
screenshots or PR text. Use normal authenticated Wrangler or dashboard sessions.

Use `wrangler pages dev OUTPUT --port 18961 --inspector-port 19361` on unused
local ports for Cloudflare's actual static routing
and header behavior. A generic file server is insufficient to certify Pages
extensionless routing, configuration headers or 404 responses. Browser checks
must use real HTTP, including narrow widths and fullscreen. Public images of
this original shell are permissible evidence; no game screenshots are approved.

Use the existing Pages project named exactly `webmelee`; generated host-specific
headers assume `webmelee.pages.dev`. Create and verify a staging
branch deployment before attaching any custom domain. Upload only the audited
output directory; the sidecar manifest is retained locally, not uploaded. Keep
all preview hostnames non-indexed. `noindex` is a crawler instruction, not access
control. If previews contain sensitive material, do not deploy them; the candidate must contain only the reviewed release inventory.

With Wrangler authenticated, deploy the configured, audited candidate to staging:

```sh
WRANGLER_SEND_METRICS=false wrangler pages deploy build/player-candidate --project-name webmelee --branch staging
```

The existing signed-in Cloudflare dashboard also supports Direct Upload. A ZIP
must contain exactly the audited directory contents at its root. Confirm the
deployment environment before submitting; a project's first dashboard upload
may create its default production deployment. No custom domain should be attached
until the candidate's returned immutable Pages URL passes verification. Do not
publish a draft containing missing-operator placeholders.

## Hosting constraints and headers

Cloudflare's current [Pages limits](https://developers.cloudflare.com/pages/platform/limits/)
allow at most 25 MiB per asset. The audit enforces that upper bound. The compiled player fits in Pages without splitting its
Wasm or data file. Pages alone suffices. R2 is not created or needed.
Disc-derived/user-selected content must never be placed in R2.

A root `404.html` disables [SPA fallback](https://developers.cloudflare.com/pages/configuration/serving-pages/).
Check that `/runtime.html`, `/prototype.html`, `/viewer.html`, `/native-menu.html`,
`/hitch-capture.mjs`, `/tests/`, `/docs/`, `/work/`, `/build/`, `/.git/config`,
`/_worker.js` and an arbitrary unknown route return 404. A pretty 404 page with
status 200 is a failure. Directory inventories must not be served.

The generated [_headers](https://developers.cloudflare.com/pages/configuration/headers/)
sets CSP, nosniff, no-referrer, DENY framing and feature permissions. The player
requires COOP `same-origin` and COEP `require-corp`. Its CSP permits same-origin
module/loader and Wasm compilation via `wasm-unsafe-eval`; generated JS
is built with dynamic JS execution disabled. Same-origin fetch is needed for
Wasm/data loading. It is not an upload prevention rule: verify the actual
application requests instead of claiming CSP blocks every same-origin POST.
Wasm is served as `application/wasm`, data as `application/octet-stream`, and
JS modules as JavaScript. Hashed runtime assets use immutable caching; HTML
revalidates. Verify WebGPU, the absence of audio output, local file selection and fullscreen against
these exact headers. Check that
Cloudflare has not injected Web Analytics, Zaraz or another script.

Cloudflare [_redirects](https://developers.cloudflare.com/pages/configuration/redirects/)
does not support domain-level redirects. Configure `www` and default Pages-host
redirects through account-level Bulk Redirects or zone redirect rules, not
unsupported host lines in `_redirects`. Preserve paths and query strings to the
apex; an unknown path should ultimately remain a 404. Restrict the exact default
production hostname, not all preview hostnames. Verify with external HTTP clients.

For [Bulk Redirects](https://developers.cloudflare.com/rules/url-forwarding/bulk-redirects/reference/parameters/),
add sources `www.webmelee.gg/` and `webmelee.pages.dev/`, target
`https://webmelee.gg/`, status 301. Enable **Subpath matching**, **Preserve path
suffix**, and **Preserve query string**. Leave **Include subdomains** disabled.
Omitting the source scheme matches HTTP and HTTPS. These are URL/flag settings;
Bulk Redirects do not use `_redirects` wildcard substitutions. Activate the list's
rule only after the apex works, and confirm that immutable preview hosts remain
separate.

### Keep the edge from modifying the release

The final custom-domain check found Cloudflare injecting RUM analytics and
rewriting email addresses even though the uploaded files contained neither.
Disable **Speed → Real user monitoring → Disable completely** and **Security
→ Settings → Email Address Obfuscation**. Recheck full response bytes with the
explicit audit client and a real browser; a successful upload alone does not
prove that the edge serves unchanged HTML.

The zone's **Network → Network Error Logging** switch is off. Because NEL
headers remained visible after that setting change, the active response-header
transform rule **Disable browser network reporting** matches
`http.host in {"webmelee.gg" "www.webmelee.gg"}` and sets these static headers:

| Header | Value |
| --- | --- |
| `NEL` | `{"max_age":0}` |
| `Report-To` | `{"group":"cf-nel","max_age":0,"endpoints":[]}` |

Verify the cancelling policy on actual apex main/legal/404 responses. A zero
maximum age also expires a browser's prior reporting policy. Provider-managed
Pages preview hosts can retain Cloudflare operational NEL headers; do not
describe an application network smoke as proof that the hosting provider
collects nothing. See Cloudflare's [NEL documentation](https://developers.cloudflare.com/network-error-logging/)
and [response-header rules](https://developers.cloudflare.com/rules/transform/response-header-modification/).

## Mail verification gate

Do not activate the public website until the exact artifact audit passes and
`legal@webmelee.gg` forwarding has been observed delivering mail to the intended
inbox. DNS MX records or a verified destination alone are not end-to-end proof.
Obtain the intended forwarding destination from the operator; do not infer it
from account or Git metadata. Keep that private destination out of public files.
The initial expired-login and missing-destination blockers were resolved by the
separate email setup task. It configured an explicit Workspace alias on an
existing user, with no new paid user or catch-all. An external test addressed
only to legal@webmelee.gg arrived at the intended inbox on September 12, 2026
at 22:02 Pacific; receipt, recipient details and TLS were checked. A separate
post-migration message with token `WM-CF-0913-A7K9` arrived at 23:36 Pacific,
before attaching the public domains. The private inbox stays out of public
documentation.

The final mail provider is **Google Workspace**, independent of Namecheap DNS.
Preserve its records during migration; do not enable Cloudflare Email Routing
or restore the old Namecheap forwarding MX/SPF records. No send-as identity,
DKIM or DMARC record was added by the incoming-alias setup task.

Send a uniquely identifiable test message from a separate sender account to
`legal@webmelee.gg`; verify receipt in the destination inbox, including recipient,
subject/token and time. A provider verification link is a different check.
Retain a private receipt without mailbox contents or credentials in Git. Retest
after changing MX, nameservers or routing providers, before website activation.
Record failed/bounced attempts honestly. Do not configure a catch-all unless
requested.

## DNS inventory and cutover

The pre-change inventory on September 12–13, 2026 was:

| Record/service | Existing value | Cutover intent |
| --- | --- | --- |
| NS | dns1.registrar-servers.com; dns2.registrar-servers.com | Change to the two nameservers assigned by Cloudflare only after a verified Pages preview |
| www CNAME | parkingpage.namecheap.com, 30-minute TTL | Replace parking with Pages plus HTTPS redirect to apex |
| @ URL Redirect | http://www.webmelee.gg/ (unmasked) | Replace parking direction with Pages apex |
| @ MX priority 10 | eforward1, eforward2, eforward3.registrar-servers.com | Preserve existing DNS unless deliberately replacing mail service |
| @ MX priority 15 / 20 | eforward4 / eforward5.registrar-servers.com | Preserve existing DNS unless deliberately replacing mail service |
| @ TXT | v=spf1 include:spf.efwd.registrar-servers.com ~all | Preserve with the matching mail setup |
| Email forwarding | No recipients or catch-all defined | Not a verified contact mailbox |
| DNSSEC / Dynamic DNS | Off / off | No DS-removal requirement observed |

The mail setup task superseded the old forwarding inventory before cutover.
The final records were re-read in Namecheap and copied to Cloudflare, and both
assigned Cloudflare nameservers answered with these values before the
nameserver change:

| Type / name | Current value | Other settings |
| --- | --- | --- |
| MX @ | `smtp.google.com` | Priority 1; DNS only; Auto TTL |
| TXT @ | `v=spf1 include:_spf.google.com ~all` | DNS only; Auto TTL |
| TXT @ | `google-site-verification=wiOtbQAdsn4dG7EIcoR62vP7HCLJahI5FJWCmUIp0ts` | DNS only; Auto TTL |
| CNAME @ | `webmelee.pages.dev` | Proxied; Auto TTL; replaces parking A |
| CNAME www | `webmelee.pages.dev` | Proxied; Auto TTL; canonical redirect after apex verification |

Read the full current Namecheap Advanced DNS page again at cutover; another
operator may have changed it. Save any additional A/AAAA/CNAME/MX/TXT/SRV/CAA,
mail routes and DNSSEC state before changing authoritative nameservers. DNS
queries alone do not discover every record. Namecheap's URL Redirect is a
provider service, not a portable DNS RR. Do not recreate the old apex-to-www loop.
Do not assume Namecheap's mail-forwarding service continues on external DNS;
there was no configured recipient to preserve at the initial inspection.

The [official custom-domain guide](https://developers.cloudflare.com/pages/configuration/custom-domains/)
requires the apex as a zone in the same Cloudflare account and authoritative
Cloudflare nameservers. Add the zone, review/import DNS records, attach the
verified Pages project, and change nameservers at Namecheap to the assigned pair.
Do not guess nameservers. Do not publish dashboard/account IDs. Add `www`, then
configure its HTTPS redirect. Wait for zone activation, custom-domain/TLS
activation and externally verified NS/A/AAAA/HTTPS. Check CAA if certificate
issuance fails. Keep unrelated zones and projects untouched.

## Post-deployment verification

Compare each downloadable resource to the local manifest using SHA-256 and byte
size; follow Pages' canonical HTML redirects deliberately. `_headers` and
`_redirects` are consumed configuration: validate their effects, not public byte
retrieval. Check `/` body and headers, all legal links, noindex on previews,
MIME types, developer-path 404s, DNS, HTTPS, www and default-host redirects.
Repeat the browser check against the immutable deployment URL and production.
Record exact successful URLs and deployment IDs without account identifiers.

Run these against each immutable deployment origin and the apex (substitute the
actual returned origin for `CANDIDATE_ORIGIN`):

```sh
python3 scripts/verify_public_http.py --url "$CANDIDATE_ORIGIN" --manifest build/player-candidate.manifest.json --report work/hosted-http.json
node tests/public_player_browser_test.mjs --url "$CANDIDATE_ORIGIN" --playwright ./work/deploy-tools/node_modules/playwright --out work/hosted-browser --disc "$WEBMELEE_LOCAL_DISC"
```

HTTP verification checks full bytes and headers for each resource and each linked
extensionless legal route. Redirects must remain on the candidate origin and use
only the expected canonical path. Missing development routes must finish on that
same origin with 404. A cross-origin redirect, legal link serving the homepage,
or HTML with missing security headers is a failure. Wrangler 4.131.1 can return a
reserved-configuration ENOTDIR/502 locally; this exception is recorded only for
loopback and never accepted for a hosted URL.

The player browser check runs the real public graph through startup, controls,
invalid-disc retry, acknowledged owned-disc import, native preparation, original
CSS/SSS navigation, pause/resume, Eject/reload and another import/launch. For the
initial silent alpha it requires zero audio contexts and no audio output; a
future audio-enabled profile needs its own license and audio validation gates.
Inspect requests through the entire session: all application
requests must be expected same-origin static GETs without bodies or queries;
no disc bytes, derived assets, file names or local hashes may be transmitted.
Check WebSocket/beacon activity, cookies, local/session storage, IndexedDB,
Cache Storage and service workers. Only keyboard preferences should persist.
Do not put the local disc path or game screenshots in public evidence.

If audio is restored later, a real audio graph alone is not proof of acoustic
output or PCM equivalence. A menu smoke is not a complete match/performance
result. Record any timing-guard pause
and manual resume in the scoped evidence. Broad game content, mobile play,
physical controllers, retail equivalence and uninterrupted performance remain
separate acceptance work.

## Reproduction, rollback and incident response

Keep at least one verified production deployment and its full manifest. Retain the
exact source-bound native files, identity, public output and operator config.
A rebuild must pass its own identity/audit and reproduce the public bytes from
that frozen native input. Independent native rebuilds can differ with compiler
or graphics-port tooling and require a new comparison and verification; do not
assume bit-for-bit native determinism without measuring it. The output has no timestamp-dependent build fields.
Do not use an undocumented mutable runtime artifact.

Cloudflare [Pages rollbacks](https://developers.cloudflare.com/pages/configuration/rollbacks/)
can promote an earlier successful production deployment from the dashboard.
Record which deployment was restored and externally recheck root, legal pages,
headers and missing routes. Preview deployments are not rollback targets. If no
earlier production exists, deploy a reviewed minimal maintenance shell with the
working contact route. Keep the apex DNS pointing to Pages while replacing the
site; reversing nameservers adds propagation delay and may disrupt unrelated DNS.

For a rights report or accidental artifact/secret publication: promptly restrict
or replace the offending deployment, stop automatic releases, preserve minimal
private evidence, notify the operator through the existing task, and investigate
all accessible immutable preview URLs as well as the apex. Removing an apex
link alone does not withdraw immutable deployments. Coordinate deleting exposed
deployments or restricting preview access where required; purge relevant edge
cache, externally verify unavailability, rotate any exposed credential through
its provider, and obtain legal advice for notices and response obligations.
Do not publicly upload the report or copyrighted evidence. The operator handles
rights correspondence; this task has not registered a DMCA agent.

## Promotion and ongoing development

The source checkpoint and runtime graph hash identify a candidate; branch names
alone do not. Build a production-mode candidate once, audit it, upload those
exact bytes to a staging branch and verify the immutable deployment URL. Then
upload that same unchanged directory to the production branch and compare the
returned immutable URL and apex to the same manifest. Record both deployment
IDs and the prior production ID. HTML links to its own runtime hash, so an old
page cannot accidentally load a newer runtime module during a rollout.

There is no automatic release of changing runtime heads. Normal development
continues in the shared native code and `melee-runtime.mjs`; diagnostics attach
only through `runtime-development.mjs` and the development native profile.
Promote a reviewed checkpoint through build → audit → real browser → staging
HTTP/browser verification → production HTTP/browser verification. A failure
keeps the previously verified production deployment serving.

Future modularization may allow replacing a heap without page reload. Until
then, one native owner per document and reload on Eject are explicit product
behavior. Do not add runtime diagnostics to production for acceptance testing;
run the separate development target when a diagnostic observer is required.
