# Local two-peer lockstep across the original route

**Source identified / Browser exercised: local loopback / Not retail compared or performance admitted**

Two fresh, headless installed Chrome instances completed original CSS → SSS →
two-human, four-stock Mario/Final Destination → original Results → original CSS
through the development network-input boundary. All 5,084 per-tick records agreed
across the six declared checksum channels. This is local functional evidence for
[A2](../ROADMAP.md#current-priorities), not Internet play or H1 acceptance.

The [portable receipt](../evidence/local-lockstep-a2-v1.json) binds the private
run names, raw reports, checksums, screenshots, build outputs, failed attempts and
reduced reproductions by size and SHA-256. It separates the native producer
`01f29ba77c700394e63a89687aefe05f0548b77f` from the final browser runner
`06970593792e40046d6dfe1753ec8d97aa3ab836`; later harness repairs do not relabel the
historical native build. The earlier build invocation lost its shell exit status;
the retained official incremental confirmation completed successfully.

## Protocol and ownership

The existing A1 development boundary supplies native input and drains native
checksums. Two Node-controlled peers communicate through a real loopback TCP
relay using bounded, length-prefixed JSON. The relay forwards opaque bytes; it
does not simulate either player. Each peer publishes only its own port and uses a
two-source-tick delay, with neutral samples for the initial two ticks. Ports 3/4
stay disconnected in this scenario. This is a harness transport, not a shipped
browser multiplayer UI.

Before tick zero, peers compare Wasm, disc DOL/FST and prepared source identity,
including the full save-card and semantic PAD-history boundaries. A dedicated
CDP session observes the actual initial Wasm response, freezes its digest before
the handshake's fresh fetch, and checks both against the producer's bytes. The
native start barrier, missing-input wait, checksum backpressure, terminal and
completion boundaries are distinct. Input is indexed: identical duplicates are
idempotent, while conflicts and gaps fail explicitly. There is no local-input
fallback or automatic timing-pause resume.

The transport maintains ordered publication, acknowledgements and bounded
retransmission. A reduced real-browser CSS probe withheld an input while ACK
traffic continued: the source cursor held at 3 for 120 ms, then progressed once
the intended sample was released. That probe also exercised duplicate and
out-of-order packets. Its eight-tick result is independent of full-route proof.

## Full route and rendered boundaries

The fresh route ran on Chrome 154.0.8037.98 with host speakers muted and audio
processing retained. The 5,082 authored input ticks plus two neutral prefix ticks
produced matching streams with SHA-256
`4ac68c125ef7de127db9ab6c56e3fd40665bd42f02bff7ad8a75519b461d82a0`.
The scene sequence was CSS, SSS, match, Results, CSS on both peers. Native setup
observations confirmed two humans, four stocks each and Final Destination.
The declared source/input/PAD/scene/object/total channels agree; raw arena hashes
differ, so this is not whole-heap equality.

Both peers retained screenshots and GPU observations at all five boundaries.
The watcher requires a new native callback with no preparation, a completed
source draw, positive draw calls and no draw suppression. Phase stayed stable
across each screenshot; source cursors continued advancing. Initial CSS was
captured at cursors 2–5, despite an older raw label saying “before input
publication.” The integration label now describes the first ready source draw.
SSS and Results screenshots capture entry animations, not fixed-tick comparisons.
The returned CSS image is named `final.png` in each peer's retained directory.

All ten screenshots were inspected by the execution agent and lead. Match images
show conspicuous magenta Final Destination geometry. The same visible symptom
exists in hash-bound A1 screenshots, whose native builds differ from this one.
That establishes a prior observation, not a rendering cause or pixel equivalence.
The rendering defect remains open in
[#175](https://github.com/ericvanlare/melee-web/issues/175). Earlier full-route
screenshots taken during preparation are preserved and do not count as ready-draw evidence.

## Negative controls and reduced failures

- Flipping input tick 10, byte 2, bit 0 on port 0 left ticks 0–11 equal and first
  diverged at delayed source tick 12, input channel 1. Both native peers held
  terminal kind 1, tick 12, channel 1 for 126 ms without changing cursor or pushed
  counts.
- Disconnecting at source tick 16 held both native peers in terminal kind 2 for
  125 ms, with cursor and pushed counts fixed at 16. Neither peer resumed timing
  or fell back to local inputs.
- The first flip attempt stopped before tick zero: Chrome evicted the large
  Wasm response from its default inspector cache. Two real HTTP/Chrome probes
  reproduced that boundary and verified a dedicated bounded CDP session. Its
  frozen digest also remained unchanged after a later response changed.
- The first disconnect attempt reached native terminals but then attempted to
  send a terminal message through the closed socket. A real TCP regression
  failed before the repair and passed afterward. Transport-close notification
  now invokes the native terminal callback without sending another packet;
  callback failures and ordinary protocol/desync notifications remain observable.
- The first full route failed its route validator because it compared per-tick
  scenes with a compressed scene sequence. The actual records remain preserved;
  the corrected validator collapses consecutive equal scenes.

The raw full-route receipt contains unused zero-valued byte placeholders under
`loopback_transport`. Actual measured relay counters are under `transport`.
Integration removes those placeholders; historical reports remain unchanged.

## Integration and remaining gates

Focused protocol, observer and native-input tests cover real TCP delivery and
failure propagation, response identity, wait recovery, indexed input and terminal
holds. The current-main integration suite passed 1,928 tests with 141 skipped
at `db65a95`. A later review found that one failed browser launch could leave its
successful sibling outside cleanup ownership. The actual runner/finalizer failed
a reduced injected-startup test before repair; all 25 focused checks passed after
`6537e35` registered every successful launch before rethrowing a sibling error.
The official Release runtime build passed on that clean source, with all 32
packaged artifact identities retained. Linux CI then rejected a compact C test
fixture under GCC's misleading-indentation warning. An explicit block fixes that
fixture without changing runtime source; its focused native test passes locally.
The failed CI log/artifact is retained, and final PR-head CI remains the merge gate.
The receipt also preserves the earlier lifecycle-fixture failure and an invalid
build CLI invocation that stopped before compilation. Browser evidence above
keeps its original source identities. Both successful controls and the full route
closed their owned browsers, relay and HTTP server; failures and active incremental builds remain
retained. Unrelated processes were left alone.

Cross-browser and cross-architecture determinism, retail state and PCM comparison,
pixel fidelity, foreground timing, physical input, audible output and sustained
performance remain separate. Internet lockstep stays gated by H1 under the
[roadmap](../ROADMAP.md#current-priorities) and
[accuracy contract](../ACCURACY_CONTRACT.md).
