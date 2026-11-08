// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import test from 'node:test';
import {createServer} from 'node:http';
import {createHash} from 'node:crypto';
import {createStagingMatchStart, stagingHeldIdentity, validateStagingLobbyState,
  validateStagingOwnedChoice, validateStagingOwnedStage} from '../staging_match_start.mjs';

const identity = {runtime_sha256:'a'.repeat(64), asset_identity_sha256:'b'.repeat(64),
  save_profile_sha256:'c'.repeat(64), profile_identity:'experimental-test-profile'};
function held({poll = 10, submission = 20, pad = '00'.repeat(44)} = {}) {
  const live = JSON.stringify({schema:'modeled-source-identity', source_frame:0, seed:'00000011'});
  const raw = new Uint8Array(48), view = new DataView(raw.buffer);
  [2,48,5,0,2,1,0,0].forEach((value,index) => view.setUint32(index*4,value,true));
  view.setBigUint64(32,BigInt(submission),true);
  view.setBigUint64(40,BigInt(submission+2),true);
  const publication = {schema:2,bytes:48,state:5,sourceFrame:0,sourceDraws:2,padPolicy:1,
    firstUsePending:0,reserved:0,priorSubmission:String(submission),
    sourceSubmission:String(submission+2),raw};
  const prefix = JSON.stringify({schema:'melee-web-runtime-match-start-prefix-v1',
    input_provenance:'ordinary-local-four-port-pad-v1',source_ticks:2,source_draws:2,
    final_ready_tick:2,initial_resident_transfer_drains:0,final_resident_transfer_drains:1,
    rows:[0,1].map(index => ({poll_serial:String(poll+index),tick_before:index,
      tick_after:index+1,source_frame_before:0,source_frame_after:0,hud_ready_before:false,
      hud_ready_after:index===1,pad_hex:pad,draw_submission:String(submission+index+1)})),
    live_identity:JSON.parse(live)});
  return {identity:live,prefix,publication};
}
function peers({offerChange, agreementChange, prefixChange, choiceNever = false,
  hostStageNever = false, heldNever = false} = {}) {
  const ends = {}, traffic = [], calls = {alpha:[],beta:[]}, failures = [], lobby = {alpha:[],beta:[]};
  let resolveReady;
  const ready = new Promise(resolve => { resolveReady=resolve; });
  const createEndpoint = options => {
    const {role}=options, other=role==='alpha'?'beta':'alpha';
    const endpoint = {options, closed:false, errors:[], ready, transport:{type:'modeled-room'},
      chain:Promise.resolve(),
      send(text) {
        assert(!endpoint.closed);
        traffic.push({role,text});
        const target=ends[other];
        assert(target, 'both modeled endpoints must exist before send');
        target.chain=target.chain.then(() => target.options.onMessage(text));
        target.chain.catch(error => { target.errors.push(error); target.options.onEndpointError(other,error); });
        return Promise.resolve();
      },
      async flush() {},
      async drainInbound() { await endpoint.chain; },
      async close() { endpoint.closed=true; },
      destroy() { endpoint.closed=true; },
    };
    ends[role]=endpoint;
    if (ends.alpha && ends.beta) resolveReady();
    return endpoint;
  };
  const owners={};
  const choices={alpha:{character:8,costume:0},beta:{character:8,costume:1}};
  const bound={};
  const offerFor=(role,stage=32)=>{
    const seed=role==='alpha'?'00000011':'00000022';
    const selection={random_seed:seed,start:{players:[
      {ckind:bound[role]?.alpha.character??8,color:bound[role]?.alpha.costume??0},
      {ckind:bound[role]?.beta.character??8,color:bound[role]?.beta.costume??1}],
    rules:{stage_kind:stage}}};
    if(role==='beta'&&offerChange)offerChange(selection);
    return JSON.stringify({schema:'melee-web-runtime-match-start-offer-v1',...identity,
      local_menu_rng_seed_provenance:seed,selection});
  };
  for (const role of ['alpha','beta']) {
    const seed=role==='alpha'?'00000011':'00000022';
    const native = {
      choice() { calls[role].push('choice');
        if(choiceNever===true||choiceNever===role)return new Promise(()=>{});
        return {...choices[role]}; },
      selectChoices(value) { calls[role].push('selectChoices');bound[role]=structuredClone(value); },
      hostStage() { calls[role].push('hostStage');
        if(role==='alpha'&&hostStageNever)return new Promise(()=>{});
        return offerFor(role); },
      applyStage(value) { calls[role].push(['applyStage',value]); },
      offer() { calls[role].push('offer');return role==='alpha'?offerFor(role):offerFor(role); },
      agreement(value) { calls[role].push(['agreement',value]);
        return JSON.stringify({nativeAgreement:{...bound[role],random_seed:value},
          ...(role==='beta' && agreementChange ? {unexpected:true} : {})}); },
      commit(value,text) { calls[role].push(['commit',value,text]); },
      held() { calls[role].push('held');
        if (heldNever===true||heldNever===role) return new Promise(() => {});
        return held(role==='beta'?{poll:100,submission:200,...(prefixChange?{pad:'01'.repeat(44)}:{})}:{});
      },
    };
    owners[role]=createStagingMatchStart({role,url:'wss://example.test',roomId:'test-room',identity,
      native,createEndpoint,timeoutMs:2000,onFailure:error=>failures.push({role,error}),
      onLobby:entry=>lobby[role].push(entry)});
  }
  return {owners,ends,traffic,calls,failures,lobby};
}

