// Fresh component fixture only. No source IO, uploaded asset, game route or
// restore is admitted here. The live owner is created by the actual factory.
#include "lib/gfx/texture.hpp"
#include "lib/gfx/frame_effect_observer.hpp"
#include "lib/webgpu/gpu.hpp"
#include <emscripten/emscripten.h>
#include <emscripten/em_js.h>
#include <webgpu/webgpu.h>
namespace {
aurora::gfx::TextureHandle target;
EM_JS(uintptr_t, import_fixture_device,(), {
 if(Module['__meleeWebTextureReadonlyFixtureV1']!==true || !Module['preinitializedWebGPUDevice'])
   throw new Error('Missing real fixture device');
 return WebGPU.importJsDevice(Module['preinitializedWebGPUDevice']);
});
EM_JS(void, expose_fixture_objects,(uintptr_t t,uintptr_t v,uintptr_t d,uintptr_t q), {
 const keys=Object.freeze([t>>>0,v>>>0,d>>>0,q>>>0]);
 const values=keys.map(k=>WebGPU.Internals.jsObjects[k]);
 Module['__meleeWebTextureReadonlyFixtureObjectsV1']=Object.freeze({
   texture:values[0],view:values[1],device:values[2],queue:values[3],keys,
   importCommandBuffer(value) {return WebGPU.importJsCommandBuffer(value);},
   importCommandEncoder(value) {return WebGPU.importJsCommandEncoder(value);},
   mutate(kind) {
     const table=WebGPU.Internals.jsObjects;
     if(kind==='set')table[keys[0]]=values[0];
     else if(kind==='define')Object.defineProperty(table,keys[0],{value:values[0]});
     else if(kind==='delete')delete table[keys[0]];
     else if(kind==='truncate')table.length=keys[0];
     else if(kind==='direct-table-alias')table[table.length+1]=values[0];
     // The real importer may allocate a native wrapper BEFORE the trap.
     // A refusal is terminal; never run native cleanup afterwards.
     else if(kind==='import-alias')WebGPU.importJsTexture(values[0]);
     else if(kind==='replace-table')WebGPU.Internals.jsObjects=[];
     else if(kind==='replace-internals')WebGPU.Internals={};
     else if(kind==='define-table')Object.defineProperty(WebGPU.Internals,'jsObjects',{value:[]});
     else throw new Error('Unknown fixture negative');
   }
 });
});
}
extern "C" {
EMSCRIPTEN_KEEPALIVE int texture_guard_fixture_init() {
 if(target || aurora::webgpu::g_device)return 0;
 aurora::webgpu::g_device=wgpu::Device::Acquire(reinterpret_cast<WGPUDevice>(import_fixture_device()));
 aurora::webgpu::g_queue=aurora::webgpu::g_device.GetQueue();
 aurora::gfx::detail::browser_frame_effect_observer().begin_persistent_attachment_lifetime(
   reinterpret_cast<uintptr_t>(aurora::webgpu::g_device.Get()));
 target=aurora::gfx::new_dynamic_texture_2d(4,4,1,GX_TF_RGBA8_PC,"texture guard real fixture");
 expose_fixture_objects(reinterpret_cast<uintptr_t>(target->texture.Get()),reinterpret_cast<uintptr_t>(target->sampleTextureView.Get()),
   reinterpret_cast<uintptr_t>(aurora::webgpu::g_device.Get()),reinterpret_cast<uintptr_t>(aurora::webgpu::g_queue.Get()));
 return 1;
}
EMSCRIPTEN_KEEPALIVE int texture_guard_fixture_issue() { return target ? aurora::gfx::issue_texture_readonly_guard(*target) : 0; }
EMSCRIPTEN_KEEPALIVE void texture_guard_fixture_detach() { target.reset(); }
// Deliberately consume the sole texture wrapper reference. This destructive
// negative does not run TextureRef detach first. After trap NO native cleanup,
// resume or restore is legal; discard the browser/module.
EMSCRIPTEN_KEEPALIVE void texture_guard_fixture_last_release() {
 auto raw=target->texture.MoveToCHandle(); wgpuTextureRelease(raw);
}
EMSCRIPTEN_KEEPALIVE void texture_guard_fixture_cleanup() {
 target.reset();
 aurora::gfx::detail::browser_frame_effect_observer().end_persistent_attachment_lifetime(
   reinterpret_cast<uintptr_t>(aurora::webgpu::g_device.Get()));
 aurora::webgpu::g_queue=nullptr; aurora::webgpu::g_device=nullptr;
}
}
int main(){return 0;}
