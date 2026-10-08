# Runtime-owned live input from CSS to SSS

**Source identified / Browser exercised**

Issue #281 has one completed, bounded headless browser capture of the original
CSS-to-SSS route using runtime-owned peers. The [portable receipt](../evidence/issue281-runtime-css-sss-v1.json)
binds the capture source at `7c5faca63a8f820d29dffbf74bd40c30f15f14f6`, the
test-only final validation head at `c414a0aa2cb9e3183a3a3d8cfff51c7e2865856c`,
and the reused ordinary Release producer `1792f5bf512d57fc1be8436d4087fb8320062ac3`.
The capture source and final test head are separate identities; no native or
runtime production files changed in the final test fix.

Installed Chrome 154.0.8037.98 ran headless and silent on the same-host Mac mini,
with loopback Room Worker signaling and a reliable, ordered host-only WebRTC
data channel. The retained run inventory is
`issue281-runtime-css-sss-seed-305419896-attempt-02`.
For each peer, the harness supplied 518 synthetic standard Gamepad samples
through the ordinary controller manager and captured the resulting native live
PAD input. Each peer completed 520 original source steps and draws, exported
520 ordered checksum records, and received all 518 input and 520 checksum ACKs.
The streams matched byte-for-byte and by SHA-256. The route observed 154 CSS
records followed by 366 SSS records, ending at source cursor 520 in native phase
3; no SSS selection or gameplay occurred. The read-only progress interval
advanced both cursors through 512 without a peer RPC.

The final SSS boundary retained a screenshot, browser driver state, and GPU
diagnostics for each peer. Both screenshots were visually reviewed as original
SSS renders. The cursor and phase remained stable across screenshot capture;
WebGPU API and adapter were present. This does not establish pixel equivalence.
Both runtime owners and the Room Worker closed; independent process and port
checks found no owned survivors, and port 8787 was bindable after cleanup.

Attempt 01 remains preserved as a functional-only capture because it omitted
the final SSS screenshots and GPU/driver diagnostics; it is not relabeled as a
complete visual observation. The first focused-control failure is retained as
a transcribed summary because its raw TAP output was not saved; the corrected
114-control raw log is retained. A later full-suite run exposed a test VM
fixture that omitted the runtime mode for an extracted legacy close path. The
one-line test-only correction was reviewed, focused Node and Python boundary
checks passed, and the final unfiltered suite passed 1,922 tests with 153 skips
and no failures. The earlier failed suite remains preserved separately.

This is bounded local browser evidence. It does not establish physical input,
remote-peer or Internet operation, live timing, sustained performance,
uninterrupted audio or audio fidelity, pixel equivalence, or whole-session
acceptance. The supported CSS → SSS → four-stock Mario/Final Destination →
Results → CSS route and its independent acceptance gates remain open.
