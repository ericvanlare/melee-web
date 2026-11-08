// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import test from 'node:test';
import {createServer} from 'node:http';
import {createHash} from 'node:crypto';
import {createStagingMatchStart, stagingHeldIdentity} from '../staging_match_start.mjs';

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
function peers({selectionChange, agreementChange, prefixChange, heldNever = false} = {}) {
  const ends = {}, traffic = [], calls = {alpha:[],beta:[]}, failures = [];
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
  for (const role of ['alpha','beta']) {
    const seed=role==='alpha'?'00000011':'00000022';
    const selection={random_seed:seed,players:[{character:8,port:0},{character:8,port:1}],stage:32,stocks:4};
    if (role==='beta') selectionChange?.(selection);
    const native = {
      offer() { calls[role].push('offer'); return JSON.stringify({
        schema:'melee-web-runtime-match-start-offer-v1',...identity,
        local_menu_rng_seed_provenance:seed,selection}); },
      agreement(value) { calls[role].push(['agreement',value]);
        return JSON.stringify({nativeAgreement:{...selection,random_seed:value},
          ...(role==='beta' && agreementChange ? {unexpected:true} : {})}); },
      commit(value,text) { calls[role].push(['commit',value,text]); },
      held() { calls[role].push('held');
        if (heldNever) return new Promise(() => {});
        return held(role==='beta'?{poll:100,submission:200,...(prefixChange?{pad:'01'.repeat(44)}:{})}:{});
      },
    };
    owners[role]=createStagingMatchStart({role,url:'wss://example.test',roomId:'test-room',identity,
      native,createEndpoint,timeoutMs:2000,onFailure:error=>failures.push({role,error})});
  }
  return {owners,ends,traffic,calls,failures};
}

test('page peers agree exact native context with host source seed and preserve startup PAD comparison',async()=>{
  const f=peers();
  const results=await Promise.all([f.owners.alpha.negotiate(),f.owners.beta.negotiate()]);
  assert.equal(results[0].seed,17); assert.equal(results[1].seed,17);
  assert.equal(results[0].agreement,results[1].agreement);
  assert.equal(results[0].heldProof,results[1].heldProof);
  assert.deepEqual(results.map(row=>row.localPort),[0,1]);
  for (const role of ['alpha','beta'])
    assert.deepEqual(f.calls[role].map(row=>Array.isArray(row)?row[0]:row),['offer','agreement','commit','held']);
  assert.equal(f.traffic.length,6);
  await Promise.all(Object.values(f.owners).map(owner=>owner.close()));
});

test('selection or native candidate mismatch cannot commit either match',async()=>{
  for (const options of [{selectionChange:value=>{value.stage=31;}},{agreementChange:true}]) {
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

test('disconnect while waiting for native readiness cancels setup without a later takeover',async()=>{
  const f=peers({heldNever:true});
  const pending=Object.values(f.owners).map(owner=>owner.negotiate());
  while (!Object.values(f.calls).every(rows=>rows.includes('held'))) await new Promise(resolve=>setTimeout(resolve,0));
  await Promise.all(Object.values(f.owners).map(owner=>owner.close()));
  const result=await Promise.allSettled(pending);
  assert(result.every(row=>row.status==='rejected' && /Room closed/.test(row.reason.message)));
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
    timeoutMs:1000,native:{offer:()=>new Promise(()=>{}),agreement(){},commit(){},held(){}}}));
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
