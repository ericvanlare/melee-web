/** Real-browser checks of the deliberately runtime-free public release. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';

const args = Object.fromEntries(process.argv.slice(2).reduce((rows, value, index, all) =>
  value.startsWith('--') ? [...rows, [value.slice(2), all[index + 1]]] : rows, []));
if (!args.url || !args.playwright || !args.out) throw Error('Use --url URL --playwright MODULE_DIR --out LOCAL_DIR');
const {chromium} = await import(pathToFileURL(path.join(args.playwright, 'index.mjs')));
await fs.mkdir(args.out, {recursive: true});
const browser = await chromium.launch({channel: 'chrome', headless: args.headed !== 'true'});
const context = await browser.newContext({viewport: {width: 1280, height: 1000}});
const page = await context.newPage();
const origin = new URL(args.url).origin;
const requests = [], errors = [], violations = [];
page.on('request', request => requests.push({url: request.url(), method: request.method(), body: request.postData()}));
page.on('pageerror', error => errors.push(error.message));
await page.addInitScript(() => {
  window.shellCspViolations = [];
  document.addEventListener('securitypolicyviolation', event => window.shellCspViolations.push({directive: event.violatedDirective, blocked: event.blockedURI}));
});
const report = {schema: 'webmelee-public-shell-browser-v1', browser: browser.version(), checks: [], gameplay: 'unavailable; no disc import or preparation was attempted'};
const pass = name => report.checks.push(name);
function checkPageResponse(response, pathname) {
  assert.equal(response.status(), 200);
  assert.equal(new URL(response.url()).origin, origin);
  assert.equal(new URL(response.url()).pathname, pathname);
  const headers = response.headers();
  assert.match(headers['content-security-policy'], /connect-src 'none'/);
  assert.equal(headers['x-content-type-options'], 'nosniff');
  assert.equal(headers['referrer-policy'], 'no-referrer');
  assert.equal(headers['x-frame-options'], 'DENY');
  assert.match(headers['permissions-policy'], /fullscreen=\(self\)/);
  if (args['index-production'] !== 'true' || new URL(origin).hostname.endsWith('.pages.dev')) {
    assert.match(headers['x-robots-tag'], /noindex/);
  }
}
try {
  const response = await page.goto(args.url, {waitUntil: 'networkidle'});
  checkPageResponse(response, '/');
  assert.match(await page.locator('body').innerText(), /Gameplay is not available/);
  assert.equal(await page.locator('input,iframe,form,canvas,audio,video').count(), 0);
  assert.equal(await page.locator('button:disabled').count(), 5);
  const environment = await page.locator('html').getAttribute('data-environment');
  assert.ok(['preview', 'production'].includes(environment));
  pass('root, security headers, honest limitation and absent importer/runtime');
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
  await page.screenshot({path: path.join(args.out, 'desktop.png'), fullPage: true});
  for (const width of [320, 390, 768]) {
    await page.setViewportSize({width, height: 844});
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true, `overflow at ${width}`);
    if (width === 390) await page.screenshot({path: path.join(args.out, 'mobile.png'), fullPage: true});
  }
  pass('desktop and 320/390/768 pixel layouts have no horizontal overflow');
  await page.setViewportSize({width: 1280, height: 1000});
  if (await page.locator('#fullscreen').isVisible()) {
    await page.locator('#fullscreen').click();
    await page.waitForFunction(() => Boolean(document.fullscreenElement));
    assert.equal(await page.locator('#fullscreen').innerText(), 'Exit fullscreen');
    await page.locator('#fullscreen').click();
    await page.waitForFunction(() => !document.fullscreenElement);
    pass('fullscreen enters and exits through real user control');
  } else pass('fullscreen unsupported and control correctly hidden');
  for (const [route, heading] of Object.entries({'/terms': 'Terms of Use', '/privacy': 'Privacy Notice',
      '/copyright': 'Copyright & contact', '/notices': 'About & legal'})) {
    const legalResponse = await page.goto(origin + route, {waitUntil: 'networkidle'});
    checkPageResponse(legalResponse, route);
    assert.equal(await page.locator('h1').innerText(), heading);
    assert.equal(await page.title(), `${environment === 'preview' ? '[staging] ' : ''}${heading} · WebMelee`);
    violations.push(...await page.evaluate(() => window.shellCspViolations));
  }
  pass('all legal pages load');
  await page.goto(args.url, {waitUntil: 'networkidle'});
  const storage = await page.evaluate(async () => ({
    local: Object.keys(localStorage), session: Object.keys(sessionStorage),
    indexed: typeof indexedDB.databases === 'function' ? await indexedDB.databases() : [],
    caches: await caches.keys(), workers: (await navigator.serviceWorker.getRegistrations()).length,
  }));
  assert.deepEqual(storage, {local: [], session: [], indexed: [], caches: [], workers: 0});
  assert.equal((await context.cookies()).length, 0);
  pass('no cookies, local/session storage, IndexedDB, Cache Storage or service worker');
  violations.push(...await page.evaluate(() => window.shellCspViolations));
  assert.deepEqual(violations, []);
  assert.deepEqual(errors, []);
  assert.ok(requests.length > 0);
  for (const request of requests) {
    assert.equal(new URL(request.url).origin, origin);
    assert.equal(request.method, 'GET');
    assert.equal(request.body, null);
    assert.ok(!/runtime|wasm|disc-image|hitch|analytics|beacon/.test(request.url));
  }
  report.normalRequests = requests.map(({url, method}) => ({path: new URL(url).pathname, method}));
  pass('all normal browser requests are same-origin public-file GETs without bodies');
  const cspProbe = await page.evaluate(async () => {
    try { await fetch('/blocked-network-probe', {method: 'POST', body: 'synthetic-csp-test'}); return false; }
    catch { return true; }
  });
  assert.equal(cspProbe, true);
  await page.waitForFunction(() => window.shellCspViolations.some(item => item.directive === 'connect-src'));
  pass('CSP rejects synthetic same-origin POST before delivery; no user data involved');
  report.storage = storage;
  report.pageErrors = errors;
  report.result = 'pass';
} catch (error) {
  report.result = 'fail'; report.failure = error.message;
  throw error;
} finally {
  await fs.writeFile(path.join(args.out, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  await browser.close();
}
console.log(JSON.stringify({result: report.result, checks: report.checks.length, browser: report.browser}));
