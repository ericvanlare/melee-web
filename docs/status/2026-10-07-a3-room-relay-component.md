# Bounded A3 room relay component

**Source identified**. Local HTTP/WebSocket behavior is scoped to the direct
Worker and A2 boundary described below.

The first bounded A3 transport component is implemented: a Cloudflare Worker
routes each room to one Durable Object, which accepts at most two WebSocket
peers and forwards A2 text packets unchanged. The client readiness message is
sent only after both peers join; a lone peer receives no A2 data, and an early
packet is rejected instead of buffered or replayed. Room IDs are bounded
base64url tokens, text packets are capped at 1 MiB, binary or oversized packets
close explicitly, and client-side send and inbound callback queues have declared
limits.

The [portable receipt](../evidence/a3-room-relay-component-v1.json) binds the
producer commit and source/config/runtime hashes, focused test outcomes, and
cleanup receipts. Against producer `7abab468a1e242a12fd27f09fff4e8a134f8b9fd`
(tree `6c77fb1d456a853a30bb3011a134368c2694125e`), the full Python suite passed
with 1,942 total tests: 1,800 passed and 142 skipped. The relay
HTTP/WebSocket test passed four cases,
the existing A2 protocol suite passed 12 cases, and the repository content
guard reported zero findings. The actual Worker tests used Wrangler 4.131.1,
Miniflare 5.20260911.0-alpha and workerd 1.20260911.1. Four direct-runtime
receipts record successful disposal, exit code zero, absent process groups, no
fallback kill, and no runtime errors.

The actual Worker contract covered method, route, origin and upgrade errors;
two-peer readiness and capacity; room isolation; opaque packet forwarding;
pre-ready, binary and oversized rejection; async message/disconnect callbacks;
and a clean sender close followed by peer disconnect. The A2 boundary over this
Worker exercised matching hello identity, input acknowledgement while the
other PAD sample was absent, the fixed two-tick release, equal indexed source
frames, the first checksum mismatch at tick 2/channel 1, a mismatched start
identity, and transport disconnect terminals. The A2 protocol source and input
semantics were not changed.

Two earlier overflow fixture attempts timed out because the raw test server
exchanged WebSocket Close frames but did not end TCP, leaving Node's client in
`CLOSING` without a close event. A retained one-socket reducer isolated that
fixture error; the corrected fixture verifies both masked Close frames and ends
TCP after the exchange. The production adapter was unchanged by this correction.

The Worker uses the standard Durable Object WebSocket API with explicit socket
ownership. The retained pinned-runtime hibernation tests failed on a cross-socket
close sequence; the direct standard-API comparison passed. Cloudflare documents
that standard accepted sockets keep the Durable Object active and may accrue
duration charges for that lifetime. No usage, cost, or performance measurement
was made. The local integration parses the real Wrangler configuration and
loads the actual Worker module through direct Miniflare; the earlier Wrangler
proxy startup/close failures remain unresolved and are not claimed fixed.

The local unittest wrapper always runs the pure adapter tests with the bundled
Node runtime. It runs the actual Miniflare Worker cases when the pinned local
packages are present; otherwise those runtime cases skip. The full local run
used the pinned packages, and its private output and four runtime receipts are
retained under `work/a3-room-relay-validation-7abab46/`.

This is component evidence only. It does not establish an Internet connection,
a two-machine complete-set, browser integration, WebRTC relay fallback,
rollback, deployment, competitive readiness, H1/#116 pause resolution, or #84
foreground/sustained acceptance. A3 remains open under [issue #179](https://github.com/ericvanlare/melee-web/issues/179); see [architecture decision 016](../ARCHITECTURE.md#016--online-play-starts-as-lockstep-from-a-shared-css-context) and the [roadmap](../ROADMAP.md#current-priorities) for the remaining outcome.
