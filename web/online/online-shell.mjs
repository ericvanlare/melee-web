import {mountMeleeRuntime} from '../melee-runtime.mjs';
import {mountControllerSettings} from '../controller-settings.mjs';
import {createRuntimeAudio} from '../runtime-audio.mjs';
import {loadNativeGameDisc,openNativeGameSession} from '../runtime-audio-assets.mjs';
import {STAGING_RELAY_URL} from '../staging-room.mjs';
import {createRoomId} from '../../scripts/net_lockstep_websocket_relay.mjs';
import {createRuntimeMatchHistoryController} from '../../src/slippi_profile/runtime_match_history_controller.mjs';
import {createStagingMatchStart} from '../../src/slippi_profile/staging_match_start.mjs';
import {createRuntimeMatchSessionBridge} from '../../src/slippi_profile/runtime_match_session_bridge.mjs';
import {createRuntimeMatchSessionLoop} from '../../src/slippi_profile/runtime_match_session_loop.mjs';
import {RUNTIME_MATCH_HISTORY_STREAM_INPUT_TICKS, RUNTIME_MATCH_HISTORY_PEER_PROFILE}
  from '../../src/slippi_profile/runtime_match_history_peer.mjs';

const $=id=>document.getElementById(id);
const runtimeBase=new URL('../../runtime/',import.meta.url);
const heldKeys=new Set();
window.addEventListener('keydown',event=>heldKeys.add(event.code),true);
window.addEventListener('keyup',event=>heldKeys.delete(event.code),true);
window.addEventListener('blur',()=>heldKeys.clear(),true);
let player,context,state,controller,setup,bridge,loop,prepareWork,loopWork,closed=false,roomUsed=false,discReady=false;
let hostIdentity,held,packageInfo,table,role,closeWork,historyOwned=false,nativeRetired=false,userLeave=false;
let startCommitted=false;
const report={schema:'melee-web-staging-rollback-session-v1',result:'pending',failure:null,
  scope:'experimental Mario/FD match-only rollback; no full-profile or timing acceptance',
  sourceBoundaryFailures:[]};
function error(error){
  const message=String(error?.message??error);$('error').textContent=message;$('error').hidden=false;
  report.failure??=message;report.result='FAIL';
}
const status=text=>{$('room-status').textContent=text;};
const hex=bytes=>[...new Uint8Array(bytes)].map(x=>x.toString(16).padStart(2,'0')).join('');
const digest=async bytes=>hex(await crypto.subtle.digest('SHA-256',bytes));
const sha=value=>typeof value==='string'&&/^[0-9a-f]{64}$/.test(value);
function render(next){
  state=next;settings.setState(next);
  $('runtime-status').textContent=next.message||next.state;
  $('choose-disc').disabled=!next.canSelectDisc||roomUsed||closed;
  $('host').disabled=$('join').disabled=!discReady||!next.canStart||roomUsed||closed;
  $('loading-panel').hidden=!next.loading;
  $('loading-label').textContent=next.loading?.message||'Preparing graphics…';
  const progress=$('loading-progress');
  if(next.loading?.total>0){progress.max=next.loading.total;progress.value=next.loading.complete;}
  else progress.removeAttribute('value');
  if(next.state==='error'){error(next.message);void close().catch(error);}
}
const settings=mountControllerSettings({container:$('controls-dialog'),openButton:$('controls-open'),
  preferenceKey:'melee-online-controls-v1',initialSources:['auto','auto','off','off'],
  onError:error,focus:()=>player?.focus()});
