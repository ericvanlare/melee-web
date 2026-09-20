# Public staging package

For the separately authorized audio-enabled listening build of PR #42, use
[Audio listening preview](AUDIO_PREVIEW.md). The silent package and deployment
contract below remains the production baseline.

`scripts/stage_public.py` prepares a static, no-audio player package for the
isolated Cloudflare Pages project `webmelee-staging`, branch `staging`, and
working URL `https://webmelee-staging.pages.dev`. The custom hostname
`https://staging.webmelee.gg` is optional. The preparation script performs no
Cloudflare, DNS, account, or deployment operations. Deployment is a separate step.

The default package remains byte-identical to the audited production/player
release. This is the first live staging baseline: it has no label overlay, and
the base `_headers` already applies the global noindex policy. An optional
`--label-staging` mode applies this deterministic three-file overlay:

- The player `index.html` `<title>` gets the `[staging] ` prefix.
- `index.html` says `staging alpha · no audio` in its edition marker.
- The legal and informational HTML pages remain byte-identical to the base.
- Production Pages host names in `_headers` are replaced by
  `webmelee-staging.pages.dev`, while the host-specific noindex policy remains.
- `robots.txt` remains byte-identical to the already-reviewed noindex policy,
  and `_redirects` is replaced by the exact comments-only staging instruction.

In either mode, the runtime, operator and legal contact, licenses, notices, CSS,
all other HTML, and every other file are byte-for-byte copied from the audited
base. The sidecar manifest records the complete inventory; the receipt records
the mode, base and overlay digests, and exact changed-file records.

## Source and base requirements

Always use a separate clean source checkout. Supply its full 40-character
commit SHA with `--sha`; the tool checks `HEAD` and requires an empty Git
status. The public source must contain the PR15 CPU safety guard: the public
packager's two CPU diagnostic rejection markers, the
`MELEE_WEB_PUBLIC_RUNTIME` CMake define, and the guarded CPU observation code.
An older source that lacks this reviewed public pipeline is rejected before a
package is made.

The initial frozen candidate is:

```text
source SHA:          08b02660fd2a09120342ffaf116e36fe8656af23
base output:         build/pr15-reconciled-candidate-01
base manifest SHA256:59725af22d375101ece877fec9fbe052caff81051db72c0ab93432e6bf19f27e
runtime hash:        68c94e397d6e7c5c
runtime identity:    c2b2404ad9a6a5d495c204f05368d9cfb5ec0ee7a07a937c57014984df9fed88
```

These values document the reviewed starting point. They are not implicit
trust: every invocation still requires the explicit source SHA and base
manifest SHA256.

Without reuse, the tool runs the checked-in source commands for the Release
`runtime-public` target and then the source `build_public.py` packager for the
production/player profile, with `NaiadAI, LLC` and `legal@webmelee.gg`. It
allocates fresh ignored base output under that checkout and does not bootstrap,
reset, or clean another checkout. The source `audit_public.py` auditor then
checks the generated base with its full producer/provenance inputs.

With reuse, pass all three options together. The base output and manifest are
read-only inputs, the manifest digest is pinned, and the exact source auditor
still runs from the exact source checkout. A copied public output and a
manifest alone are not full provenance: the source checkout must still expose
the producer identity, runtime identity, dependency/tool hashes, seed, and
other files expected by `audit_public.py`.

## Preparation commands

For the initial frozen candidate, where `$CANDIDATE` is the clean checkout
containing the frozen `build/` output:

```sh
python3 scripts/stage_public.py \
  --source-root "$CANDIDATE" \
  --sha 08b02660fd2a09120342ffaf116e36fe8656af23 \
  --reuse-output "$CANDIDATE/build/pr15-reconciled-candidate-01" \
  --reuse-manifest "$CANDIDATE/build/pr15-reconciled-candidate-01.manifest.json" \
  --base-manifest-sha256 59725af22d375101ece877fec9fbe052caff81051db72c0ab93432e6bf19f27e \
  --output /tmp/webmelee-staging-public \
  --receipt /tmp/webmelee-staging.receipt.json
```

The command above is the exact baseline mode. Add `--label-staging` only when
the labeled three-file overlay is specifically requested.

