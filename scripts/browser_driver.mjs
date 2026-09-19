/** Shared ordinary-input browser operations. No source mutation or timing recovery.
 * Uses enabled UI controls and existing native lifecycle exports, never status prose.
 * This is harness plumbing; a successful operation is not gameplay acceptance.
 */
const SURFACES = Object.freeze({
  development: {import:'#disc', start:'#launch', pause:'#pause', unload:'#unload'},
  public: {import:'#choose-disc', start:'#start-game', pause:'#pause-game', unload:'#end-session'},
});

export function createBrowserDriver(page, {surface='development', timeoutMs=60000,
  deadline=Infinity}={}) {
  const controls=SURFACES[surface];
  if (!controls) throw Error('Unknown browser driver surface: '+surface);
  if (!Number.isFinite(timeoutMs)||timeoutMs<=0||!(deadline>0)) throw Error('Invalid browser driver deadline');
  const errors=[];
  const note=(kind,message)=>{if(errors.length<32)errors.push({kind,message:String(message).slice(0,2000)});};
  const pageError=error=>note('pageerror',error.message||error);
  const consoleError=message=>{if(message.type()==='error')note('console',message.text());};
  page.on('pageerror',pageError);page.on('console',consoleError);
  let inputQueue=Promise.resolve();
  const remaining=()=>{
    const ms=Math.min(timeoutMs,deadline-Date.now());
    if(ms<=0)throw Error('Browser operation wall-time bound exhausted');
    return Math.max(1,Math.floor(ms));
  };
  async function diagnostics() {
    // Run only at an explicit boundary/failure, never sample every source tick.
    let timer;
    try {
      return {...await Promise.race([page.evaluate(controls=>{
        const text=id=>document.getElementById(id)?.textContent?.slice(-4000)||null;
        const read=name=>{
          try {return typeof globalThis.Module?.[name]==='function'?globalThis.Module[name]():null;}
          catch {return null;}
        };
        return {url:location.href,hidden:document.hidden,focused:document.hasFocus(),
          status:text('status'),error:document.querySelector('#status')?.dataset.runtimeError||text('error'),log:text('log'),
          phase:read('_melee_web_native_menu_phase'),running:read('_melee_web_native_menu_running'),
          controls:Object.fromEntries(Object.entries(controls).map(([name,selector])=>{
            const node=document.querySelector(selector);
            return [name,node?{disabled:!!node.disabled,visible:!!node.getClientRects().length}:null];
          }))};
      },controls),new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('Diagnostic snapshot timed out')),3000);})]),errors:[...errors]};
    } catch(error) {return {unavailable:String(error.message||error),errors:[...errors]};}
    finally {clearTimeout(timer);}
  }
  async function step(name,run) {
    try {remaining();return await run();}
    catch(cause) {
      const error=new Error(`Browser driver ${surface}/${name}: ${cause.message||cause}`,{cause});
      error.step=name;error.diagnostics=await diagnostics();
      throw error;
    }
  }
  async function enabled(selector,{allowRecovery=false}={}) {
    // waitForFunction also detects a visible application error immediately.
    const result=await page.waitForFunction(({selector,allowRecovery})=>{
      const control=document.querySelector(selector);
      const ready=control&&!control.disabled&&control.getClientRects().length;
      const runtimeError=document.querySelector('#status')?.dataset.runtimeError;
      if(runtimeError&&!(allowRecovery&&ready))return {error:runtimeError};
      const dialog=document.querySelector('#error-dialog[open]');
      if(dialog)return {error:document.querySelector('#error')?.textContent||'Application error'};
      return ready?{ready:true}:false;
    },{selector,allowRecovery},{timeout:remaining()});
    const value=await result.jsonValue();await result.dispose();
    if(value.error)throw Error(value.error);
  }
  const waitForImport=()=>step('wait-for-import',()=>enabled(controls.import));
  const waitForStart=()=>step('wait-for-start',()=>enabled(controls.start));
  const waitForPhase=phase=>step('wait-for-phase-'+phase,async()=>{
    if(!Number.isInteger(phase)||phase<0)throw Error('Invalid native scene phase');
    const result=await page.waitForFunction(({phase,pause})=>{
      const runtimeError=document.querySelector('#status')?.dataset.runtimeError;
      if(runtimeError)return {error:runtimeError};
      if(document.querySelector('#error-dialog[open]'))return {error:document.querySelector('#error')?.textContent||'Application error'};
      const module=globalThis.Module;
      return typeof module?._melee_web_native_menu_phase==='function'&&
        module._melee_web_native_menu_phase()===phase&&module._melee_web_native_menu_running()&&
        document.querySelector(pause)&&!document.querySelector(pause).disabled?{ready:true}:false;
    },{phase,pause:controls.pause},{timeout:remaining()});
    const value=await result.jsonValue();await result.dispose();
    if(value.error)throw Error(value.error);
  });
  async function selectDisc(file) {
    return step('select-disc',async()=>{
      await enabled(controls.import,{allowRecovery:true});
      if(surface==='development') {
        await page.locator(controls.import).setInputFiles(file,{timeout:remaining()});
      } else {
        await page.locator(controls.import).click({timeout:remaining()});
        if(!await page.locator('#disc-continue').isDisabled())throw Error('Disc acknowledgement was bypassed');
        await page.locator('#disc-ack').check({timeout:remaining()});
        const [chooser]=await Promise.all([
          page.waitForEvent('filechooser',{timeout:remaining()}),
          page.locator('#disc-continue').click({timeout:remaining()}),
        ]);
        await chooser.setFiles(file,{timeout:remaining()});
      }
      // Invalid-input tests inspect their own error. Call waitForStart separately.
    });
  }
  const launch=(phase=1)=>step('launch',async()=>{
    await enabled(controls.start);await page.locator(controls.start).click({timeout:remaining()});
    await waitForPhase(phase);
  });
  function pressChord(keys,{holdMs=120,releaseMs=150}={}) {
    // Concurrent callers are serialized as complete down/hold/up sequences.
    // A failed sequence stops queued input; the caller must diagnose the failure.
    const next=inputQueue.then(()=>step('keyboard-chord',async()=>{
      if(!Array.isArray(keys)||!keys.length||new Set(keys).size!==keys.length||
        keys.some(key=>typeof key!=='string'||!key))throw Error('Expected distinct keyboard keys');
      if(![holdMs,releaseMs].every(ms=>Number.isFinite(ms)&&ms>=0&&ms<=5000))throw Error('Invalid keyboard hold/release duration');
      if(holdMs+releaseMs>=remaining())throw Error('Keyboard sequence exceeds remaining wall-time bound');
      await page.locator('#canvas').focus({timeout:remaining()});
      const held=[];
      try {
        // A transport rejection can arrive after delivery. Release attempted
        // keys too, so an ambiguous down cannot leave a modifier held.
        for(const key of keys){held.push(key);await page.keyboard.down(key);}
        await page.waitForTimeout(holdMs);
      } finally {
        // Always attempt every release, including after a down/hold failure.
        const releases=[];
        for(const key of held.reverse())try{await page.keyboard.up(key);}catch(error){releases.push(error);}
        if(releases.length)throw new AggregateError(releases,'Keyboard release failed');
      }
      await page.waitForTimeout(releaseMs);
    }));
    inputQueue=next;
    return next;
  }
  const unload=()=>step('unload',async()=>{
    await enabled(controls.unload,{allowRecovery:true});
    if(surface==='public')await Promise.all([
      page.waitForEvent('load',{timeout:remaining()}),
      page.locator(controls.unload).click({timeout:remaining()}),
    ]);
    else await page.locator(controls.unload).click({timeout:remaining()});
    await enabled(controls.import);
    if(surface==='development') {
      const result=await page.waitForFunction(()=>{
        const error=document.querySelector('#status')?.dataset.runtimeError;
        if(error)return {error};
        const module=globalThis.Module;
        return module?._melee_web_native_menu_phase?.()===0&&
          !module._melee_web_native_menu_running()?{ready:true}:false;
      },null,{timeout:remaining()});
      const value=await result.jsonValue();await result.dispose();
      if(value.error)throw Error(value.error);
    }
  });
  return {waitForImport,waitForStart,waitForPhase,selectDisc,launch,pressChord,unload,diagnostics,
    dispose(){page.off('pageerror',pageError);page.off('console',consoleError);}};
}
