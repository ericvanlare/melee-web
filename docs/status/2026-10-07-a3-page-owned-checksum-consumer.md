# A3 bounded page-owned checksum evidence consumption

**Compiled; source-only controller/core component checks**

The [portable receipt](../evidence/a3-page-owned-checksum-consumer-v1.json)
records the bounded interface for
[issue #255](https://github.com/ericvanlare/melee-web/issues/255). Source producer
`e22594d`, based on main `75c2541`, preserves the reviewed two-file patch across
the disjoint main refresh. The normal ordinary Release `gameplay_menu_browser`
build passed with a fresh 32-file inventory and all ten optional flags OFF. The
full suite passed 2,040 tests with 152 skipped. These are integration/component
checks, not a new browser or native lifecycle experiment.

The optional `checksumConsumer(batch, {signal})` accepts one immutable bounded
batch of original 64-byte records. The existing protocol receives the same
frozen bytes first. Exactly `true` acknowledges the whole batch; removal occurs
only while its token remains current and the peer has no failure, terminal,
closing or aborted state. There is no partial-acceptance or persistence promise.
Without a consumer, the original 512-record overflow and drain-RPC behavior stay
unchanged. Consumer mode rejects conflicting diagnostic drain ownership.

Failure, terminal and close signal cancellation outside the serialized queue.
Close bounds the settlement join by the existing timeout and still joins endpoint
cleanup. A callback that ignores abort leaves an explicit unjoined failure and
retained bytes. Late settlement cannot remove evidence, schedule native work or
turn cancellation into acceptance. Start-RPC consumer failures now set the
existing protocol/native terminal directly, without depending on another wake.
The source clock, native arithmetic and wire protocol are unchanged.

The positive fixture uses the actual controller and two actual `LockstepPeer`
cores with explicitly synthetic native/endpoint primitives. Both cores compare
520 fixture-produced records, with observed input ACK 517 and checksum ACK 519;
all produced, consumed and remote records are equal. There is no drain RPC,
pending batch, mismatch or terminal in that fixture. This does not establish
original-game, native or browser state agreement.

All 23 focused tests and the existing lockstep contract check pass. Controls
cover immutable acceptance, default overflow, refusal/throw/rejection/non-true
acknowledgment, startup terminal without progress notification, slow consumption
and coalesced wakes without native overlap, remote terminal while pending,
prompt/ignored abort, bounded join and late settlement. The two earlier 16-of-19
runs are preserved. They exposed an incorrect startup test assumption and the
real direct-RPC terminal gap; neither failure was relabeled as successful.

Before the new build, all 32 earlier native outputs and cache from producer
`15bc102` were copied and verified externally. The earlier
[#247 capture receipt](../evidence/a3-room-worker-webrtc-native-pump-v1.json),
its `5fe6b23` source identity, runtime manifest, failures and retained bytes remain
unchanged. The fresh build in this receipt is a separate producer.

Product progress/controller adapters, user-facing sessions and actual browser
consumer validation remain open. Physical input, sustained native ring/core
memory bounds, Internet/two-machine play, original full-route or whole-session
accuracy, pixels, PCM, uninterrupted audio, timing, performance and tournament
acceptance remain separate [roadmap](../ROADMAP.md) gates.
