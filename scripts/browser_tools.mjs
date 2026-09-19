/** Shared browser configuration for local checks. Does not launch a browser. */
import fs from 'node:fs/promises';
import {constants} from 'node:fs';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';

async function executable(file) {
  try {await fs.access(file,constants.X_OK);return (await fs.stat(file)).isFile();}
  catch {return false;}
}

export async function resolvePlaywright(requested, env=process.env) {
  const configured=requested||env.MELEE_PLAYWRIGHT_DIR;
  let entry;
  if(configured)entry=path.join(path.resolve(configured),'index.mjs');
  else {
    try {entry=fileURLToPath(import.meta.resolve('playwright'));}
    catch {throw Error('Playwright is unavailable. Set MELEE_PLAYWRIGHT_DIR or pass --playwright with the installed package directory.');}
  }
  try {
    const module=await import(pathToFileURL(entry).href);
    if(typeof module.chromium?.launch!=='function')throw Error('Missing chromium.launch');
    return path.dirname(entry);
  } catch(error) {throw Error(`Cannot load Playwright at ${entry}: ${error.message}`);}
}

export async function resolveBrowserTools(requested, env=process.env) {
  const playwrightPath=await resolvePlaywright(requested,env);
  if(env.MELEE_BROWSER_PATH) {
    const browserPath=path.resolve(env.MELEE_BROWSER_PATH);
    if(!await executable(browserPath))throw Error('MELEE_BROWSER_PATH must name an executable browser file.');
    return {playwrightPath,browserPath,browser:{executablePath:browserPath}};
  }
  const candidates=process.platform==='darwin'
    ? ['/Applications/Google Chrome.app/Contents/MacOS/Google Chrome']
    : process.platform==='win32'
      ? [env.PROGRAMFILES,env['PROGRAMFILES(X86)'],env.LOCALAPPDATA].filter(Boolean).map(base=>path.join(base,'Google/Chrome/Application/chrome.exe'))
      : (env.PATH||'').split(path.delimiter).flatMap(base=>['google-chrome','google-chrome-stable'].map(name=>path.join(base,name)));
  for(const browserPath of candidates)if(await executable(browserPath))
    return {playwrightPath,browserPath,browser:{executablePath:browserPath}};
  throw Error('Google Chrome is unavailable. Install it or set MELEE_BROWSER_PATH to an installed Chromium browser.');
}

export async function loadBrowserTools(requested) {
  const config=await resolveBrowserTools(requested);
  const {chromium}=await import(pathToFileURL(path.join(config.playwrightPath,'index.mjs')).href);
  return {...config,chromium};
}
