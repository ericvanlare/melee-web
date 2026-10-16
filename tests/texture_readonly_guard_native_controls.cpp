// Actual observer API; fake raw identities are inputs to diagnostic validation,
// never claimed to be GPU objects. Host issue stub records the authentic facts.
#include "lib/gfx/frame_effect_observer.hpp"
#include "lib/gfx/frame_pending_facts.hpp"
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
// Actual shared packet copier/predicate; diagnostic POD encoder flag is not a GPU object.
static int native_pending_controls(){
 using namespace aurora::gfx::detail;
 std::array<FramePacket,FrameSlotCount> packets{};NativeFramePendingFacts facts{};
 auto copy=[&](){return copy_native_frame_pending_facts({},packets.data(),FrameSlotCount,facts);};
 REQUIRE(copy());REQUIRE(native_frame_pending_empty(facts));
 packets[0].frameId=181;packets[0].frameIndex=180;
 REQUIRE(copy());REQUIRE(native_frame_pending_empty(facts));REQUIRE(facts.slots[0].frameId==181);
 packets[0].renderPasses.emplace_back();REQUIRE(copy());REQUIRE(facts.slots[0].renderPassCount==1);REQUIRE(!native_frame_pending_empty(facts));packets[0].renderPasses.clear();
 packets[0].textureCopies.push_back(TextureCopy{});REQUIRE(copy());REQUIRE(facts.slots[0].textureCopyCount==1);REQUIRE(!native_frame_pending_empty(facts));packets[0].textureCopies.clear();
 packets[0].encoderTasks.push_back(EncoderTask{});REQUIRE(copy());REQUIRE(facts.slots[0].encoderTaskCount==1);REQUIRE(!native_frame_pending_empty(facts));packets[0].encoderTasks.clear();
 packets[0].ops.push_back(FrameOp{});REQUIRE(copy());REQUIRE(facts.slots[0].opCount==1);REQUIRE(!native_frame_pending_empty(facts));packets[0].ops.clear();
 packets[0].textureUploads.emplace_back(wgpu::TexelCopyBufferLayout{},wgpu::TexelCopyTextureInfo{},wgpu::Extent3D{});
 REQUIRE(copy());REQUIRE(facts.slots[0].textureUploadCount==1);REQUIRE(!native_frame_pending_empty(facts));packets[0].textureUploads.clear();
 packets[0].afterSubmitCallbacks.emplace_back([](){});REQUIRE(copy());REQUIRE(facts.slots[0].afterSubmitCallbackCount==1);REQUIRE(!native_frame_pending_empty(facts));packets[0].afterSubmitCallbacks.clear();
 using Packet=FramePacket;
 const std::array<aurora::ByteBuffer Packet::*,5> buffers{&Packet::verts,&Packet::uniforms,&Packet::indices,&Packet::storage,&Packet::textureUpload};
 const std::array<uint64_t NativeFrameSlotPendingFacts::*,5> lengths{&NativeFrameSlotPendingFacts::vertsBytes,&NativeFrameSlotPendingFacts::uniformsBytes,&NativeFrameSlotPendingFacts::indicesBytes,&NativeFrameSlotPendingFacts::storageBytes,&NativeFrameSlotPendingFacts::textureUploadBytes};
 for(size_t i=0;i<buffers.size();++i){auto& buffer=packets[0].*buffers[i];buffer.reserve_extra(8);REQUIRE(copy());REQUIRE(native_frame_pending_empty(facts));buffer.append_zeroes(1);REQUIRE(copy());REQUIRE(facts.slots[0].*lengths[i]==1);REQUIRE(!native_frame_pending_empty(facts));buffer.clear();}
 // Multiple fields and a later authored active slot are all retained.
 packets.back().ops.push_back(FrameOp{});packets.back().encoderTasks.push_back(EncoderTask{});
 REQUIRE(copy());REQUIRE(facts.slots.back().opCount==1 && facts.slots.back().encoderTaskCount==1);REQUIRE(!native_frame_pending_empty(facts));packets.back().ops.clear();packets.back().encoderTasks.clear();
 for(auto recorder:{NativeRecorderPendingFacts{1,0,0},NativeRecorderPendingFacts{0,1,0},NativeRecorderPendingFacts{0,0,1}}){REQUIRE(copy_native_frame_pending_facts(recorder,packets.data(),FrameSlotCount,facts));REQUIRE(!native_frame_pending_empty(facts));}
 REQUIRE(copy());facts.slots[0].encoderPresent=1;REQUIRE(!native_frame_pending_empty(facts));facts.slots[0].encoderPresent=2;REQUIRE(!native_frame_pending_empty(facts));
 REQUIRE(copy());facts.schemaVersion=2;REQUIRE(!native_frame_pending_empty(facts));
 REQUIRE(copy());facts.activeSlotCount=FrameSlotCount+1;REQUIRE(!native_frame_pending_empty(facts));
 REQUIRE(copy());facts.slots[1].slot=0;REQUIRE(!native_frame_pending_empty(facts));
 REQUIRE(!copy_native_frame_pending_facts({},nullptr,1,facts));REQUIRE(facts.schemaVersion==0 && facts.activeSlotCount==0);
 REQUIRE(!copy_native_frame_pending_facts({},packets.data(),0,facts));
 REQUIRE(!copy_native_frame_pending_facts({},packets.data(),FrameSlotCount+1,facts));
 REQUIRE(!copy_native_frame_pending_facts({2,0,0},packets.data(),1,facts));
 REQUIRE(copy_native_frame_pending_facts({},packets.data(),1,facts));REQUIRE(native_frame_pending_empty(facts));
 facts.slots[1].frameId=1;REQUIRE(!native_frame_pending_empty(facts));
 return 0;
}
static int native_submission_controls(){
 using namespace aurora::gfx::detail;
 using Book=BrowserSubmissionOwnerBook<StagingBufferCount>;
 Book owners{AURORA_BROWSER_SUBMISSION_REGISTERED,AURORA_BROWSER_SUBMISSION_COMPLETED,AURORA_BROWSER_SUBMISSION_FAILED,AURORA_BROWSER_SUBMISSION_IMMEDIATE_RELEASE};
 std::atomic_uint64_t generation{7};
 std::array<FramePacket,FrameSlotCount> packets{};NativeSubmissionPendingFacts facts{};
 REQUIRE(copy_native_frame_pending_facts({},packets.data(),FrameSlotCount,facts.packets));
 facts.schemaVersion=1;facts.ownerBookPresent=1;
 facts.before.generation=7;facts.before.queueHandle=0x11;facts.before.activeFrameCount=FrameSlotCount;
 facts.before.activeStagingCount=StagingBufferCount;facts.before.freeFrameCount=FrameSlotCount;facts.before.freeStagingCount=StagingBufferCount;facts.before.workerIdle=1;
 auto snapshot=[&](){for(size_t i=0;i<StagingBufferCount;++i)facts.owners[i]=owners.snapshot(i);facts.after=facts.before;};
 snapshot();REQUIRE(native_submission_component_ready(facts));
 // First decisive refusal: the real book still owns a registration even though
 // diagnostic packets/free counters/worker state all appear empty and idle.
 owners.publish_registration(0,1,7,0x11,0x13);facts.before.registrationSequence=1;snapshot();
 REQUIRE(!native_submission_component_ready(facts));
 REQUIRE(owners.complete_callback(0,1,7,generation,1,true)==BrowserOwnerCallbackOutcome::Completed);
 facts.before.callbackSequence=1;snapshot();REQUIRE(!native_submission_component_ready(facts)); // no Future publication yet
 owners.publish_future_if_current(0,1,7,generation,77);facts.before.freeStagingCount=StagingBufferCount-1;snapshot();
 REQUIRE(!native_submission_component_ready(facts)); // callback state alone cannot return slot ownership
 facts.before.freeStagingCount=StagingBufferCount;snapshot();REQUIRE(native_submission_component_ready(facts));
 facts.after.callbackSequence++;REQUIRE(!native_submission_component_ready(facts));snapshot();
 facts.before.processEventsQueued=1;snapshot();REQUIRE(!native_submission_component_ready(facts));facts.before.processEventsQueued=0;
 facts.before.workerIdle=0;snapshot();REQUIRE(!native_submission_component_ready(facts));facts.before.workerIdle=1;
 facts.before.freeFrameCount=FrameSlotCount+1;snapshot();REQUIRE(!native_submission_component_ready(facts));facts.before.freeFrameCount=FrameSlotCount;
 facts.before.activeStagingCount=StagingBufferCount+1;snapshot();REQUIRE(!native_submission_component_ready(facts));facts.before.activeStagingCount=StagingBufferCount;
 facts.before.staleCallbackCount=1;snapshot();REQUIRE(!native_submission_component_ready(facts));facts.before.staleCallbackCount=0;
 facts.before.queueHandle=0x21;snapshot();REQUIRE(!native_submission_component_ready(facts));facts.before.queueHandle=0x11;
 facts.before.generation=8;snapshot();REQUIRE(!native_submission_component_ready(facts));facts.before.generation=7;
 snapshot();facts.ownerBookPresent=0;REQUIRE(!native_submission_component_ready(facts));facts.ownerBookPresent=1;
 snapshot();facts.owners[0].state=99;REQUIRE(!native_submission_component_ready(facts));
 owners.publish_registration(0,2,7,0x11,0x13);facts.before.registrationSequence=2;
 REQUIRE(owners.complete_callback(0,2,7,generation,2,false)==BrowserOwnerCallbackOutcome::Failed);
 facts.before.callbackSequence=2;snapshot();REQUIRE(!native_submission_component_ready(facts));
 owners.mark_immediate(0,7);snapshot();REQUIRE(native_submission_component_ready(facts));
 facts.owners[0].futureId=77;REQUIRE(!native_submission_component_ready(facts)); // no invented completion on immediate release
 return 0;
}
int main(){
 REQUIRE(native_pending_controls()==0);
 REQUIRE(native_submission_controls()==0);
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
 using Witness=BrowserFrameEffectObserver::SelectedSampledTextureWitness;
 Witness witness{};
 REQUIRE(!o->selected_sampled_texture_witness(witness));
 auto select=[&](){o->request_selected_sampled_texture();o->observe_selected_sampled_texture(0,pair,91,4,8,16,0,181);};
 select();REQUIRE(o->selected_sampled_texture_witness(witness));
 REQUIRE(witness.owner==91 && witness.texture==4 && witness.view==8 && witness.device==12 && witness.frameId==181 && witness.slot==0);
 REQUIRE(o->issue_selected_sampled_texture(pair,91,4,8,16));REQUIRE(issued==2);
 REQUIRE(!o->issue_selected_sampled_texture(pair,91,4,8,16));REQUIRE(issued==2);
 select();REQUIRE(!o->issue_selected_sampled_texture(pair,92,4,8,16));REQUIRE(issued==2);
 select();REQUIRE(!o->issue_selected_sampled_texture(pair,91,5,8,16));REQUIRE(issued==2);
 // Authentic newly created token, including reused raw identities, cannot replace selection.
 auto replacement=o->observe_owned_texture_creation(o->TextureRefTextureRole,0,4,12,td);
 auto replacementPair=o->observe_owned_texture_view_creation(replacement,8,4,12,nullptr);
 o->bind_owned_texture_ref(91,replacementPair,4,8,0);
 select();REQUIRE(!o->issue_selected_sampled_texture(replacementPair,91,4,8,16));REQUIRE(issued==2);
 select();o->detach_owned_texture_ref(91,pair);REQUIRE(detached==1);
 REQUIRE(!o->selected_sampled_texture_witness(witness));
 REQUIRE(!o->issue_selected_sampled_texture(pair,91,4,8,16));REQUIRE(issued==2);
 select();o->end_persistent_attachment_lifetime(12);o->begin_persistent_attachment_lifetime(24);
 REQUIRE(!o->issue_selected_sampled_texture(pair,91,4,8,16));REQUIRE(issued==2);
 o->end_persistent_attachment_lifetime(24);o->begin_persistent_attachment_lifetime(12);

 o->end_persistent_attachment_lifetime(12);REQUIRE(!o->issue_texture_readonly_guard(pair,91,4,8,16));
 o->begin_persistent_attachment_lifetime(12);REQUIRE(!o->issue_texture_readonly_guard(pair,91,4,8,16));
 REQUIRE(epochs==7);
 // Actual observer submission/fence/frame join, no mirrored predicate or GPU call.
 for(unsigned mode=0;mode<6;++mode){
  auto selected=std::make_unique<BrowserFrameEffectObserver>();selected->begin_persistent_attachment_lifetime(12);
  auto texture=selected->observe_owned_texture_creation(selected->TextureRefTextureRole,0,4,12,td);
  auto current=selected->observe_owned_texture_view_creation(texture,8,4,12,nullptr);selected->bind_owned_texture_ref(91,current,4,8,0);
  REQUIRE(selected->arm_first_draw(0,181));selected->observe_draw(0,181,0,0);
  selected->request_selected_sampled_texture();selected->observe_selected_sampled_texture(0,current,91,4,8,16,mode==1?1:0,mode==2?182:181);
  REQUIRE(!selected->submitted_selected_sampled_texture_witness(witness));
  if(mode!=3)selected->observe_submit(181);
  if(mode!=4)selected->observe_fence(181,mode!=5);
  REQUIRE(selected->submitted_selected_sampled_texture_witness(witness)==(mode==0));
  if(mode==0){
   REQUIRE(witness.frameIndex==0&&witness.frameId==181);REQUIRE(selected->issue_selected_sampled_texture(current,91,4,8,16));
   REQUIRE(selected->issued_selected_sampled_texture_witness(witness));
   REQUIRE(selected->issued_selected_sampled_texture_owner_matches(current,91,4,8,16));
   REQUIRE(!selected->issued_selected_sampled_texture_owner_matches(current,92,4,8,16));
   REQUIRE(!selected->issued_selected_sampled_texture_owner_matches(current,91,5,8,16));
   selected->detach_owned_texture_ref(91,current);REQUIRE(!selected->issued_selected_sampled_texture_witness(witness));
   REQUIRE(!selected->issued_selected_sampled_texture_owner_matches(current,91,4,8,16));
  }
 }
 std::puts("actual selected submission/frame/fence witness controls passed");return 0;
}
