# A3 single-peer room relay endpoint

**Source identified** for the bounded single-endpoint transport slice in
[issue #197](https://github.com/ericvanlare/melee-web/issues/197). The
[portable receipt](../evidence/a3-single-endpoint-client-v1.json) binds the
original readiness-gap reproducer, implementation, focused real-server tests,
actual Worker/A2 contract, full suite and Release runtime build.

Before the change, a real local HTTP/WebSocket server wrote READY immediately
followed by an opaque hello in one write. The Node pair helper returned after
READY, then its caller attached message listeners; both first packets were
missed. The retained run binds this observation to the original `1607b34`
producer and includes the cleanup caveat that its transport errors were caused
by deliberate socket destruction during teardown.

The adapter now exposes `createRoomRelayPeerEndpoint`, which owns one role and
installs its initial receiver synchronously before returning the handle. Its
`ready` promise resolves only after the WebSocket opens and the relay sends
READY. A2 sends wait for that promise, so callers can prepare local agreement
state and attach `LockstepPeer.receive` before the first remote hello is
delivered. The pair helper remains compatible: it composes two handles and
retains early messages in the existing bounded serialized callback lane until
the pair caller subscribes. Pair shutdown shares one cached promise, preserving
callback failures across concurrent and repeated calls.

The focused Node 24 transport suite passed all 15 tests, including READY and
hello in the same server write, early close before READY, rejected pending
sends, bounded early-message queues, no Node-only imports/globals, and a delayed
callback failure observed by concurrent close callers. The actual Worker/DO and
existing A2 transport contract passed all four tests. Full unittest discovery
passed 1,964 tests (1,819 passed, 145 skipped), and the Release `runtime` build
succeeded. Logs and selected artifact hashes are in the receipt.

This is transport-component evidence only. The Release build was not executed
in a browser, and the new endpoint has not been imported or exercised by a
browser. Browser-owned peers, a two-machine or Internet session, WebRTC,
rollback, deployment, complete-set acceptance, gameplay accuracy and
performance remain untested. The older READY/first-hello miss remains preserved
as the pre-change reproducer; it is not relabelled as a browser failure.
