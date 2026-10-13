import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import test from 'node:test';
import {validateMatchedSetupCfRequest, validateMatchedSetupCfPreflight,
  validateMatchedSetupCfIdentities, matchedSetupCfRuntimeFailure, PREFIX_V2_SCOPE}
  from './stadium_matched_setup_cf_runner_gate.mjs';

const CANDIDATE_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const ROOT = path.resolve(process.env.MELEE_STADIUM_CF_RUNNER_TEST_REPO_ROOT || CANDIDATE_ROOT);
const RUNNER = path.join(CANDIDATE_ROOT, 'tests/stadium_c1a_browser_test.mjs');
const HELPER = path.join(CANDIDATE_ROOT, 'tests/stadium_matched_setup_cf_consumer.mjs');
const SCOPE = 'same-run completed Setup, then exact original 86-tick/85-draw prefix; compare C before draw84 and F before draw85';
const EXPORTS = {
  arm: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_arm',
  queue: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_queue',
  approve: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_approve',
};
const PREFIX_V2_EXPORTS = {
  arm: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_arm',
  queue: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_queue',
  approve: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_approve',
};
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const identity = (file, bytes) => ({path: file, basename: path.basename(file),
  bytes: bytes.byteLength, sha256: hash(bytes)});

test('the matched Setup C/F opt-in and preflight gate fail before server/browser startup', async () => {
  const scratch = process.env.MELEE_STADIUM_CF_RUNNER_TEST_ROOT;
  assert.ok(scratch, 'Python OwnedWorkspaceTests scratch path is required');
  const runnerSource = await fs.readFile(RUNNER, 'utf8');
  assert.match(runnerSource,
    /\['first-sss-vs-setup-cf', \{type: 'boolean', default: false\}\]/,
    'the continuation must remain opt-in by default');
  assert.match(runnerSource,
    /source_request_scope: firstSssVsSetupCf\s*\? firstSssVsSetupCfV2\s*\?/,
    'the v2 proper-prefix scope must be separately labeled');
  assert.match(runnerSource,
    /exact original 86-tick\/85-draw schedule; compare C after tick84 before draw84 and F after tick85 before draw85/,
    'legacy v1 scope must retain its fixed schedule wording');
  assert.match(runnerSource, /await driver\.arm\(/,
    'arm must await the actual native export result');
  assert.match(runnerSource, /await driver\.submit\(/,
    'each action must await the actual native queue result');
  assert.match(runnerSource, /await driver\.accept\(/,
    'cursor advancement must await native approval');
  assert.doesNotMatch(runnerSource, /adapter\.flush\(/,
    'native actions cannot be staged and acknowledged later');
  const adapterStart = runnerSource.indexOf('function matchedSetupCfNativeAdapter(');
  const adapterEnd = runnerSource.indexOf('async function runMatchedSetupCfContinuation', adapterStart);
  const adapterSource = runnerSource.slice(adapterStart, adapterEnd);
  assert.match(adapterSource, /executeMatchedSetupCfNativeCommand\(command\)/,
    'the host driver adapter must await the actual native export response');
  assert.doesNotMatch(adapterSource, /return 1;/,
    'the host driver adapter cannot invent native success');
  const serverStart = runnerSource.indexOf('const port = await allocateLoopbackPort()');
  const packetGate = runnerSource.indexOf('matchedSetupCfInputs = await loadMatchedSetupCfInputs()');
  assert.ok(packetGate > 0 && serverStart > packetGate,
    'the packet identity gate must run before opening the local server');
  assert.equal(validateMatchedSetupCfRequest({enabled: undefined, setup: false,
    textTail: false, packetPath: null, assemblyPath: null}), false);
  assert.equal(validateMatchedSetupCfRequest({enabled: false, setup: true,
    textTail: false, packetPath: null, assemblyPath: null}), false);
  assert.throws(() => validateMatchedSetupCfRequest({enabled: true, setup: true,
    textTail: false, packetPath: null, assemblyPath: '/tmp/assembly.json'}), /packet/);
  assert.throws(() => validateMatchedSetupCfRequest({enabled: true, setup: true,
    textTail: true, packetPath: '/tmp/packet.json', assemblyPath: '/tmp/assembly.json'}), /SIS/);
  assert.throws(() => validateMatchedSetupCfRequest({enabled: false, setup: true,
    textTail: false, packetPath: '/tmp/packet.json', assemblyPath: null}), /opt-in/);

  const packet = path.join(scratch, 'packet.json');
  const assembly = path.join(scratch, 'assembly.json');
  const output = path.join(scratch, 'output');
  const build = path.join(CANDIDATE_ROOT, 'build/browser-stadium-c1a-release');
  const packetBytes = Buffer.from('{"deliberately":"not admitted"}\n');
  const assemblyBytes = Buffer.from('{}\n');
  await fs.writeFile(packet, packetBytes);
  await fs.writeFile(assembly, assemblyBytes);
  const helperBytes = await fs.readFile(HELPER);
  const packetId = identity(packet, packetBytes);
  const assemblyId = identity(assembly, assemblyBytes);
  const helperId = identity(HELPER, helperBytes);
  const frozen = {scope: SCOPE, packet: packetId, assembly_manifest: assemblyId,
    helper: helperId, diagnostic_exports: EXPORTS};
  const preflight = {route_mode: 'first-sss-vs-setup', matched_setup_cf: frozen};
  assert.equal(validateMatchedSetupCfPreflight(preflight, {enabled: true,
    packetPath: packet, assemblyPath: assembly, helperPath: HELPER}), frozen);
  const v2Frozen = {...frozen, scope: PREFIX_V2_SCOPE, diagnostic_exports: PREFIX_V2_EXPORTS};
  assert.equal(validateMatchedSetupCfPreflight({route_mode: 'first-sss-vs-setup',
    matched_setup_cf: v2Frozen}, {enabled: true, packetPath: packet,
    assemblyPath: assembly, helperPath: HELPER}), v2Frozen);
  assert.throws(() => validateMatchedSetupCfPreflight({...preflight,
    matched_setup_cf: {...frozen, packet: {...packetId, path: `${packet}.other`}}},
  {enabled: true, packetPath: packet, assemblyPath: assembly, helperPath: HELPER}), /packet path/);
  assert.throws(() => validateMatchedSetupCfIdentities(frozen, {
    packet: {...packetId, sha256: '0'.repeat(64)}, assembly_manifest: assemblyId,
    helper: helperId}), /packet differs/);

  assert.equal(matchedSetupCfRuntimeFailure({native: {}, consoleWarnings: []}), null);
  assert.match(matchedSetupCfRuntimeFailure({native: {},
    consoleWarnings: ['Ignoring nested GX_CMD_CALL_DL']}), /Missing display-list provenance/);
  assert.match(matchedSetupCfRuntimeFailure({native: {},
    consoleMessages: [{type: 'log', text: 'Ignoring nested GX_CMD_CALL_DL'}]}),
  /Missing display-list provenance/);
  assert.match(matchedSetupCfRuntimeFailure({native: {},
    consoleWarnings: ['WebGPU uncaptured error: invalid command buffer']}), /GPU reported an error/);
  assert.match(matchedSetupCfRuntimeFailure({native: {},
    consoleMessageCaptureOverflow: true}), /capture overflowed/);
  assert.match(matchedSetupCfRuntimeFailure({native: {},
    pageErrors: ['uncaught exception']}), /Browser page error/);
  assert.match(matchedSetupCfRuntimeFailure({native: {error: 'runtime failed'}}),
    /Native runtime error/);
  assert.match(matchedSetupCfRuntimeFailure({native: {},
    consoleErrors: ['GPU validation failed']}), /Browser console error/);

  const head = spawnSync('git', ['rev-parse', 'HEAD'], {cwd: ROOT, encoding: 'utf8'});
  assert.equal(head.status, 0, head.stderr);
  const runnerBytes = await fs.readFile(RUNNER);
  const preflightFile = path.join(scratch, 'preflight.json');
  const preflightBytes = {
    schema: 'melee-web-stadium-c1a-browser-preflight-v1',
    route_mode: 'first-sss-vs-setup', source_revision: head.stdout.trim(),
    build: {path: build},
    disc: {path: '/unused/original.iso', sha256: 'b7de482eb955c8a96b6746dfa043b69ae7bf6c7c2a09ac382b9da126faa7055c'},
    browser: {playwright_package_path: '/unused/playwright'},
    output: {path: output}, test_script: {path: RUNNER, sha256: hash(runnerBytes)},
    first_sss_vs_setup: {text_tail_probe: false},
    matched_setup_cf: {...frozen, packet: {...packetId, sha256: '0'.repeat(64)}},
  };
  await fs.writeFile(preflightFile, JSON.stringify(preflightBytes));
  const child = spawnSync(process.execPath, [RUNNER,
    '--build', build,
    '--disc', '/unused/original.iso', '--disc-sha256', preflightBytes.disc.sha256,
    '--out', output, '--playwright', '/unused/playwright', '--preflight', preflightFile,
    '--source-revision', head.stdout.trim(), '--first-sss-vs-setup',
    '--first-sss-pair-expected', path.join(scratch, 'unused-pair.json'),
    '--first-sss-tick-expected', path.join(scratch, 'unused-tick.json'),
    '--first-sss-tick-input', path.join(scratch, 'unused-tick-input.json'),
    '--first-sss-draw-expected', path.join(scratch, 'unused-draw.json'),
    '--first-sss-prefix-expected', path.join(scratch, 'unused-prefix.json'),
    '--first-sss-prefix-input', path.join(scratch, 'unused-prefix-input.json'),
    '--first-sss-vs-setup-expected', path.join(scratch, 'unused-setup.json'),
    '--first-sss-vs-entry-expected', path.join(scratch, 'unused-entry.json'),
    '--first-sss-vs-setup-cf', '--first-sss-vs-setup-cf-packet', packet,
    '--first-sss-vs-setup-cf-assembly', assembly], {cwd: ROOT, encoding: 'utf8'});
  assert.notEqual(child.status, 0, 'a mismatched packet hash must refuse the run');
  let reportBytes;
  try { reportBytes = await fs.readFile(path.join(output, 'report.json'), 'utf8'); }
  catch (error) {
    assert.fail(`runner exited before writing its owned report: ${error}; stdout=${child.stdout}; stderr=${child.stderr}`);
  }
  const report = JSON.parse(reportBytes);
  assert.match(report.error, /packet differs from the frozen preflight identity/);
  await assert.rejects(fs.access(path.join(output, 'server.stdout.log')));
  await assert.rejects(fs.access(path.join(output, 'server.stderr.log')));
  assert.equal(report.cleanup.server_process_pid ?? null, null);
  assert.equal(report.cleanup.browser_process_pid ?? null, null);
});
