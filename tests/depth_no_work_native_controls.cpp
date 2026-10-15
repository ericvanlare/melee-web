// Actual depth TU getter/service paths, no fake GPU object or enabled renderer claim.
#include "lib/gfx/depth_peek.hpp"
#include "lib/gfx/frame_effect_observer.hpp"
#include <cstdio>
#include <memory>
#include <cstdlib>
namespace aurora { AuroraConfig g_config{};
void log_internal(AuroraLogLevel,const char*,const char*,unsigned int) noexcept { std::abort(); } // unavailable service must fail
}
namespace aurora::webgpu { wgpu::Device g_device; }
static uint32_t events[16]{},eventCount=0;
namespace aurora::gfx::detail {
bool frame_effect_recording_identity(uint32_t& i,uint64_t& id) noexcept{i=0;id=0;return false;}
}
namespace aurora::gfx::depth_peek {
extern "C" void depth_no_work_event_host(uint32_t kind){
 if(eventCount<16)events[eventCount++]=kind;
 // Deliberate control-only reentry proves these event points released mutex.
 DepthNoWorkFacts facts{};capture_depth_no_work_facts(facts);
}
extern "C" int depth_no_work_facts_host(uint32_t,uint32_t,const uint32_t*,uint32_t){return 0;} // never admit synthetic host
}
#define REQUIRE(x) do{if(!(x)){std::fprintf(stderr,"depth native line%d\n",__LINE__);return 1;}}while(false)
int main(){
 using namespace aurora::gfx::depth_peek;
 DepthNoWorkFacts facts{};capture_depth_no_work_facts(facts);
 REQUIRE(facts.schema==1&&facts.slotCount==3&&facts.enabled==0&&facts.requested==0&&facts.latestDataCount==0);
 for(size_t i=0;i<3;++i)REQUIRE(facts.slots[i].slot==i&&facts.slots[i].state==0&&!facts.slots[i].storage&&!facts.slots[i].readback&&!facts.slots[i].params);
 uint32_t z=0x12345678;REQUIRE(!read_latest(0,0,z));REQUIRE(z==0x12345678);REQUIRE(eventCount==1&&events[0]==3);
 request_snapshot();REQUIRE(eventCount==2&&events[1]==4);REQUIRE(!testing::snapshot_requested());
 after_submit();REQUIRE(eventCount==2); // original disabled/no-work path stays inert
 testing::set_latest(1,1,{0xabcdef});capture_depth_no_work_facts(facts);REQUIRE(facts.latestWidth==1&&facts.latestHeight==1&&facts.latestDataCount==1);
 REQUIRE(read_latest(0,0,z)&&z==0xabcdef);REQUIRE(events[eventCount-1]==3);
 testing::reset();REQUIRE(events[eventCount-1]==9);capture_depth_no_work_facts(facts);REQUIRE(facts.latestDataCount==0);
 std::puts("actual depth getter/read/request/reset return values preserved; no GPU admission");return 0;
}