test('local menu choices and progress reach both peers before either commits',async()=>{
  const f=peers();
  const css={phase:'css',choice:{character:8,costume:0},stage:null};
  const guestCss={phase:'css',choice:{character:8,costume:1},stage:null};
  await f.owners.alpha.publishLobby(css);
  await f.owners.beta.publishLobby(guestCss);
  await Promise.all(Object.values(f.ends).map(e=>e.drainInbound()));
  for(const role of ['alpha','beta']){
    assert(f.lobby[role].some(e=>e.role==='alpha'&&e.state.choice.costume===0));
    assert(f.lobby[role].some(e=>e.role==='beta'&&e.state.choice.costume===1));
    assert.equal(f.calls[role].length,0,'a cursor observation must not commit or create a match');
  }
  await f.owners.alpha.publishLobby(css);
  assert.equal(f.traffic.length,2,'unchanged observations should not flood the room');
  await assert.rejects(f.owners.beta.publishLobby({...guestCss,stage:32}),
    /Only the host may own the stage selection/);
  await f.owners.alpha.publishLobby({...css,choice:{character:8,costume:1}});
  await f.ends.beta.drainInbound();
  assert.equal(f.lobby.beta.at(-1).revision,2);
  assert.equal(f.lobby.beta.at(-1).state.choice.costume,1);
  await Promise.all(Object.values(f.owners).map(o=>o.negotiate()));
  await assert.rejects(f.owners.alpha.publishLobby(css),/frozen local choice/);
  await Promise.all(Object.values(f.owners).map(o=>o.close()));
});

test('owned character choices are one exact character and costume pair',()=>{
  assert.deepEqual(validateStagingOwnedChoice({character:8,costume:1}),
    {character:8,costume:1});
  for(const value of [null,{}, {character:8}, {character:8,costume:1,stage:32},
    {character:-1,costume:0}, {character:8,costume:256}, {character:8.5,costume:0}])
    assert.throws(()=>validateStagingOwnedChoice(value),/Invalid owned character choice/);
});

test('only alpha may own a stage choice; beta must send no stage',()=>{
  assert.equal(validateStagingOwnedStage('alpha',32),32);
  assert.equal(validateStagingOwnedStage('alpha',null),null);
  assert.equal(validateStagingOwnedStage('beta',null),null);
  assert.throws(()=>validateStagingOwnedStage('beta',32),/Only the host may own/);
  assert.throws(()=>validateStagingOwnedStage('alpha',256),/Invalid host stage choice/);
  assert.throws(()=>validateStagingOwnedStage('gamma',null),/Unknown room role/);
});

test('lobby updates reject malformed, replayed and unknown-version packets',async()=>{
  for(const [role,value] of [['alpha',{phase:'css',choice:{character:8,costume:0},stage:null,extra:true}],
    ['alpha',{phase:'playing',choice:{character:8,costume:0},stage:null}],
    ['beta',{phase:'css',choice:{character:8,costume:0},stage:32}],
    ['beta',{phase:'sss',choice:{character:8,costume:0},stage:Infinity}]])
    assert.throws(()=>validateStagingLobbyState(value,role),/Invalid shared lobby state|Only the host may own/);
  const f=peers();
  await f.owners.alpha.publishLobby({phase:'css',choice:{character:8,costume:0},stage:null});
  await f.ends.beta.drainInbound();
  assert.throws(()=>f.ends.beta.options.onMessage(f.traffic[0].text),/Invalid shared lobby revision/);
  await Promise.all(Object.values(f.owners).map(o=>o.close()));
  const old=peers();
  assert.throws(()=>old.ends.alpha.options.onMessage(JSON.stringify({
    schema:'melee-web-staging-match-session-v1'})),/versions differ.*Reload both/);
  await Promise.all(Object.values(old.owners).map(o=>o.close()));
});

