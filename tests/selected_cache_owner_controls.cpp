// Actual production bodies are inserted by the permanent unittest each run.
// Synthetic raw GPU handles/releases and host bridge, no live GPU claim.
#include "lib/gx/texture.hpp"
#include "lib/gfx/frame_effect_observer.hpp"
#include "lib/gfx/frame_pending_facts.hpp"
#include <string>
#include <absl/container/flat_hash_map.h>
#include <cstdio>
#include <cstdlib>
#include <memory>
static unsigned bridgeCalls=0,detachCalls=0,releases=0;
static int bridgeResult=1;
static uintptr_t expectedOwner=0;
static bool requireDetachedRelease=false,releaseBeforeDetach=false;
extern "C" void wgpuTextureRelease(WGPUTexture){++releases;if(requireDetachedRelease&&!detachCalls)releaseBeforeDetach=true;}
extern "C" void wgpuTextureViewRelease(WGPUTextureView){++releases;if(requireDetachedRelease&&!detachCalls)releaseBeforeDetach=true;}
extern "C" void wgpuQueueRelease(WGPUQueue){}
namespace aurora::gfx::detail {
extern "C" int issue_texture_readonly_guard_host(uintptr_t owner,uintptr_t texture,uintptr_t view,uintptr_t device,uintptr_t queue,
 uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t){
 if(owner!=expectedOwner||texture!=4||view!=8||device!=12||queue!=16)return 0;
 ++bridgeCalls;return bridgeResult;
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
// Synthetic ready native context for the verbatim production issue body below.
struct TestObservation {bool selected_texture_issue_attempted=false,selected_texture_issued=false;std::string error;};
static TestObservation observation;
static void refuse(const char* reason){observation.error=reason;}
static const char* copy_ready_first_draw_component_facts(aurora::gfx::detail::NativeSubmissionPendingFacts& facts){
 using namespace aurora::gfx::detail;
 // Synthetic copied POD ready facts only; the actual project readiness predicate runs below.
 // No real FramePacket/GPU wrapper ownership is required for this issue-stack control.
 facts.packets.schemaVersion=1;facts.packets.activeSlotCount=FrameSlotCount;
 for(size_t i=0;i<FrameSlotCount;++i)facts.packets.slots[i].slot=static_cast<uint32_t>(i);
 facts.schemaVersion=1;facts.ownerBookPresent=1;
 facts.before.generation=1;facts.before.queueHandle=16;facts.before.lastBeginFrameId=181;facts.before.lastEndFrameId=181;
 facts.before.frameIndex=180;facts.before.activeFrameCount=aurora::gfx::detail::FrameSlotCount;
 facts.before.activeStagingCount=aurora::gfx::detail::StagingBufferCount;facts.before.freeFrameCount=aurora::gfx::detail::FrameSlotCount;
 facts.before.freeStagingCount=aurora::gfx::detail::StagingBufferCount;facts.before.workerIdle=1;facts.after=facts.before;
 return nullptr;
}
// ACTUAL_PROJECT_ISSUER
#define REQUIRE(x) do{if(!(x)){std::fprintf(stderr,"cache owner control line %d: %s\n",__LINE__,#x);return 1;}}while(false)
int main(int argc,char** argv){
 REQUIRE(argc==2);const unsigned mode=unsigned(std::strtoul(argv[1],nullptr,10));REQUIRE(mode<32);
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
 if(mode==27)gx::g_gxState.textures[0].texObj.texObjId=0;
 if(mode==23)gx::g_gxState.textures[0].texObj.set_no_cache(true);
 if(mode==24)gx::s_pendingInvalidations.store(1);
 gx::texture::observe_selected_cached_texture(0,gx::g_gxState.textures[0],180,181);
 BrowserFrameEffectObserver::SelectedSampledTextureCacheKey key{};
 REQUIRE(o.selected_sampled_texture_cache_key(key)==(mode!=1&&mode!=21&&mode!=23&&mode!=24&&mode!=27));
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
 if(mode==25||mode==30)bridgeResult=0;
 if(mode==26){++gx::s_textureObjectCaches[31].texDataVersion;++gx::s_textureObjectCaches[31].tlutObjId;}
 if(mode==28)gx::s_textureObjectCaches.erase(31);
 if(mode==29)gx::s_textureObjectCaches[31].handle=replacement;
 const auto useCount=first?first.use_count():0;
 if(mode>=28){
  const int result=melee_web_native_menu_frame_effect_issue_selected_texture();
  const char* expected=mode==28 ? "selected_texture_cache_entry_absent" : mode==29 ? "selected_texture_current_owner" : mode==30 ? "selected_texture_host_bridge_refused" : "";
  REQUIRE(result==(mode==31?1:0));REQUIRE(observation.error==expected);
  REQUIRE(observation.selected_texture_issue_attempted&&observation.selected_texture_issued==(mode==31));
  REQUIRE(bridgeCalls==((mode==30||mode==31)?1u:0u));REQUIRE(first.use_count()==useCount);
  std::printf("actual project issue case %u error=%s passed; synthetic ready context/GPU endpoints\n",mode,expected);
  return 0;
 }
 const bool success=mode==0||mode==15||mode==16||mode==17||mode==18;
 using Reason=gx::SelectedTextureIssueRefusal;
 Reason reason=Reason::None,expectedReason=Reason::None;
 switch(mode){
 case 1:expectedReason=Reason::CaptureEntryAbsent;break;
 case 2:expectedReason=Reason::CacheEntryAbsent;break;
 case 3:expectedReason=Reason::CurrentOwner;break;
 case 4:case 26:expectedReason=Reason::CacheDataVersion;break;
 case 5:expectedReason=Reason::CacheTlutOwner;break;
 case 6:expectedReason=Reason::CacheTlutVersion;break;
 case 7:expectedReason=Reason::CacheReplacement;break;
 case 8:expectedReason=Reason::CacheGeneration;break;
 case 9:expectedReason=Reason::CachePendingInvalidation;break;
 case 10:expectedReason=Reason::CachePendingClear;break;
 case 11:expectedReason=Reason::CurrentTextureSerial;break;
 case 12:case 13:case 14:case 19:case 20:expectedReason=Reason::SubmittedWitnessMissing;break;
 case 21:expectedReason=Reason::CaptureDataVersion;break;
 case 22:expectedReason=Reason::CurrentQueue;break;
 case 23:expectedReason=Reason::CaptureNoCache;break;
 case 24:expectedReason=Reason::CapturePendingInvalidation;break;
 case 25:expectedReason=Reason::HostBridgeRefused;break;
 case 27:expectedReason=Reason::CaptureMissingId;break;
 }
 // Mode18 now specifically preserves the existing zero-argument API caller.
 const auto result=mode==18 ? gx::issue_selected_sampled_texture_readonly_guard() : gx::issue_selected_sampled_texture_readonly_guard(reason);
 REQUIRE(reason==expectedReason);
 REQUIRE(result==(success?1:0));REQUIRE(bridgeCalls==((success||mode==25)?1u:0u));
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
 REQUIRE(reason==expectedReason); // Read-only current getter never alters the issue-stack code.
 REQUIRE(gx::s_stats.objectHits==71&&gx::s_stats.uploadBytes==79&&gx::s_stats.hashedBytes==83);
 requireDetachedRelease=false;
 std::printf("actual cache-owner case %u passed; synthetic GPU endpoints, no restore admission\n",mode);
 return 0;
}
