import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import http from 'node:http';
import {readHashedPreload} from './results_preload_observer.mjs';

const body = Buffer.from([0, 255, 34, 71, 9, 0, 3]);
const server = http.createServer((request, response) => {
  if (request.url !== '/fixture.data') { response.writeHead(503); response.end(); return; }
  response.writeHead(200, {'Content-Type':'application/octet-stream', 'Content-Length':body.length});
  response.end(body);
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
try {
  const root = `http://127.0.0.1:${server.address().port}`;
  const read = await readHashedPreload(root + '/fixture.data');
  assert.deepEqual(Buffer.from(read.buffer), body);
  assert.deepEqual(read.observation, {
    url:root + '/fixture.data', status:200, bytes:body.length,
    sha256:createHash('sha256').update(body).digest('hex'),
    method:'fixture-received-array-buffer-webcrypto',
  });
  await assert.rejects(readHashedPreload(root + '/missing'), /Preload HTTP 503/);
} finally { await new Promise(resolve => server.close(resolve)); }
console.log('Exact received preload bytes/hash and failed HTTP are observed without game calls');
