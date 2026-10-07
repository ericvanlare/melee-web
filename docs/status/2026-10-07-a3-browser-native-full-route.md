# A3 browser-owned native full route

**Source identified / Compiled / Browser exercised** through the local actual
Worker relay. The [portable receipt](../evidence/a3-browser-native-full-route-v1.json)
binds the source, runtime inventories, launch packets, reports, retained
failures, suite/build logs and cleanup for [issue #205](https://github.com/ericvanlare/melee-web/issues/205).

Both headless Chrome pages own the unchanged portable protocol and WebSocket
endpoint, indexed native input queue, checksum drain and terminal callbacks.
The runner selects this with `--peer-owner browser`; the default Node-owned mode
remains available. Node supplies the recorded recipe and orchestrates evidence.
The page export queue is bounded to 512 records, and overflow or asynchronous
callback failure remains sticky and fails the run. Active records submitted to
the protocol before export are counted separately from post-terminal evidence.

The complete supported original CSS → SSS → four-stock Mario/Final Destination
→ Results → CSS run used executable producer
`a57a0e421e4e4abc61f249e0958ab8b6e5dfb1af`, tree
`37c881f5ccde730202b2786e2fd75fd7d8ce3f56`, based on main `789de9b`.
Both peers consumed all 5,082 recorded inputs with two source ticks of delay.
Each recorded exactly 5,084 source steps, draws and checksum records; each
protocol peer received and compared all 5,084 remote records. Both terminals
were null. The 325,376-byte raw checksum streams matched at SHA-256
`4ac68c125ef7de127db9ab6c56e3fd40665bd42f02bff7ad8a75519b461d82a0`.
All records were active page-owned submissions; post-terminal counts were zero.
The reports retain input/checksum duplicates, out-of-order inputs and ACK counts.
Timing-resume arrays were empty; this adds no timing or performance acceptance.

An earlier eight-source-tick/six-input CSS probe and bounded disconnect and
first-desync controls used historical producer `bf407f6`, tree
`16db37f776f5cf781f3bd7c83b0bd3dad2cb3d04`. The probe held alpha's cursor at
3 for 120 ms. Disconnect withheld source tick 16 (missing input 14): both native
cursors held at 16 for 124 ms, with 16 matching raw records but only 14 protocol
comparisons before termination. Flipping input tick 10, port 0, byte 2 bit 0
first diverged at source tick 12/channel 1 after equal raw ticks 0–11. Both
protocol/native terminals agreed, with cursors 15/14 and pushed queues 20 stable
for 122 ms. These historical controls were not repeated on the current-main
producer; their identities remain separate in the receipt.

All 35 source/staged/local and pre/post HTTP artifact identities matched per
run, including the existing 32-name runtime catalog and three exact peer
modules. Each page's actual peer module response bodies and initially loaded
WASM body matched their frozen identities. Start identity agreed before tick 0.
The current-main Release build changed only WASM relative to the historical
inventory. Full Python discovery passed 1,988 tests: 1,843 passed and 145 skipped,
in 346.362 seconds; the ordinary Release `runtime` build passed with normal job
defaults. Offline validator mutations are separate synthetic checks. Retained
component-test and packet-preparation failures are identified in the receipt.

All five route-boundary captures per peer passed ready-draw, GPU diagnostic and
phase-stability checks. Root inspected all five alpha phases and beta's match.
The known Final Destination magenta/geometry defect [#175](https://github.com/ericvanlare/melee-web/issues/175)
persists. Screenshots preserve the route phase; source cursors may advance while
they are taken. This is not pixel fidelity. The full-route owner reconciled 22
OS observations covering 20 PIDs; all owned processes were absent without
signals, HTTP/Worker were closed, disposal needed no fallback, and passing
profiles were removed. CDP process inventory was not observed.

This is single-host, headless, recorded-input functional lockstep through direct
Miniflare. It does not establish Internet/two-machine or WebRTC acceptance,
live/physical input, foreground timing, uninterrupted audio, audible output,
pixel/PCM equivalence, general performance, whole competitive-session or
tournament acceptance. Speakers stayed muted while normal audio processing
continued. Broader A3, H1/#116 and #84 gates remain open.

The next bounded experiment should sample one neutral input and one declared
synthetic button transition inside the browser, then trace delayed delivery,
ACKs and native consumption through the existing Worker. Stop on the first
missing/reordered input, ACK or cursor discrepancy. Keep physical input,
foreground timing, Internet and full-route claims excluded. Reuse the page
controller and relay; inspect existing room signaling before a separate WebRTC
experiment. This is a source-only follow-up proposal, with no implementation or
new capture in this change.
