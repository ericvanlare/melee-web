// SPDX-License-Identifier: GPL-3.0-or-later
/* Shared original CSS/SSS/agreed-start route for one-page and paired browser
 * diagnostics. It owns no runtime, browser, network, or native lifecycle. */
import assert from 'node:assert/strict';

const door=(setup,port)=>({kind:setup.doors[port*10],slotType:setup.doors[port*10+4],
  character:setup.doors[port*10+3],slot:setup.doors[port*10+6]});

export function createRuntimeMatchAdmission({page,driver,report,host,pageErrors=()=>[]}={}){
  if(!page||!driver||!report?.route||typeof pageErrors!=='function')
    throw Error('Shared match admission requires its owned page, driver, report, and error observer');
  const getHost=()=>typeof host==='function'?host():host;
  let preparedAgreement=null,preparedSeed=null,publicationDeadline=null;
  const addCheck=(name,details=null)=>report.checks.push({name,details});
  async function observePageHealth(label,{requireRunning=false}={}){
    const state=await page.evaluate(()=>{
      const m=globalThis.Module;
      const text=()=>{try{return typeof m?._melee_web_native_menu_message==='function'
        ?m.UTF8ToString(m._melee_web_native_menu_message()):null;}catch{return null;}};
      const timing=text();
      const error=document.querySelector('#status')?.dataset.runtimeError||
        document.querySelector('#error-dialog[open]')&&(document.querySelector('#error')?.textContent||'Application error');
      const running=typeof m?._melee_web_native_menu_running==='function'?m._melee_web_native_menu_running():null;
      const history=globalThis.__meleeRuntimeMatchHistory?.status()??null;
      const historyError=history&&(history.firstFailure||history.metadata.firstFailure||history.rendererErrors)
        ?m.UTF8ToString(m._melee_web_native_menu_match_history_error()):null;
      return {timing_message:timing,runtime_error:error||null,running,history,history_error:historyError,
        phase:typeof m?._melee_web_native_menu_phase==='function'?m._melee_web_native_menu_phase():null,
        replayActive:!!globalThis.meleeNetRuntimeLockstepSnapshot?.(),
        diagnosticStatus:document.querySelector('#status')?.textContent?.slice(0,600)||null};
    });
    report.last_health={label,...state};
    if(state.history_error)throw Error(`${label}: ${state.history_error}`);
    if(state.runtime_error)throw Error(`${label}: ${state.runtime_error}`);
    if(/^Paused after a timing disruption/i.test(state.timing_message||''))
      throw Error(`${label}: timing pause observed; automatic recovery is forbidden`);
    if(requireRunning&&state.running!==1)throw Error(`${label}: source runtime stopped (${state.timing_message||'no native message'})`);
    if(state.replayActive)throw Error(`${label}: a network/replay runtime owner is active`);
    if(pageErrors().length)throw Error(`${label}: browser error ${pageErrors()[0]}`);
    return state;
  }
  async function waitNativePhase(phase,label,{timeoutMs=30000,deadline=Date.now()+timeoutMs}={}){
    let last=null;
    while(Date.now()<deadline){
      last=await observePageHealth(label);
      const current=await page.evaluate(()=>({phase:Module._melee_web_native_menu_phase(),
        running:Module._melee_web_native_menu_running()}));
      if(current.phase===phase&&current.running===1)return {...last,...current};
      await page.waitForTimeout(20);
    }
    throw Error(`${label}: native phase ${phase} not reached: ${JSON.stringify(last)}`);
  }
  async function waitInitialCss(deadline){
    const readiness={null_observations:0,last_health:null};report.route.css_readiness=readiness;
    while(Date.now()<deadline){
      readiness.last_health=await observePageHealth('original CSS publication',{requireRunning:true});
      const setup=await page.evaluate(()=>window.menuObserveCssSetup?.()??null);
      if(setup!==null){
        assert(setup.cursors?.length===16&&setup.doors?.length===40&&setup.geometry?.length===48,
          'Original CSS source observer returned malformed extents');
        return setup;
      }
      readiness.null_observations++;await page.waitForTimeout(20);
    }
    throw Error(`Original CSS source observation was not published within entry deadline: ${JSON.stringify(readiness)}`);
  }
  async function observeMenuSourceFrame(){
    return page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>{
      const module=globalThis.Module,phase=module?._melee_web_native_menu_phase?.()??null;
      const running=module?._melee_web_native_menu_running?.()??null;
      const messagePointer=module?._melee_web_native_menu_message?.();
      const diagnosticsPointer=module?._melee_web_native_menu_diagnostics?.();
      const state={at_ms:performance.now(),phase,running,
        message:messagePointer?module.UTF8ToString(messagePointer):null,
        diagnostics:diagnosticsPointer?module.UTF8ToString(diagnosticsPointer):null,
        runtime_error:document.querySelector('#status')?.dataset.runtimeError||null,
        hidden:document.hidden,focused:document.hasFocus(),css:null,sss:null,input:null,observer_error:null};
      if(phase===1){
        if(typeof module?._melee_web_css_observe_port!=='function')state.observer_error='CSS source observer export is absent';
        else{
          const ids=module._malloc(14*4),geometry=module._malloc(8*4);
          if(!ids||!geometry){if(ids)module._free(ids);if(geometry)module._free(geometry);
            state.observer_error='CSS source observer allocation failed';}
          else try{
            const observed=module._melee_web_css_observe_port(0,8,ids,geometry)===1;
            state.css={observed,ids:observed?Array.from(module.HEAP32.subarray(ids>>2,(ids>>2)+14)):null,
              geometry:observed?Array.from(module.HEAPF32.subarray(geometry>>2,(geometry>>2)+8)):null};
          }catch(error){state.observer_error=`CSS source observation failed: ${String(error?.message||error)}`;}
          finally{module._free(ids);module._free(geometry);}
        }
      }
      if(phase===3){
        const sss={stage:null,gate:null};
        try{sss.stage=typeof window.menuObserveStage==='function'?window.menuObserveStage(32):null;}
        catch(error){sss.stage_error=String(error?.message||error);}
        if(typeof module?._melee_web_sss_observe_input_gate!=='function')
          sss.gate={available:false,observed:false,cooldown:null};
        else{
          const cooldown=module._malloc(4);
          if(!cooldown)sss.gate={available:true,observed:false,cooldown:null,error:'SSS cooldown observation allocation failed'};
          else try{
            const observed=module._melee_web_sss_observe_input_gate(32,cooldown)===1;
            sss.gate={available:true,observed,cooldown:observed?module.HEAP32[cooldown>>2]:null};
          }catch(error){sss.gate={available:true,observed:false,cooldown:null,error:String(error?.message||error)};}
          finally{module._free(cooldown);}
        }
        state.sss=sss;
      }
      try{if(typeof module?._melee_web_input_message==='function')
        state.input=JSON.parse(module.UTF8ToString(module._melee_web_input_message()));}
      catch(error){state.observer_error||=`Input state observation failed: ${String(error?.message||error)}`;}
      resolve(state);
    })));
  }
  function assertMenuSourceHealth(state,label){
    if(state.runtime_error)throw Error(`${label}: ${state.runtime_error}`);
    if(/^Paused after a timing disruption/i.test(state.message||''))
      throw Error(`${label}: timing pause observed; automatic recovery is forbidden`);
    if(state.observer_error)throw Error(`${label}: ${state.observer_error}`);
    if(pageErrors().length)throw Error(`${label}: browser error ${pageErrors()[0]}`);
    return state;
  }
  const cssStartReady=state=>{
    const ids=state.css?.ids;
    return state.phase===1&&state.running===1&&state.css?.observed===true&&
      ids?.length===14&&ids[5]===0&&ids[7]===0&&ids[8]!==0;
  };
  const sssInputReady=state=>{
    const gate=state.sss?.gate,stage=state.sss?.stage;
    return state.phase===3&&state.running===1&&stage?.ids?.length===4&&stage.geometry?.length===6&&
      gate?.available===true&&gate.observed===true&&Number.isInteger(gate.cooldown)&&gate.cooldown===0;
  };
  const menuReadinessSample=state=>{
    const ids=state.css?.ids,gate=state.sss?.gate,stage=state.sss?.stage;
    return {at_ms:state.at_ms,phase:state.phase,running:state.running,
      css_observed:state.css?.observed??null,css_cooldown:ids?.[5]??null,
      css_pending_scene:ids?.[7]??null,css_start_ready:ids?.[8]??null,
      css_last_start_trigger:ids?.[11]??null,css_last_start_ready:ids?.[12]??null,
      css_last_start_pending:ids?.[13]??null,sss_observed:stage!==null&&stage!==undefined,
      sss_selected_stage:stage?.ids?.[1]??null,sss_gate_available:gate?.available??null,
      sss_gate_observed:gate?.observed??null,sss_cooldown:gate?.cooldown??null};
  };
  async function waitForSourceReady({deadline,predicate,label,readiness}){
    let last=null;
    while(Date.now()<deadline){
      last=await observeMenuSourceFrame();readiness.last_state=last;
      try{assertMenuSourceHealth(last,label);}
      catch(error){readiness.outcome='observer-error';readiness.failure_state=last;throw error;}
      readiness.samples.push(menuReadinessSample(last));
      if(Date.now()>=deadline){readiness.outcome='deadline';readiness.failure_state=last;
        throw Error(`${label}: live source readiness was not observed before the shared 30-second route deadline: ${JSON.stringify(readiness.samples.at(-1)||null)}`);}
      if(predicate(last)){readiness.outcome='ready';readiness.ready_state=last;return last;}
      if(last.phase===3&&last.sss?.gate?.available===false){readiness.outcome='observer-unavailable';readiness.failure_state=last;
        throw Error(`${label}: private SSS cooldown observer export is unavailable`);}
    }
    readiness.outcome='deadline';readiness.failure_state=last;
    throw Error(`${label}: live source readiness did not become true before the shared 30-second route deadline: ${JSON.stringify(readiness.samples.at(-1)||null)}`);
  }
  async function configureControls(){
    await page.locator('#controls-open').click();await page.locator('#controls-dialog[open]').waitFor();
    await page.locator('#keyboard-layout').selectOption('two');
    await page.locator('#player-one-source').selectOption('keyboard');
    await page.locator('#player-two-source').selectOption('keyboard');
    await page.waitForFunction(()=>document.querySelector('#keyboard-layout')?.value==='two'&&
      document.querySelector('#player-one-source')?.value==='keyboard'&&
      document.querySelector('#player-two-source')?.value==='keyboard'&&
      document.querySelector('#player-one-source-status')?.textContent?.trim()==='Keyboard'&&
      document.querySelector('#player-two-source-status')?.textContent?.trim()==='Keyboard');
    report.route.controls=await page.evaluate(()=>({layout:document.querySelector('#keyboard-layout').value,
      playerOne:document.querySelector('#player-one-source').value,playerTwo:document.querySelector('#player-two-source').value,
      statusOne:document.querySelector('#player-one-source-status').textContent.trim(),
      statusTwo:document.querySelector('#player-two-source-status').textContent.trim()}));
    assert.deepEqual(report.route.controls,{layout:'two',playerOne:'keyboard',playerTwo:'keyboard',
      statusOne:'Keyboard',statusTwo:'Keyboard'});
    await page.locator('#controls-close').click();await page.locator('#controls-dialog').waitFor({state:'hidden'});
    addCheck('real controls select two-player keyboard for both ports',report.route.controls);
  }
  function verifyOriginalHumans(setup){
    report.route.css_before=[0,1,2,3].map(port=>door(setup,port));
    assert.deepEqual(report.route.css_before.map(row=>[row.kind,row.slotType]),
      [[0,0],[0,0],[3,3],[3,3]],'Original CSS did not present two Human doors');
    assert.deepEqual(report.route.css_before.slice(0,2).map(row=>row.character),[8,8],
      'Original CSS did not present Mario on both active doors');
    report.route.css_after=report.route.css_before;
    addCheck('original CSS presents two Human Mario doors',report.route.css_after);
  }
  async function chooseFinalDestination(){
    const stageDeadline=Date.now()+30000;
    const cssStartRoute=report.route.css_start={readiness:{deadline_ms:30000,samples:[],outcome:'waiting'},
      before_key:null,after_key:null,after_key_error:null};
    const cssReady=await waitForSourceReady({deadline:stageDeadline,predicate:cssStartReady,
      label:'original CSS Start source readiness',readiness:cssStartRoute.readiness});
    cssStartRoute.before_key=cssReady;
    let cssStartError=null;
    try{await driver.pressChord(['Enter'],{holdMs:80,releaseMs:100});}catch(error){cssStartError=error;}
    try{cssStartRoute.after_key=await observeMenuSourceFrame();
      assertMenuSourceHealth(cssStartRoute.after_key,'original CSS Start after-key source state');}
    catch(error){cssStartRoute.after_key_error=String(error?.message||error);}
    if(cssStartError)throw cssStartError;
    if(cssStartRoute.after_key_error)throw Error(`Could not retain original CSS Start after-key source state: ${cssStartRoute.after_key_error}`);
    addCheck('original CSS Start was sent by the keyboard driver after source cooldown/readiness',
      menuReadinessSample(cssStartRoute.before_key));

    const sssReadinessRoute=report.route.sss_readiness={deadline_ms:30000,deadline_at:new Date(stageDeadline).toISOString(),
      samples:[],outcome:'waiting',ready_state:null,failure_state:null};
    let sourceState=await waitForSourceReady({deadline:stageDeadline,predicate:sssInputReady,
      label:'original SSS source input readiness',readiness:sssReadinessRoute});
    const stageInputRoute=report.route.stage_input={before_first_movement:null,movements:[],
      after_last_movement:null,complete:false,failure_state:null};
    sourceState=await observeMenuSourceFrame();stageInputRoute.before_first_movement=sourceState;
    try{assertMenuSourceHealth(sourceState,'original SSS immediately before first cursor movement');}
    catch(error){stageInputRoute.failure_state=sourceState;throw error;}
    if(!sssInputReady(sourceState)){stageInputRoute.failure_state=sourceState;
      throw Error(`Original SSS source cooldown was not zero immediately before cursor movement: ${JSON.stringify(menuReadinessSample(sourceState))}`);}
    addCheck('original SSS cursor stage was source-ready before cursor input',menuReadinessSample(sourceState));
    let stage=sourceState.sss.stage;
    for(let attempt=0;attempt<240&&stage?.ids?.[1]!==32;attempt++){
      if(Date.now()>=stageDeadline){stageInputRoute.failure_state=sourceState;
        throw Error(`Original SSS cursor selection exceeded the shared 30-second route deadline: ${JSON.stringify(menuReadinessSample(sourceState))}`);}
      assert(sssInputReady(sourceState),`Original SSS source cooldown/readiness was unavailable before cursor movement ${attempt+1}: ${JSON.stringify(menuReadinessSample(sourceState))}`);
      const [x,y,tx,ty]=stage.geometry,keys=[];
      if(Math.abs(tx-x)>1.2)keys.push(tx>x?'d':'a');
      if(Math.abs(ty-y)>1.2)keys.push(ty>y?'w':'s');
      if(!keys.length)keys.push(tx>=x?'d':'a');
      await driver.pressChord(keys,{holdMs:24,releaseMs:24});
      const afterMovement=await observeMenuSourceFrame();
      stageInputRoute.movements.push({keys,after:menuReadinessSample(afterMovement)});
      stageInputRoute.after_last_movement=afterMovement;
      try{assertMenuSourceHealth(afterMovement,`SSS source cursor movement ${attempt+1}`);}
      catch(error){stageInputRoute.failure_state=afterMovement;throw error;}
      sourceState=afterMovement;
      if(!sssInputReady(sourceState)){stageInputRoute.failure_state=sourceState;
        throw Error(`Original SSS source cooldown/readiness was unavailable after cursor movement ${attempt+1}: ${JSON.stringify(menuReadinessSample(sourceState))}`);}
      stage=sourceState.sss.stage;
    }
    if(stage?.ids?.[1]!==32){stageInputRoute.failure_state=sourceState;
      throw Error(`Original SSS did not highlight Final Destination: ${JSON.stringify(menuReadinessSample(sourceState))}`);}
    stageInputRoute.complete=true;report.route.stage=stage;
    const sssConfirmRoute=report.route.sss_confirm={before_key:null,after_key:null,after_key_error:null};
    if(Date.now()>=stageDeadline){sssConfirmRoute.failure_state=sourceState;
      throw Error(`Original SSS confirmation exceeded the shared 30-second route deadline: ${JSON.stringify(menuReadinessSample(sourceState))}`);}
    const confirmState=await observeMenuSourceFrame();sssConfirmRoute.before_key=confirmState;
    try{assertMenuSourceHealth(confirmState,'original SSS immediately before Final Destination confirmation');}
    catch(error){sssConfirmRoute.failure_state=confirmState;throw error;}
    if(!sssInputReady(confirmState)||confirmState.sss.stage.ids[1]!==32){sssConfirmRoute.failure_state=confirmState;
      throw Error(`Original SSS confirmation requires source cooldown zero, live readiness, and Final Destination selected: ${JSON.stringify(menuReadinessSample(confirmState))}`);}
    let confirmError=null;
    try{await driver.pressChord(['j'],{holdMs:80,releaseMs:100});}catch(error){confirmError=error;}
    try{sssConfirmRoute.after_key=await observeMenuSourceFrame();
      assertMenuSourceHealth(sssConfirmRoute.after_key,'original SSS confirmation after-key source state');}
    catch(error){sssConfirmRoute.after_key_error=String(error?.message||error);}
    if(confirmError)throw confirmError;
    if(sssConfirmRoute.after_key_error)throw Error(`Could not retain original SSS confirmation after-key source state: ${sssConfirmRoute.after_key_error}`);
    addCheck('SSS cursor movement and Final Destination confirmation were gated by the live source cooldown observer',
      menuReadinessSample(sssConfirmRoute.before_key));
  }
  async function prepareOffer(){
    if(!getHost())throw Error('Shared match admission requires the loaded host identity before menu start');
    const cssEntryDeadline=Date.now()+90000;
    await page.locator('#launch').click();
    await waitNativePhase(1,'original CSS entry',{deadline:cssEntryDeadline});
    verifyOriginalHumans(await waitInitialCss(cssEntryDeadline));
    report.route.agreed_start={enabled:await page.evaluate(()=>globalThis.__meleeRuntimeMatchHistory.startEnable()),
      last_state:1,agreement:null,held:null,offer:null};
    await chooseFinalDestination();
    publicationDeadline=Date.now()+90000;
    const route=report.route.agreed_start,deadline=publicationDeadline;let state=null;
    while(Date.now()<deadline){
      state=await observePageHealth('waiting for source-selected match offer');
      const startState=await page.evaluate(()=>globalThis.__meleeRuntimeMatchHistory.startState());
      route.last_state=startState;
      if(startState===2){
        const offer=await page.evaluate(({host})=>{
          const offerJSON=globalThis.__meleeRuntimeMatchHistory.startOffer(host),value=JSON.parse(offerJSON);
          if(value.schema!=='melee-web-runtime-match-start-offer-v1')throw Error('Unexpected native offer schema');
          const seedText=value.selection?.random_seed;
          if(typeof seedText!=='string'||!/^(?:0x)?[0-9a-fA-F]{8}$/.test(seedText))
            throw Error('Native offer omitted its exact source selection seed');
          return {offer_json:offerJSON,offer:value,seed:Number.parseInt(seedText.replace(/^0x/,''),16)};
        },{host:getHost()});
        route.offer=offer;return offer;
      }
      await page.waitForTimeout(25);
    }
    throw Error(`Original source selection did not publish an offer: ${JSON.stringify({state,start:route.last_state})}`);
  }
  async function prepareAgreement({seed}={}){
    assert(Number.isInteger(seed)&&seed>=0&&seed<=0xffffffff,'Agreed start requires an exact uint32 seed');
    if(preparedAgreement!==null)throw Error('Native agreed-start JSON was already prepared for this page');
    const currentHost=getHost();
    if(!currentHost)throw Error('Agreed start requires the loaded host identity');
    const agreementJSON=await page.evaluate(({host,seed})=>{
      const controller=globalThis.__meleeRuntimeMatchHistory;
      return controller.startAgreement(seed,host);
    },{host:currentHost,seed});
    if(typeof agreementJSON!=='string'||!agreementJSON)throw Error('Native agreed-start preparation returned no exact JSON');
    preparedAgreement=agreementJSON;preparedSeed=seed;
    return agreementJSON;
  }
  async function commitAgreement({seed,agreementJSON=preparedAgreement,
    seedProvenance='source-selected match seed'}={}){
    assert(Number.isInteger(seed)&&seed>=0&&seed<=0xffffffff,'Agreed start requires an exact uint32 seed');
    assert.equal(seed,preparedSeed,'Agreed-start commit seed differs from the locally prepared agreement');
    assert.equal(agreementJSON,preparedAgreement,'Agreed-start commit JSON differs byte-for-byte from local native preparation');
    const currentHost=getHost();if(!currentHost)throw Error('Agreed start requires the loaded host identity');
    const committedState=await page.evaluate(({host,seed,agreementJSON})=>
      globalThis.__meleeRuntimeMatchHistory.startCommit(seed,agreementJSON,host),
      {host:currentHost,seed,agreementJSON});
    assert.equal(committedState,3,'Native agreed-start commit did not enter the committed state');
    const offer=report.route.agreed_start.offer;
    report.route.agreed_start.agreement={offer_json:offer.offer_json,agreement_json:preparedAgreement,
      seed,seed_provenance:seedProvenance,committed_state:committedState};
    return preparedAgreement;
  }
  async function agreeAndCommit({seed,expectedAgreementJSON=null,
    seedProvenance='source-selected match seed'}={}){
    const agreementJSON=await prepareAgreement({seed});
    if(expectedAgreementJSON!==null&&agreementJSON!==expectedAgreementJSON)
      throw Error('Native per-page agreement JSON differs byte-for-byte from the shared alpha agreement');
    return commitAgreement({seed,agreementJSON,seedProvenance});
  }
  async function waitHeld({deadline=publicationDeadline}={}){
    assert(Number.isFinite(deadline),'Held publication requires its original absolute deadline');
    const route=report.route.agreed_start;let state=null;
    while(Date.now()<deadline){
      state=await observePageHealth('waiting for agreed frame-zero match publication');
      const startState=await page.evaluate(()=>globalThis.__meleeRuntimeMatchHistory.startState());
      route.last_state=startState;
      if(startState===5){
        const held=await page.evaluate(()=>{
          const controller=globalThis.__meleeRuntimeMatchHistory;
          const publication=controller.startPublication(),liveIdentity=controller.startLiveIdentity();
          const prefixJSON=controller.startPrefix();
          const prefix=globalThis.__meleeValidateRuntimeMatchStartPrefix(prefixJSON,{publication,liveIdentity});
          return {publication:{...publication,raw:Array.from(publication.raw)},live_identity:liveIdentity,
            prefix_json:prefixJSON,prefix_summary:{source_ticks:prefix.source_ticks,source_draws:prefix.source_draws,
              final_ready_tick:prefix.final_ready_tick,input_provenance:prefix.input_provenance,
              initial_resident_transfer_drains:prefix.initial_resident_transfer_drains,
              final_resident_transfer_drains:prefix.final_resident_transfer_drains},
            match:JSON.parse(Module.UTF8ToString(Module._melee_web_native_menu_match_observe())),
            running:Module._melee_web_native_menu_running()};
        });
        route.held=held;report.route.match=held.match;
        assert.equal(held.running,0,'Agreed match must remain held before terminal takeover');
        assert.equal(held.match.frame,0,'Gameplay counter advanced beyond the actual GO boundary');
        assert.equal(held.match.ready,true,'Original HUD did not enable gameplay at the held boundary');
        addCheck('ordinary source startup reaches the actual GO boundary at held gameplay frame zero',{
          frame:held.match.frame,stage:held.match.rules.stage,prefix:held.prefix_summary,
          sourceCharacters:held.match.rules.player_source_characters,stocks:held.match.rules.player_stocks});
        return held;
      }
      await page.waitForTimeout(25);
    }
    throw Error(`Agreed original match did not publish frame zero: ${JSON.stringify({state,start:route.last_state})}`);
  }
  return Object.freeze({configureControls,prepareOffer,prepareAgreement,commitAgreement,
    agreeAndCommit,waitHeld});
}
