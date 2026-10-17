// Browser-module fixture logic only. A reviewed headless browser_tools owner
// must run each negative in a fresh realm/module and retain GPU diagnostics.
export async function runTextureReadonlyGuardFixtureV1(Module, kind='positive', selectedComponent=false) {
  const check=(x,m)=>{if(!x)throw new Error(m);};
  const expectedCodes=Object.freeze({
    write:'TARGET_writeTexture','method-alias':'TARGET_writeTexture',
    'copy-buffer':'TARGET_copyBufferToTexture','copy-texture':'TARGET_copyTextureToTexture',
    destroy:'TARGET_destroy','device-destroy':'TARGET_destroy',
    detach:'OWNER_DETACH','last-native-release':'NATIVE_FINAL_RELEASE_OR_TABLE_DELETE',
    set:'MAPPING_MUTATION',define:'MAPPING_MUTATION',
    'direct-table-alias':'MAPPING_MUTATION','import-alias':'MAPPING_MUTATION',
    delete:'NATIVE_FINAL_RELEASE_OR_TABLE_DELETE',truncate:'MAPPING_TRUNCATION',
    'replace-table':'TABLE_REPLACEMENT','replace-internals':'TABLE_REPLACEMENT','define-table':'TABLE_REPLACEMENT'
  });
  const componentCodes=Object.freeze({'preissue-buffer':'PREISSUE_COMMAND_BUFFER',
    'preissue-encoder':'PREISSUE_OR_FOREIGN_ENCODER','import-preinstall-buffer':'UNKNOWN_COMMAND_BUFFER',
    'import-preinstall-encoder':'UNKNOWN_ENCODER','close-before-end':'CLOSE_STATE','submit-after-end':'COMPONENT_QUEUE_PHASE'});
  const expectedCode=kind==='positive'?null:(selectedComponent?componentCodes[kind]:expectedCodes[kind]);
  check(kind==='positive' || typeof expectedCode==='string','Known exact negative seam required');
  check(Module._texture_guard_fixture_init()===1,'Actual factory initialization');
  const objects=Module.__meleeWebTextureReadonlyFixtureObjectsV1;
  const {device,queue,texture,view}=objects;
  const guard=Module.__meleeWebTextureReadonlyGuardV1;
  const data=new Uint8Array(64);
  for(let i=0;i<data.length;i+=4)data.set([19,51,103,255],i);
  queue.writeTexture({texture},data,{bytesPerRow:16,rowsPerImage:4},{width:4,height:4});
  await queue.onSubmittedWorkDone();
  let oldEncoder=null,oldBuffer=null;
  if(selectedComponent&&(kind==='preissue-buffer'||kind==='preissue-encoder')){
    const source=device.createBuffer({size:1024,usage:GPUBufferUsage.COPY_SRC,mappedAtCreation:true});
    new Uint8Array(source.getMappedRange()).fill(127);source.unmap();
    oldEncoder=device.createCommandEncoder();
    oldEncoder.copyBufferToTexture({buffer:source,bytesPerRow:256},{texture},{width:4,height:4});
    if(kind==='preissue-buffer')oldBuffer=oldEncoder.finish();
  }
  check(Module._texture_guard_fixture_issue()===1,'Authentic live native capability');
  const cap=guard.capability();
  if(selectedComponent)await guard.armSelectedComponent(cap);else await guard.armFixture(cap);
  const before=guard.status();
  if(kind!=='positive') {
    const encoder=device.createCommandEncoder();
    const other=device.createTexture({size:[4,4],format:'rgba8unorm',usage:GPUTextureUsage.COPY_SRC|GPUTextureUsage.COPY_DST|GPUTextureUsage.TEXTURE_BINDING});
    const src=device.createBuffer({size:1024,usage:GPUBufferUsage.COPY_SRC,mappedAtCreation:true});
    new Uint8Array(src.getMappedRange()).fill(127);src.unmap();
    const commandBefore=guard.status().commandDispatches;
    let caught=false,caughtMessage='';
    try {
      if(selectedComponent){
        if(kind==='preissue-buffer')queue.submit([oldBuffer]);
        else if(kind==='preissue-encoder')oldEncoder.finish();
        else if(kind==='import-preinstall-buffer'){
          const value=Module.__meleeWebTextureReadonlyPreinstallCommandBufferV1;
          check(value,'Actual pre-install command buffer required');objects.importCommandBuffer(value);queue.submit([value]);
        }else if(kind==='import-preinstall-encoder'){
          const value=Module.__meleeWebTextureReadonlyPreinstallCommandEncoderV1;
          check(value,'Actual pre-install encoder required');objects.importCommandEncoder(value);value.finish();
        }else if(kind==='close-before-end')guard.close(cap);
        else if(kind==='submit-after-end'){await guard.endSelectedComponent(cap);queue.submit([]);}
        else throw Error('Unknown selected component negative');
      }else if(kind==='write')queue.writeTexture({texture},data,{bytesPerRow:16},{width:4,height:4});
      else if(kind==='method-alias'){const f=queue.writeTexture;Reflect.apply(f,queue,[{texture},data,{bytesPerRow:16},{width:4,height:4}]);}
      else if(kind==='copy-buffer')encoder.copyBufferToTexture({buffer:src,bytesPerRow:256},{texture},{width:4,height:4});
      else if(kind==='copy-texture')encoder.copyTextureToTexture({texture:other},{texture},{width:4,height:4});
      else if(kind==='destroy')texture.destroy();
      else if(kind==='device-destroy')device.destroy();
      else if(kind==='detach')Module._texture_guard_fixture_detach();
      else if(kind==='last-native-release')Module._texture_guard_fixture_last_release();
      else objects.mutate(kind);
    } catch(e) {caught=true;caughtMessage=String(e);}
    const result=guard.status();
    check(caught && result.phase==='TERMINAL' && result.firstFailure,'Explicit terminal refusal');
    check(result.firstFailure.code===expectedCode && caughtMessage.includes(expectedCode),'Exact intended refusal seam');
    const firstFailure=result.firstFailure;
    const firstFailureBytes=JSON.stringify(firstFailure);
    let closeRefused=false;
    try {guard.close(cap);}catch(e){closeRefused=String(e).includes(expectedCode);}
    const retained=guard.status();
    check(closeRefused && retained.phase==='TERMINAL','Poisoned pair cannot close successfully');
    check(retained.firstFailure===firstFailure && JSON.stringify(retained.firstFailure)===firstFailureBytes,'First failure retained exactly');
    check(retained.dispatches===result.dispatches,'Close refusal must not dispatch GPU work');
    check(result.dispatches===before.dispatches,'No guarded original GPU API dispatched');
    if(selectedComponent)check(result.commandDispatches===commandBefore,'No original command finish/submit dispatched at refusal');
    // No calls into the poisoned native module, GPU cleanup, restore or resume.
    return {scope:'real-GPU-component-negative',kind,expectedCode,result,retained,closeRefused,nativeBookkeepingMayAlreadyBeMutated:kind==='last-native-release'||kind==='import-alias'||kind==='import-preinstall-buffer'||kind==='import-preinstall-encoder'};
  }
  const shader=device.createShaderModule({code:`
    @group(0) @binding(0) var image:texture_2d<f32>;
    @vertex fn vs(@builtin(vertex_index) i:u32)->@builtin(position) vec4f {
      var p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));return vec4f(p[i],0,1);
    }
    @fragment fn fs(@builtin(position) p:vec4f)->@location(0) vec4f {return textureLoad(image,vec2i(p.xy),0);}`});
  const pipeline=device.createRenderPipeline({layout:'auto',vertex:{module:shader,entryPoint:'vs'},fragment:{module:shader,entryPoint:'fs',targets:[{format:'rgba8unorm'}]}});
  const group=device.createBindGroup({layout:pipeline.getBindGroupLayout(0),entries:[{binding:0,resource:view}]});
  const other=device.createTexture({size:[4,4],format:'rgba8unorm',usage:GPUTextureUsage.COPY_DST});
  queue.writeTexture({texture:other},data,{bytesPerRow:16},{width:4,height:4});
  async function leg() {
    const out=device.createTexture({size:[4,4],format:'rgba8unorm',usage:GPUTextureUsage.RENDER_ATTACHMENT|GPUTextureUsage.COPY_SRC});
    const read=device.createBuffer({size:1024,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});
    const encoder=device.createCommandEncoder();
    const pass=encoder.beginRenderPass({colorAttachments:[{view:out.createView(),loadOp:'clear',storeOp:'store',clearValue:[0,0,0,0]}]});
    pass.setPipeline(pipeline);pass.setBindGroup(0,group);pass.draw(3);pass.end();
    encoder.copyTextureToBuffer({texture:out},{buffer:read,bytesPerRow:256},{width:4,height:4});
    queue.submit([encoder.finish()]);await queue.onSubmittedWorkDone();
    await read.mapAsync(GPUMapMode.READ);
    const bytes=new Uint8Array(read.getMappedRange());
    for(let row=0;row<4;++row)for(let x=0;x<16;++x)check(bytes[row*256+x]===data[row*16+x],'Actual sampled output bytes');
    const retained=[];for(let row=0;row<4;++row)retained.push(...bytes.slice(row*256,row*256+16));
    read.unmap();read.destroy();out.destroy();return retained;
  }
  const firstReadback=await leg();
  const hostMarker=Object.freeze({epoch:guard.status().epoch,scope:'host-marker-no-Wasm-restore'});
  const secondReadback=await leg();
  if(selectedComponent)await guard.endSelectedComponent(cap);
  const result=guard.close(cap);
  check(result.phase==='CLOSED' && !result.firstFailure && !result.gameplayAdmission,'Continuous component only');
  Module._texture_guard_fixture_cleanup();other.destroy();device.destroy();
  if(selectedComponent)return {scope:'real-GPU-selected-texture-component-positive',result,hostMarker,legs:2,
    expectedBytes:Array.from(data),readbacks:[firstReadback,secondReadback]};
  return {scope:'real-GPU-component-positive',result,hostMarker,legs:2};
}

export async function runTextureReadonlySelectedIntervalFixtureV2(Module,kind='positive'){
 return runTextureReadonlyGuardFixtureV1(Module,kind,true);
}
