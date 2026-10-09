import assert from 'node:assert/strict';
import test from 'node:test';
import vm from 'node:vm';
import {installRuntimeDiagnosticsCapture, readRuntimeDiagnosticsCapture, readRuntimeDiagnosticCounters} from './runtime_callback_recorder.mjs';
import {createCssHumanJoinDriver} from './vs_css_two_human_driver.mjs';

function cssFixture({connected=true,wrongSlot=false,foreignJoin=false}={}) {
  const setup={cursors:Array(16).fill(0),doors:Array(40).fill(0),geometry:Array(48).fill(0)};
  for(let port=0;port<4;port++){
    setup.doors[port*10]=setup.doors[port*10+4]=[0,1,3,3][port];
    setup.doors[port*10+3]=port<2?8:-1;
  }
  if(wrongSlot)setup.doors[6]=1;
  setup.geometry[0]=5;setup.geometry[1]=12;
  setup.geometry[16]=10;setup.geometry[17]=20;
  const inputs=[],report={checks:[],inputConfiguration:{},competitiveMatchStart:{css_roster:[]}};
  const controls={layout:'boxx',playerOneSource:'keyboard',playerTwoSource:'off',playerTwoStatus:'Off'};
  const page={
    locator:selector=>({click:async()=>{},selectOption:async value=>{
      if(selector==='#keyboard-layout')controls.layout=value;
      if(selector==='#player-two-source'){controls.playerTwoSource=value;controls.playerTwoStatus='Keyboard';}
    }}),
    waitForFunction:async()=>{},evaluate:async()=>({...controls}),
    waitForTimeout:async()=>{throw Error('source P2 remains unavailable');},
  };
  const helpers=createCssHumanJoinDriver({page,report,shot:async()=>{},
    observeCssSetup:async()=>structuredClone(setup),resumeTimingPause:async()=>{},ensureNoError:async()=>{},
    sourcePadSample:async(buttons,x,y)=>{
      inputs.push({buttons,x,y});assert.equal(buttons,0);
      assert.deepEqual([x,y],[80,-80]);setup.geometry[0]=15;setup.geometry[1]=-2.2;
    },
    sourcePadTap:async buttons=>{
      inputs.push({buttons});assert.equal(buttons,0x0100);
      const next=setup.doors[10]===1?3:connected?0:1;
      setup.doors[10]=setup.doors[14]=next;
      if(next===0&&foreignJoin)setup.doors[20]=setup.doors[24]=0;
    },
  });
  return {helpers,inputs,report};
}

test('reviewed CSS helper uses source bounds and CPU-empty-Human without changing original slots',async()=>{
  const {helpers,inputs,report}=cssFixture();
  await helpers.configureSecondHuman();
  assert.deepEqual(inputs,[{buttons:0,x:80,y:-80},{buttons:0x100},{buttons:0x100}]);
  assert.deepEqual(report.competitiveMatchStart.css_roster.map(row=>row.door?.p_kind??row.doors?.[1].p_kind),[1,3,0]);
  const final=report.competitiveMatchStart.css_roster.at(-1).door;
  assert.equal(final.slot,0);assert.equal(final.source_port,1);
});

test('CSS helper rejects foreign raw slots before PAD, missing P2 source and unrelated joined doors',async()=>{
  const wrong=cssFixture({wrongSlot:true});
  await assert.rejects(wrong.helpers.configureSecondHuman(),/zero-valued raw slot/);
  assert.deepEqual(wrong.inputs,[]);
  const missing=cssFixture({connected:false});
  await assert.rejects(missing.helpers.configureSecondHuman(),/P2 remains unavailable/);
  assert.equal(missing.report.competitiveMatchStart.css_roster.at(-1).door.p_kind,3);
  const foreign=cssFixture({foreignJoin:true});
  await assert.rejects(foreign.helpers.configureSecondHuman(),/Human.*empty|source roster|doors/i);
});

async function recorderFixture({throwSample=false,throwIncident=false}={}) {
  let clock=0;
  const context=vm.createContext({performance:{now:()=>clock+=51},
    menuDiagnosticSample:()=>{if(throwSample)throw Error('sample primary');return 'sample result';},
    menuDiagnosticIncident:()=>{if(throwIncident)throw Error('incident primary');return 'incident result';}});
  const fakePage={evaluate:async(fn,args)=>vm.runInContext(`(${fn.toString()})(${JSON.stringify(args)})`,context)};
  await installRuntimeDiagnosticsCapture({source:'fixture'},{scope:'mock'},fakePage);
  const sample=(phase=14,steps=2)=>{
    const row=Array(19).fill(0);row[1]=1;row[2]=phase;row[16]=steps;row[18]=1;
    return context.menuDiagnosticSample(...row);
  };
  return {context,sample,page:fakePage,capture:()=>JSON.parse(JSON.stringify(context.__meleeWebRuntimeIncidentCampaignCapture))};
}

test('shared recorder preserves callbacks, bounded rings and every phase-tagged source-step count',async()=>{
  const {context,sample,capture}=await recorderFixture();
  for(let i=0;i<250;i++)assert.equal(sample(),'sample result');
  for(let i=0;i<30;i++)assert.equal(context.menuDiagnosticIncident(7,0,0,1,14,0),'incident result');
  sample(99,1);sample(14,NaN);
  const recorded=capture();
  assert.equal(recorded.callback_count,252);assert.equal(recorded.phase_source_steps[14],500);
  assert.equal(recorded.phase_source_steps.length,16);assert.equal(recorded.invalid_phase_steps,2);
  assert.equal(recorded.samples.length,100);assert.equal(recorded.dropped_samples,152);
  assert.equal(recorded.incidents.length,24);assert.equal(recorded.dropped_incidents,6);
  assert.equal(recorded.reason_counts[7],30);assert.equal(recorded.dropped_reason_counts[7],6);
  assert.equal(recorded.invalid_preparation_count,0);
});

test('original sample and incident exceptions remain primary while finally retains observations',async()=>{
  const {context,sample,capture}=await recorderFixture({throwSample:true,throwIncident:true});
  assert.throws(()=>sample(),/sample primary/);
  assert.throws(()=>context.menuDiagnosticIncident(7,0,0,1,14,0),/incident primary/);
  const recorded=capture();
  assert.equal(recorded.callback_count,1);assert.equal(recorded.phase_source_steps[14],2);
  assert.equal(recorded.reason_counts[7],1);assert.equal(recorded.incidents.length,1);
});


test('counter polling omits retained rings while full snapshot preserves bounded records',async()=>{
  const {page,sample}=await recorderFixture();
  sample();
  const counters=JSON.parse(JSON.stringify(await readRuntimeDiagnosticCounters(page)));
  const snapshot=JSON.parse(JSON.stringify(await readRuntimeDiagnosticsCapture(page)));
  assert.equal(counters.phase_source_steps[14],2);
  assert.equal('samples' in counters,false);assert.equal('incidents' in counters,false);
  assert.equal(snapshot.samples.length,1);assert.equal(snapshot.phase_source_steps[14],2);
  assert.equal(snapshot.status,'installed');
  const emptyPage={evaluate:async fn=>vm.runInNewContext(`(${fn.toString()})()`,{})};
  assert.deepEqual(JSON.parse(JSON.stringify(await readRuntimeDiagnosticCounters(emptyPage))),{status:'unavailable'});
  assert.equal((await readRuntimeDiagnosticsCapture(emptyPage)).status,'unavailable');
});
