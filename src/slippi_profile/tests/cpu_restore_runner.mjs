// Private single-interval CPU restore runner. No owner census or Node rewind.
import crypto from 'node:crypto';
export const CPU_EVIDENCE_BYTES = 11416;
const key = row => JSON.stringify([row.producer_target,row.source,row.input_object,row.input_section]);
const policies = new Map([
  ['ORIGINAL_SOURCE_CPU_VALUE_BUNDLE_RESTORE',[1,1]],
  ['INCLUDED_ORIGINAL_MPLIB_CPU_VALUE_BUNDLE_RESTORE',[1,2]],
  ['AUTHORED_COLLISION_TABLE_EQUALITY_GUARD',[2,2]],
  ['SOURCE_SELECTED_HEAP_EQUALITY_GUARD_NO_REWIND',[2,1]],
  ['PATCHED_SCOPE_OR_BASELINE_EQUALITY_GUARD_NO_REWIND',[2,1]],
  ['PROJECT_COLLISION_OWNER_IDENTITY_EQUALITY_GUARD',[2,2]],
  ['TYPED_CLOCK_WRITER_EXCLUDE_FROM_BYTE_BUNDLE',null],
  ['TYPED_CADENCE_WRITER_EXCLUDE_FROM_BYTE_BUNDLE',null],
]);
export function validateCpuRestoreConfiguration(config, policy, manifest, identities) {
  if(config?.schema!=='aggregate-cpu-final-linked-configuration-v1'||
     policy?.schema!=='aggregate-source-static-review-contract-v2'||
     !Array.isArray(policy.rows)||!Array.isArray(config.rows))throw new Error('CPU configuration schema');
  for(const field of ['wasmSha256','mapSha256','manifestSha256','policySha256'])
    if(!/^[a-f0-9]{64}$/.test(config[field]||'')||config[field]!==identities[field])throw new Error(`CPU configuration pin: ${field}`);
  const fresh=[...(manifest.mutable_input_sections||[]),...(manifest.excluded_mutable_input_sections||[])];
  const byKey=new Map();for(const row of fresh){const k=key(row);if(byKey.has(k))throw new Error('Duplicate final manifest identity');byKey.set(k,row);}
  const supplied=new Map();for(const row of config.rows){const k=key(row);if(supplied.has(k))throw new Error('Duplicate configuration row');supplied.set(k,row);}
  const approved=new Set(),wire=[];
  for(const row of policy.rows){
    const k=key(row);if(approved.has(k)||!policies.has(row.aggregate_policy))throw new Error('Duplicate/unknown reviewed policy');approved.add(k);
    const current=byKey.get(k),provided=supplied.get(k);
    if(!current||!provided||current.address!==provided.address||current.size!==provided.size||
       current.decision!==provided.decision||current.exclusion_reason!==provided.exclusion_reason||
       provided.aggregate_policy!==row.aggregate_policy||row.size!==current.size)throw new Error(`CPU final row drift: ${k}`);
    const p=policies.get(row.aggregate_policy);if(p)wire.push([current.address,current.size,...p]);
  }
  if(supplied.size!==approved.size)throw new Error('Unreviewed configuration row');
  for(const row of manifest.mutable_input_sections||[])if(row.decision==='source-c-static-candidate'&&!approved.has(key(row)))throw new Error(`Unclassified source static: ${key(row)}`);
  wire.sort((a,b)=>a[0]-b[0]);let end=0;
  for(const row of wire){if(row.some(value=>!Number.isSafeInteger(value)||value<0||value>0xffffffff)||
    row[0]===0||row[1]===0||row[0]<end||row[0]+row[1]>0x100000000)throw new Error('Noncanonical CPU wire range');end=row[0]+row[1];}
  if(!wire.length)throw new Error('Empty CPU wire table');
  return wire;
}
export async function waitForAuroraSubmission(module,sample) {
  const deadline=performance.now()+5000;let status;
  for(;;){
    status=JSON.parse(module.UTF8ToString(module._melee_web_synctest_submission_json()));
    if(status.rendererErrorCount!==0)throw new Error(`Aurora renderer logged native errors at sample ${sample}: ${JSON.stringify(status)}`);
    if(status.pendingFramePackets===0&&status.pendingStagingBuffers===0&&status.workerBusy===0)return status;
    if(performance.now()>=deadline)throw new Error(`Aurora submission did not quiesce for sample ${sample}: ${JSON.stringify(status)}`);
    await new Promise(resolve=>setTimeout(resolve,10));
  }
}
export async function runBrowserCpuRestore(page,wire,checkpoint,expectedInputMapping,expectedStocks,helpers) {
  const plan={wire,checkpointSourceFrame:checkpoint.boundary.sourceFrame,checkpointPad:checkpoint.pad,
    expectedInputMapping,expectedStocks,evidenceBytes:CPU_EVIDENCE_BYTES,...helpers,
    waitSource:waitForAuroraSubmission.toString()};
  try {
    return await page.evaluate(async plan=>{
      const {module}=window.__slippiSynctest;
      const progress={status:'running',commands:[],nativeEvidence:null};window.__slippiSynctest.cpuRestoreProgress=progress;
      if(typeof SharedArrayBuffer!=='undefined'&&module.HEAPU8.buffer instanceof SharedArrayBuffer)throw new Error('Shared Wasm heap refused before direct native reads');
      const read=(pointer,size)=>{
        if(!Number.isSafeInteger(pointer)||pointer<=0||!Number.isSafeInteger(size)||size<=0||pointer+size>module.HEAPU8.length)throw new Error('CPU evidence extent');
        return module.HEAPU8.slice(pointer,pointer+size);
      };
      const encode=bytes=>{let s='';for(let i=0;i<bytes.length;i+=0x6000)s+=String.fromCharCode(...bytes.subarray(i,i+0x6000));return btoa(s);};
      const evidence=()=>{
        const size=module._melee_web_synctest_cpu_restore_evidence_bytes();if(size!==plan.evidenceBytes)throw new Error('CPU evidence ABI');
        const bytes=read(module._melee_web_synctest_cpu_restore_evidence(),size);const view=new DataView(bytes.buffer);
        if(view.getUint32(0,true)!==1||view.getUint32(4,true)!==size)throw new Error('CPU evidence header');
        const value={bytes:size,base64:encode(bytes),state:view.getUint32(8,true),firstFailure:view.getUint32(12,true),
          preFrame:view.getUint32(16,true),postFrame:view.getUint32(20,true),commitResult:view.getUint32(24,true),equality:view.getUint32(28,true),
          checkedDomains:view.getUint32(11368,true),matchedDomains:view.getUint32(11372,true),
          comparison:Object.fromEntries(['domain','status','row','offset','expected','actual','address','extent','detail']
            .map((name,index)=>[name,view.getUint32(11376+index*4,true)]))};
        progress.nativeEvidence=value;return value;
      };
      const command=async number=>{
        const record={command:number,outcome:'pending'};progress.commands.push(record);
        try{record.result=await module.ccall('melee_web_synctest_cpu_restore_command','number',['number'],[number],{async:true});record.outcome='returned';
          evidence();if(record.result!==0)throw new Error(`CPU command ${number} refused ${record.result}: ${module.UTF8ToString(module._melee_web_snapshot_error())}`);
        }catch(error){record.error=String(error);record.outcome=record.outcome==='pending'?'threw':record.result!==0?'returned-nonzero':'evidence-error';throw error;}
      };
      if(module._melee_web_snapshot_observation_size()!==1280||module._melee_web_snapshot_pcm_size()!==4272||
         module._melee_web_synctest_draw_diagnostics_size()!==132)throw new Error('Fixture observation/PCM/draw ABI');
      const observed=read(module._melee_web_snapshot_observation(),1280);
      if(new DataView(observed.buffer).getUint32(0,true)!==plan.checkpointSourceFrame||
         encode(read(module._melee_web_snapshot_input(),44))!==plan.checkpointPad)throw new Error('Actual sample299 live boundary drift');
      const bytes=new Uint8Array(plan.wire.length*16),view=new DataView(bytes.buffer);
      plan.wire.forEach((row,i)=>row.forEach((value,j)=>view.setUint32(i*16+j*4,value,true)));
      const pointer=module._malloc(bytes.length);if(!pointer)throw new Error('CPU wire allocation failed');
      try{
        if(typeof SharedArrayBuffer!=='undefined'&&module.HEAPU8.buffer instanceof SharedArrayBuffer)throw new Error('Shared Wasm memory refused');
        module.HEAPU8.set(bytes,pointer);
        const status=module._melee_web_synctest_cpu_restore_configure(pointer,plan.wire.length);
        progress.configurationResult=status;if(status!==0)throw new Error(`CPU configuration refused ${status}`);
      }finally{module._free(pointer);}
      await command(1);await command(2);
      const current=evidence();if(current.equality!==1||current.commitResult!==0||current.postFrame!==current.preFrame+1||
         current.checkedDomains!==0x3fe||current.matchedDomains!==0x3fe||current.comparison.status!==0)throw new Error('CPU equality contract missing');
      const native=read(module._melee_web_synctest_cpu_restore_evidence(),plan.evidenceBytes);
      const paired=[[32,1312,1280],[2592,6864,4272],[11136,11180,44],[11224,11256,32],[11288,11328,40]];
      for(const [a,b,n]of paired)for(let i=0;i<n;++i)if(native[a+i]!==native[b+i])throw new Error(`CPU paired evidence differs at ${a+i}`);
      if(encode(native.slice(11136,11180))!=='AgAAAAAAAAAAAAABALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=')throw new Error('Literal sample300 PAD drift');
      await command(3);
      const status=await new Function(`return (${plan.waitSource})`)()(module,300);
      const checked=new Function(`return (${plan.checkedSource})`)();await checked(module,'native postdraw quiescence','melee_web_snapshot_quiescent');
      await command(4);
      const observation=read(module._melee_web_snapshot_observation(),1280),pcm=read(module._melee_web_snapshot_pcm(),4272),pad=read(module._melee_web_snapshot_input(),44);
      const diagnostics=read(module._melee_web_synctest_draw_diagnostics(),132);
      const mapping=new Function(`return (${plan.inputMappingSource})`)();
      const inspect=new Function('DRAW_MOTION_BOUNDARY','validateSourceInputMapping',`return (${plan.inspectSource})`)({interactive:0,entry:1,death:2,respawn:3,unknown:4},mapping);
      const boundary=inspect(observation,{classifyMotion:motion=>module._melee_web_synctest_motion_boundary(motion),
        isMarioKind:kind=>module._melee_web_synctest_is_mario_kind(kind)===1,slotCount:new DataView(diagnostics.buffer).getUint32(0,true),
        expectedInputMapping:plan.expectedInputMapping,expectedStocks:plan.expectedStocks});
      if(!boundary.interactive)throw new Error('Final present interactive guard refused');
      progress.status='cpu-restore-replay-and-one-present-component-complete';evidence();
      return {observation:encode(observation),pcm:encode(pcm),pad:encode(pad),drawDiagnostics:encode(diagnostics),boundary,status,cpuRestore:progress};
    },plan);
  }catch(error){
    try{error.cpuRestorePartial=await page.evaluate(()=>window.__slippiSynctest?.cpuRestoreProgress||null);}catch(readError){error.cpuRestorePartialReadError=String(readError);}
    throw error;
  }
}
export const cpuRestoreSha256=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