function healthy(){
  if(closed)throw Error('Room was closed. Reload to begin a new session.');
  if(state?.state==='error')throw Error(state.message);
}
async function until(read,{signal,timeoutMs=60000}={}){
  const deadline=performance.now()+timeoutMs;
  for(;;){
    healthy();signal?.throwIfAborted();
    const value=read();if(value)return value;
    if(performance.now()>=deadline)throw Error('The native player did not reach the expected boundary.');
    await new Promise(resolve=>setTimeout(resolve,20));
  }
}
function neutralControls(){
  // The native offer holds source polling. Inspect release on the actual browser
  // input side; never invent a PAD row or poll the game to manufacture neutrality.
  if(player.controllers.getPortSource(0)==='touch')
    throw Error('This desktop online slice supports keyboard and controller input. Choose either in Controls.');
  if(!document.hasFocus()||document.activeElement!==$('canvas')||heldKeys.size)return false;
  return player.controllers.inspect().filter(row=>row.enabled&&row.port>=0&&row.port<2)
    .every(row=>row.status==='ready'&&row.pad.buttons===0&&
      [...row.pad.stick,...row.pad.cstick,...row.pad.triggers].every(value=>value===0));
}
async function baselineDigest(){
  healthy();
  // The original card manifest is SaveData plus seven authored NameTag banks.
  const bytes=0x1790+7*0x1f2c,m=context.Module;
  const copy=await context.boundary(()=>{
    const pointer=m._malloc(bytes);if(!pointer)throw Error('Cannot allocate baseline export.');
    try{
      context.check(m._melee_web_native_menu_snapshot_save_profile(pointer,bytes,1));
      return m.HEAPU8.slice(pointer,pointer+bytes);
    }finally{m._free(pointer);}
  });
  return digest(copy);
}
async function loadPackage(){
  const response=await fetch(new URL('rollback.json',runtimeBase),{cache:'no-cache'});
  if(!response.ok)throw Error('The staging runtime manifest is unavailable.');
  const manifest=await response.json();
  if(manifest.schema!=='melee-web-rollback-staging-player-v1'||
     manifest.profileIdentity!==RUNTIME_MATCH_HISTORY_PEER_PROFILE||
     !/^[0-9a-f]{40}$/.test(manifest.sourceSha)||!sha(manifest.wasmSha256)||
     !sha(manifest.tableSha256)||!sha(manifest.policySha256))
    throw Error('The staging runtime identity is invalid.');
  async function read(name,expected){
    if(!sha(expected))throw Error('Missing artifact identity: '+name);
    const response=await fetch(new URL(name,runtimeBase),{cache:'no-cache'});
    if(!response.ok)throw Error('The staging runtime file is unavailable: '+name);
    const bytes=await response.arrayBuffer();
    if(await digest(bytes)!==expected)throw Error('The staging runtime file changed: '+name);
    return bytes;
  }
  if(manifest.artifacts?.['gameplay_menu_browser.wasm']!==manifest.wasmSha256)
    throw Error('The staging manifest has inconsistent Wasm identities.');
  const [wasm,data,tableBytes]=await Promise.all([
    read('gameplay_menu_browser.wasm',manifest.wasmSha256),
    read('gameplay_menu_browser.data',manifest.artifacts['gameplay_menu_browser.data']),
    read('linked-statics.json',manifest.tableSha256)]);
  const linked=JSON.parse(new TextDecoder().decode(tableBytes));
  if(linked.schema!=='melee-web-rollback-linked-statics-v1'||linked.wasmSha256!==manifest.wasmSha256||
     linked.policySha256!==manifest.policySha256||!Array.isArray(linked.rows)||linked.rows.length>65536)
    throw Error('The staging restore table does not bind this runtime.');
  const js=manifest.artifacts['gameplay_menu_browser.js'];
  if(!sha(js))throw Error('Missing staging loader identity.');
  const integrity='sha256-'+btoa(String.fromCharCode(...js.match(/../g).map(x=>parseInt(x,16))));
  return {manifest,linked,wasm,data,integrity};
}
async function close(){
  if(closeWork)return closeWork;
  closed=true;loop?.abortLateScenario();
  closeWork=(async()=>{
    const failures=[];
    for(const action of [()=>bridge?.close(),()=>setup?.close(),async()=>{
      // Preparation can already own an asynchronous native command before the
      // live loop promise exists. Let it settle before asking that owner to retire.
      await Promise.allSettled([prepareWork,loopWork].filter(Boolean));
      if(controller?.status().ownerState===1){
        // Native cleanup closes the game and cancels its main loop. An ordinary
        // queued unload cannot run after that terminal command.
        report.cleanupOwner=await controller.command('cleanup');nativeRetired=true;
        await context.pauseAudioForPreparation();
      }
      historyOwned=false;
    }]){
      try{await action();}catch(cause){failures.push(cause);}
    }
    $('host').disabled=$('join').disabled=$('leave').disabled=$('controls-open').disabled=true;
    report.closed=true;
    if(failures.length)throw new AggregateError(failures,'Room cleanup failed.');
  })();
  closeWork.catch(()=>{});return closeWork;
}
// Bounded, read-only diagnostics for the same UI that players use. Native
// observers copy source state; callers receive no runtime or command handles.
export async function onlineSessionSnapshot({source=false,diagnostics=false}={}){
  const supplement=async snapshot=>{
    snapshot.stagingDiagnosticsEnabled=context?.callbacks?.menuStagingDiagnosticsEnabled()??false;
    if(diagnostics){
      snapshot.session=structuredClone(report);
      snapshot.incidents=context?.diagnostics?.exportReports()??null;
      snapshot.retainedIncidents=await context?.diagnostics?.exportRetained()??null;
    }
    return snapshot;
  };
  // Once the agreed start is committed, takeover may cancel the ordinary
  // callback queue before a newly queued read runs. Observe this transition in
  // JavaScript only; the typed history owner resumes native reads below.
  if(!controller||(!historyOwned&&!nativeRetired&&(state?.requiresReload||startCommitted)))return supplement(structuredClone({
    role,roomId:report.roomId,stage:closed?'closed':report.failure?'failed':roomUsed?'starting':'loading',
    error:report.failure,phase:null,running:null,owner:null,result:report.result,
    closed:!!report.closed,
  }));
  const sample=()=>{
    const m=context?.Module,owner=controller?.status()??null;
    const phase=m?._melee_web_native_menu_phase?.()??null;
    const snapshot={role,roomId:report.roomId,localPort:role==='alpha'?0:role==='beta'?1:null,
      stage:closed?'closed':report.failure?'failed':report.result==='MATCH_FINAL_COMPLETE'?'complete':
        loopWork?'playing':roomUsed?'starting':discReady?'ready':'loading',
      roomStatus:$('room-status').textContent,error:report.failure,phase,
      running:m?._melee_web_native_menu_running?.()??null,owner,
      progress:report.progress??null,result:report.result,closed:!!report.closed};
    if(source&&m){
      const readJSON=name=>{const pointer=m[name]?.();return pointer?JSON.parse(m.UTF8ToString(pointer)):null;};
      snapshot.input=readJSON('_melee_web_input_message');
      if(phase===1||phase===3){
        const sizes=phase===1?[64,160,192]:[16,24,4],pointers=[];
        try{
          for(const size of sizes){const p=m._malloc(size);if(!p)throw Error('Cannot allocate menu observation.');pointers.push(p);}
          if(phase===1&&m._melee_web_css_observe_setup(...pointers)){
            snapshot.css={cursors:Array.from(m.HEAP32.subarray(pointers[0]>>2,(pointers[0]>>2)+16)),
              doors:Array.from(m.HEAP32.subarray(pointers[1]>>2,(pointers[1]>>2)+40)),
              geometry:Array.from(m.HEAPF32.subarray(pointers[2]>>2,(pointers[2]>>2)+48))};
          }else if(phase===3&&m._melee_web_sss_observe(32,pointers[0],pointers[1])){
            const gateObserved=m._melee_web_sss_observe_input_gate(32,pointers[2])===1;
            snapshot.sss={ids:Array.from(m.HEAP32.subarray(pointers[0]>>2,(pointers[0]>>2)+4)),
              geometry:Array.from(m.HEAPF32.subarray(pointers[1]>>2,(pointers[1]>>2)+6)),
              gateObserved,cooldown:gateObserved?m.HEAP32[pointers[2]>>2]:null};
          }
        }finally{for(const pointer of pointers)m._free(pointer);}
      }
      if(owner?.ownerState===1&&!owner.pendingCommand&&owner.completeSequence===owner.requestSequence&&
          !owner.gpuPending&&!owner.endpointActive&&!owner.firstFailure&&!owner.rendererErrors)
        snapshot.match=readJSON('_melee_web_native_menu_match_observe');
    }
    return structuredClone(snapshot);
  };
  // The ordinary runtime owns menu/loading Asyncify. During rollback the typed
  // controller guards its own inline calls instead; never re-enter either owner.
  return supplement(!historyOwned&&!nativeRetired?await context.boundary(sample):sample());
}
window.onlineSessionSnapshot=onlineSessionSnapshot;
async function openRoom(nextRole,roomId){
  if(roomUsed||!discReady||!state.canStart)throw Error('Choose a disc before starting a new room.');
  if(!/^[A-Za-z0-9_-]{24,64}$/.test(roomId))throw Error('Paste the complete room code.');
  roomUsed=true;role=nextRole;report.role=role;report.roomId=roomId;
  $('room-code').value=roomId;$('host').disabled=$('join').disabled=true;
  $('copy').disabled=false;$('leave').disabled=false;
  status(role==='alpha'?'Room created. Share the code and choose locally.':'Joining room…');
  await player.start();
  await until(()=>context.Module._melee_web_native_menu_phase()===1);
  controller.startEnable();
  const disc=await player.discIdentity();healthy();
  const assetIdentity=JSON.stringify({algorithm:disc.algorithm,dol:disc.dol,fst:disc.fst,
    dolSize:disc.dolSize,fstSize:disc.fstSize});
  hostIdentity={runtime_sha256:packageInfo.wasmSha256,
    asset_identity_sha256:await digest(new TextEncoder().encode(assetIdentity)),
    save_profile_sha256:await baselineDigest(),profile_identity:packageInfo.profileIdentity};
  healthy();
  report.identity=hostIdentity;
  $('instructions').textContent='On each Mac, keep both Mario selections. Press Start after READY TO FIGHT, then choose Final Destination. Release controls while the match prepares. You will play P'+(role==='alpha'?'1':'2')+'.';
  setup=createStagingMatchStart({role,url:STAGING_RELAY_URL,roomId,identity:hostIdentity,onState:status,
    onFailure:cause=>{error(cause);void close().catch(error);},
    native:{
      async offer({signal}){
        await until(()=>controller.startState()===2,{signal,timeoutMs:120000});
        status('Release controls; waiting for the other player.');
        await until(neutralControls,{signal,timeoutMs:10000});
        return controller.startOffer(hostIdentity);
      },
      agreement:seed=>controller.startAgreement(seed,hostIdentity),
      commit:(seed,agreement)=>{
        startCommitted=true;
        return controller.startCommit(seed,agreement,hostIdentity);
      },
      async held({signal}){
        await until(()=>controller.startState()===5&&!controller.status().gpuPending,{signal});
        held={identity:controller.startLiveIdentity(),prefix:controller.startPrefix(),
          publication:controller.startPublication()};
        return held;
      },
    }});
  context.attachNetworkSession(close);player.focus();
  const agreed=await setup.negotiate();healthy();
  report.start=agreed;
  const localPort=agreed.localPort,remotePort=1-localPort;
  const prefix=JSON.parse(held.prefix),raw=prefix.rows.at(-1).pad_hex;
  if(!/^[0-9a-f]{88}$/.test(raw))throw Error('The source start has no actual four-port PAD row.');
  const initialRemotePad=raw.match(/../g).slice(remotePort*11,(remotePort+1)*11).map(x=>parseInt(x,16));
  if(localPort===1){
    const controllers=player.controllers.inspect();
    const local=controllers.find(row=>row.port===0&&row.status==='ready');
    if(local&&!controllers.some(row=>row.port===1&&row.status==='ready'))
      player.controllers.assign(local.key,1);
  }
  historyOwned=true;
  controller.takeOver({sourceFrame:0,localPort,remotePort});controller.configure(table.rows);
  const identity=controller.identity(hostIdentity);
  if(identity!==agreed.liveIdentity)throw Error('Native match identity changed at takeover.');
  bridge=createRuntimeMatchSessionBridge({owner:controller,role,relayUrl:STAGING_RELAY_URL,roomId,
    sourceIdentityJson:identity,loadedIdentity:hostIdentity,initialRemotePad,streaming:true,
    inputTicks:RUNTIME_MATCH_HISTORY_STREAM_INPUT_TICKS,matchFinal:true,
    createEndpoint:setup.createMatchEndpoint});
  await bridge.call('peerStart');healthy();
  loop=createRuntimeMatchSessionLoop({owner:controller,peer:bridge,role,streaming:true,
    inputTicks:RUNTIME_MATCH_HISTORY_STREAM_INPUT_TICKS,matchFinal:true,networkBackpressure:true,
    onState:summary=>{report.progress=summary;}});
  prepareWork=loop.prepare();await prepareWork;healthy();
  $('report').disabled=true;
  status('Playing · P'+(localPort+1)+' · rollback active');
  $('instructions').textContent='Use Controls for your assigned player slot. Menus and Results run locally. Reload to create another room.';
  player.focus();loopWork=loop.run();
  const result=await loopWork;report.match=result;$('report').disabled=false;
  if(result.result!=='MATCH_FINAL_COMPLETE'){
    if(closed&&userLeave&&!report.failure&&result.result==='CANCELLED'&&
        !controller.status().firstFailure&&!controller.status().rendererErrors){
      report.result='LEFT_EARLY'; // Retain the loop failure/abort detail; this is no completed-match claim.
      return;
    }
    throw Error(result.failure?.message||'Rollback session stopped.');
  }
  historyOwned=false;
  startCommitted=false;
  report.result='MATCH_FINAL_COMPLETE';
  await bridge.close();
  status('Match confirmed. Continue through the original Results screen.');
  $('leave').disabled=false;
}
$('choose-disc').onclick=()=>$('disc-dialog').showModal();
$('disc-cancel').onclick=()=>$('disc-dialog').close();
$('disc-confirm').onclick=()=>{$('disc-dialog').close();void player.activateAudio().catch(error);$('disc-file').click();};
$('disc-file').onchange=async()=>{
  const file=$('disc-file').files[0];if(!file)return;
  try{
    discReady=false;render(state);status('Reading your local disc…');
    await until(()=>state.canImport);
    await player.importDisc(file);discReady=true;render(player.getState());
    status('Ready. Host a match or paste a room code.');
  }catch(cause){error(cause);}
  finally{$('disc-file').value='';}
};
for(const [id,nextRole] of [['host','alpha'],['join','beta']])
  $(id).onclick=()=>void openRoom(nextRole,nextRole==='alpha'?createRoomId():$('room-code').value.trim())
    .catch(cause=>{error(cause);void close().catch(error);});
