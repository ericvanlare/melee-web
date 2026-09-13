# Public shell deployment runbook

This is a Cloudflare Pages **Direct Upload** shell release. It does not deploy a
native runtime or change repository visibility. Never upload the repository,
`web/`, a runtime build, an iframe staging package or a directory of accumulated
work. See `PUBLIC_RELEASE_REVIEW.md` before changing the approved inventory.

## Build, audit and preview

Build using `python3 scripts/build_public.py --help`; use a fresh ignored output
and keep its generated manifest outside that directory. Production requires
operator-supplied contact facts. Review all generated legal pages with those
values. Run `python3 scripts/audit_public.py --help` and audit the candidate
against the sidecar manifest immediately before upload. Preserve the manifest,
source commit, environment, tool versions and upload result together locally.
The manifest inventories configuration files as well as public resources.

Run the focused release tests and the repository's full unittest discovery.
Native dependencies must be installed independently in this worktree using
`scripts/bootstrap.py`; never borrow another track's modified sources or build.
No native engine target is changed or part of this shell release.

The launch uses Wrangler 4.131.1 from the npm registry as a local development
and deployment tool; `WRANGLER_SEND_METRICS=false` disables its optional metrics.
Install tools under ignored `work/`, not the public directory. Do not put tokens,
account IDs, private contact details or local paths into Git, published manifests,
screenshots or PR text. Use normal authenticated Wrangler or dashboard sessions.

Use `wrangler pages dev OUTPUT --port 8795` for Cloudflare's actual static routing
and header behavior. A generic file server is insufficient to certify Pages
extensionless routing, configuration headers or 404 responses. Browser checks
must use real HTTP, including narrow widths and fullscreen. Public images of
this original shell are permissible evidence; no game screenshots are approved.

Create a Pages project named `webmelee` if available. Create and verify a staging
branch deployment before attaching any custom domain. Upload only the audited
output directory; the sidecar manifest is retained locally, not uploaded. Keep
all preview hostnames non-indexed. `noindex` is a crawler instruction, not access
control. If previews contain sensitive material, do not deploy them; this
candidate is intended to contain only reviewed public shell content.

## Hosting constraints and headers

Cloudflare's current [Pages limits](https://developers.cloudflare.com/pages/platform/limits/)
allow at most 25 MiB per asset. The audit enforces that upper bound; this shell is
text-only and much smaller. Pages alone suffices. R2 is not created or needed.
Disc-derived/user-selected content must never be placed in R2.

A root `404.html` disables [SPA fallback](https://developers.cloudflare.com/pages/configuration/serving-pages/).
Check that `/runtime.html`, `/prototype.html`, `/viewer.html`, `/native-menu.html`,
`/hitch-capture.mjs`, `/tests/`, `/docs/`, `/work/`, `/build/`, `/.git/config`,
`/_worker.js` and an arbitrary unknown route return 404. A pretty 404 page with
status 200 is a failure. Directory inventories must not be served.

The generated [_headers](https://developers.cloudflare.com/pages/configuration/headers/)
sets CSP, nosniff, no-referrer, DENY framing and feature permissions. This shell
requires neither COOP nor COEP, Wasm nor binary MIME overrides. Introducing a
runtime requires explicit Wasm/binary MIME checks, required isolation headers,
worker/audio/fullscreen validation, and a new CSP review. Do not weaken CSP
preemptively. Hashed CSS/JS use immutable caching; HTML revalidates. Check that
Cloudflare has not injected Web Analytics, Zaraz or another script.

Cloudflare [_redirects](https://developers.cloudflare.com/pages/configuration/redirects/)
does not support domain-level redirects. Configure `www` and default Pages-host
redirects through account-level Bulk Redirects or zone redirect rules, not
unsupported host lines in `_redirects`. Preserve paths and query strings to the
apex; an unknown path should ultimately remain a 404. Restrict the exact default
production hostname, not all preview hostnames. Verify with external HTTP clients.

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

The no-upload result is scoped to this shell: it has no disc selection/preparation
path. Do not write that a production disc import passed. Validate no file input,
iframe/runtime loads, storage or unsolicited network connections. Fullscreen
can be tested; Wasm, gameplay audio and disc behavior are unavailable and cannot
be certified by this deployment.

## Reproduction, rollback and incident response

Keep at least one verified production deployment and its full manifest. Rebuild
from its source commit and operator config into a fresh directory, audit, compare
manifest equality and upload. The output has no timestamp-dependent build fields.
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

## Runtime integration boundary

The exact contract and extraction sequence are in `PROTOTYPE.md` under future
`mountMeleeRuntime`. The missing production owner must provide `importDisc`,
`prepare`, `start`, `focus`, `pause`, `resume`, `getState`, `unload`, `destroy`;
keep audio acknowledgement and native command draining synchronous at their
existing boundary, and retain source preparation/teardown ordering. Remove the
iframe and DOM/status parsing. Keep diagnostic attachments, raw-PAD/replay/memory
controls and evidence POST endpoints outside the production graph. No developer
host can be renamed into a production asset.

When that API is available, integrate only a clean pushed runtime checkpoint,
resolve distribution rights and complete transitive notices/corresponding source,
freeze the rebuilt runtime inventory, enforce MIME/isolation/size/network/storage
gates, show a real disc acknowledgement, then perform the relevant existing
accuracy/audio/input/performance admission. Fresh gameplay holdouts are not UI QA.
This shell release does not authorize enabling gameplay by flipping a feature flag.
