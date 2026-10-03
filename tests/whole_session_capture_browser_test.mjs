/** Real HTTP/installed headless Chrome CLI controls, not gameplay evidence. */
import assert from 'node:assert/strict';
import http from 'node:http';
import fs from 'node:fs/promises';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {execFile} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {parseArgs, promisify} from 'node:util';
import {loadBrowserTools} from '../scripts/browser_tools.mjs';

const {values} = parseArgs({options: {playwright: {type: 'string'}, out: {type: 'string'}}});
assert(values.out, 'Use --out NEW_DIRECTORY to retain the browser contract reports');
const output = path.resolve(values.out);
await fs.mkdir(output, {recursive: false});
const {playwrightPath, browserPath} = await loadBrowserTools(values.playwright);
const cases = ['clean', 'pageerror', 'console', 'http', 'request-failure', 'runtime', 'late-console',
  'export-failure', 'missing-port', 'missing-report', 'empty-exports',
  'empty-artifact', 'duplicate', 'prefix', 'timing-pause', 'rng-probe', 'rng-probe-invalid'];
const fixture = mode => String.raw`<!doctype html><html><body>
<canvas id="canvas"></canvas><div id="status">Synthetic transport fixture</div><div id="log"></div>
<input type="file" id="disc"><button id="launch" disabled>Play</button><button id="unload">Unload</button>
<details><summary>Diagnostics</summary><select id="retail-replay-mode"><option value="state">state</option></select>
<input type="file" id="retail-replay-file"><button id="retail-replay-start" disabled>Replay</button></details>
<pre id="retail-replay-report"></pre><div id="retail-replay-downloads"></div>
<script>
const mode=${JSON.stringify(mode)}, $=id=>document.getElementById(id);
let cursor=0;
window.Module={_melee_web_native_menu_phase:()=>7,_melee_web_native_menu_running:()=>1,
 _melee_web_native_menu_replay_cursor:()=>cursor};
$('disc').onchange=()=>{$('launch').disabled=false;};
$('retail-replay-file').onchange=()=>{$('retail-replay-start').disabled=false;};
function link(name,text,blob=true){
 const a=document.createElement('a');a.download=name;
 a.href=blob?URL.createObjectURL(new Blob([text])):'data:text/plain,fixture';
 $('retail-replay-downloads').append(a);return a;
}
function publish(complete=true){
 const report={complete,pass:complete,final_scene:1,failures:complete?[]:['deliberate prefix']};
 $('retail-replay-report').textContent=JSON.stringify(report);
 if(mode!=='missing-port'){
  const a=link('retail-port.jsonl',mode==='empty-artifact'?'':'synthetic trace\n',mode!=='empty-exports');
  if(mode==='export-failure')URL.revokeObjectURL(a.href);
 }
 link('retail-browser-report.json',JSON.stringify(report),!['missing-report','empty-exports'].includes(mode));
 if(mode==='duplicate')link('retail-port.jsonl','duplicate synthetic trace\n');
}
$('retail-replay-start').onclick=async()=>{
 cursor=1;
 if(mode==='rng-probe'||mode==='rng-probe-invalid')window.meleeRngDrawObservation(JSON.stringify({
  schema:'melee-web-rng-draw-probe',version:1,source_cursor:0,overflowed:false,
  draws:[{kind:'HSD_Randf',seed_after:mode==='rng-probe'?'1234abcd':'1234ABCd'}]}));
 if(mode==='prefix')return;
 if(mode==='timing-pause'){
  $('status').textContent='Paused after a timing disruption. Resume to continue.';
  return;
 }
 if(mode==='pageerror'){
  setTimeout(()=>{throw Error('synthetic pageerror');},0);
  await new Promise(resolve=>setTimeout(resolve,30));
 }
 if(mode==='console')console.error('synthetic console error');
 if(mode==='http')await fetch('/synthetic-http-failure');
 if(mode==='request-failure')await fetch('/synthetic-network-failure').catch(()=>{});
 if(mode==='runtime')$('status').dataset.runtimeError='synthetic runtime failure';
 if(mode==='late-console'){
  const original=window.fetch;
  window.fetch=(...args)=>{console.error('synthetic export-time error');return original(...args);};
 }
 publish();
};
$('unload').onclick=()=>publish(false);
</script></body></html>`;
const server = http.createServer((req, res) => {
  const url = new URL(req.url, 'http://localhost');
  if (url.pathname === '/synthetic-network-failure') {req.socket.destroy();return;}
  const headers = {'content-type': 'text/html', 'Cross-Origin-Opener-Policy': 'same-origin',
    'Cross-Origin-Embedder-Policy': 'require-corp'};
  if (url.pathname === '/synthetic-http-failure') {res.writeHead(503, headers);res.end('synthetic failure');return;}
  if (url.pathname === '/favicon.ico') {res.writeHead(204, headers);res.end();return;}
  res.writeHead(200, headers);res.end(fixture(url.searchParams.get('case')));
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const results = [];
try {
  const disc = path.join(output, 'fixture.input');
  const recipe = path.join(output, 'fixture.mwrc');
  await fs.writeFile(disc, 'Synthetic harness input; no game content');
  const header = Buffer.alloc(20);
  header.write('MWRC');header.writeUInt32BE(8, 4);header.writeUInt32BE(1, 12);
  await fs.writeFile(recipe, header);
  for (const mode of cases) {
    const out = path.join(output, mode);
    const args = [fileURLToPath(new URL('../scripts/capture_whole_session_browser.mjs', import.meta.url)),
      '--url', `http://127.0.0.1:${server.address().port}/runtime.html?case=${mode}`,
      '--disc', disc, '--recipe', recipe, '--out', out, '--playwright', playwrightPath,
      '--phase-timeout', '10000', '--replay-timeout', '10000', '--poll-ms', '50'];
    if (mode === 'prefix') args.push('--stop-after-source-frames', '1');
    if (mode === 'rng-probe'||mode === 'rng-probe-invalid') args.push('--rng-draw-probe-cursors', '0');
    const processResult = await promisify(execFile)(process.execPath, args, {timeout: 30000})
      .then(value => ({...value, code: 0}), error => ({code: error.code, stdout: error.stdout, stderr: error.stderr}));
    const report = JSON.parse(await fs.readFile(path.join(out, 'report.json'), 'utf8'));
    await fs.writeFile(path.join(out, 'process.json'), JSON.stringify(processResult, null, 2) + '\n');
    const passing = mode === 'clean' || mode === 'rng-probe';
    assert.equal(processResult.code, passing ? 0 : 1, `${mode}: ${JSON.stringify(processResult)}`);
    assert.equal(report.result, passing ? 'pass' : mode === 'prefix' ? 'incomplete' : 'fail', mode);
    assert.equal(/^pass:/m.test(processResult.stdout || ''), passing, mode);
    if (mode === 'clean') {
      assert.equal(report.first_error, null);
      assert.deepEqual(report.browser_errors, []);
      assert.deepEqual(report.saved_downloads.map(row => row.name), ['retail-port.jsonl', 'retail-browser-report.json']);
    } else if (mode === 'rng-probe') {
      assert.deepEqual(report.saved_downloads.map(row => row.name),
        ['retail-port.jsonl', 'retail-browser-report.json', 'rng-draw-probe.jsonl']);
      assert.equal(report.rng_draw_probe.complete, true);
      assert.deepEqual(report.rng_draw_probe.captured_cursors, [0]);
      const artifact = await fs.readFile(path.join(out, 'rng-draw-probe.jsonl'));
      assert.equal(report.rng_draw_probe.artifact.sha256,
        createHash('sha256').update(artifact).digest('hex'));
    } else assert(report.finalization_failures.length, mode);
    if (mode === 'rng-probe-invalid') assert.match(report.rng_draw_probe_error, /invalid kind or 32-bit post-update seed/);
    if (['pageerror', 'console', 'http', 'request-failure', 'late-console'].includes(mode)) {
      assert(report.first_error, mode);assert(report.browser_errors.length, mode);
    }
    if (mode === 'request-failure') assert(report.browser_errors.some(row => row.kind === 'requestfailed'));
    if (mode === 'runtime') assert.match(report.first_error.message, /synthetic runtime/);
    if (mode === 'timing-pause') {
      assert.match(report.first_error.message, /timing disruption at cursor 1/);
      assert.deepEqual(report.timing_pause_resumes, []);
      assert.match(report.final_snapshot.status, /Paused after a timing disruption/);
    }
    if (mode === 'export-failure') assert(report.download_error);
    if (mode === 'prefix') assert(report.deliberate_prefix_stop && report.failure);
    await fs.access(path.join(out, 'page.txt'));
    await fs.access(path.join(out, 'final.png'));
    results.push({case: mode, result: report.result, exit_code: processResult.code});
  }
  await fs.writeFile(path.join(output, 'contract-report.json'), JSON.stringify({
    scope: 'Synthetic real HTTP/headless Chrome transport contract; no gameplay claim',
    browser: path.basename(browserPath), result: 'pass', cases: results,
  }, null, 2) + '\n');
  console.log(`Whole-session real-browser CLI contract: ${results.length} cases passed`);
} finally {await new Promise(resolve => server.close(resolve));}
