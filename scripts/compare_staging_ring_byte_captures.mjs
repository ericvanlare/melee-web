#!/usr/bin/env node
/** Reproduce functional byte comparison while retaining all evidence limitations. */
import fs from 'node:fs/promises';
import path from 'node:path';
import crypto from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {parseArgs} from 'node:util';
import {compareStagingRingReports} from '../tests/staging_ring_report_comparison.mjs';
const ROOT = path.resolve(import.meta.dirname, '..');
const {values, positionals} = parseArgs({allowPositionals: true, options: {
  out: {type: 'string'}, 'recover-missing-ring2-post-map': {type: 'boolean'}}});
if (positionals.length !== 2 || !values.out)
  throw Error('Use RING2_REPORT RING4_REPORT --out NEW_JSON [--recover-missing-ring2-post-map]');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const paths = positionals.map(value => path.resolve(value));
const bytes = await Promise.all(paths.map(name => fs.readFile(name)));
const [two, four] = bytes.map(value => JSON.parse(value));
const git = (...args) => execFileSync('git', args, {cwd: ROOT, encoding: 'utf8'}).trim();
for (const report of [two, four]) {
  if (!/^[0-9a-f]{40}$/.test(report.source?.commit ?? '') ||
      git('rev-parse', `${report.source.commit}^{tree}`) !== report.source.tree)
    throw Error('Capture commit/tree identity does not resolve to the retained source');
  if (sha(execFileSync('git', ['show', `${report.source.commit}:patches/aurora-browser.patch`], {cwd: ROOT})) !==
      report.source.aurora_patch_sha256) throw Error('Capture Aurora patch hash is not its source commit patch');
}
const diff = execFileSync('git', ['diff', '--binary', two.source.commit, four.source.commit], {cwd: ROOT});
const names = git('diff', '--name-only', two.source.commit, four.source.commit);
// Historical captures keep their own executable inventory as main adds modules.
// Both source identities are resolved above; do not substitute today's manifest.
const inventoryPath = 'tools/browser_build_artifacts.json';
const inventories = [two, four].map(report => execFileSync('git',
  ['show', `${report.source.commit}:${inventoryPath}`], {cwd: ROOT}));
if (!inventories[0].equals(inventories[1]))
  throw Error('Capture source commits declare different runtime artifact inventories');
const artifactNames = JSON.parse(inventories[0]);
const sourceDifference = {verified: true, ring2_commit: two.source.commit, ring4_commit: four.source.commit,
  ring2_tree: two.source.tree, ring4_tree: four.source.tree, changed_paths: names ? names.split('\n') : [],
  artifact_inventory: {path: inventoryPath, sha256: sha(inventories[0]), names: artifactNames},
  diff_sha256: sha(diff), policy: 'Only unserved tests/*.mjs harness code may differ; full served/local executable maps remain checked'};
const result = compareStagingRingReports(two, four, {artifactNames, sourceDifference,
  recoverMissingRing2PostMap: values['recover-missing-ring2-post-map'] === true});
result.report_inputs = Object.fromEntries(['ring2', 'ring4'].map((label, index) =>
  [label, {path: paths[index], bytes: bytes[index].length, sha256: sha(bytes[index])}]));
result.comparator_source = {commit: git('rev-parse', 'HEAD'),
  script_sha256: sha(await fs.readFile(import.meta.filename)),
  report_validator_sha256: sha(await fs.readFile(path.join(ROOT, 'tests/staging_ring_report_comparison.mjs'))),
  byte_validator_sha256: sha(await fs.readFile(path.join(ROOT, 'tests/staging_ring_byte_capture.mjs'))),
  completion_classifier_sha256: sha(await fs.readFile(path.join(ROOT, 'tests/staging_ring_replay_completion.mjs')))};
await fs.writeFile(path.resolve(values.out), JSON.stringify(result, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({result: result.result, frames: result.byte_comparison.frames,
  fingerprinted_write_count: result.byte_comparison.fingerprinted_write_count,
  artifact_binding: result.artifact_binding, fixture_sha256: two.inputs.recipe.sha256}));