test('stale choice/stage revisions and guest stage packets cannot advance setup',async()=>{
  const stale=peers({choiceNever:true});
  const choicePacket={schema:'melee-web-staging-match-session-v3',role:'beta',channel:'setup',
    stage:'choice',payload:JSON.stringify({schema:'melee-web-staging-choice-v1',revision:0,
      provenance:'native-closed-css-p1-v1',character:8,costume:1})};
  assert.throws(()=>stale.ends.alpha.options.onMessage(JSON.stringify(choicePacket)),
    /Invalid frozen character choice/);
  await Promise.all(Object.values(stale.owners).map(owner=>owner.close()));

  const wrongOwner=peers({choiceNever:true});
  const guestStage={schema:'melee-web-staging-match-session-v3',role:'beta',channel:'setup',
    stage:'stage',payload:JSON.stringify({schema:'melee-web-staging-host-stage-v1',revision:1,stage:32})};
  assert.throws(()=>wrongOwner.ends.alpha.options.onMessage(JSON.stringify(guestStage)),
    /Duplicate or out-of-order setup packet/);
  await Promise.all(Object.values(wrongOwner.owners).map(owner=>owner.close()));

  const staleHostStage=peers({choiceNever:true});
  const hostChoice={schema:'melee-web-staging-match-session-v3',role:'alpha',channel:'setup',
    stage:'choice',payload:JSON.stringify({schema:'melee-web-staging-choice-v1',revision:1,
      provenance:'native-closed-css-p1-v1',character:8,costume:0})};
  staleHostStage.ends.beta.options.onMessage(JSON.stringify(hostChoice));
  const stagePacket={schema:'melee-web-staging-match-session-v3',role:'alpha',channel:'setup',
    stage:'stage',payload:JSON.stringify({schema:'melee-web-staging-host-stage-v1',revision:2,stage:32})};
  assert.throws(()=>staleHostStage.ends.beta.options.onMessage(JSON.stringify(stagePacket)),
    /Invalid host stage choice/);
  await Promise.all(Object.values(staleHostStage.owners).map(owner=>owner.close()));
});

test('a peer can keep publishing its own choice while the other is already locked',async()=>{
  const f=peers({choiceNever:'beta'});
  await f.owners.alpha.publishLobby({phase:'css',choice:{character:8,costume:0},stage:null});
  const pending=Promise.allSettled(Object.values(f.owners).map(owner=>owner.negotiate()));
  const deadline=performance.now()+1000;
  try{
    while(!f.traffic.some(({role,text})=>role==='alpha'&&JSON.parse(text).stage==='choice')){
      assert(performance.now()<deadline,'host choice never reached the relay');
      await new Promise(resolve=>setTimeout(resolve,0));
    }
    assert(f.owners.beta.publishLobby,
      'the guest must retain lobby publication while its own source choice is pending');
    await f.owners.beta.publishLobby({phase:'css',choice:null,stage:null});
    await f.owners.beta.publishLobby({phase:'css',choice:{character:8,costume:1},stage:null});
    await f.ends.alpha.drainInbound();
    assert(f.lobby.alpha.some(entry=>entry.role==='beta'&&entry.state.choice===null),
      'unselected source progress must not be treated as a failed or frozen choice');
    assert(f.lobby.alpha.some(entry=>entry.role==='beta'&&entry.state.choice?.costume===1));
    assert.equal(f.calls.beta.includes('selectChoices'),false,
      'a received host choice must not freeze the guest before its own native CSS confirmation');
  }finally{
    await Promise.all(Object.values(f.owners).map(owner=>owner.close()));
    await pending;
  }
});

