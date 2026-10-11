# Public relay keyboard checkpoint — 2026-10-11

## Branch and scope

Branch `codex/public-relay-keyboard` starts from merged local-route milestone
`2a73717682372d440dbad16ef4bcb418e31a948d` (#303). The executable source
checkpoint is `b612176a1c73f12e8db4c962be2748c6c61f96bc`; subsequent edits are
this handoff and the existing local-route status/CI receipt. This branch is
source-only and is not merged or validated against a deployed Worker.

The existing Host/Join keyboard harness now accepts paired `--relay-url` and
`--relay-preflight` arguments. Omit both to keep the owned local Worker. The
first external mode retains the tested client at
`http://127.0.0.1:8787/runtime.html`; the selected Worker must allow that exact
origin. It reuses the existing runtime, transport, invite and full-route driver.
External mode does not start or fall back to a local Worker and never claims
ownership or disposal of the remote service.

`a3_public_relay_preflight.mjs` validates a supplied preflight's exact WSS URL,
staging deployment/source/config identities and allowed client origin. It
binds read-only evidence filenames and hashes. This is structural validation,
not an independent provider query or proof that a service exists. No positive
preflight or public endpoint has been supplied. Do not manufacture one from the
historical not-found receipt or a local deployment dry run.

## Validation and retained evidence

Both focused tests pass on the executable checkpoint with the repository's
pinned SDK Node: semantic preflight/refusal controls and actual CLI controls
that stop before disc import/browser setup. The CLI distinguishes absent
options from an explicitly supplied JSON `null`. Default local mode reaches
the deliberately missing disc, while null preflights refuse. Syntax and diff
checks pass. Independent source review and a bounded review fix are retained.

The initial syntax failure, missing-SDK environment failure, overly strict
PATH-Node test, and null/undefined CLI regression are retained. The corrected
Python wrapper uses the existing pinned-runtime lookup and meaningful behavior
checks; it does not inspect implementation text or hide missing prerequisites.
No browser, capture, provider request, auth refresh, deployment or build was
performed for this adapter. Full regression and current-head CI are pending.

Private receipts live in run `public-relay-keyboard-20261011-141954-bcb8e535`;
find its verified external path in the coordination checkpoint's
`public_endpoint_interface.output_root`. `prepared-checkout-focused-result.json`
has SHA-256 `4a03c4df24443da94e5bc6973f97f3af551266d9dd7f652b72856f8785915a12`.
The final source freeze is `sol-cli-refusal-fix/freeze.json`, SHA-256
`b55c1e1ef1eeda6e96b04516fc2be6d48c1ba7abf2b08560be2f3a01d9624bae`.
Earlier local full-route evidence remains in its separate producer receipt;
do not relabel that capture with this adapter's source revision.

## Next experiment and stopping rule

First resolve a real staging endpoint and retain its deployed identity and
exact allowed-origin evidence. Then run a bounded two-profile actual Host/Join
and keyboard CSS check through that public URL; extend with `--full-route` only
after the boundary passes. Record exact source/build/disc identities, native
checksum coverage, input delay and observed network waits. RTT is unmeasured;
exclude it explicitly from the first CSS witness and leave the broader #288
metric gate open. No setup-time proxy or local fallback counts as Internet RTT.

Stop on the first identity, origin, protocol, runtime or cleanup failure and
retain it. After two experiments at the same boundary, reduce before retrying.
Do not change budgets, Resume timing pauses, relax comparisons or alter game
arithmetic to pass. Current provider credentials are missing or expired; no
new paid infrastructure, account/security changes or production deployment is
authorized by this source checkpoint. Continue independent engineering while
that service prerequisite remains unresolved. Physical, two-machine,
foreground and audible gates remain unrun.
