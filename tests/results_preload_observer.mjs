// Hash the exact bytes delivered to the fixture before Emscripten consumes
// them. Large preload bodies can be evicted from Chrome's inspector cache;
// this observation does not depend on Network.getResponseBody retention.
export async function readHashedPreload(url) {
  const response = await fetch(url);
  if (!response.ok) throw Error(`Preload HTTP ${response.status}: ${response.url}`);
  const buffer = await response.arrayBuffer();
  const digest = await crypto.subtle.digest('SHA-256', buffer);
  return {
    buffer,
    observation: {
      url: response.url,
      status: response.status,
      bytes: buffer.byteLength,
      sha256: [...new Uint8Array(digest)].map(value => value.toString(16).padStart(2, '0')).join(''),
      method: 'fixture-received-array-buffer-webcrypto',
    },
  };
}
