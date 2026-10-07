import net from 'node:net';
import {RelayDecoder, relayFrame} from './net_lockstep_protocol.mjs';

/** Two local peer sockets bridged as opaque length-framed bytes. The relay
 * performs no peer, input, acknowledgement, or checksum interpretation. */
export async function openLoopbackPeerPair({onDisconnect = () => {}, onEndpointError = () => {}} = {}) {
  const sockets = [];
  const early = [[], []];
  const traffic = {alpha_to_beta_bytes: 0, beta_to_alpha_bytes: 0};
  let acceptResolve;
  const accepted = new Promise(resolve => { acceptResolve = resolve; });
  const server = net.createServer(socket => {
    if (sockets.length >= 2) { socket.destroy(); return; }
    const index = sockets.length;
    sockets.push(socket);
    socket.setNoDelay(true);
    socket.on('data', chunk => {
      if (index === 0) traffic.alpha_to_beta_bytes += chunk.length;
      else traffic.beta_to_alpha_bytes += chunk.length;
      const other = sockets[index ^ 1];
      if (other && !other.destroyed) other.write(chunk);
      else early[index].push(Buffer.from(chunk));
    });
    socket.on('close', () => onDisconnect(index === 0 ? 'alpha' : 'beta', 'loopback socket closed'));
    socket.on('error', error => onDisconnect(index === 0 ? 'alpha' : 'beta', String(error.message || error)));
    if (sockets.length === 2) {
      for (const [sender, chunks] of early.entries())
        for (const chunk of chunks) if (!sockets[sender ^ 1].destroyed) sockets[sender ^ 1].write(chunk);
      early[0].length = early[1].length = 0;
      acceptResolve();
    }
  });
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const {port} = server.address();
  const connect = () => new Promise((resolve, reject) => {
    const socket = net.createConnection({host: '127.0.0.1', port});
    socket.once('connect', () => resolve(socket));
    socket.once('error', reject);
  });
  const alpha = await connect(), beta = await connect();
  await accepted;

  const endpoint = (socket, role) => {
    const decoder = new RelayDecoder();
    const listeners = new Set();
    let chain = Promise.resolve();
    const errors = [];
    const reportError = error => {
      const row = {role, message: String(error?.stack || error?.message || error)};
      errors.push(row);
      try { onEndpointError(role, error); } catch {}
    };
    socket.on('data', chunk => {
      let messages;
      try { messages = decoder.push(chunk); }
      catch (error) { reportError(error); socket.destroy(error); return; }
      for (const message of messages)
        for (const listener of listeners) chain = chain.then(() => listener(message)).catch(error => {
          reportError(error);
        });
    });
    return {
      send(text) {
        if (socket.destroyed) return Promise.reject(Error('Loopback peer socket is closed'));
        const bytes = relayFrame(text);
        return new Promise((resolve, reject) => socket.write(bytes, error => error ? reject(error) : resolve()));
      },
      onMessage(listener) { listeners.add(listener); return () => listeners.delete(listener); },
      get errors() { return [...errors]; },
      async flush() { await chain; },
      close() { if (!socket.destroyed) socket.end(); },
      destroy() { if (!socket.destroyed) socket.destroy(); },
      get closed() { return socket.destroyed; },
    };
  };
  return {
    alpha: endpoint(alpha, 'alpha'), beta: endpoint(beta, 'beta'), traffic, port,
    async close() {
      for (const socket of sockets) if (!socket.destroyed) socket.destroy();
      await new Promise(resolve => server.close(() => resolve()));
    },
  };
}
