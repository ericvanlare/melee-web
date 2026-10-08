/** Fresh local/served identities for every name in the committed runtime allowlist. */
import fs from 'node:fs/promises';
import {createReadStream} from 'node:fs';
import {createHash} from 'node:crypto';
import path from 'node:path';

const inventoryFile = new URL('../tools/browser_build_artifacts.json', import.meta.url);

export function validateBrowserArtifactNames(names) {
  if (!Array.isArray(names) || names.length === 0 ||
      names.some(name => typeof name !== 'string' || !name || name !== name.trim() ||
        name === '.' || name === '..' || /[?#%]/.test(name) || path.posix.basename(name) !== name ||
        path.win32.basename(name) !== name || path.posix.isAbsolute(name) ||
        path.win32.isAbsolute(name)) || new Set(names).size !== names.length)
    throw Error('The committed browser artifact allowlist requires nonempty unique file names without path escapes');
  return names;
}

export async function browserArtifactInventory({artifactRoot, url}) {
  const names = validateBrowserArtifactNames(JSON.parse(await fs.readFile(inventoryFile, 'utf8')));
  artifactRoot = path.resolve(artifactRoot);
  const rows = await Promise.all(names.map(async name => {
    const localPath = path.resolve(artifactRoot, name);
    if (!localPath.startsWith(artifactRoot + path.sep))
      throw Error(`Browser artifact path escaped its build directory: ${name}`);
    const stat = await fs.stat(localPath);
    const localHash = createHash('sha256');
    for await (const chunk of createReadStream(localPath)) localHash.update(chunk);
    const localSha256 = localHash.digest('hex');
    const response = await fetch(new URL(name, url), {cache: 'no-store',
      signal: AbortSignal.timeout(8000)});
    if (!response.ok) throw Error(`Served browser artifact ${name} returned HTTP ${response.status}`);
    const hash = createHash('sha256');
    let servedBytes = 0;
    for await (const chunk of response.body) { servedBytes += chunk.length; hash.update(chunk); }
    const servedSha256 = hash.digest('hex');
    return {name, local_bytes: stat.size, local_sha256: localSha256,
      served_bytes: servedBytes, served_sha256: servedSha256,
      equal: stat.size === servedBytes && localSha256 === servedSha256};
  }));
  const mismatches = rows.filter(row => !row.equal).map(row => row.name);
  return {path: 'tools/browser_build_artifacts.json', artifact_root: artifactRoot,
    count: rows.length, rows, mismatches, equal: mismatches.length === 0};
}
