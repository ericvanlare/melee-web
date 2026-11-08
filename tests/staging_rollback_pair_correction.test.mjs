import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';

const driver = fs.readFileSync(new URL('../scripts/staging_rollback_pair_browser.mjs', import.meta.url), 'utf8');
const start = 'function requireDirtyCorrection(';
const end = 'function compareTerminalReceipts(';
assert.equal(driver.split(start).length, 2);
assert.equal(driver.split(end).length, 2);
const startAt = driver.indexOf(start), endAt = driver.indexOf(end);
assert(endAt > startAt);
const requireDirtyCorrection = vm.runInNewContext(
  driver.slice(startAt, endAt) + '\nrequireDirtyCorrection', {assert},
);

function retainedCorrection() {
  const raw = Array(44).fill(0), selected = raw.slice();
  selected[15] = 127;
  const remote = selected.slice(11, 22);
  const row = {schema: 1, bytes: 176, sourceFrame: 26, ordinal: 5, state: 2,
    localPort: 0, remotePort: 1, ticketId: 231, pollSerial: '901', incarnation: 1,
    raw, forward: raw.slice(), selected};
  const rows = Array(5).fill(null);
  rows.push({row, unchangedPrefix: false, status: {ownerState: 1, pendingCommand: 0,
    lastCompletedCommand: 15, requestSequence: 250, completeSequence: 250,
    metadata: {preFrame: 21, currentFrame: 27, incarnation: 1, restoreCount: 1}}});
  const frames = [26, 27].map(tick => ({tick, local_pad: raw.slice(0, 11), remote_pad: remote.slice(),
    remote_source: 'actual', speculative: false, frame: selected.slice()}));
  const correction = {kind: 'fully-confirmed-correction', dirty: 26, pre: 21, frontier: 28, confirmed: 28,
    replay: {plan: {kind: 'replay', from: 26, through: 27, speculative_ticks: 0, frames}, rows}};
  const matches = [{role: 'alpha', corrections: [correction], retention: {limit: 64},
    events: [{operation: 'episodeAdmit', value: {tick: 26}, result: {receipt: {
      accepted_by: 'lockstep-protocol', sender_role: 'beta', protocol_input_tick: 26, remote_port: 1, pad: remote,
    }}}], polls: [{sourceFrame: 26, ticketId: 231, pollSerial: '901', raw: raw.slice()}]},
  {role: 'beta', corrections: [], retention: {limit: 64},
    polls: [{sourceFrame: 26, localPort: 1, raw: selected.slice()}]}];
  return {matches, correction, row, frames};
}

test('a retained actual dirty replay proves changed peer bytes on the original ticket', () => {
  const {matches} = retainedCorrection();
  const proof = requireDirtyCorrection(matches);
  assert.equal(proof.dirty, 26);
  assert.equal(proof.ticketId, 231);
  assert.equal(proof.pollSerial, '901');
  assert.equal(proof.predictedRemote[4], 0);
  assert.equal(proof.actualRemote[4], 127);
  assert(Object.values(proof.retainedJoins).every(Boolean));

  matches[0].events = [];
  for (const match of matches) match.polls = [];
  const retained = requireDirtyCorrection(matches);
  assert.equal(retained.actualRemote[4], 127);
  assert(Object.values(retained.retainedJoins).every(value => value === false));
});

test('empty or clean reports and unchanged or speculative remote bytes cannot pass', () => {
  const {matches, correction, row, frames} = retainedCorrection();
  matches[0].corrections = [];
  assert.throws(() => requireDirtyCorrection(matches), /lacks a retained non-vacuous/);
  matches[0].corrections = [correction];
  correction.dirty = null;
  assert.throws(() => requireDirtyCorrection(matches), /lacks a retained non-vacuous/);
  correction.dirty = 26;
  frames[0].remote_source = 'last_known_prediction';
  assert.throws(() => requireDirtyCorrection(matches), /speculative or unindexed/);
  frames[0].remote_source = 'actual';
  row.forward = row.selected.slice();
  assert.throws(() => requireDirtyCorrection(matches), /did not replace/);
});

test('fabricated ticket, raw PAD, and independent peer admission mismatches fail', () => {
  const {matches, row} = retainedCorrection();
  row.ticketId = 232;
  assert.throws(() => requireDirtyCorrection(matches));
  row.ticketId = 231;
  row.pollSerial = '0';
  assert.throws(() => requireDirtyCorrection(matches), /native replay ticket/);
  row.pollSerial = '901';
  row.raw[0] = 1;
  assert.throws(() => requireDirtyCorrection(matches));
  row.raw[0] = 0;
  matches[0].events[0].result.receipt.remote_port = 0;
  assert.throws(() => requireDirtyCorrection(matches), /actual peer packet/);
});
