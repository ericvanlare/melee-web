# Simulated-network room test lane

**Browser exercised / Same-machine simulated network / Incomplete functional milestone**

[Milestone #307](https://github.com/ericvanlare/melee-web/issues/307) builds a
repeatable network test lane for two independent headless Chrome profiles on
one machine. Both use the existing Host/Join UI and actual keyboard bindings.
A separate opaque TCP proxy shapes their WebSocket traffic to the pinned local
Worker. It preserves byte order and records per-connection hashes, bounded
queues and disposal. This is simulated-network evidence; public staging #288
and two-machine acceptance remain separate.

The [scoped receipt](../evidence/network-simulation-lane-v1.json) binds the
runtime producer, capture source, raw reports and independent review. The first
moderate-delay CSS attempt failed at disconnect. Both original CSS keyboard
paths and the complete declared checksum comparison passed, and every proxy
connection preserved its bytes. Beta then attempted a queued input publication
while its relay socket was closing. That rejection became a sticky protocol
error, and its UI Leave action did not confirm clean teardown. Native
quiescence and final owned-process absence were verified separately. The run
remains failed; these component results do not satisfy the milestone.

Real WebSocket reducers now cover publication before and after the disconnect
callback, queued input, inbound acknowledgements and native-progress sends.
The repair retains exact sampled inputs and records cancelled packet attempts;
another packet may already have carried the same input. Genuine transport and
input errors remain failures. Focused controls pass, with the full validation
and repaired browser result still pending. The existing Leave, checksum and
native cleanup guards stay intact before another bounded browser run.

The receipt retains the earlier proxy half-close/read-buffer failures and the
independently identified shared-counter bug through the external evidence
inventory. Focused real-socket controls now cover small and large writes,
concurrent connections, byte hashes, delay, bandwidth, stalls, cuts and cleanup.
Those controls are not a successful complete browser session.

No full match, public routing, TLS/NAT, human physical input, audible output,
foreground timing or original-game comparison was established by this attempt.
Configured delays describe each client/Worker leg; the measured HTTP round
trips are not peer RTT or input latency. Failed profiles and reports remain
retained. Stadium stays checkpointed while this sprint owns the capture lane.
