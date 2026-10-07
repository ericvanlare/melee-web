import {DurableObject} from 'cloudflare:workers';

const RELAY_VERSION = 1;
const MAX_ROOM_ID_BYTES = 64;
const MIN_ROOM_ID_BYTES = 22;
const MAX_PACKET_BYTES = 1024 * 1024;
const MAX_ROOM_PEERS = 2;
const PEER_DISCONNECTED = 4001;
const RELAY_POLICY_ERROR = 4002;
const RELAY_PACKET_ERROR = 4003;

const ROOM_PATH = /^\/v1\/rooms\/([^/]+)\/socket$/;
const ROOM_ID = /^[A-Za-z0-9_-]+$/;
const encoder = new TextEncoder();

function json(value, status) {
  return new Response(JSON.stringify(value), {
    status,
    headers: {
      'content-type': 'application/json; charset=utf-8',
      'cache-control': 'no-store',
      'x-content-type-options': 'nosniff',
      'referrer-policy': 'no-referrer',
    },
  });
}

function validRoomId(value) {
  const bytes = encoder.encode(value).byteLength;
  return bytes >= MIN_ROOM_ID_BYTES && bytes <= MAX_ROOM_ID_BYTES && ROOM_ID.test(value);
}

function configuredOrigins(env) {
  const raw = env?.RELAY_ALLOWED_ORIGINS;
  if (typeof raw !== 'string' || !raw.trim()) return new Set();
  return new Set(raw.split(',').map(value => value.trim()).filter(Boolean));
}

function route(request, env) {
  const url = new URL(request.url);
  const match = ROOM_PATH.exec(url.pathname);
  if (!match) return json({error: 'unknown relay route'}, 404);
  const roomId = match[1];
  if (!validRoomId(roomId)) return json({error: 'invalid room id'}, 400);
  if (request.method !== 'GET') return json({error: 'WebSocket room requires GET'}, 405);
  if ((request.headers.get('Upgrade') || '').toLowerCase() !== 'websocket')
    return json({error: 'WebSocket upgrade required'}, 426);

  const origin = request.headers.get('Origin');
  const allowedOrigins = configuredOrigins(env);
  if (origin !== null && !allowedOrigins.has(origin))
    return json({error: 'origin is not allowed'}, 403);

  const id = env.ROOMS.idFromName(roomId);
  return env.ROOMS.get(id).fetch(request);
}

export default {
  fetch(request, env) {
    try { return route(request, env); }
    catch (_) { return json({error: 'relay configuration or routing failure'}, 500); }
  },
};

/**
 * One object owns the two accepted server sockets for the lifetime of a room.
 * This uses the standard WebSocket API: its listener-owned Set is not restored
 * after eviction, because accepted standard-API sockets keep the object active.
 */
export class RoomRelay extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.peers = new Set();
  }

  async fetch(request) {
    if ((request.headers.get('Upgrade') || '').toLowerCase() !== 'websocket')
      return json({error: 'WebSocket upgrade required'}, 426);
    if (this.peers.size >= MAX_ROOM_PEERS)
      return json({error: 'room already has two peers'}, 409);

    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);
    server.accept();
    this.peers.add(server);
    server.addEventListener('message', event => this.onMessage(server, event.data));
    server.addEventListener('close', event => this.onClose(server, event.code, event.reason));
    server.addEventListener('error', () => this.onError(server));

    // The first peer waits without sending A2 packets. The ready control packet
    // is emitted only after the second accepted socket is owned by this object.
    if (this.peers.size === MAX_ROOM_PEERS) {
      const ready = JSON.stringify({relay: RELAY_VERSION, event: 'ready'});
      try {
        for (const peer of this.peers) {
          if (peer.readyState !== WebSocket.OPEN)
            throw Error('room peer closed before the ready barrier');
          peer.send(ready);
        }
      } catch (error) {
        this.closeRoom(server, PEER_DISCONNECTED, 'peer transport failed');
        throw error;
      }
    }
    return new Response(null, {status: 101, webSocket: client});
  }

  onMessage(sender, message) {
    if (!this.peers.has(sender)) return;
    if (typeof message !== 'string') {
      this.closeRoom(sender, RELAY_PACKET_ERROR, 'A2 relay accepts text packets only');
      return;
    }
    if (encoder.encode(message).byteLength > MAX_PACKET_BYTES) {
      this.closeRoom(sender, RELAY_PACKET_ERROR, 'A2 packet exceeds 1 MiB');
      return;
    }
    const recipients = [...this.peers].filter(peer => peer !== sender);
    // Readiness is an explicit barrier. Never queue or replay pre-ready A2 data.
    if (this.peers.size !== MAX_ROOM_PEERS || recipients.length !== 1) {
      this.closePeer(sender, RELAY_POLICY_ERROR, 'peer is not ready');
      return;
    }
    const recipient = recipients[0];
    if (recipient.readyState !== WebSocket.OPEN) {
      this.closeRoom(sender, PEER_DISCONNECTED, 'peer disconnected');
      return;
    }
    try { recipient.send(message); }
    catch (_) { this.closeRoom(sender, PEER_DISCONNECTED, 'peer transport failed'); }
  }

  onClose(sender) {
    if (!this.peers.has(sender)) return;
    // The standard API dispatches close after the sender's close handshake has
    // completed. Close its partner cleanly; do not try to echo on a CLOSED socket.
    for (const peer of this.peers)
      if (peer !== sender) this.closePeer(peer, PEER_DISCONNECTED, 'peer disconnected');
    this.peers.delete(sender);
  }

  onError(sender) {
    if (!this.peers.has(sender)) return;
    this.closeRoom(sender, PEER_DISCONNECTED, 'peer transport failed');
  }

  closeRoom(sender, code, reason) {
    for (const peer of this.peers)
      if (peer !== sender) this.closePeer(peer, PEER_DISCONNECTED, 'peer disconnected');
    this.closePeer(sender, code, reason);
  }

  closePeer(peer, code, reason) {
    if (peer.readyState === WebSocket.OPEN || peer.readyState === WebSocket.CONNECTING)
      peer.close(code, reason);
  }
}
