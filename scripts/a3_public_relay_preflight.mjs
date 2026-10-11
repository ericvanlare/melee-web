import {validateRoomRelayBaseUrl} from './net_lockstep_websocket_relay.mjs';

const PREFLIGHT_SCHEMA = 'melee-web-a3-public-relay-preflight-v1';
const SHA256 = /^[0-9a-f]{64}$/;
const FILE_NAME = /^[^/\\][^/\\]*$/;

function objectWithKeys(value, keys, label) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      Object.keys(value).sort().join(',') !== [...keys].sort().join(','))
    throw Error(`${label} has unsupported or missing fields`);
  return value;
}

function nonempty(value, label) {
  if (typeof value !== 'string' || !value.trim()) throw Error(`${label} must be a nonempty string`);
  return value;
}

function receipt(value, label) {
  objectWithKeys(value, ['file', 'read_only', 'sha256'], label);
  if (typeof value.file !== 'string' || !FILE_NAME.test(value.file) || value.file === '.' || value.file === '..')
    throw Error(`${label}.file must be a basename`);
  if (value.read_only !== true) throw Error(`${label} must cite read-only evidence`);
  if (typeof value.sha256 !== 'string' || !SHA256.test(value.sha256))
    throw Error(`${label}.sha256 must be a lowercase SHA-256 digest`);
  return {file: value.file, read_only: true, sha256: value.sha256};
}

function canonicalClientOrigin(value, label) {
  if (typeof value !== 'string') throw Error(`${label} must be an origin`);
  let url;
  try { url = new URL(value); }
  catch { throw Error(`${label} must be an HTTP(S) origin`); }
  if (!['http:', 'https:'].includes(url.protocol) || url.origin !== value ||
      url.username || url.password || url.pathname !== '/' || url.search || url.hash)
    throw Error(`${label} must be a canonical HTTP(S) origin without path, credentials, query, or fragment`);
  return value;
}

/** Validate a supplied, read-only public staging Worker preflight against the
 * exact relay URL and page origin used by the browser. This checks document
 * structure and bindings only; it does not contact or independently verify the
 * deployed Worker or the referenced evidence files. */
export function resolveRoomRelayMode({relayUrl, preflight, clientOrigin} = {}) {
  const hasRelayUrl = relayUrl !== undefined;
  const hasPreflight = preflight !== undefined;
  if (!hasRelayUrl && !hasPreflight) return {mode: 'local'};
  if (!hasRelayUrl || !hasPreflight)
    throw Error('External relay mode requires both --relay-url and --relay-preflight');

  const endpointOrigin = validateRoomRelayBaseUrl(relayUrl);
  const parsedEndpoint = new URL(relayUrl);
  if (parsedEndpoint.protocol !== 'wss:' || relayUrl !== endpointOrigin)
    throw Error('External public relay URL must be a canonical wss base URL');
  const pageOrigin = canonicalClientOrigin(clientOrigin, 'Client origin');

  objectWithKeys(preflight,
    ['schema', 'version', 'scope', 'observed_at', 'runtime', 'source', 'client', 'evidence'],
    'Public relay preflight');
  if (preflight.schema !== PREFLIGHT_SCHEMA || preflight.version !== 1 ||
      preflight.scope !== 'read-only-public-staging-worker-binding')
    throw Error('Public relay preflight schema or scope is unsupported');
  if (typeof preflight.observed_at !== 'string' ||
      !/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,3})?Z$/.test(preflight.observed_at) ||
      !Number.isFinite(Date.parse(preflight.observed_at)))
    throw Error('Public relay preflight observed_at must be a UTC timestamp');

  const runtime = objectWithKeys(preflight.runtime, ['relay_url', 'relay_origin'],
    'Public relay preflight runtime');
  if (runtime.relay_url !== relayUrl || runtime.relay_origin !== endpointOrigin)
    throw Error('Public relay preflight endpoint does not match --relay-url');

  const source = objectWithKeys(preflight.source,
    ['worker_name', 'environment', 'deployment_id', 'script_sha256', 'config_sha256'],
    'Public relay preflight source');
  const workerName = nonempty(source.worker_name, 'Worker name');
  const deploymentId = nonempty(source.deployment_id, 'Worker deployment ID');
  if (source.environment !== 'staging') throw Error('Public relay preflight must identify the staging environment');
  if (typeof source.script_sha256 !== 'string' || !SHA256.test(source.script_sha256) ||
      typeof source.config_sha256 !== 'string' || !SHA256.test(source.config_sha256))
    throw Error('Public relay preflight must bind Worker source and config SHA-256 digests');

  const client = objectWithKeys(preflight.client, ['origin', 'allowed_origins'],
    'Public relay preflight client');
  if (canonicalClientOrigin(client.origin, 'Preflight client origin') !== pageOrigin)
    throw Error('Public relay preflight client origin does not match the browser page');
  if (!Array.isArray(client.allowed_origins) || client.allowed_origins.length === 0 ||
      client.allowed_origins.some(origin => canonicalClientOrigin(origin, 'Allowed client origin') !== origin) ||
      !client.allowed_origins.includes(pageOrigin))
    throw Error('Public relay preflight does not establish that the exact client Origin is allowed');

  const evidence = objectWithKeys(preflight.evidence,
    ['deployment_receipt', 'origin_policy_receipt'], 'Public relay preflight evidence');
  return {
    mode: 'external',
    endpoint: {url: relayUrl, origin: endpointOrigin},
    source: {worker_name: workerName, environment: 'staging', deployment_id: deploymentId,
      script_sha256: source.script_sha256, config_sha256: source.config_sha256},
    client_origin: pageOrigin,
    allowed_client_origins: [...client.allowed_origins],
    observed_at: preflight.observed_at,
    evidence: {
      deployment_receipt: receipt(evidence.deployment_receipt, 'Deployment receipt'),
      origin_policy_receipt: receipt(evidence.origin_policy_receipt, 'Origin policy receipt'),
    },
    validation_scope: 'supplied preflight structure and requested URL/origin bindings only; no live provider query',
  };
}