$('leave').onclick=async()=>{userLeave=true;try{await close();if(!nativeRetired)await player.unload();status('Room closed. Reload to play again.');}catch(cause){error(cause);}};
$('copy').onclick=async()=>{try{
  const url=new URL(location.href);url.hash=new URLSearchParams({room:$('room-code').value}).toString();
  await navigator.clipboard.writeText(url.href);status('Invite copied.');
}catch{status('Copy the room code above and share it.');}};
$('report').onclick=()=>{
  const url=URL.createObjectURL(new Blob([JSON.stringify({...report,
    diagnostics:context?.diagnostics?.exportReports()},null,2)],{type:'application/json'}));
  const a=document.createElement('a');a.href=url;a.download='webmelee-rollback-session.json';a.click();
  setTimeout(()=>URL.revokeObjectURL(url),1000);
};
const invite=new URLSearchParams(location.hash.slice(1)).get('room');
if(invite&&/^[A-Za-z0-9_-]{24,64}$/.test(invite))$('room-code').value=invite;
try{
  const loaded=await loadPackage();packageInfo=loaded.manifest;table=loaded.linked;report.producer=packageInfo;
  player=await mountMeleeRuntime({canvas:$('canvas'),onState:render,onError:error,
    onLog:(text,isError)=>{
      if(isError&&text.startsWith('MATCH_HISTORY_SOURCE_BOUNDARY_REFUSED ')&&
          report.sourceBoundaryFailures.length<8)
        report.sourceBoundaryFailures.push({at:performance.now(),text:text.slice(0,512)});
    },
    onOwner:owner=>{context=owner;player=owner.handle;},createAudio:createRuntimeAudio,
    readDisc:loadNativeGameDisc,openDisc:openNativeGameSession,
    stagingDiagnosticsOrigin:'https://webmelee-staging.pages.dev',
    loaderUrl:new URL('gameplay_menu_browser.js',runtimeBase),loaderIntegrity:loaded.integrity,
    configureModule(module){
      module.wasmBinary=loaded.wasm;
      module.getPreloadedPackage=(name,size)=>{
        if(!name.endsWith('gameplay_menu_browser.data')||size!==loaded.data.byteLength)
          throw Error('Unexpected native preload package.');
        return loaded.data;
      };
      installRuntimeCache(module,()=>{});
    }});
  controller=createRuntimeMatchHistoryController(context.Module,{mode:'staging'});
  await settings.bindPlayer(player);render(player.getState());
  status('Choose a local disc to begin.');
}catch(cause){error(cause);}
window.addEventListener('pagehide',()=>{void close().catch(()=>{});},{once:true});
