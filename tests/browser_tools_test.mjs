import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {browserLaunchOptions, resolveBrowserTools} from '../scripts/browser_tools.mjs';

const temporary=await fs.mkdtemp(path.join(os.tmpdir(),'melee-browser-tools-'));
try {
  const packagePath=path.join(temporary,'playwright');
  await fs.mkdir(packagePath);
  await fs.writeFile(path.join(packagePath,'index.mjs'),'export const chromium={launch(){}};\n');
  const env={MELEE_PLAYWRIGHT_DIR:packagePath,MELEE_BROWSER_PATH:process.execPath};
  const result=await resolveBrowserTools(undefined,env);
  assert.equal(result.playwrightPath,packagePath);
  assert.deepEqual(result.browser,{executablePath:process.execPath});
  assert.deepEqual(browserLaunchOptions(result.browser),{
    executablePath:process.execPath,headless:true,chromiumSandbox:true,
  });
  assert.deepEqual(browserLaunchOptions(result.browser,{headed:true,timeout:321}),{
    executablePath:process.execPath,headless:false,chromiumSandbox:true,timeout:321,
  });
  assert.throws(()=>browserLaunchOptions(result.browser,{headed:'true'}),/headed must be a boolean/);
  assert.throws(()=>browserLaunchOptions(result.browser,{timeout:-1}),/timeout must be a non-negative integer/);
  await assert.rejects(resolveBrowserTools(path.join(temporary,'missing'),env),/Cannot load Playwright/);
  await assert.rejects(resolveBrowserTools(undefined,{...env,MELEE_PLAYWRIGHT_DIR:path.join(temporary,'missing')}),/Cannot load Playwright/);
  await assert.rejects(resolveBrowserTools(undefined,{...env,MELEE_BROWSER_PATH:path.join(temporary,'missing')}),/executable browser file/);
  await assert.rejects(resolveBrowserTools(undefined,{...env,MELEE_BROWSER_PATH:packagePath}),/executable browser file/);
  console.log('Shared browser environment and explicit-path resolution passed');
} finally {await fs.rm(temporary,{recursive:true,force:true});}
