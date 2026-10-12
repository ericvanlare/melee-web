import {LOCKSTEP_DELAY, LOCKSTEP_MAX_SOURCE_TICKS} from './net_lockstep_core.mjs';
import {createRoomId, validateRoomRelayBaseUrl} from './net_lockstep_websocket_relay.mjs';

export const ROOM_INVITE_PREFIX = 'MELEE-ROOM-V1:';
export const ROOM_INVITE_MAX_CHARS = 2048;
const ROOM_ID_PATTERN = /^[A-Za-z0-9_-]{22,64}$/;
const BASE64URL = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_';
const INVITE_KEYS = 'inputDelay,roomId,schema,seed,sourceTicks,url,version';

function encodeBytes(bytes) {
  let result = '';
  for (let offset = 0; offset < bytes.length; offset += 3) {
    const left = bytes.length - offset;
    const value = (bytes[offset] << 16) | ((left > 1 ? bytes[offset + 1] : 0) << 8) |
      (left > 2 ? bytes[offset + 2] : 0);
    result += BASE64URL[(value >>> 18) & 63] + BASE64URL[(value >>> 12) & 63];
    if (left > 1) result += BASE64URL[(value >>> 6) & 63];
    if (left > 2) result += BASE64URL[value & 63];
  }
  return result;
}

function decodeBytes(text) {
  if (typeof text !== 'string' || !/^[A-Za-z0-9_-]+$/.test(text) || text.length % 4 === 1)
    throw Error('Room invite encoding is invalid');
  const bytes = [];
  for (let offset = 0; offset < text.length; offset += 4) {
    const count = Math.min(4, text.length - offset);
    const values = Array.from(text.slice(offset, offset + count), char => BASE64URL.indexOf(char));
    if (values.some(value => value < 0)) throw Error('Room invite encoding is invalid');
    const value = ((values[0] ?? 0) << 18) | ((values[1] ?? 0) << 12) |
      ((values[2] ?? 0) << 6) | (values[3] ?? 0);
    bytes.push((value >>> 16) & 255);
    if (count > 2) bytes.push((value >>> 8) & 255);
    if (count > 3) bytes.push(value & 255);
  }
  return Uint8Array.from(bytes);
}

function validateInviteFields({schema, version, url, roomId, seed, sourceTicks, inputDelay} = {}) {
  if (schema !== 'melee-web-runtime-room' || version !== 1 ||
      typeof roomId !== 'string' || !ROOM_ID_PATTERN.test(roomId) ||
      !Number.isSafeInteger(seed) || seed < 0 || seed > 0xffffffff ||
      sourceTicks !== LOCKSTEP_MAX_SOURCE_TICKS || inputDelay !== LOCKSTEP_DELAY)
    throw Error('Room invite has unsupported or invalid shared settings');
  return Object.freeze({schema, version, url: validateRoomRelayBaseUrl(url), roomId,
    seed, sourceTicks, inputDelay});
}

export function encodeRoomInvite({url, roomId, seed} = {}) {
  const invite = validateInviteFields({schema: 'melee-web-runtime-room', version: 1,
    url, roomId, seed, sourceTicks: LOCKSTEP_MAX_SOURCE_TICKS, inputDelay: LOCKSTEP_DELAY});
  const json = JSON.stringify(invite);
  const text = ROOM_INVITE_PREFIX + encodeBytes(new TextEncoder().encode(json));
  if (text.length > ROOM_INVITE_MAX_CHARS) throw Error('Room invite exceeds its size limit');
  return text;
}

export function createRoomInvite({url} = {}) {
  const baseUrl = validateRoomRelayBaseUrl(url);
  const cryptoApi = globalThis.crypto;
  if (typeof cryptoApi?.getRandomValues !== 'function')
    throw Error('Secure random room invite settings require Web Crypto');
  const seed = cryptoApi.getRandomValues(new Uint32Array(1))[0];
  return encodeRoomInvite({url: baseUrl, roomId: createRoomId(), seed});
}

export function parseRoomInvite(text) {
  if (typeof text !== 'string' || text.length > ROOM_INVITE_MAX_CHARS ||
      !text.startsWith(ROOM_INVITE_PREFIX))
    throw Error('Paste a supported bounded Melee room invite');
  let decoded;
  try {
    decoded = new TextDecoder('utf-8', {fatal: true})
      .decode(decodeBytes(text.slice(ROOM_INVITE_PREFIX.length)));
  } catch { throw Error('Room invite encoding is invalid'); }
  let value;
  try { value = JSON.parse(decoded); }
  catch { throw Error('Room invite data is not valid JSON'); }
  const keys = value && typeof value === 'object' && !Array.isArray(value)
    ? Object.keys(value).sort().join(',') : '';
  if (keys !== INVITE_KEYS) throw Error('Room invite fields are unsupported');
  const invite = validateInviteFields(value);
  if (encodeRoomInvite(invite) !== text) throw Error('Room invite is not in canonical form');
  return invite;
}
