# A3 native input sampling over local WebRTC

**Source identified / Compiled / Browser exercised**

The [portable receipt](../evidence/a3-webrtc-input-sampling-v1.json) records
the bounded result for [issue #225](https://github.com/ericvanlare/melee-web/issues/225).
Two headless Chrome pages used a direct, reliable ordered WebRTC DataChannel
with host candidates only. Alpha attached its endpoint at `createDataChannel`;
beta attached synchronously in the `datachannel` event. The existing browser
peer and native sampling path supplied four 11-byte PADStatus records per peer
and six source checksums per peer.

Alpha's synthetic Gamepad sampled neutral, A, release, neutral at local port 0;
beta's Gamepad sampled neutral at port 1 for all four inputs. The native input
component hash for each source tick matched the expected sampled input at
source cursor S+2, with neutral bootstrap inputs at ticks 0 and 1. During the
withheld-input wait at cursor 2, alpha's capture count, bytes, and poll serial
remained unchanged until beta released input 0. Both peers completed six CSS
source steps and draws, acknowledged four local inputs, and compared six checksums.

The receipt includes 900×700 screenshot hashes and cross-origin-isolation and
WebGPU diagnostics. The canvases were 640×480. Root's visual review noted red
elongated shapes and a cropped presentation; screenshots and GPU data document
the capture only, with no visual-fidelity or pixel-equivalence pass. Audio
processing remained enabled while speaker output was muted. No uninterrupted
audio or audible-output acceptance is claimed; retained underrun diagnostics
are diagnostic evidence only.

The Release build and capture both use producer `0d8556e`. Rebase onto current
main preserved the six changed JavaScript/test file hashes and the staged
runtime inventory. Current-main unittest discovery passed 2,024 tests (1,873
passed, 151 skipped). The earlier precommit suite ran on the exact six-file
dirty patch recorded in the receipt. An initial focused syntax failure was
reported, but its raw log is not retained. The first packet-summary attempt
raised a Python `NameError` after writing the coordinator; no browser or server
started, and no repository source changed in that attempt.

This is same-host synthetic input and a six-tick CSS component result only.
Physical and keyboard input, Internet or two-machine play, foreground timing,
performance, pixel fidelity, continuous audio, the full supported route, and
whole-session accuracy remain unaccepted.