test('page peers agree exact native context with host source seed and preserve startup PAD comparison',async()=>{
  const f=peers();
  const results=await Promise.all([f.owners.alpha.negotiate(),f.owners.beta.negotiate()]);
  assert.equal(results[0].seed,17); assert.equal(results[1].seed,17);
  assert.equal(results[0].agreement,results[1].agreement);
  assert.equal(results[0].heldProof,results[1].heldProof);
  assert.deepEqual(results[0].choices,{alpha:{character:8,costume:0},beta:{character:8,costume:1}});
  assert.equal(results[0].hostStage,32);
  assert.deepEqual(results.map(row=>row.localPort),[0,1]);
  assert.deepEqual(f.calls.alpha.map(row=>Array.isArray(row)?row[0]:row),
    ['choice','selectChoices','hostStage','offer','agreement','commit','held']);
  assert.deepEqual(f.calls.beta.map(row=>Array.isArray(row)?row[0]:row),
    ['choice','selectChoices','applyStage','offer','agreement','commit','held']);
  assert.equal(f.traffic.length,10);
  const setupPackets=f.traffic.filter(row=>JSON.parse(row.text).channel==='setup')
    .map(row=>[row.role,JSON.parse(row.text).stage]);
  assert.deepEqual(setupPackets.slice(0,4),[
    ['alpha','choice'],['beta','choice'],['alpha','stage'],['beta','stage_ack']]);
  for(const role of ['alpha','beta'])
    assert.deepEqual(setupPackets.filter(row=>row[0]===role).map(row=>row[1]),
      ['choice',...(role==='alpha'?['stage']:['stage_ack']),'offer','agreement','held']);
  await Promise.all(Object.values(f.owners).map(owner=>owner.close()));
});

test('selection or native candidate mismatch cannot commit either match',async()=>{
  for (const options of [{offerChange:value=>{value.start.players[1].color=0;}},{agreementChange:true}]) {
    const f=peers(options);
    const results=await Promise.allSettled(Object.values(f.owners).map(owner=>owner.negotiate()));
    assert(results.every(row=>row.status==='rejected'));
    assert(Object.values(f.calls).every(rows=>!rows.some(row=>row[0]==='commit')));
    await new Promise(resolve=>setTimeout(resolve,0));
    assert(Object.values(f.ends).every(endpoint=>endpoint.closed),
      'setup refusal must close both owned endpoints without a caller cleanup');
    await Promise.all(Object.values(f.owners).map(owner=>owner.close()));
  }
});

test('malformed setup packet closes its endpoint without waiting for native selection',async()=>{
  const f=peers({heldNever:true});
  assert.throws(()=>f.ends.alpha.options.onMessage(JSON.stringify({bad:'packet'})),
    /Unexpected room session packet/);
  await new Promise(resolve=>setTimeout(resolve,0));
  assert.equal(f.ends.alpha.closed,true);
  await f.owners.alpha.close();
  await f.owners.beta.close();
});

test('different actual startup input refuses takeover, not normalized away',async()=>{
  const f=peers({prefixChange:true});
  const results=await Promise.allSettled(Object.values(f.owners).map(owner=>owner.negotiate()));
  assert(results.every(row=>row.status==='rejected' && /startup inputs differ/.test(row.reason.message)));
  assert.throws(()=>f.owners.alpha.createMatchEndpoint({}),/startup inputs differ/);
  await Promise.all(Object.values(f.owners).map(owner=>owner.close()));
});

test('only local poll and submission identities are omitted from the held comparison',async()=>{
  assert.equal(await stagingHeldIdentity(held()),await stagingHeldIdentity(held({poll:100,submission:500})));
  assert.notEqual(await stagingHeldIdentity(held()),await stagingHeldIdentity(held({pad:'01'.repeat(44)})));
});

test('same socket hands off once and preserves early match traffic plus normal close drain',async()=>{
  const f=peers();
  await Promise.all(Object.values(f.owners).map(owner=>owner.negotiate()));
  const seen=[], options=role=>({role,url:'wss://example.test',roomId:'test-room',
    onMessage:text=>seen.push({role,text}),onDisconnect(){},onEndpointError(){}});
  const a=f.owners.alpha.createMatchEndpoint(options('alpha'));
  await a.send('actual-match-hello');
  await f.ends.beta.drainInbound();
  const b=f.owners.beta.createMatchEndpoint(options('beta'));
  await b.drainInbound();
  assert.deepEqual(seen,[{role:'beta',text:'actual-match-hello'}]);
  assert.throws(()=>f.owners.alpha.createMatchEndpoint(options('alpha')),/takeover refused/);
  await Promise.all([a.close(),b.close()]);
  await Promise.all([a.drainInbound(),b.drainInbound()]);
  assert(a.closed && b.closed);
});

