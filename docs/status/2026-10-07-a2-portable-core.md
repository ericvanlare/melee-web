# Browser-portable A2 lockstep core

**Source identified**. The A2 peer state machine now lives in the import- and
transport-free [`net_lockstep_core.mjs`](../../scripts/net_lockstep_core.mjs).
It uses copied byte arrays, `DataView`, canonical base64 and WebCrypto SHA-256;
the existing Node module remains a compatibility facade for Buffer frame
callbacks, checksum parsing and TCP framing.

The [portable receipt](../evidence/a2-portable-core-v1.json) binds the source
commit, SDK Node runtime, pre-change fixtures, focused tests, full suite and
Release runtime build. The hello, agreement, state, raw-text, frame and
checksum-parser fixtures were captured from the unchanged Node implementation
before the migration and copied unchanged into the test suite. Controlled
WebCrypto tests cover startup atomicity, duplicate starts, held/rejected
hashes, serialized state acceptance, byte-exact duplicate handling,
terminal-during-digest behavior and bounded inbound message/UTF-8 bytes.

The focused Node 24 suite passes all 21 cases, including a strict core import
and handshake with the global Buffer removed. The full suite passes 1,964 tests
(1,819 passed, 145 skipped). The Release `runtime` build also exits successfully;
that build is repository build validation and does not include or execute the
portable core in a browser.

This is offline protocol-portability evidence for [issue #195](https://github.com/ericvanlare/melee-web/issues/195). It does not establish
browser import or execution, browser-owned peer endpoints, WebSocket endpoint
extraction, Internet or two-machine acceptance, live input, WebRTC, rollback,
deployment, gameplay accuracy or performance. The following single-endpoint
transport boundary remains a separate slice.
