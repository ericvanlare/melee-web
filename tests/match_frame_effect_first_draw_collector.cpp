// Actual private collector control. No WebGPU service or command encoding is
// fabricated: these are diagnostic event/callback association inputs only.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include "lib/gfx/frame_effect_observer.hpp"
#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr,"first-draw collector failure line %d: %s\n",__LINE__,#x);return 1; } } while(false)
using aurora::gfx::detail::BrowserFrameEffectObserver;

static int selected_completion(bool afterFence){
 std::unique_ptr<BrowserFrameEffectObserver> storage(new(std::nothrow) BrowserFrameEffectObserver);
 REQUIRE(storage);auto& o=*storage;
 REQUIRE(o.arm_first_draw(3,100));
 o.observe_command(3,100,0,0,0,false);
 o.observe_submit(100);
 const auto owner=o.observe_depth_map_registration(0,8,uintptr_t{0x1234},3,100);
 REQUIRE(owner==1&&o.depth_owners().pendingCount==1);
 const auto selected=o.snapshot();
 // An unrelated second real draw would deliver these different packet identities
 // before the selected fence. No selected event/owner may be created by them.
 o.observe_command(4,101,0,0,0,false);
 o.observe_depth_request(2,true,false,false,0,1,1,4,101);
 o.observe_depth_state(2,true,false,false,true,0,0,0,1,1,8,0,0,0,1,1,4,101);
 REQUIRE(o.observe_depth_map_registration(1,8,uintptr_t{0x2345},4,101)==0);
 o.observe_depth_map_callback(1,0,1,true,0,1,1,4,101);
 o.observe_submit(101);o.observe_fence(101,true);
 REQUIRE(o.snapshot().eventCount==selected.eventCount);
 REQUIRE(o.snapshot().handleCount==selected.handleCount);
 REQUIRE(o.snapshot().submitCount==1&&o.snapshot().fenceCount==0);
 REQUIRE(o.depth_owners().registeredCount==1&&o.depth_owners().pendingCount==1);
 REQUIRE(o.depth_owners().ignoredOtherIssuerCount==4);
 REQUIRE(!o.finalize_first_draw());
 if(afterFence)o.observe_fence(100,true);
 REQUIRE(!o.finalize_first_draw());
 o.observe_depth_map_callback(0,owner,1,true,0,1,1,3,100);
 REQUIRE(o.depth_owners().completedCount==1&&o.depth_owners().pendingCount==0);
 REQUIRE(o.snapshot().eventCount==selected.eventCount+(afterFence?2:1));
 if(!afterFence)o.observe_fence(100,true);
 REQUIRE(o.snapshot().fenceCount==1&&o.snapshot().fenceSuccess==1);
 REQUIRE(o.finalize_first_draw()&&o.depth_owners().finalized==1);
 o.observe_depth_map_callback(0,owner,1,true,0,1,1,3,100);
 REQUIRE(o.snapshot().firstBlocker==AURORA_BROWSER_FRAME_EFFECT_DEPTH_MAP_OWNER_UNKNOWN);
 REQUIRE(o.depth_owners().completedCount==1&&o.depth_owners().pendingCount==0);
 std::printf("selected completion %s fence retained; later draw excluded; duplicate blocked\n",afterFence?"after":"before");
 return 0;
}
static int unknown_identity(){
 std::unique_ptr<BrowserFrameEffectObserver> storage(new(std::nothrow) BrowserFrameEffectObserver);
 REQUIRE(storage);auto& o=*storage;REQUIRE(o.arm_first_draw(3,100));
 o.observe_command(3,100,0,0,0,false);o.observe_submit(100);
 o.observe_depth_request(2,true,false,false,0,0,0,0,0);
 REQUIRE(o.snapshot().firstBlocker==AURORA_BROWSER_FRAME_EFFECT_DEPTH_MAP_OWNER_UNKNOWN);
 o.observe_depth_map_callback(0,99,1,true,0,1,1,3,100);
 REQUIRE(o.snapshot().firstBlocker==AURORA_BROWSER_FRAME_EFFECT_DEPTH_MAP_OWNER_UNKNOWN);
 REQUIRE(o.depth_owners().registeredCount==0&&o.depth_owners().completedCount==0);
 return 0;
}
int main(){
 if(selected_completion(false)||selected_completion(true)||unknown_identity())return 1;
 std::puts("first-draw actual collector issuing identity and selected map-owner controls passed");
}
