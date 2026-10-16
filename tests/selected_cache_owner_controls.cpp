// Actual production bodies are inserted by the permanent unittest each run.
// Synthetic raw GPU handles/releases and host bridge, no live GPU claim.
#include "lib/gx/texture.hpp"
#include "lib/gfx/frame_effect_observer.hpp"
#include <absl/container/flat_hash_map.h>
#include <cstdio>
#include <cstdlib>
#include <memory>
static unsigned bridgeCalls=0,detachCalls=0,releases=0;
static uintptr_t expectedOwner=0;
static bool requireDetachedRelease=false,releaseBeforeDetach=false;
extern "C" void wgpuTextureRelease(WGPUTexture){++releases;if(requireDetachedRelease&&!detachCalls)releaseBeforeDetach=true;}
extern "C" void wgpuTextureViewRelease(WGPUTextureView){++releases;if(requireDetachedRelease&&!detachCalls)releaseBeforeDetach=true;}
extern "C" void wgpuQueueRelease(WGPUQueue){}
namespace aurora::gfx::detail {
extern "C" int issue_texture_readonly_guard_host(uintptr_t owner,uintptr_t texture,uintptr_t view,uintptr_t device,uintptr_t queue,
 uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t){
 if(owner!=expectedOwner||texture!=4||view!=8||device!=12||queue!=16)return 0;
 ++bridgeCalls;return 1;
}
extern "C" void detach_texture_readonly_guard_host(uintptr_t){++detachCalls;}
extern "C" void texture_readonly_guard_device_epoch_change(){}
}
// ACTUAL_TEXTURE_OWNER_BRIDGES
namespace aurora::webgpu {wgpu::Queue g_queue;}
namespace aurora::gx {
GXState g_gxState;
// ACTUAL_CACHE_ENTRY_AND_GLOBALS
namespace texture {
// ACTUAL_CACHE_OPERATIONS
}
// ACTUAL_GX_OPERATIONS
}
#define REQUIRE(x) do{if(!(x)){std::fprintf(stderr,"cache owner control line %d: %s\n",__LINE__,#x);return 1;}}while(false)
int main(int argc,char** argv){
 REQUIRE(argc==2);const unsigned mode=unsigned(std::strtoul(argv[1],nullptr,10));REQUIRE(mode<25);
 using namespace aurora;
 using namespace aurora::gfx::detail;
 auto& o=browser_frame_effect_observer();o.begin_persistent_attachment_lifetime(12);
 webgpu::g_queue=wgpu::Queue::Acquire(reinterpret_cast<WGPUQueue>(16));
 const wgpu::TextureDescriptor td{.usage=wgpu::TextureUsage::CopyDst|wgpu::TextureUsage::TextureBinding,
  .dimension=wgpu::TextureDimension::e2D,.size={4,4,1},.format=wgpu::TextureFormat::RGBA8Unorm,.mipLevelCount=1,.sampleCount=1};
 auto make=[&](){
  auto t=o.observe_owned_texture_creation(o.TextureRefTextureRole,0,4,12,td);
  auto p=o.observe_owned_texture_view_creation(t,8,4,12,nullptr);
  return std::make_shared<gfx::TextureRef>(wgpu::Texture::Acquire(reinterpret_cast<WGPUTexture>(4)),
    wgpu::TextureView::Acquire(reinterpret_cast<WGPUTextureView>(8)),wgpu::TextureView{},td.size,td.format,1,0,p);
 };
 auto first=make();expectedOwner=reinterpret_cast<uintptr_t>(first.get());
 GXTexObj_ obj{};obj.texObjId=31;obj.texDataVersion=7;
 gx::s_textureObjectCaches[31]={first,7,43,11,59,101};
 gx::s_stats.objectHits=71;gx::s_stats.uploadBytes=79;gx::s_stats.hashedBytes=83;
 gx::g_gxState.textures[0]=gfx::TextureBind(obj,first,1);
 REQUIRE(o.arm_first_draw(180,181));o.observe_draw(180,181,0,0);
 gx::request_selected_sampled_texture();
 if(mode==1)gx::s_textureObjectCaches.erase(31);
 if(mode==21)gx::s_textureObjectCaches[31].texDataVersion=8;
 if(mode==23)gx::g_gxState.textures[0].texObj.set_no_cache(true);
 if(mode==24)gx::s_pendingInvalidations.store(1);
 gx::texture::observe_selected_cached_texture(0,gx::g_gxState.textures[0],180,181);
 BrowserFrameEffectObserver::SelectedSampledTextureCacheKey key{};
 REQUIRE(o.selected_sampled_texture_cache_key(key)==(mode!=1&&mode!=21&&mode!=23&&mode!=24));
 if(mode==1||mode==21)gx::s_textureObjectCaches[31]={first,7,43,11,59,101};
 if(mode==24)gx::s_pendingInvalidations.store(0); // Even later supported state cannot reseat selection.
 // Later first-slot replacement is ordinary native ownership, not reselection.
 auto replacement=make();gx::g_gxState.textures[0]=gfx::TextureBind(obj,replacement,1);
 gx::texture::observe_selected_cached_texture(0,gx::g_gxState.textures[0],180,181);
 if(mode==17){
  auto aliasObj=obj;aliasObj.texObjId=32;gx::s_textureObjectCaches[32]={first,7,43,11,59,101};
  const gfx::TextureBind alias(aliasObj,first,1);
  gx::texture::observe_selected_cached_texture(1,alias,180,181);
  REQUIRE(o.selected_sampled_texture_cache_key(key)&&key.texObjId==31);
 }
 o.observe_submit(181);o.observe_fence(181,true);
 if(mode==2)gx::s_textureObjectCaches.erase(31);
 if(mode==3)gx::s_textureObjectCaches[31].handle=replacement;
 if(mode==4)++gx::s_textureObjectCaches[31].texDataVersion;
 if(mode==5)++gx::s_textureObjectCaches[31].tlutObjId;
 if(mode==6)++gx::s_textureObjectCaches[31].tlutDataVersion;
 if(mode==7)++gx::s_textureObjectCaches[31].replacementId;
 if(mode==8)++gx::s_bindGeneration;
 if(mode==9)gx::s_pendingInvalidations.store(1);
 if(mode==10)gx::s_pendingCacheClears.store(1);
 if(mode==11){
  auto t=o.observe_owned_texture_creation(o.TextureRefTextureRole,0,4,12,td);
  first->frameEffectOwnerFacts=o.observe_owned_texture_view_creation(t,8,4,12,nullptr);
  o.bind_owned_texture_ref(expectedOwner,first->frameEffectOwnerFacts,4,8,0);
 }
 if(mode==12)o.end_persistent_attachment_lifetime(12);
 if(mode==13)gx::request_selected_sampled_texture();
 if(mode==14){
  gx::s_textureObjectCaches.erase(31);requireDetachedRelease=true;first.reset();
  REQUIRE(detachCalls==1&&releases==2&&!releaseBeforeDetach);
  REQUIRE(!o.selected_sampled_texture_cache_key(key));
 }
 if(mode==19)o.clear_selected_sampled_texture();
 if(mode==20){o.end_persistent_attachment_lifetime(12);o.begin_persistent_attachment_lifetime(12);}
 if(mode==22){(void)webgpu::g_queue.MoveToCHandle();webgpu::g_queue=wgpu::Queue::Acquire(reinterpret_cast<WGPUQueue>(17));}
 const auto useCount=first?first.use_count():0;
 const bool success=mode==0||mode==15||mode==16||mode==17||mode==18;
 const auto result=gx::issue_selected_sampled_texture_readonly_guard();
 REQUIRE(result==(success?1:0));REQUIRE(bridgeCalls==(success?1u:0u));
 if(first)REQUIRE(first.use_count()==useCount);
 if(success){
  REQUIRE(gx::selected_sampled_texture_readonly_guard_current());
  REQUIRE(gx::s_textureObjectCaches[31].lastUsedFrame==101);
  if(mode==15)gx::s_textureObjectCaches.erase(31);
  if(mode==16)gx::s_textureObjectCaches[31].handle=replacement;
  REQUIRE(gx::selected_sampled_texture_readonly_guard_current()==(mode!=15&&mode!=16));
  REQUIRE(!gx::issue_selected_sampled_texture_readonly_guard());REQUIRE(bridgeCalls==1);
 }else REQUIRE(!gx::selected_sampled_texture_readonly_guard_current());
 REQUIRE(gx::s_pendingInvalidations.load()==(mode==9?1u:0u));
 REQUIRE(gx::s_pendingCacheClears.load()==(mode==10?1u:0u));
 REQUIRE(gx::s_stats.objectHits==71&&gx::s_stats.uploadBytes==79&&gx::s_stats.hashedBytes==83);
 requireDetachedRelease=false;
 std::printf("actual cache-owner case %u passed; synthetic GPU endpoints, no restore admission\n",mode);
 return 0;
}
