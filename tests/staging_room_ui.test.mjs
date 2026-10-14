import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';
import {encodeRoomInvite, parseRoomInvite} from '../scripts/net_lockstep_room_invite.mjs';
import {isStagingRoomSurface, parseStagingRoomInvite, stagingRoomInviteFromHash,
  stagingRoomInviteLink, STAGING_HOST, STAGING_RELAY_URL} from '../web/staging-room.mjs';

const root = new URL('../', import.meta.url);
const validInvite = (url = STAGING_RELAY_URL) => encodeRoomInvite({url,
  roomId: 'a'.repeat(32), seed: 0x1234abcd});

test('staging surface matches only the exact HTTPS host or explicit staging query', () => {
  assert.equal(isStagingRoomSurface(`https://${STAGING_HOST}/runtime.html`), true);
  assert.equal(isStagingRoomSurface(`http://${STAGING_HOST}/runtime.html`), false);
  assert.equal(isStagingRoomSurface('https://preview-melee-web-room-relay-staging-288.mywebcomic.workers.dev/runtime.html'), false);
  assert.equal(isStagingRoomSurface('http://127.0.0.1:8787/runtime.html?surface=staging'), true);
  assert.equal(isStagingRoomSurface('https://example.test/runtime.html?surface=other'), false);
});

test('staging invite parser accepts the canonical fixed relay and rejects other services', () => {
  const text = validInvite();
  assert.equal(parseStagingRoomInvite(text, parseRoomInvite).url, STAGING_RELAY_URL);
  assert.throws(() => parseStagingRoomInvite(validInvite('wss://relay.example'), parseRoomInvite),
    /different room service/);
  assert.throws(() => parseStagingRoomInvite('not an invite', parseRoomInvite), /invite/);
});

test('invite share link round-trips through the hash as prefill data and rejects extra fields', () => {
  const text = validInvite();
  const link = stagingRoomInviteLink('https://example.test/runtime.html?surface=staging', text, parseRoomInvite);
  const url = new URL(link);
  assert.equal(url.pathname, '/runtime.html');
  assert.equal(url.search, '?surface=staging');
  assert.equal(stagingRoomInviteFromHash(url.hash, parseRoomInvite), text);
  assert.equal(stagingRoomInviteFromHash('', parseRoomInvite), null);
  assert.equal(stagingRoomInviteFromHash('#unrelated=value', parseRoomInvite), null);
  assert.throws(() => stagingRoomInviteFromHash(`${url.hash}&other=value`, parseRoomInvite), /fields are unsupported/);
  assert.throws(() => stagingRoomInviteFromHash(`#invite=${encodeURIComponent(validInvite('wss://relay.example'))}`, parseRoomInvite),
    /different room service/);
});

test('staging activation updates the page title and heading and only prefills a shared invite', async () => {
  const runtime = await readFile(new URL('web/runtime-development.mjs', root), 'utf8');
  const start = runtime.indexOf('const stagingRoomEnabled=isStagingRoomSurface(window.location);');
  const end = runtime.indexOf('const developmentHooks = {};', start);
  assert.ok(start >= 0 && end > start);

  const invite = validInvite();
  const link = new URL(stagingRoomInviteLink('https://example.test/runtime.html?surface=staging', invite, parseRoomInvite));
  const ids = ['runtime-kicker', 'page-heading', 'staging-intro', 'disc-label', 'room-help', 'role-controls-help',
    'lockstep-relay-url', 'lockstep-status', 'input-guidance', 'lockstep-host', 'lockstep-join',
    'lockstep-leave', 'unload', 'lockstep-link-label', 'lockstep-copy-link', 'lockstep-join-invite'];
  const elements = Object.fromEntries(ids.map(id => [id, {textContent: '', value: '', hidden: id !== 'lockstep-join-invite',
    firstChild: {textContent: 'Melee disc image '}, onclick: null}]));
  const document = {title: 'Old title', body: {dataset: {}}, getElementById(id) { return elements[id]; }};
  const window = {location: link};
  vm.runInNewContext(runtime.slice(start, end), {window, document, isStagingRoomSurface,
    parseStagingRoomInvite: (text, parser) => parseStagingRoomInvite(text, parser),
    stagingRoomInviteFromHash: (hash, parser) => stagingRoomInviteFromHash(hash, parser),
    stagingRoomInviteLink: (url, text, parser) => stagingRoomInviteLink(url, text, parser),
    STAGING_RELAY_URL, parseRoomInvite});

  assert.equal(document.title, 'Melee Web · Play together');
  assert.equal(elements['page-heading'].textContent, 'Play together');
  assert.equal(elements['role-controls-help'].hidden, false);
  assert.equal(elements['lockstep-join-invite'].value, invite);
  assert.equal(elements['lockstep-host'].onclick, null);
  assert.match(elements['lockstep-status'].textContent, /Invite loaded/);
});

test('staging page reuses the runtime room controls and keeps launch/diagnostics out of the primary surface', async () => {
  const html = await readFile(new URL('web/runtime.html', root), 'utf8');
  const runtime = await readFile(new URL('web/runtime-development.mjs', root), 'utf8');
  const css = await readFile(new URL('web/staging-room.css', root), 'utf8');
  assert.match(html, /id="lockstep-host"/);
  assert.match(html, /id="lockstep-join"/);
  assert.match(html, /id="lockstep-copy-link"/);
  assert.match(html, /id="controls-dialog"/);
  assert.match(html, /Controls → Advanced controller test and remapping/);
  assert.match(html, /Controllers start on Port 1: change its Player port to Port 2/);
  assert.match(html, /Right Shift attack/);
  assert.match(html, /id="launch"/); // preserved for ordinary runtime mode
  assert.match(html, /id="developer-diagnostics"/); // preserved for ordinary runtime mode
  assert.match(html, /href="staging-room\.css"/);
  assert.match(runtime, /window\.meleeStagingRoom\.relayUrl/);
  assert.match(runtime, /window\.meleeStagingRoom\.parseInvite\(inviteText\)/);
  assert.match(runtime, /inviteFromHash\(window\.location\.hash\)/);
  assert.match(runtime, /document\.title='Melee Web · Play together'/);
  assert.match(runtime, /getElementById\('page-heading'\)\.textContent='Play together'/);
  assert.match(runtime, /\.value=invite/); // hash invite only prefills; it does not auto-join
  const prefillStart = runtime.indexOf('const invite=window.meleeStagingRoom.inviteFromHash(window.location.hash)');
  const prefillEnd = runtime.indexOf('}catch(error)', prefillStart);
  assert.ok(prefillStart >= 0 && prefillEnd > prefillStart);
  assert.doesNotMatch(runtime.slice(prefillStart, prefillEnd), /startLockstepRoom|lockstep-join.*onclick/);
  assert.doesNotMatch(css, /#launch[^\{]*\{[^}]*display:\s*inline/);
  assert.match(css, /#runtime-actions #launch/);
  assert.match(css, /#developer-diagnostics \{ display: none/);
  assert.doesNotMatch(html, /<iframe\b/i);
});
