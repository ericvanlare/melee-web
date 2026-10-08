/** Real local HTTP inventory controls; no browser, native binary or game assets. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {browserArtifactInventory, validateBrowserArtifactNames} from '../scripts/browser_artifact_inventory.mjs';

const names = JSON.parse(await fs.readFile(new URL('../tools/browser_build_artifacts.json', import.meta.url), 'utf8'));
assert.equal(names.length, 34);
assert(names.includes('net_lockstep_core.mjs'));
assert(names.includes('net_lockstep_native_adapter.mjs'));
assert.deepEqual(validateBrowserArtifactNames(names), names);
for (const invalid of [[], ['one', 'one'], [''], [' '], [42], [null],
  ['../outside'], ['sub/../outside'], ['/absolute'], ['\\outside'],
  ['..\\outside'], ['C:\\outside'], ['%2e%2e%2foutside'], ['file?query'], ['file#fragment']])
  assert.throws(() => validateBrowserArtifactNames(invalid), /allowlist/);

const root = await fs.mkdtemp(path.join(os.tmpdir(), 'melee-artifact-inventory-'));
const localRoot = path.join(root, 'local'), servedRoot = path.join(root, 'served');
await fs.mkdir(localRoot);
await fs.mkdir(servedRoot);
const bytes = name => Buffer.from(`synthetic artifact: ${name}\n`);
for (const name of names) {
  await fs.writeFile(path.join(localRoot, name), bytes(name));
  await fs.writeFile(path.join(servedRoot, name), bytes(name));
}
const server = http.createServer(async (request, response) => {
  const name = decodeURIComponent(new URL(request.url, 'http://localhost').pathname.slice(1));
  if (!names.includes(name)) { response.writeHead(404).end(); return; }
  try { const body = await fs.readFile(path.join(servedRoot, name)); response.writeHead(200).end(body); }
  catch { response.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const url = `http://127.0.0.1:${server.address().port}/runtime.html`;
try {
  const pre = await browserArtifactInventory({artifactRoot: localRoot, url});
  assert.equal(pre.count, 34);
  assert.equal(pre.equal, true);
  assert.deepEqual(pre.rows.map(row => row.name), names);
  for (const row of pre.rows) {
    const expected = createHash('sha256').update(bytes(row.name)).digest('hex');
    assert.equal(row.local_sha256, expected);
    assert.equal(row.served_sha256, expected);
    assert.equal(row.local_bytes, bytes(row.name).length);
    assert.equal(row.served_bytes, bytes(row.name).length);
  }
  const post = await browserArtifactInventory({artifactRoot: localRoot, url});
  assert.deepEqual(post.rows, pre.rows);
  await fs.unlink(path.join(servedRoot, 'net_lockstep_core.mjs'));
  await assert.rejects(browserArtifactInventory({artifactRoot: localRoot, url}), /net_lockstep_core\.mjs returned HTTP 404/);
  await fs.writeFile(path.join(servedRoot, 'net_lockstep_core.mjs'), bytes('net_lockstep_core.mjs'));
  await fs.writeFile(path.join(servedRoot, 'net_lockstep_native_adapter.mjs'), 'changed served adapter');
  const changed = await browserArtifactInventory({artifactRoot: localRoot, url});
  assert.equal(changed.equal, false);
  assert.deepEqual(changed.mismatches, ['net_lockstep_native_adapter.mjs']);
  assert.equal(changed.rows.find(row => row.name === 'net_lockstep_native_adapter.mjs').equal, false);
  console.log('Current 34-file inventory, malformed names, missing served module and changed served hash controls passed.');
} finally {
  server.closeAllConnections();
  await new Promise(resolve => server.close(resolve));
  await fs.rm(root, {recursive: true, force: false});
}
