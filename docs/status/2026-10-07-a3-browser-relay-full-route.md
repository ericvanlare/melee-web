# A3 browser lockstep through the local Worker relay

**Source identified / Compiled / Browser exercised: local Worker relay**. The
[portable receipt](../evidence/a3-browser-relay-full-route-controls-v1.json)
binds the historical browser capture producer, served Release artifacts, the
actual local Worker runtime, scenario reports, and cleanup evidence. It records
a separate fresh build and suite validation after an identical-patch rebase onto
current main; the browser route was not repeated.

The Node harness owns both A2 `LockstepPeer` protocol instances and WebSocket
endpoints; two independent headless Chrome runtimes load and execute the game
source while the harness supplies each peer's inputs. The actual local Worker
and Durable Object relay route those Node-owned endpoints. The run used producer
`4b58eaeb6a1821f7ff7bbe12f5f060408261f71a` (tree
`a4293650c96867e7be230ee0ae55ae49362b0b74`), based on main
`6c0ccecd6c88d50670668a032a6882463639e809`. Both peers completed the original
CSS → SSS → four-stock Mario/Final Destination → Results → CSS route with scene
sequence `[1, 2, 3, 4, 1]`. Five route-boundary captures per peer reported ready
source draws and GPU observations. The peers agreed on all 5,084 checksum
records and each recorded 5,084 source steps and draws.

Two bounded controls used the same transport and A2 protocol. Flipping local
port 0 at input tick 10, PAD byte 2 bit 0 first diverged at delayed source tick
12 on channel 1, after an equal checksum prefix through tick 11. Both peers
reported a desync terminal and stayed held with cursor 16 and pushed count 20
unchanged for 125 ms. The transport-disconnect control closed the beta socket
at source tick 16; both peers reported disconnect terminals and held cursor and
pushed count 16 unchanged for 124 ms. In all three runs, the identity handshake
completed before tick 0, callback/source accounting passed, and the coordinator
recorded no endpoint, transport, callback, or cleanup errors.

The historical browser batch used headless installed Chrome 154.0.8037.98 and
the pinned SDK Node 24.19.0, Wrangler 4.131.1, Miniflare 5.20260911.0-alpha
and workerd 1.20260911.1. Its Release artifacts were built from capture producer
`4b58eaeb6a1821f7ff7bbe12f5f060408261f71a`; the 32-file local and HTTP
inventories matched before and after every scenario. Browser audio output was
muted while normal audio processing remained enabled. The capture-producer full
Python suite passed with 1,961 total tests: 1,817 passed and 144 skipped.

After preserving those 32 historical served files, the implementation was
rebased onto main `84fe5c4b64a3fc83459b00bba9730e5b38825613`. The range-diff
confirmed both implementation patches were identical. A fresh Release `runtime`
build and full Python suite passed on executable producer
`51d7d76e450981d4e33ad7abbea0af17f43066b5` (tree
`a22d9fcb8ddac3f2b68752cfe7e42e848fa1691c`): 1,964 tests total, 1,819 passed,
and 145 skipped. The build produced a new 32-file served inventory; only
`gameplay_menu_browser.wasm` differed from the preserved pre-build inventory.
This was build/suite validation only; it did not repeat the browser capture.

The positive run recorded the startup network wait at source tick 2 for both
peers. The flip run recorded the same initial wait; the disconnect run recorded
wait observations at ticks 2 and 16. The run reports retain these observations
and their `timing_resumes` arrays, which were empty; no timing threshold or
performance claim was added. The positive report also retains one aborted
`gameplay_menu_browser.data` request during browser teardown; there were no
page errors and all finalizer checks passed.

The browser owner exited normally in each run. All 65 owned browser process
identities and their six groups were absent without signal intervention; all
three HTTP server groups and Worker groups were gone, and nine owned ports were
free. Each Worker completed IPC disposal and exited zero without fallback or
runtime errors. The six exact passing browser profiles were removed after
cleanup verification. The earlier v3 CSS-probe validation failure and v4
CSS-only pass remain separate historical evidence in the receipt; neither is
relabelled as full-route evidence. A first offline disconnect-report assertion
mistook the missing-input index for a source tick; its failed assertion and
corrected two-tick-delay relationship are retained separately.

This is a single-host, headless functional run, not two-machine or Internet
acceptance. It does not establish WebRTC, rollback, production deployment,
performance or timing equivalence, pixel or PCM equivalence, foreground input
or audible-output acceptance, or tournament readiness. Root's image review
noted the existing magenta Final Destination appearance tracked in
[#175](https://github.com/ericvanlare/melee-web/issues/175). This is component
evidence for [issue #187](https://github.com/ericvanlare/melee-web/issues/187),
not broad online or whole-session acceptance.
