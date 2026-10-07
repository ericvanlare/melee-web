# A3 first-checksum desync over Room Worker-signaled WebRTC

**Source identified / Browser exercised**

The [portable capture receipt](../evidence/a3-room-worker-webrtc-desync-v1.json)
records one accepted bounded original CSS experiment for
[issue #243](https://github.com/ericvanlare/melee-web/issues/243). Capture source
`a94c37e`, based on main `1923f73`, reused native ordinary Release producer
`0d8556e` with exact unchanged target inputs and 37 staged identities. This was
not a new native build at the capture source. The later
[current-main integration receipt](../evidence/a3-room-worker-webrtc-desync-integration-v1.json)
records a separate fresh ordinary Release build and full suite after integration
onto main `b58747c`; it does not replace or repeat the browser capture.

Both pages owned their protocol peers, reliable ordered host-only WebRTC channels
and signaling through the existing local RoomRelay Worker. Node orchestrated
recorded input, polling/evidence export and lifecycle. The selected recipe had
18 inputs plus two neutral bootstrap ticks. The existing native-only alpha
mutation changed input 10, byte 2, bit 0, leaving exchanged recorded contributions
unchanged. Both peers observed input ACK 17 and exactly one versus zero native
local divergences. Raw and protocol evidence first differed at source tick 12,
channel 1, after an equal prefix through 11. Both native terminals held
kind 1/tick 12/channel 1 at actual cursor 16/pushed 20 for 124 ms.

Each peer retained 16 raw records, source indices 0–15, with 16 exact source steps
and draws. The matching protocol prefix was 12 records, remote checksum count 14
and checksum ACK 10. All 16 local records were submitted while active; zero were
classified as post-terminal evidence. The three raw records 13–15 were equal in
this run, an observed tail equality that does not establish later protocol
comparison or consumption stopping exactly at the mismatch. The receipt keeps
raw tails, active submission, remote receipts, ACKs and comparison counts separate.

The publisher fix follows actual peer RPC terminal observations before another
publication or ACK wait. Expected flip desync returns into the existing terminal
pair, native hold and raw first-mismatch checks; unexpected or mixed terminals
fail explicitly. The actual-controller/core reproducer retained the original
inactive-beta and ACK-stall failures. Focused tests cover alpha/beta publication,
ACK refresh, between-batch drain, positive exact ACK, mixed terminal and missing
ACK without terminal. These fixtures use explicitly synthetic native/endpoint
primitives and do not add browser evidence. The initial staging refusal while
the full suite held the checkout mutex and the corrected copied packet exclusion
are also retained.

Both held original CSS screenshots have ready source draw diagnostics and GPU/
isolation observations. Reviewed images show red banner/horizontal roster
geometry and lower viewport clipping. Alpha's screenshot overlay shows 64 audio
underrun frames and beta's 107; these are snapshots, not complete or final audio
witnesses. Speakers were muted with audio processing enabled. There was no pixel,
PCM, uninterrupted audio, audible-output, foreground timing or performance comparison.

Root independently verified 26 recorded PIDs and seven groups absent, including
outer wrapper/coordinator identities recorded while live; three ports bindable;
Worker-owned paths absent; Chrome temporary storage empty; successful profiles
removed; and all 37 served identities unchanged before/after over HTTP and local
storage. Endpoint, callback, transport, page and cleanup errors were empty. Root
released the exact timing reservation after independent result review. Earlier
failed #238 profiles and evidence remain preserved.

Autonomous browser progress/native checksum pumping and user-facing session
integration remain open: this harness still drives the page pump through Node
RPCs. Internet/two-machine play, STUN/TURN, relay fallback, physical/live input,
full-route negative control, retail/whole-session accuracy and general A3 or
tournament acceptance are unclaimed. The broader
[roadmap](../ROADMAP.md) gates remain open.
