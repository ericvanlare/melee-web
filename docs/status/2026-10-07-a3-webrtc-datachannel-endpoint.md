# A3 local WebRTC DataChannel endpoint

**Source identified / Browser exercised**

The [portable receipt](../evidence/a3-webrtc-datachannel-endpoint-v1.json)
records the component result for [issue #222](https://github.com/ericvanlare/melee-web/issues/222).
Two local pages in headless Chrome attached the portable A2 peer to a direct,
reliable ordered WebRTC DataChannel and exchanged the existing start-identity
hello. Alpha attached when its channel was `connecting`; beta attached
synchronously in the `datachannel` event when its channel was already `open`.
Both peers reached ready with the same agreement hash and complementary local
ports.

The probe loaded only its CSS page and the portable A2/WebRTC modules. Each
portable peer sent two exact neutral bootstrap records to an explicit test
sink; no native runtime JavaScript or WebAssembly was loaded. Both peers closed
with one disconnect notification, no endpoint/page/request errors, and no
cleanup errors. Four ready/closed screenshots and WebGPU adapter metadata are
bound by the receipt. The CSS fixture does not validate game rendering.

The first launch packet stopped during prelaunch review: it awaited beta's
`datachannel` event before producing its answer and expected beta to be
`connecting` at that event. The revised packet returned the answer before the
coordinator awaited attachment and asserted the spec-defined beta `open` state.
No browser run failed. The v1 packet, manifest and self-review metadata remain,
but its private runner and page-probe bytes were overwritten during v2
preparation; the receipt does not claim those bytes were retained.

After rebasing onto current main, the core and WebRTC endpoint file hashes match
the captured producer exactly. The existing Python lockstep-contract wrapper
now includes the WebRTC endpoint unit file. The focused Node suite passed 50
tests, the wrapper passed, and current-main unittest discovery passed 2,013
tests (1,864 passed, 149 skipped). The validation receipt distinguishes the
suite's pre-commit execution tree plus the exact one-line test-wrapper patch
from the later clean integration-test commit. No native build was needed.

This is a local browser transport component result only. Native runtime use,
gameplay input, keyboard or physical controllers, Internet or two-machine
play, hosted signaling, foreground timing, performance, audio acceptance,
game-render accuracy and the full supported route remain untested.