`--reuse-output`, `--reuse-manifest`, and `--base-manifest-sha256` are
all-or-none. `--output` must be a new directory outside the source checkout;
the generated staging manifest is a new sibling of that directory, and
`--receipt` must be a new file outside it. Neither sidecar belongs in the Pages
upload. Omit the reuse options to build and audit a fresh base from the clean
source checkout. The exact mode preserves the base manifest's raw JSON bytes;
label mode writes the deterministic overlay sidecar.

The reusable Python boundary is:

```python
audit_staging(
    source_root, sha, base_output, base_manifest,
    base_manifest_sha256, output, manifest,
    *, label_staging=None,
) -> dict
```

It rechecks source cleanliness and the PR15 guard, invokes the full source
auditor, verifies both complete inventories, rejects symlinks, path escapes,
extra files, changed directories, metadata tampering, and protected-byte
changes, then accepts either the unchanged base or regenerates the exact label
overlay transformations. The companion
`validate_receipt(receipt, *, source_sha=None,
base_manifest_sha256=None, overlay_manifest_sha256=None)` checks the portable
receipt schema and optional pinned identities. Receipts set `staging_mode` to
`exact` or `label`; exact receipts preserve equal base and package manifest
digests, while label receipts require the distinct deterministic overlay digest.

The receipt hard-codes the staging project, branch, and working Pages URL and includes
the source SHA, base/overlay manifest digests, release profile, operator/legal
facts, runtime identity, native artifact hashes, silent audio policy, and
allowlisted changed files. It rejects private checkout paths so it can be
stored with the deployment record without exposing local provenance paths.

## Deployment wrapper handoff

After preparation, the separate `scripts/deploy_staging.py` wrapper accepts
`deploy`, `verify`, or `rollback` and requires the same explicit source/base
identities plus the prepared package manifest digest:

```sh
python3 scripts/deploy_staging.py verify \
  --source-root "$CANDIDATE" \
  --sha 08b02660fd2a09120342ffaf116e36fe8656af23 \
  --base-output "$CANDIDATE/build/pr15-reconciled-candidate-01" \
  --base-manifest "$CANDIDATE/build/pr15-reconciled-candidate-01.manifest.json" \
  --base-manifest-sha256 59725af22d375101ece877fec9fbe052caff81051db72c0ab93432e6bf19f27e \
  --output /tmp/webmelee-staging-public \
  --manifest "$CANDIDATE/build/pr15-reconciled-candidate-01.manifest.json" \
  --manifest-sha256 59725af22d375101ece877fec9fbe052caff81051db72c0ab93432e6bf19f27e \
  --report-dir /tmp/webmelee-staging-report \
  --immutable-url https://0123abcd.webmelee-staging.pages.dev
```

`deploy` additionally takes an explicit Wrangler path and account ID;
`rollback` takes an exact staging deployment ID. Both require the deployment
tooling checkout to be clean and committed. When replacing an existing staging
deployment, `deploy` also requires `--previous-record` with its deployment ID,
project, and pinned manifest digest; this record is preserved before upload.
The wrapper re-audits the
copied upload, binds operations to project `webmelee-staging` and branch
`staging`, and owns HTTP checks for the immutable and Pages origins by default.
Add `--verify-custom-domain` to check `https://staging.webmelee.gg` after DNS
is available. This flag does not modify DNS or domain associations.
The exact byte-identical frozen baseline is accepted by this
wrapper's `audit_staging` preflight with the base manifest and digest unchanged.

The initial exact baseline is deployment
`2918b6d1-ce9b-461f-a9a7-a5175e8a18a9`, available at
`https://2918b6d1.webmelee-staging.pages.dev`. Its 20 files retain the frozen
base manifest above. Stable staging deployments move the Pages alias;
`deploy` is therefore unsuitable for an experiment that must preserve that
alias. Such an experiment needs a separately authorized preview deployment
and its own source, patch, artifact, and verification records.

## Boundary and limitations

The wrapper exposes staging operations only. Its
fixed host rules and comments make the intended audience visible, but noindex
and robots directives are advisory and do not provide access control. The
preparation command does not perform browser, HTTP, DNS, Pages, or Cloudflare
verification. The deployment wrapper verifies Pages identity and real HTTP;
headed browser and DNS checks are separate. Failed preparation outputs are
retained for diagnosis and must not be treated as approved packages. This workflow does not
claim that a staging deployment is suitable for production or that runtime
behavior has been revalidated beyond the source and public release audits.
