# A3 room relay transport

The Worker accepts `GET /v1/rooms/{roomId}/socket` WebSocket upgrades and routes each room to one Durable Object. A room owns at most two accepted server sockets. Room IDs are 22–64 base64url characters; the client helper creates a fresh 24-byte random token.

The relay forwards opaque A2 text packets without decoding or changing them. Each packet is limited to 1 MiB. Binary packets and oversized text packets close the sender with relay code 4003 and the other peer with 4001. The client adapter caps each socket's queued outbound bytes at 1 MiB, including the new packet, and bounds pending inbound callback work to 1 MiB and 256 messages.

The first socket waits without sending A2 data. When the second joins, the relay sends both `{"relay":1,"event":"ready"}`. The adapter consumes that control message and returns the pair only after both sockets are ready. The Worker never buffers or replays pre-ready A2 packets: an early packet closes its sender with 4002 (`peer is not ready`). Readiness remains a barrier before the existing A2 identity handshake.

The Worker uses the standard Durable Object WebSocket API: each server socket calls `accept()`, installs message/close/error listeners, and is tracked in an explicit room `Set`. The runtime completes a clean sender close before dispatching that socket's close event; the Worker closes the remaining peer with 4001. The sender's received close code and reason therefore come from the WebSocket close handshake, and the relay does not try to close an already-closed sender socket. Runtime close behavior and cleanup are covered by the local HTTP/WebSocket integration.

This API choice has an operating tradeoff. Cloudflare documents that the standard API retains the Durable Object while its WebSocket is open and can accrue duration charges for that lifetime. The hibernation API can hibernate eligible objects and does not accrue duration while hibernating. The retained pinned-runtime hibernation reducers failed when one socket's close listener closed the other socket; a direct Miniflare standard-API reducer passed both clean closes. No production deployment, usage, duration cost or performance was measured. See Cloudflare's [WebSocket API guidance](https://developers.cloudflare.com/durable-objects/best-practices/websockets/), [standard WebSocket server example](https://developers.cloudflare.com/durable-objects/examples/websocket-server/) and [pricing](https://developers.cloudflare.com/durable-objects/platform/pricing/).

Inbound application callbacks run serially and their returned promises are awaited. `drainInbound()` waits for callbacks already received and surfaces rejected callbacks. `flush()` waits only for the local WebSocket send buffer to drain. Neither method implies a peer acknowledgement; A2's own ACK protocol remains responsible for that.

The local integration parses the actual `wrangler.jsonc` with the locked Wrangler config reader, loads the actual Worker module and relative imports in Miniflare, validates the derived options, and uses a real HTTP/WebSocket listener. It deliberately bypasses Wrangler's local proxy because its earlier startup/close failures remain retained and unresolved; this test does not claim that proxy path is fixed. The local tools are pinned in `dependencies.lock.json`:

```sh
MELEE_A3_ROOM_RELAY_TOOLS=work/a3-room-relay-tools/node_modules \
  python3 scripts/agent_workspace.py run -- node --test tests/net_room_relay_worker.test.mjs
```

This is a transport and A2 component boundary only. It does not establish complete two-machine session acceptance, WebRTC fallback, rollback, H1/#116 acceptance, #84 foreground/sustained acceptance, competitive readiness, or production deployment. See [issue #179](https://github.com/ericvanlare/melee-web/issues/179) and [architecture decision 016](../../docs/ARCHITECTURE.md#016--online-play-starts-as-lockstep-from-a-shared-css-context) for the stopping rules and larger acceptance boundaries.
