export const STAGING_HOST = 'melee-web-room-relay-staging-288.mywebcomic.workers.dev';
export const STAGING_RELAY_URL = `wss://${STAGING_HOST}`;

function asUrl(value) {
  if (value instanceof URL) return new URL(value.href);
  if (typeof value === 'string') return new URL(value);
  if (value && typeof value.href === 'string') return new URL(value.href);
  if (value && typeof value.hostname === 'string' && typeof value.protocol === 'string') {
    return new URL(`${value.protocol}//${value.hostname}${value.pathname || '/'}${value.search || ''}`);
  }
  throw Error('A page URL is required for the staging room');
}

export function isStagingRoomSurface(locationLike = globalThis.location) {
  const url = asUrl(locationLike);
  return (url.protocol === 'https:' && url.hostname === STAGING_HOST) ||
    url.searchParams.get('surface') === 'staging';
}

export function parseStagingRoomInvite(text, parseRoomInvite) {
  if (typeof parseRoomInvite !== 'function') throw Error('The room invite parser is unavailable');
  const invite = parseRoomInvite(text);
  if (invite.url !== STAGING_RELAY_URL)
    throw Error('This invite is for a different room service. Ask the host for a staging invite.');
  return invite;
}

export function stagingRoomInviteFromHash(hash, parseRoomInvite) {
  if (typeof hash !== 'string') throw Error('A room invite link fragment must be text');
  const fragment = hash.startsWith('#') ? hash.slice(1) : hash;
  if (!fragment) return null;
  const params = new URLSearchParams(fragment);
  const invites = params.getAll('invite');
  if (!invites.length) return null;
  if (params.size !== 1 || invites.length !== 1)
    throw Error('Room invite link fields are unsupported');
  parseStagingRoomInvite(invites[0], parseRoomInvite);
  return invites[0];
}

export function stagingRoomInviteLink(pageUrl, inviteText, parseRoomInvite) {
  parseStagingRoomInvite(inviteText, parseRoomInvite);
  const url = asUrl(pageUrl);
  if (!['http:', 'https:'].includes(url.protocol))
    throw Error('Room invite links require an HTTP or HTTPS player page');
  const params = new URLSearchParams();
  params.set('invite', inviteText);
  url.hash = params.toString();
  return url.href;
}
