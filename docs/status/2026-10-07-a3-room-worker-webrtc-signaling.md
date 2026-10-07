# A3 page-owned Room Worker signaling and native input sample

**Source identified / Compiled / Browser exercised**

The [portable receipt](../evidence/a3-room-worker-webrtc-input-sampling-v1.json)
records the bounded result for [issue #231](https://github.com/ericvanlare/melee-web/issues/231).
Two headless Chrome peers negotiated a reliable ordered WebRTC DataChannel
through the existing local RoomRelay Worker. Alpha attached its endpoint when
creating the channel; beta attached synchronously in the `datachannel` event.
Offer and answer stayed in the pages, and raw SDP was absent from Node-visible
reports and logs.

The existing native input path supplied four exact 11-byte PADStatus samples
per peer and six CSS source checksums per peer. Alpha's synthetic Gamepad sent
neutral, A, release, neutral to local port 0; beta sent neutral to port 1 for
all four samples. A sample captured at source cursor S was consumed at S+2,
with neutral bootstrap inputs at source ticks 0 and 1. During a 120 ms remote
wait at cursor 2, alpha's capture count, bytes, and poll serial stayed fixed
until beta released input 0. The expected native input-component hash matched
every recorded native checksum input component on both peers. Both peers
acknowledged four inputs and compared identical six-record checksum streams.

The source producer is `5b805d7`; the reused ordinary Release runtime was
built at `0d8556e`. The source changes did not alter runtime target inputs, so
the historical 36-file inventory was reused in the 37-file capture staging;
the 32 native runtime files remained unchanged. No fresh runtime build was
needed. The current-main Python suite passed 2,024 tests total, with 151
skipped; focused signaling, Worker HTTP/WebSocket, and contract checks also
passed. These identities and logs are recorded in the portable receipt.

The receipt includes 900×700 screenshot hashes, WebGPU/isolation checks, and
source draw counts. The canvases were 640×480. Root's review saw elongated red
geometry and lower clipping; screenshots and GPU data document the run, with
no pixel or visual-fidelity pass. Audio processing stayed enabled while
speakers were muted; retained diagnostics show 107 alpha and 86 beta underrun
frames. No continuous-audio or audible-output acceptance is claimed.

Evidence provenance has one explicit gap: the initial frozen source-test
manifest, SHA-256
`1fe0580da3afa0a2358cc0407c31f42175c4ce85b07d1ef0ad9c5d1d5d03745b`, was
overwritten while integration validation was added. Its exact bytes were not
recovered or reconstructed. The later supplement and overwrite correction
hashes are recorded in the receipt and are not presented as the original.

This is same-host synthetic input and a six-tick CSS component result only.
Physical or keyboard input, Internet or two-machine play, foreground timing,
performance, visual fidelity, continuous audio, the full supported route, and
whole-session accuracy remain unaccepted.
