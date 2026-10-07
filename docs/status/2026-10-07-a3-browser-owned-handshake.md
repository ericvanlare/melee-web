# A3 browser-owned peer handshake

**Source identified / Browser exercised / Compiled** for the local component
probe.
The [portable receipt](../evidence/a3-browser-owned-handshake-v1.json) binds
the source and served-module identities, the retained failure and reduced
Origin check, the successful browser/Node probe, cleanup, full suite, and
Release runtime build for
[issue #201](https://github.com/ericvanlare/melee-web/issues/201).

The probe used one headless Chrome alpha peer and one Node beta peer. Chrome
owned the alpha portable core and WebSocket endpoint; Node owned the beta peer
and endpoint. Both connected through the actual Worker using direct Miniflare.
The peers reached READY, exchanged one hello each, and agreed on identity hash
`04e0100de1e0f847136cb6148aa1615b7dc528175edb0e34a37b82fb15d09519`. Each
observer emitted one validated two-frame neutral batch. These were two emitted
source frames per peer and zero native frame executions; input and checksum
calls were also zero. On intentional shutdown, alpha observed one `4001: peer
disconnected` callback and beta one `1000: client closed` callback, with no
endpoint errors. Raw `CloseEvent.wasClean` was not observed.

An earlier r4 attempt on producer `e69d052495d4d2e326371098ee3d4b9c9c957b91`
failed before READY: browser WebSocket upgrade returned 403. The retained review
traced this to unsupported `vars` in the pinned Miniflare V4 conversion and an
owned Miniflare temp path landing under browser `TMPDIR`. That failure and its
cleanup evidence remain separate. On producer `179aaea1ae857ebb31b3f9dccc1b53da160a5054`,
a reduced direct-Worker check returned 101 for the configured Origin and 403
for an untrusted Origin. It only establishes upgrade acceptance: the raw 101
fixture destroys its socket and does not test WebSocket close lifecycle.

The successful r5 probe used the same `179aaea` producer and passed its owned
process, CDP/OS identity, profile, temporary-directory, port, and Worker
disposal checks. Full unittest discovery passed 1,979 tests total: 1,834 passed
and 145 skipped. This wrapper invoked the focused MJS contract suites through
SDK Node 24.19.0; pinned Worker packages were present, so the Worker tests did
not take their dependency-missing skip. The Release `runtime` build also
completed successfully. The receipt records exact log and runtime artifact
hashes.

This is local browser/Node component evidence, not an Internet or two-machine
session. No native gameplay ran, and no full CSS → SSS → gameplay → Results →
CSS route, whole-session acceptance, physical input, audio, visual equivalence,
performance, WebRTC, rollback, deployment, paid provisioning, or
account/DNS/secret change was tested. The Release runtime was compiled but not
run in the browser probe. Wrangler-proxy startup/close failures remain a
separate unresolved path; the successful probe loaded the actual Worker via
direct Miniflare. Broader online-play work remains open under
[issue #158](https://github.com/ericvanlare/melee-web/issues/158), with H1/#116
and #84 acceptance gates still open.