test('attached setup delegates close but ignores only explicit matched-final disposition',async()=>{
  for(const expected of [true,false]){
    const f=peers();
    await Promise.all(Object.values(f.owners).map(owner=>owner.negotiate()));
    let forwarded=0;
    f.owners.alpha.createMatchEndpoint({role:'alpha',url:'wss://example.test',roomId:'test-room',
      onMessage(){},onEndpointError(){},onDisconnect(role,reason){
        ++forwarded;
        assert.equal(role,'alpha');
        assert.equal(reason,'4001: peer disconnected');
        return expected?'expected-final-disconnect':undefined;
      }});
    await f.ends.alpha.options.onDisconnect('alpha','4001: peer disconnected');
    assert.equal(forwarded,1);
    if(expected)assert.equal(f.owners.alpha.failed,null);
    else assert.match(f.owners.alpha.failed.message,/Room disconnected: 4001: peer disconnected/);
    await Promise.all(Object.values(f.owners).map(owner=>owner.close()));
  }
});

test('local close while waiting for a frozen choice cancels setup without a later takeover',async()=>{
  const f=peers({choiceNever:true});
  const pending=Object.values(f.owners).map(owner=>owner.negotiate());
  while (!Object.values(f.calls).every(rows=>rows.includes('choice'))) await new Promise(resolve=>setTimeout(resolve,0));
  await Promise.all(Object.values(f.owners).map(owner=>owner.close()));
  const result=await Promise.allSettled(pending);
  assert(result.every(row=>row.status==='rejected' && /Room closed/.test(row.reason.message)));
  assert.deepEqual(f.failures,[],'an intentional local close is not a setup failure');
});

test('local close during host-stage wait aborts both peers without reporting a real failure',async()=>{
  const f=peers({hostStageNever:true});
  const pending=Object.values(f.owners).map(owner=>owner.negotiate());
  while(!f.calls.alpha.includes('hostStage')||!f.calls.beta.includes('selectChoices'))
    await new Promise(resolve=>setTimeout(resolve,0));
  await Promise.all(Object.values(f.owners).map(owner=>owner.close()));
  const result=await Promise.allSettled(pending);
  assert(result.every(row=>row.status==='rejected'&&/Room closed/.test(row.reason.message)));
  assert.deepEqual(f.failures,[],'an intentional local close is not a transport/native failure');
  assert(Object.values(f.ends).every(endpoint=>endpoint.closed));
});

test('actual room endpoint drains a server disconnect without waiting on its own callback',async()=>{
  const sockets=[], server=createServer();
  let joined;
  const bothJoined=new Promise(resolve=>{joined=resolve;});
  server.on('upgrade',(request,socket)=>{
    const accept=createHash('sha1').update(request.headers['sec-websocket-key']+
      '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
    socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n'+
      'Connection: Upgrade\r\nSec-WebSocket-Accept: '+accept+'\r\n\r\n');
    socket.on('error',()=>{});
    sockets.push(socket);
    if(sockets.length===2){
      const body=Buffer.from(JSON.stringify({relay:1,event:'ready'}));
      for(const peer of sockets)peer.write(Buffer.concat([Buffer.from([0x81,body.length]),body]));
      joined();
    }
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const owners=['alpha','beta'].map(role=>createStagingMatchStart({role,
    url:`ws://127.0.0.1:${server.address().port}`,roomId:'actual-endpoint-control-room',identity,
    timeoutMs:1000,native:{choice:()=>new Promise(()=>{}),selectChoices(){},hostStage(){},applyStage(){},
      offer:()=>new Promise(()=>{}),agreement(){},commit(){},held(){}}}));
  const pending=owners.map(owner=>owner.negotiate());
  try{
    await bothJoined;
    // Real WebSocket close frames, after real upgrade/room readiness. No
    // replacement of endpoint close(), its inbound chain or drain behavior.
    const start=performance.now();
    for(const socket of sockets)socket.end(Buffer.from([0x88,0x02,0x03,0xe8]));
    const results=await Promise.allSettled(pending);
    assert(results.every(row=>row.status==='rejected' && /Room disconnected/.test(row.reason.message)));
    await Promise.all(owners.map(owner=>owner.close()));
    assert(performance.now()-start<500,'disconnect cleanup waited for the endpoint timeout');
  }finally{
    await Promise.allSettled(owners.map(owner=>owner.close()));
    for(const socket of sockets)socket.destroy();
    await new Promise(resolve=>server.close(resolve));
  }
});
