import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';
import {MatchInputTimeline} from '../scripts/net_rollback_input_timeline.mjs';

const source=fs.readFileSync(new URL('../scripts/staging_rollback_pair_browser.mjs',import.meta.url),'utf8');
const extract=(start,end)=>{
  const begin=source.indexOf(start),limit=source.indexOf(end,begin);
  assert(begin>=0&&limit>begin);
  return source.slice(begin,limit);
};

function walkFixture({deliver=true}={}){
  const neutral=Array(11).fill(0),left=[0,0,128,...Array(8).fill(0)];
  const alpha=new MatchInputTimeline({localPort:0,remotePort:1,startTick:6,initialRemotePad:neutral});
  const beta=new MatchInputTimeline({localPort:1,remotePort:0,startTick:6,initialRemotePad:neutral});
  const forward=(timeline,tick,pad)=>{
    assert.equal(timeline.recordLocal(tick,pad).status,'accepted');
    const plan=timeline.planForward();
    timeline.commitForward(plan);
    return plan;
  };
  for(let tick=6;tick<15;tick++){
    if(tick<9)beta.acceptRemote(tick,neutral);
    if(tick<12){
      forward(beta,tick,neutral);
      alpha.acceptRemote(tick,neutral);
    }
    forward(alpha,tick,neutral);
  }
  assert.equal(beta.status().remote_contiguous_through,8);
  assert.equal(alpha.status().remote_contiguous_through,11);
  assert.equal(beta.status().simulated_through,11);
  assert.equal(alpha.status().simulated_through,14);

  const initial={frame:15,players:[{stocks:4}]};
  const caps=[], keys=[], timelineEvents=[];
  let now=1000,keyAt=null,sample=736,outcomes=0;
  const item={inputEvents:[],page:{
    locator:()=>({async focus({timeout}){assert.equal(timeout,15000);}}),
    keyboard:{
      async down(key){keyAt=now;keys.push(['down',key]);},
      async up(key){assert(keyAt!==null);keyAt=null;keys.push(['up',key]);},
    },
    evaluate:async (fn,args)=>fn(args),
  }};
  const context={assert,Date:{now:()=>now},performance:{now:()=>now},
    setTimeout(fn,ms){now+=ms;fn();},deadline:50000,hardDeadline:70000,
    MATCH_STATE_TIMEOUT_MS:15000,OBSERVER_BUSY:'busy',
    p1Direction:()=>({key:'a',stickX:-1}),
    readSnapshot:async()=>({input:{last_non_neutral:{sample:489}}}),
    remaining:bound=>bound-now,
    bracketLive:async (_item,_label,action)=>action(),
    withinBound:async (action,bound)=>{
      caps.push(bound);
      const value=await action();
      assert(now<bound);
      return value;
    },
    window:{async onlineSessionSnapshot(){
      assert(keyAt!==null,'the direction remains held until the actual receipt');
      if(deliver&&sample===736&&now-keyAt>=608){
        // The peer's previously retained exact indexed samples unblock its
        // quiet frontier, then its normal source rows unblock this owner.
        for(let tick=9;tick<12;tick++)beta.acceptRemote(tick,neutral);
        assert.equal(beta.status().confirmed_through,11);
        for(let tick=12;tick<15;tick++){
          const plan=forward(beta,tick,neutral);
          const actual=plan.frame.slice(11,22);
          assert.deepEqual(actual,neutral);
          assert.equal(alpha.acceptRemote(tick,actual).status,'accepted');
          timelineEvents.push(tick);
        }
        assert.equal(alpha.status().confirmed_through,14);
        assert.equal(alpha.status().dirty_from,null);
        const resumed=forward(alpha,15,left);
        assert.deepEqual(resumed.frame.slice(0,11),left);
        sample++;
      }else if(sample===736){
        assert.equal(alpha.status().simulated_through,14,'owned wait cannot fabricate local polls');
      }
      return {input:{samples:sample,keyboard_layout:0,keyboard_active_mask:3,last_non_neutral:sample===736?
        {sample:489,port:0,source:'keyboard',err:0,buttons:256,stick:[0,0]}:
        {sample,port:0,source:'keyboard',err:0,buttons:0,stick:[-128,0],cstick:[0,0],triggers:[0,0]}}};
    }},
    waitSnapshot:async (_item,predicate,_label,{source,bound})=>{
      outcomes++;caps.push(bound);
      assert(source&&keyAt!==null&&sample===737);
      const value={match:{frame:40,players:[{stocks:3}]}};
      assert(predicate(value));
      return value;
    },
    stockSample:snapshot=>snapshot.match,
  };
  const walkOff=vm.runInNewContext(extract('async function sourceInputObserved(', 'function newPageOwner(')+
    extract('async function walkOff(', 'async function playFullMatch(')+'\nwalkOff',context);
  return {run:()=>walkOff(item,initial),item,caps,keys,timelineEvents,stats:()=>({now,outcomes,sample})};
}

test('held walk consumes the same action deadline after a legal no-poll wait',async()=>{
  const fixture=walkFixture();
  assert.equal((await fixture.run()).players[0].stocks,3);
  assert.deepEqual(fixture.timelineEvents,[12,13,14]);
  assert.equal(fixture.item.inputEvents[0].receipt.sample,737);
  assert.deepEqual(fixture.caps,[16000,16000,16000,69000]);
  assert.deepEqual(fixture.keys,[['down','a'],['up','a']]);
  assert(fixture.stats().now>1500,'modeled wait exceeds the short-pulse wall budget');
});

test('held walk still requires an actual receipt and releases on failure',async()=>{
  const fixture=walkFixture({deliver:false});
  await assert.rejects(fixture.run(),/lacked an actual PAD receipt/);
  assert.deepEqual(fixture.keys,[['down','a'],['up','a']]);
  assert.deepEqual(fixture.stats(),{now:15980,outcomes:0,sample:736});
  assert.deepEqual(fixture.caps,[16000,16000,69000]);
});
