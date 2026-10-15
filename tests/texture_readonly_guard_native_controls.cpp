// Actual observer API; fake raw identities are inputs to diagnostic validation,
// never claimed to be GPU objects. Host issue stub records the authentic facts.
#include "lib/gfx/frame_effect_observer.hpp"
#include <cstdio>
#include <memory>
static unsigned issued=0,detached=0,epochs=0;
namespace aurora::gfx::detail {
extern "C" int issue_texture_readonly_guard_host(uintptr_t owner,uintptr_t texture,uintptr_t view,uintptr_t device,uintptr_t queue,
 uint32_t sl,uint32_t sh,uint32_t vl,uint32_t vh,uint32_t el,uint32_t eh) {
 if(owner!=91 || texture!=4 || view!=8 || device!=12 || queue!=16 || !sl || sh || !vl || vh || !el || eh)return 0;
 ++issued;return 1;
}
extern "C" void detach_texture_readonly_guard_host(uintptr_t){++detached;}
extern "C" void texture_readonly_guard_device_epoch_change(){++epochs;}
}
#define REQUIRE(x) do{if(!(x)){std::fprintf(stderr,"guard native control line %d\n",__LINE__);return 1;}}while(false)
int main(){
 using namespace aurora::gfx::detail;
 auto o=std::make_unique<BrowserFrameEffectObserver>();o->begin_persistent_attachment_lifetime(12);
 wgpu::TextureDescriptor td{.usage=wgpu::TextureUsage::CopyDst|wgpu::TextureUsage::TextureBinding,.dimension=wgpu::TextureDimension::e2D,.size={4,4,1},.format=wgpu::TextureFormat::RGBA8Unorm,.mipLevelCount=1,.sampleCount=1};
 auto t=o->observe_owned_texture_creation(o->TextureRefTextureRole,0,4,12,td);
 auto pair=o->observe_owned_texture_view_creation(t,8,4,12,nullptr);
 REQUIRE(!o->issue_texture_readonly_guard(pair,91,4,8,16)); // unbound owner
 o->bind_owned_texture_ref(91,pair,4,8,0);
 REQUIRE(o->issue_texture_readonly_guard(pair,91,4,8,16));REQUIRE(issued==1);
 REQUIRE(!o->issue_texture_readonly_guard(pair,92,4,8,16));
 REQUIRE(!o->issue_texture_readonly_guard(pair,91,5,8,16));
 REQUIRE(!o->issue_texture_readonly_guard(pair,91,4,9,16));
 REQUIRE(!o->issue_texture_readonly_guard(pair,91,4,8,0));REQUIRE(issued==1);
 o->detach_owned_texture_ref(91,pair);REQUIRE(detached==1);
 o->end_persistent_attachment_lifetime(12);REQUIRE(!o->issue_texture_readonly_guard(pair,91,4,8,16));
 o->begin_persistent_attachment_lifetime(12);REQUIRE(!o->issue_texture_readonly_guard(pair,91,4,8,16));
 REQUIRE(epochs==3);return 0;
}
