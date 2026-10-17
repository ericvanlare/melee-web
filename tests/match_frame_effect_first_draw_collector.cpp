// Actual private collector control. No WebGPU service or command encoding is
// fabricated: these are diagnostic event/callback association inputs only.
#include <array>
#include <limits>
#include "lib/gx/gx.hpp"
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include "lib/gfx/frame_effect_observer.hpp"
// ACTUAL_TEXTURE_OWNER_BRIDGES
#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr,"first-draw collector failure line %d: %s\n",__LINE__,#x);return 1; } } while(false)
namespace aurora::gfx::detail {
struct BrowserFrameEffectObserverControls {
  static uint64_t attachment_serial(BrowserFrameEffectObserver& o,uint32_t role,size_t slot) { return o.warm_[o.warm_index(role,slot)].serial; }
  static void stale_attachment_parent(BrowserFrameEffectObserver& o,size_t slot) { ++o.warm_[o.warm_index(o.PersistentAttachmentViewRole,slot)].parentSerial; }
  static void exhaust_serials(BrowserFrameEffectObserver& observer) { observer.nextCreateSerial_=UINT64_MAX; }
  static BrowserFrameEffectObserver::SamplerCreation corrupt_sampler(
      const BrowserFrameEffectObserver::SamplerCreation& source, unsigned corruption) {
    auto result=source;
    switch(corruption){
      case 0:++result.raw;break;
      case 1:++result.device;break;
      case 2:++result.serial;break;
      case 3:result.words[0]^=1;break;
      case 4:result.valid=false;break;
    }
    return result;
  }
  static TextureOwnerPair corrupt_texture_owner(const TextureOwnerPair& source,unsigned corruption) {
    auto result=source;switch(corruption){
      case 0:++result.texture.raw;break;case 1:++result.texture.device;break;case 2:++result.texture.serial;break;
      case 3:++result.view.parentSerial;break;case 4:result.texture.words[0]^=1;break;case 5:result.view.words[2]^=1;break;
      case 6:++result.texture.deviceEpoch;break;case 7:++result.texture.domainEpoch;break;case 8:++result.textureRefOwner;break;
    }return result;
  }
  static void exhaust_layout_rows(BrowserFrameEffectObserver& observer) { observer.layoutEntryCount_=observer.LayoutEntryCapacity; }
  static void exhaust_provenance_rows(BrowserFrameEffectObserver& observer) { observer.provenanceCount_=observer.ResourceCapacity; }
};
}

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
// Static descriptor controls exercise the same actual observer with synthetic
// opaque identities; they create no GPU resources and make no restore claim.
#define STATIC_GROUP_CONTROL_CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "static-group control failure line %d: %s\n", __LINE__, #expr); return false; } } while (false)

namespace {
int ownerToken, deviceToken, groupToken, layoutToken, vertexToken, storageToken;
int wrongOwnerToken, wrongDeviceToken, wrongGroupToken, wrongLayoutToken, wrongVertexToken, wrongStorageToken;

uintptr_t address(const void* p) { return reinterpret_cast<uintptr_t>(p); }
WGPUBuffer as_buffer(uintptr_t p) { return reinterpret_cast<WGPUBuffer>(p); }
WGPUBindGroup as_group(uintptr_t p) { return reinterpret_cast<WGPUBindGroup>(p); }
WGPUBindGroupLayout as_layout(uintptr_t p) { return reinterpret_cast<WGPUBindGroupLayout>(p); }

struct Ids {
  uintptr_t owner = address(&ownerToken);
  uintptr_t device = address(&deviceToken);
  uintptr_t group = address(&groupToken);
  uintptr_t layout = address(&layoutToken);
  uintptr_t vertex = address(&vertexToken);
  uintptr_t storage = address(&storageToken);
};

struct Fixture {
  std::unique_ptr<BrowserFrameEffectObserver> observer{new (std::nothrow) BrowserFrameEffectObserver};
  Ids ids{};
  wgpu::BufferDescriptor vertexDescriptor{};
  wgpu::BufferDescriptor storageDescriptor{};
  std::array<WGPUBindGroupEntry, 2> entries{};

  Fixture() {
    vertexDescriptor.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
    vertexDescriptor.size = 5u * 1024u * 1024u;
    storageDescriptor.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    storageDescriptor.size = 29u * 1024u * 1024u;
    entries[0].binding = 0;
    entries[0].buffer = as_buffer(ids.vertex);
    entries[0].offset = 0;
    entries[0].size = WGPU_WHOLE_SIZE;
    entries[1].binding = 1;
    entries[1].buffer = as_buffer(ids.storage);
    entries[1].offset = 0;
    entries[1].size = WGPU_WHOLE_SIZE;
  }

  bool begin() {
    if (!observer) return false;
    observer->begin_static_resource_lifetime(ids.owner, ids.device);
    const std::array<wgpu::BindGroupLayoutEntry,2> layoutEntries{{
      {.binding=0,.visibility=wgpu::ShaderStage::Vertex,.buffer={.type=wgpu::BufferBindingType::ReadOnlyStorage}},
      {.binding=1,.visibility=wgpu::ShaderStage::Vertex|wgpu::ShaderStage::Fragment,.buffer={.type=wgpu::BufferBindingType::ReadOnlyStorage}}
    }};
    const wgpu::BindGroupLayoutDescriptor layout{.entryCount=layoutEntries.size(),.entries=layoutEntries.data()};
    observer->remember_renderer_layout(BrowserFrameEffectObserver::StaticLayoutRole,ids.owner,ids.device,ids.layout,layout);
    return true;
  }
  void remember_buffer(uint32_t role, uintptr_t raw, const wgpu::BufferDescriptor& desc) {
    observer->remember_static_buffer_descriptor(role, ids.owner, ids.device, raw, desc);
  }
  void remember_both() {
    remember_buffer(BrowserFrameEffectObserver::StaticVertexBufferRole, ids.vertex, vertexDescriptor);
    remember_buffer(BrowserFrameEffectObserver::StaticStorageBufferRole, ids.storage, storageDescriptor);
  }
  void remember_group(uintptr_t owner, uintptr_t device, uintptr_t group, uintptr_t layout,
                      uintptr_t vertex, uintptr_t storage, size_t entryCount = 2) {
    WGPUBindGroupDescriptor descriptor{};
    descriptor.layout = as_layout(layout);
    descriptor.entryCount = entryCount;
    descriptor.entries = entries.data();
    observer->remember_static_bind_group_descriptor(owner, device, group, layout, vertex, storage, descriptor);
  }
  bool arm_and_begin_frame() {
    if (!observer->arm_first_draw(3158, 3159)) return false;
    observer->observe_command(3158, 3159, 0, 0, 0, false);
    return observer->snapshot().state == AURORA_BROWSER_FRAME_EFFECT_COLLECTING;
  }
  void bind(const Ids& used, uint32_t slot = 0) {
    observer->observe_static_bind_group_set(slot, used.owner, used.device, used.group, used.layout,
                                            used.vertex, used.storage, 0, nullptr);
  }
};

bool record_complete(Fixture& f) {
  if (!f.begin()) return false;
  f.remember_both();
  f.remember_group(f.ids.owner, f.ids.device, f.ids.group, f.ids.layout, f.ids.vertex, f.ids.storage);
  return true;
}

bool positive_exact_descriptors() {
  Fixture f;
  STATIC_GROUP_CONTROL_CHECK(f.observer && record_complete(f));
  STATIC_GROUP_CONTROL_CHECK(f.observer->snapshot().resourceCount == 0); // initialization witnesses are not prematurely exported
  STATIC_GROUP_CONTROL_CHECK(f.arm_and_begin_frame());
  f.bind(f.ids);
  STATIC_GROUP_CONTROL_CHECK(f.observer->snapshot().firstBlocker == AURORA_BROWSER_FRAME_EFFECT_BLOCKER_NONE);
  f.observer->begin_draw_callback();
  f.observer->observe_actual_draw(1, 3, 0, 1, 0);
  f.observer->end_draw_callback();
  const auto summary = f.observer->snapshot();
  STATIC_GROUP_CONTROL_CHECK(summary.resourceCount == 4 && summary.bindingEntryCount == 2 && summary.layoutEntryCount == 2 && summary.provenanceCount == 4);
  uint64_t vertexId = 0, storageId = 0, groupId = 0;
  uint32_t bufferRows = 0, groupRows = 0;
  for (uint32_t i = 0; i < summary.resourceCount; ++i) {
    AuroraBrowserFrameEffectResource row{};
    STATIC_GROUP_CONTROL_CHECK(f.observer->resource(i, &row));
    if (row.kind == AURORA_BROWSER_FRAME_EFFECT_HANDLE_BUFFER) {
      ++bufferRows;
      STATIC_GROUP_CONTROL_CHECK(row.wordCount == 5 && row.words[4] == 0);
      if (row.words[0] == f.vertexDescriptor.size &&
          row.words[2] == static_cast<uint32_t>(static_cast<uint64_t>(f.vertexDescriptor.usage))) {
        vertexId = row.captureId;
      } else if (row.words[0] == f.storageDescriptor.size &&
                 row.words[2] == static_cast<uint32_t>(static_cast<uint64_t>(f.storageDescriptor.usage))) {
        storageId = row.captureId;
      } else {
        STATIC_GROUP_CONTROL_CHECK(false);
      }
      STATIC_GROUP_CONTROL_CHECK(row.words[1] == 0 && row.words[3] == 0);
    } else if (row.kind == AURORA_BROWSER_FRAME_EFFECT_HANDLE_BIND_GROUP) {
      ++groupRows;
      groupId = row.captureId;
      STATIC_GROUP_CONTROL_CHECK(row.wordCount == 5 && row.words[2] == 2);
    } else if (row.kind == AURORA_BROWSER_FRAME_EFFECT_HANDLE_BIND_GROUP_LAYOUT) {
      STATIC_GROUP_CONTROL_CHECK(row.wordCount == 1 && row.words[0] == 2);
    } else {
      STATIC_GROUP_CONTROL_CHECK(false);
    }
  }
  STATIC_GROUP_CONTROL_CHECK(bufferRows == 2 && groupRows == 1 && vertexId != 0 && storageId != 0 && groupId != 0);
  for (uint32_t i = 0; i < 2; ++i) {
    AuroraBrowserFrameEffectBindingEntry row{};
    STATIC_GROUP_CONTROL_CHECK(f.observer->binding_entry(i, &row));
    STATIC_GROUP_CONTROL_CHECK(row.bindGroupId == groupId && row.resourceId == (i == 0 ? vertexId : storageId));
    STATIC_GROUP_CONTROL_CHECK(row.binding == i && row.kind == AURORA_BROWSER_FRAME_EFFECT_HANDLE_BUFFER);
    STATIC_GROUP_CONTROL_CHECK(row.offset == 0 && row.size == WGPU_WHOLE_SIZE);
  }
  bool sawSlotZero = false;
  for (uint32_t i = 0; i < summary.eventCount; ++i) {
    AuroraBrowserFrameEffectEvent row{};
    STATIC_GROUP_CONTROL_CHECK(f.observer->event(i, &row));
    if (row.kind == AURORA_BROWSER_FRAME_EFFECT_EVENT_BIND_GROUP_STATE && row.flags == 0) {
      sawSlotZero = row.handleA == groupId;
    }
  }
  STATIC_GROUP_CONTROL_CHECK(sawSlotZero);
  AuroraBrowserFrameEffectDraw draw{};
  STATIC_GROUP_CONTROL_CHECK(f.observer->draw(0, &draw));
  STATIC_GROUP_CONTROL_CHECK(draw.bindGroupIds[0] == groupId);
  STATIC_GROUP_CONTROL_CHECK(!f.observer->draw(1, &draw));
  return true;
}

enum class BadEntry { WrongBinding, ChainedEntry, WrongBuffer };
bool malformed_entry_cannot_heal(BadEntry bad) {
  Fixture f; STATIC_GROUP_CONTROL_CHECK(f.observer && f.begin()); f.remember_both();
  const auto validEntry = f.entries[0];
  switch (bad) {
    case BadEntry::WrongBinding:
      f.entries[0].binding = 1;
      break;
    case BadEntry::ChainedEntry:
      f.entries[0].nextInChain = reinterpret_cast<decltype(f.entries[0].nextInChain)>(&wrongGroupToken);
      break;
    case BadEntry::WrongBuffer:
      f.entries[0].buffer = as_buffer(address(&wrongVertexToken));
      break;
  }
  f.remember_group(f.ids.owner, f.ids.device, f.ids.group, f.ids.layout, f.ids.vertex, f.ids.storage);
  f.entries[0] = validEntry;
  f.remember_group(f.ids.owner, f.ids.device, f.ids.group, f.ids.layout, f.ids.vertex, f.ids.storage);
  STATIC_GROUP_CONTROL_CHECK(f.arm_and_begin_frame()); f.bind(f.ids);
  const auto summary = f.observer->snapshot();
  STATIC_GROUP_CONTROL_CHECK(summary.firstBlocker == AURORA_BROWSER_FRAME_EFFECT_UNKNOWN_RESOURCE);
  STATIC_GROUP_CONTROL_CHECK(summary.resourceCount == 0 && summary.bindingEntryCount == 0);
  return true;
}

bool missing_vertex_refuses() {
  Fixture f; STATIC_GROUP_CONTROL_CHECK(f.observer && f.begin());
  f.remember_buffer(BrowserFrameEffectObserver::StaticStorageBufferRole, f.ids.storage, f.storageDescriptor);
  f.remember_group(f.ids.owner, f.ids.device, f.ids.group, f.ids.layout, f.ids.vertex, f.ids.storage);
  STATIC_GROUP_CONTROL_CHECK(f.arm_and_begin_frame()); f.bind(f.ids);
  STATIC_GROUP_CONTROL_CHECK(f.observer->snapshot().firstBlocker == AURORA_BROWSER_FRAME_EFFECT_UNKNOWN_RESOURCE);
  STATIC_GROUP_CONTROL_CHECK(f.observer->snapshot().resourceCount == 0);
  return true;
}

bool missing_storage_refuses() {
  Fixture f; STATIC_GROUP_CONTROL_CHECK(f.observer && f.begin());
  f.remember_buffer(BrowserFrameEffectObserver::StaticVertexBufferRole, f.ids.vertex, f.vertexDescriptor);
  f.remember_group(f.ids.owner, f.ids.device, f.ids.group, f.ids.layout, f.ids.vertex, f.ids.storage);
  STATIC_GROUP_CONTROL_CHECK(f.arm_and_begin_frame()); f.bind(f.ids);
  STATIC_GROUP_CONTROL_CHECK(f.observer->snapshot().firstBlocker == AURORA_BROWSER_FRAME_EFFECT_UNKNOWN_RESOURCE);
  STATIC_GROUP_CONTROL_CHECK(f.observer->snapshot().resourceCount == 0);
  return true;
}

bool missing_group_refuses() {
  Fixture g; STATIC_GROUP_CONTROL_CHECK(g.observer && g.begin()); g.remember_both();
  STATIC_GROUP_CONTROL_CHECK(g.arm_and_begin_frame()); g.bind(g.ids);
  STATIC_GROUP_CONTROL_CHECK(g.observer->snapshot().firstBlocker == AURORA_BROWSER_FRAME_EFFECT_UNKNOWN_RESOURCE);
  STATIC_GROUP_CONTROL_CHECK(g.observer->snapshot().resourceCount == 0);
  return true;
}

bool invalid_descriptor_cannot_heal() {
  Fixture f; STATIC_GROUP_CONTROL_CHECK(f.observer && f.begin()); f.remember_both();
  f.remember_group(f.ids.owner, f.ids.device, f.ids.group, f.ids.layout, f.ids.vertex, f.ids.storage, 1);
  f.remember_group(f.ids.owner, f.ids.device, f.ids.group, f.ids.layout, f.ids.vertex, f.ids.storage, 2);
  STATIC_GROUP_CONTROL_CHECK(f.arm_and_begin_frame()); f.bind(f.ids);
  STATIC_GROUP_CONTROL_CHECK(f.observer->snapshot().firstBlocker == AURORA_BROWSER_FRAME_EFFECT_UNKNOWN_RESOURCE);
  STATIC_GROUP_CONTROL_CHECK(f.observer->snapshot().resourceCount == 0);
  return true;
}

enum class Changed { Owner, Device, Group, Layout, Vertex, Storage, ZeroGroup };
bool wrong_identity_refuses(Changed changed) {
  Fixture f; STATIC_GROUP_CONTROL_CHECK(f.observer && record_complete(f)); STATIC_GROUP_CONTROL_CHECK(f.arm_and_begin_frame());
  Ids used = f.ids;
  switch (changed) {
    case Changed::Owner: used.owner = address(&wrongOwnerToken); break;
    case Changed::Device: used.device = address(&wrongDeviceToken); break;
    case Changed::Group: used.group = address(&wrongGroupToken); break;
    case Changed::Layout: used.layout = address(&wrongLayoutToken); break;
    case Changed::Vertex: used.vertex = address(&wrongVertexToken); break;
    case Changed::Storage: used.storage = address(&wrongStorageToken); break;
    case Changed::ZeroGroup: used.group = 0; break;
  }
  f.bind(used);
  STATIC_GROUP_CONTROL_CHECK(f.observer->snapshot().firstBlocker == AURORA_BROWSER_FRAME_EFFECT_RESOURCE_ID_REUSE);
  return true;
}

bool zero_device_refuses() {
  Fixture f; STATIC_GROUP_CONTROL_CHECK(f.observer); f.ids.device = 0; STATIC_GROUP_CONTROL_CHECK(f.begin()); f.remember_both();
  f.remember_group(f.ids.owner, 0, f.ids.group, f.ids.layout, f.ids.vertex, f.ids.storage);
  STATIC_GROUP_CONTROL_CHECK(f.arm_and_begin_frame()); f.bind(f.ids);
  STATIC_GROUP_CONTROL_CHECK(f.observer->snapshot().firstBlocker == AURORA_BROWSER_FRAME_EFFECT_UNKNOWN_RESOURCE);
  return true;
}

bool unexpected_slot_refuses() {
  Fixture f; STATIC_GROUP_CONTROL_CHECK(f.observer && record_complete(f)); STATIC_GROUP_CONTROL_CHECK(f.arm_and_begin_frame()); f.bind(f.ids, 1);
  STATIC_GROUP_CONTROL_CHECK(f.observer->snapshot().firstBlocker == AURORA_BROWSER_FRAME_EFFECT_UNKNOWN_RESOURCE);
  STATIC_GROUP_CONTROL_CHECK(f.observer->snapshot().resourceCount == 0);
  return true;
}

bool shutdown_reinit_does_not_reuse_old_witness() {
  Fixture f; STATIC_GROUP_CONTROL_CHECK(f.observer && record_complete(f));
  f.observer->end_static_resource_lifetime(f.ids.owner);
  f.observer->begin_static_resource_lifetime(f.ids.owner, f.ids.device); // same raw owner/device, new observer epoch
  STATIC_GROUP_CONTROL_CHECK(f.arm_and_begin_frame()); f.bind(f.ids);
  STATIC_GROUP_CONTROL_CHECK(f.observer->snapshot().firstBlocker == AURORA_BROWSER_FRAME_EFFECT_UNKNOWN_RESOURCE);
  STATIC_GROUP_CONTROL_CHECK(f.observer->snapshot().resourceCount == 0);
  return true;
}
} // namespace

static int static_group_descriptor_controls() {
  if (!positive_exact_descriptors() || !missing_vertex_refuses() || !missing_storage_refuses() ||
      !missing_group_refuses() || !invalid_descriptor_cannot_heal() ||
      !malformed_entry_cannot_heal(BadEntry::WrongBinding) ||
      !malformed_entry_cannot_heal(BadEntry::ChainedEntry) ||
      !malformed_entry_cannot_heal(BadEntry::WrongBuffer) ||
      !wrong_identity_refuses(Changed::Owner) || !wrong_identity_refuses(Changed::Device) ||
      !wrong_identity_refuses(Changed::Group) || !wrong_identity_refuses(Changed::Layout) ||
      !wrong_identity_refuses(Changed::Vertex) || !wrong_identity_refuses(Changed::Storage) ||
      !wrong_identity_refuses(Changed::ZeroGroup) || !zero_device_refuses() ||
      !unexpected_slot_refuses() || !shutdown_reinit_does_not_reuse_old_witness()) return 1;
  std::puts("static slot-0 descriptor witness positive, draw-state event, malformed-entry no-healing, identity and lifecycle controls passed");
  return 0;
}

namespace {
using OwnerObserver = BrowserFrameEffectObserver;
int uniformToken,indexToken,stagingToken,uniformLayoutToken,emptyLayoutToken,uniformGroupToken,emptyGroupToken,emptyTextureToken,emptyViewToken,emptySamplerToken;

bool renderer_owner_graph_positive(unsigned samplerOrder=0, int samplerCorruption=-1) {
  Fixture f; STATIC_GROUP_CONTROL_CHECK(f.observer && record_complete(f));
  auto& o=*f.observer; const auto owner=f.ids.owner,device=f.ids.device;
  const auto uniform=address(&uniformToken),index=address(&indexToken),staging=address(&stagingToken);
  const wgpu::BufferDescriptor uniformDesc{.usage=wgpu::BufferUsage::Uniform|wgpu::BufferUsage::CopyDst,.size=aurora::gfx::UniformBufferSize};
  const wgpu::BufferDescriptor indexDesc{.usage=wgpu::BufferUsage::Index|wgpu::BufferUsage::CopyDst,.size=aurora::gfx::IndexBufferSize};
  const wgpu::BufferDescriptor stagingDesc{.usage=wgpu::BufferUsage::CopyDst|wgpu::BufferUsage::CopySrc,.size=aurora::gfx::detail::StagingBufferSize};
  o.remember_renderer_buffer(OwnerObserver::UniformBufferRole,0,owner,device,uniform,uniformDesc);
  o.remember_renderer_buffer(OwnerObserver::IndexBufferRole,0,owner,device,index,indexDesc);
  o.remember_renderer_buffer(OwnerObserver::StagingBufferRole,1,owner,device,staging,stagingDesc);
  const wgpu::BindGroupLayoutEntry uniformEntry{.binding=0,.visibility=wgpu::ShaderStage::Vertex|wgpu::ShaderStage::Fragment,.buffer={.type=wgpu::BufferBindingType::Uniform,.hasDynamicOffset=true}};
  const wgpu::BindGroupLayoutDescriptor uniformLayout{.entryCount=1,.entries=&uniformEntry};
  o.remember_renderer_layout(OwnerObserver::UniformLayoutRole,owner,device,address(&uniformLayoutToken),uniformLayout);
  WGPUBindGroupEntry uniformBinding{};uniformBinding.binding=0;uniformBinding.buffer=as_buffer(uniform);uniformBinding.size=aurora::gx::MaxUniformSize;
  WGPUBindGroupDescriptor uniformGroup{};uniformGroup.layout=as_layout(address(&uniformLayoutToken));uniformGroup.entryCount=1;uniformGroup.entries=&uniformBinding;
  o.remember_renderer_group(OwnerObserver::UniformGroupRole,owner,device,address(&uniformGroupToken),uniformGroup);
  std::array<wgpu::BindGroupLayoutEntry,OwnerObserver::TextureEntryCount> layouts{};
  for(size_t i=0;i<layouts.size();++i){layouts[i].binding=uint32_t(i);layouts[i].visibility=wgpu::ShaderStage::Fragment;
    if(i%2==0){layouts[i].texture.sampleType=wgpu::TextureSampleType::Float;layouts[i].texture.viewDimension=wgpu::TextureViewDimension::e2D;}
    else layouts[i].sampler.type=wgpu::SamplerBindingType::Filtering;
  }
  const wgpu::BindGroupLayoutDescriptor emptyLayout{.entryCount=layouts.size(),.entries=layouts.data()};
  o.remember_renderer_layout(OwnerObserver::EmptyLayoutRole,owner,device,address(&emptyLayoutToken),emptyLayout);
  const wgpu::SamplerDescriptor sampler{};
  const auto creation=o.observe_sampler_creation(address(&emptySamplerToken),device,sampler);
  o.select_empty_sampler(owner,creation,sampler);
  o.select_empty_sampler(owner,creation,sampler); // exact cache hit retains the actual creation token
  const wgpu::TextureDescriptor texture{.usage=wgpu::TextureUsage::TextureBinding,.size={1,1},.format=wgpu::TextureFormat::RGBA8Unorm};
  o.remember_empty_texture(owner,device,address(&emptyTextureToken),texture);
  o.remember_empty_view(owner,device,address(&emptyViewToken),address(&emptyTextureToken));
  std::array<WGPUBindGroupEntry,OwnerObserver::TextureEntryCount> bindings{};
  for(size_t i=0;i<bindings.size();++i){bindings[i].binding=uint32_t(i);
    bindings[i].size=WGPU_WHOLE_SIZE;
    if(i%2==0)bindings[i].textureView=reinterpret_cast<WGPUTextureView>(&emptyViewToken);
    else bindings[i].sampler=reinterpret_cast<WGPUSampler>(&emptySamplerToken);
  }
  WGPUBindGroupDescriptor emptyGroup{};emptyGroup.layout=as_layout(address(&emptyLayoutToken));emptyGroup.entryCount=bindings.size();emptyGroup.entries=bindings.data();
  o.remember_renderer_group(OwnerObserver::EmptyGroupRole,owner,device,address(&emptyGroupToken),emptyGroup);
  if (samplerOrder==4) {
    STATIC_GROUP_CONTROL_CHECK(o.arm_first_draw(3158,3159));
    o.observe_cached_sampler_descriptor(creation,sampler);
    const auto armed=o.snapshot();
    STATIC_GROUP_CONTROL_CHECK(armed.state==AURORA_BROWSER_FRAME_EFFECT_ARMED && armed.firstBlocker==0 &&
        armed.resourceCount==1 && armed.bindingEntryCount==0 && armed.layoutEntryCount==0 && armed.provenanceCount==0);
    o.observe_command(3158,3159,0,0,0,false);
    STATIC_GROUP_CONTROL_CHECK(o.snapshot().state==AURORA_BROWSER_FRAME_EFFECT_COLLECTING);
  } else STATIC_GROUP_CONTROL_CHECK(f.arm_and_begin_frame());
  if (samplerCorruption>=0 && samplerOrder!=2) {
    // The fixture alone can corrupt the otherwise private cache-entry token.
    const auto altered=aurora::gfx::detail::BrowserFrameEffectObserverControls::corrupt_sampler(creation,unsigned(samplerCorruption));
    auto requested=sampler;
    if(samplerCorruption==5)requested.maxAnisotropy=2;
    if(samplerCorruption==6)requested.nextInChain=reinterpret_cast<const wgpu::ChainedStruct*>(&wrongGroupToken);
    o.observe_cached_sampler_descriptor(altered,requested);
    const auto before=o.snapshot();
    STATIC_GROUP_CONTROL_CHECK(before.firstBlocker!=0 && before.resourceCount==0 && before.bindingEntryCount==0 && before.layoutEntryCount==0 && before.provenanceCount==0);
    o.observe_renderer_group_set(OwnerObserver::EmptyGroupRole,2,owner,device,address(&emptyGroupToken),address(&emptyLayoutToken),0,nullptr);
    const auto after=o.snapshot();
    STATIC_GROUP_CONTROL_CHECK(after.firstBlocker==before.firstBlocker && after.resourceCount==0 && after.bindingEntryCount==0 && after.layoutEntryCount==0 && after.provenanceCount==0);
    return true;
  }
  if (samplerOrder==1) {
    o.observe_cached_sampler_descriptor(creation,sampler);
    const auto prior=o.snapshot();
    STATIC_GROUP_CONTROL_CHECK(prior.firstBlocker==0 && prior.resourceCount==1 && prior.bindingEntryCount==0 && prior.layoutEntryCount==0 && prior.provenanceCount==0);
  }
  if (samplerOrder==3) {
    // Descriptor equality without a cache-entry token cannot bootstrap provenance.
    o.observe_sampler_descriptor(address(&emptySamplerToken),device,sampler);
    const auto prior=o.snapshot();
    STATIC_GROUP_CONTROL_CHECK(prior.firstBlocker==0 && prior.resourceCount==1 && prior.provenanceCount==0);
    o.observe_renderer_group_set(OwnerObserver::EmptyGroupRole,2,owner,device,address(&emptyGroupToken),address(&emptyLayoutToken),0,nullptr);
    const auto refused=o.snapshot();
    STATIC_GROUP_CONTROL_CHECK(refused.firstBlocker==AURORA_BROWSER_FRAME_EFFECT_RESOURCE_ID_REUSE && refused.resourceCount==1 && refused.bindingEntryCount==0 && refused.layoutEntryCount==0 && refused.provenanceCount==0);
    return true;
  }
  f.bind(f.ids);
  uint32_t offset=256;
  o.observe_renderer_group_set(OwnerObserver::UniformGroupRole,1,owner,device,address(&uniformGroupToken),address(&uniformLayoutToken),1,&offset);
  o.observe_renderer_group_set(OwnerObserver::UniformGroupRole,3,owner,device,address(&uniformGroupToken),address(&uniformLayoutToken),1,&offset);
  o.observe_renderer_group_set(OwnerObserver::EmptyGroupRole,2,owner,device,address(&emptyGroupToken),address(&emptyLayoutToken),0,nullptr);
  STATIC_GROUP_CONTROL_CHECK(o.promote_renderer_buffer(OwnerObserver::IndexBufferRole,0,index));
  STATIC_GROUP_CONTROL_CHECK(o.promote_renderer_buffer(OwnerObserver::StagingBufferRole,1,staging));
  if (samplerOrder==2 && samplerCorruption>=0) {
    const auto prior=o.snapshot();
    const auto altered=aurora::gfx::detail::BrowserFrameEffectObserverControls::corrupt_sampler(creation,unsigned(samplerCorruption));
    auto requested=sampler;
    if(samplerCorruption==5)requested.maxAnisotropy=2;
    if(samplerCorruption==6)requested.nextInChain=reinterpret_cast<const wgpu::ChainedStruct*>(&wrongGroupToken);
    o.observe_cached_sampler_descriptor(altered,requested);
    const auto refused=o.snapshot();
    STATIC_GROUP_CONTROL_CHECK(prior.firstBlocker==0 && refused.firstBlocker!=0 &&
        refused.resourceCount==prior.resourceCount && refused.bindingEntryCount==prior.bindingEntryCount &&
        refused.layoutEntryCount==prior.layoutEntryCount && refused.provenanceCount==prior.provenanceCount);
    return true;
  }
  if (samplerOrder==2) o.observe_cached_sampler_descriptor(creation,sampler);
  const auto before=o.snapshot();
  STATIC_GROUP_CONTROL_CHECK(before.firstBlocker==0 && before.resourceCount==14 && before.provenanceCount==14 && before.layoutEntryCount==OwnerObserver::LayoutEntryCapacity && before.bindingEntryCount==OwnerObserver::LayoutEntryCapacity);
  // Repeated uses do not duplicate descriptors or create-token provenance.
  o.observe_renderer_group_set(OwnerObserver::EmptyGroupRole,2,owner,device,address(&emptyGroupToken),address(&emptyLayoutToken),0,nullptr);
  STATIC_GROUP_CONTROL_CHECK(o.snapshot().resourceCount==before.resourceCount && o.snapshot().provenanceCount==before.provenanceCount);
  o.begin_draw_callback();o.observe_actual_draw(1,3,0,1,0);o.end_draw_callback();
  AuroraBrowserFrameEffectDraw draw{};
  STATIC_GROUP_CONTROL_CHECK(o.draw(0,&draw) && !o.draw(1,&draw));
  STATIC_GROUP_CONTROL_CHECK(draw.bindGroupIds[0]&&draw.bindGroupIds[1]&&draw.bindGroupIds[2]&&draw.bindGroupIds[3]==draw.bindGroupIds[1]);
  STATIC_GROUP_CONTROL_CHECK(draw.dynamicOffsetCount[1]==1&&draw.dynamicOffsets[1][0]==offset&&draw.dynamicOffsets[3][0]==offset);
  std::array<uint64_t,OwnerObserver::WarmOwnerCapacity> serials{};uint64_t epoch=0;
  for(uint32_t i=0;i<before.provenanceCount;++i){AuroraBrowserFrameEffectProvenance row{};STATIC_GROUP_CONTROL_CHECK(o.provenance(i,&row));
    STATIC_GROUP_CONTROL_CHECK(row.creationSerial&&row.rendererEpoch&&row.deviceId&&row.captureId);
    if(!epoch)epoch=row.rendererEpoch;STATIC_GROUP_CONTROL_CHECK(row.rendererEpoch==epoch);
    for(uint32_t j=0;j<i;++j)STATIC_GROUP_CONTROL_CHECK(serials[j]!=row.creationSerial);serials[i]=row.creationSerial;
  }
  uint32_t current=0;uint64_t layoutId=0;
  for(uint32_t i=0;i<before.layoutEntryCount;++i){AuroraBrowserFrameEffectLayoutEntry row{};STATIC_GROUP_CONTROL_CHECK(o.layout_entry(i,&row));
    if(layoutId!=row.layoutId){layoutId=row.layoutId;current=0;}
    STATIC_GROUP_CONTROL_CHECK(row.entryIndex==current&&row.words[0]==current);++current;
    STATIC_GROUP_CONTROL_CHECK(row.words[2]==0&&row.words[3]==0&&row.words[7]==0);
  }
  return true;
}

bool renderer_owner_sampler_order_controls() {
  STATIC_GROUP_CONTROL_CHECK(renderer_owner_graph_positive(1));
  STATIC_GROUP_CONTROL_CHECK(renderer_owner_graph_positive(2));
  STATIC_GROUP_CONTROL_CHECK(renderer_owner_graph_positive(4));
  STATIC_GROUP_CONTROL_CHECK(renderer_owner_graph_positive(3));
  for(int corruption=0;corruption<=6;++corruption) {
    STATIC_GROUP_CONTROL_CHECK(renderer_owner_graph_positive(0,corruption));
    STATIC_GROUP_CONTROL_CHECK(renderer_owner_graph_positive(2,corruption));
  }
  std::puts("actual collector authenticated sampler generic-before-warm, warm-before-generic and untrusted/mutated refusals passed");
  return true;
}

bool renderer_owner_release_and_malformed_controls() {
  {Fixture f;STATIC_GROUP_CONTROL_CHECK(f.observer&&record_complete(f));STATIC_GROUP_CONTROL_CHECK(f.arm_and_begin_frame());f.bind(f.ids);
   f.observer->release_renderer_role(OwnerObserver::StaticVertexBufferRole,0,f.ids.owner,f.ids.vertex);
   STATIC_GROUP_CONTROL_CHECK(f.observer->snapshot().firstBlocker==AURORA_BROWSER_FRAME_EFFECT_RESOURCE_ID_REUSE);}
  {Fixture f;STATIC_GROUP_CONTROL_CHECK(f.observer&&record_complete(f));
   f.observer->release_renderer_role(OwnerObserver::StaticVertexBufferRole,0,f.ids.owner,f.ids.vertex);
   f.remember_buffer(OwnerObserver::StaticVertexBufferRole,f.ids.vertex,f.vertexDescriptor);
   STATIC_GROUP_CONTROL_CHECK(f.arm_and_begin_frame());f.bind(f.ids);
   STATIC_GROUP_CONTROL_CHECK(f.observer->snapshot().firstBlocker==AURORA_BROWSER_FRAME_EFFECT_UNKNOWN_RESOURCE&&f.observer->snapshot().resourceCount==0&&f.observer->snapshot().layoutEntryCount==0);}
  {Fixture f;STATIC_GROUP_CONTROL_CHECK(f.observer&&f.begin());
   const wgpu::BufferDescriptor d{.usage=wgpu::BufferUsage::CopySrc,.size=1};
   f.observer->remember_renderer_buffer(OwnerObserver::StagingBufferRole,OwnerObserver::WarmOwnerCapacity,f.ids.owner,f.ids.device,address(&stagingToken),d);
   STATIC_GROUP_CONTROL_CHECK(f.arm_and_begin_frame());
   STATIC_GROUP_CONTROL_CHECK(!f.observer->promote_renderer_buffer(OwnerObserver::StagingBufferRole,0,address(&stagingToken))&&f.observer->snapshot().resourceCount==0);}
  {Fixture f;STATIC_GROUP_CONTROL_CHECK(f.observer);f.observer->begin_static_resource_lifetime(f.ids.owner,f.ids.device,1);
   const wgpu::BufferDescriptor d{.usage=wgpu::BufferUsage::CopySrc,.size=1};
   f.observer->remember_renderer_buffer(OwnerObserver::StagingBufferRole,1,f.ids.owner,f.ids.device,address(&stagingToken),d);
   STATIC_GROUP_CONTROL_CHECK(f.arm_and_begin_frame());STATIC_GROUP_CONTROL_CHECK(!f.observer->promote_renderer_buffer(OwnerObserver::StagingBufferRole,1,address(&stagingToken)));}
  {Fixture f;STATIC_GROUP_CONTROL_CHECK(f.observer&&f.begin());wgpu::SamplerDescriptor d{};
   auto created=f.observer->observe_sampler_creation(address(&emptySamplerToken),f.ids.device,d);d.maxAnisotropy=2;
   f.observer->select_empty_sampler(f.ids.owner,created,d);
   STATIC_GROUP_CONTROL_CHECK(f.arm_and_begin_frame());STATIC_GROUP_CONTROL_CHECK(!f.observer->promote_renderer_buffer(OwnerObserver::StaticVertexBufferRole,0,f.ids.vertex));}
  return true;
}
} // namespace

namespace {
bool renderer_owner_layout_and_budget_refusals() {
  using Access=aurora::gfx::detail::BrowserFrameEffectObserverControls;
  for (unsigned corruption=0;corruption<8;++corruption) {
    Fixture f;STATIC_GROUP_CONTROL_CHECK(f.observer&&record_complete(f));
    wgpu::BindGroupLayoutEntry entry{.binding=0,.visibility=wgpu::ShaderStage::Vertex|wgpu::ShaderStage::Fragment,.buffer={.type=wgpu::BufferBindingType::Uniform,.hasDynamicOffset=true}};
    wgpu::BindGroupLayoutDescriptor descriptor{.entryCount=1,.entries=&entry};
    const auto chain=reinterpret_cast<const wgpu::ChainedStruct*>(&wrongGroupToken);
    switch(corruption){
      case 0:descriptor.nextInChain=chain;break;
      case 1:entry.nextInChain=chain;break;
      case 2:entry.buffer.nextInChain=chain;break;
      case 3:entry.sampler.nextInChain=chain;break;
      case 4:entry.texture.nextInChain=chain;break;
      case 5:entry.storageTexture.nextInChain=chain;break;
      case 6:descriptor.entryCount=0;break;
      case 7:entry.binding=1;break;
    }
    f.observer->remember_renderer_layout(OwnerObserver::UniformLayoutRole,f.ids.owner,f.ids.device,address(&uniformLayoutToken),descriptor);
    // A later valid descriptor cannot erase the first malformed creation.
    entry.nextInChain=entry.buffer.nextInChain=entry.sampler.nextInChain=entry.texture.nextInChain=entry.storageTexture.nextInChain=nullptr;
    entry.binding=0;descriptor.nextInChain=nullptr;descriptor.entryCount=1;
    f.observer->remember_renderer_layout(OwnerObserver::UniformLayoutRole,f.ids.owner,f.ids.device,address(&uniformLayoutToken),descriptor);
    STATIC_GROUP_CONTROL_CHECK(f.arm_and_begin_frame());f.bind(f.ids);
    const auto r=f.observer->snapshot();STATIC_GROUP_CONTROL_CHECK(r.firstBlocker==AURORA_BROWSER_FRAME_EFFECT_UNKNOWN_RESOURCE&&r.resourceCount==0&&r.bindingEntryCount==0&&r.layoutEntryCount==0&&r.provenanceCount==0);
  }
  for(unsigned budget=0;budget<2;++budget){Fixture f;STATIC_GROUP_CONTROL_CHECK(f.observer&&record_complete(f));STATIC_GROUP_CONTROL_CHECK(f.arm_and_begin_frame());
    if(budget==0)Access::exhaust_layout_rows(*f.observer);else Access::exhaust_provenance_rows(*f.observer);
    f.bind(f.ids);STATIC_GROUP_CONTROL_CHECK(f.observer->snapshot().firstBlocker==AURORA_BROWSER_FRAME_EFFECT_DESCRIPTOR_OVERFLOW&&f.observer->snapshot().resourceCount==0&&f.observer->snapshot().bindingEntryCount==0);
  }
  {Fixture f;STATIC_GROUP_CONTROL_CHECK(f.observer&&f.begin());Access::exhaust_serials(*f.observer);f.remember_both();
    STATIC_GROUP_CONTROL_CHECK(f.arm_and_begin_frame());f.bind(f.ids);STATIC_GROUP_CONTROL_CHECK(f.observer->snapshot().firstBlocker==AURORA_BROWSER_FRAME_EFFECT_RESOURCE_ID_REUSE&&f.observer->snapshot().resourceCount==0);}
  {Fixture f;STATIC_GROUP_CONTROL_CHECK(f.observer&&record_complete(f));const wgpu::BufferDescriptor d{.usage=wgpu::BufferUsage::CopySrc,.size=1};
    f.observer->remember_renderer_buffer(OwnerObserver::StagingBufferRole,0,f.ids.owner,f.ids.device,address(&stagingToken),d);
    STATIC_GROUP_CONTROL_CHECK(f.arm_and_begin_frame());
    STATIC_GROUP_CONTROL_CHECK(!f.observer->promote_renderer_copy(0,address(&stagingToken),OwnerObserver::IndexBufferRole,address(&indexToken))&&f.observer->snapshot().resourceCount==0);}
  {Fixture f;STATIC_GROUP_CONTROL_CHECK(f.observer&&record_complete(f));STATIC_GROUP_CONTROL_CHECK(f.arm_and_begin_frame());
    std::array<uint32_t,1> words{0};
    for(uintptr_t i=1;i<6141;++i)f.observer->observe_resource_descriptor(AURORA_BROWSER_FRAME_EFFECT_HANDLE_BUFFER,UINTPTR_MAX-i,f.ids.device,words.data(),1);
    const auto before=f.observer->snapshot();f.bind(f.ids);const auto after=f.observer->snapshot();
    STATIC_GROUP_CONTROL_CHECK(after.firstBlocker==AURORA_BROWSER_FRAME_EFFECT_DESCRIPTOR_OVERFLOW&&after.resourceCount==before.resourceCount&&after.bindingEntryCount==0&&after.layoutEntryCount==0&&after.provenanceCount==0);}
  return true;
}
}

namespace {
struct AttachmentFixture {
  Fixture base;
  std::array<int,3> owners{},textures{},views{};
  wgpu::TextureDescriptor texture{.usage=wgpu::TextureUsage::RenderAttachment|wgpu::TextureUsage::TextureBinding|wgpu::TextureUsage::CopySrc|wgpu::TextureUsage::CopyDst,
    .dimension=wgpu::TextureDimension::e2D,.size={640,480,1},.format=wgpu::TextureFormat::RGBA8Unorm,.mipLevelCount=1,.sampleCount=1};
  wgpu::TextureViewDescriptor view{.dimension=wgpu::TextureViewDimension::e2D};
  void create(size_t slot,uintptr_t device=0) {
    auto& o=*base.observer;if(!device)device=base.ids.device;
    o.remember_persistent_attachment_texture(slot,address(&owners[slot]),device,address(&textures[slot]),texture);
    o.remember_persistent_attachment_view(slot,address(&owners[slot]),device,address(&views[slot]),address(&textures[slot]),view);
  }
  void release(size_t slot) {
    base.observer->release_persistent_attachment(slot,address(&owners[slot]),address(&textures[slot]),address(&views[slot]));
  }
  bool start(size_t count=3) {
    if(!base.observer)return false;
    base.observer->begin_persistent_attachment_lifetime(base.ids.device);
    for(size_t slot=0;slot<count;++slot)create(slot);
    return true;
  }
};
void observe_attachment_load_pass(AttachmentFixture& f) {
  // Actual collector/API path; fake identity wrappers are detached before any
  // destructor, so no WebGPU service/refcount calls or command encoding occur.
  aurora::gfx::detail::FramePacket frame;frame.frameIndex=3158;frame.frameId=3159;
  aurora::gfx::detail::RenderPass pass;pass.colorAttachmentCount=1;
  for (size_t i=0;i<23;++i) {
    const aurora::gfx::detail::Command::Data data{.debugMarkerIndex=i};
    pass.commands.push_back({.type=aurora::gfx::detail::CommandType::DebugMarker,.data=data});
  }
  pass.hasDepth=true;pass.hasStencil=true;
  pass.colorAttachments[0].size={640,480,1};pass.colorAttachments[0].clear=false;
  pass.colorAttachments[0].view=wgpu::TextureView::Acquire(reinterpret_cast<WGPUTextureView>(address(&f.views[0])));
  pass.colorAttachments[0].resolveView=wgpu::TextureView::Acquire(reinterpret_cast<WGPUTextureView>(address(&f.views[1])));
  std::array<wgpu::RenderPassColorAttachment,aurora::gfx::MaxColorAttachments> colors{};
  colors[0].loadOp=wgpu::LoadOp::Load;colors[0].storeOp=wgpu::StoreOp::Store;
  colors[0].clearValue={std::bit_cast<double>(uint64_t{0x8000000000000000}),std::bit_cast<double>(uint64_t{0x7ff8000000000001}),0.0,1.0};
  colors[0].view=wgpu::TextureView::Acquire(reinterpret_cast<WGPUTextureView>(address(&f.views[0])));
  colors[0].resolveTarget=wgpu::TextureView::Acquire(reinterpret_cast<WGPUTextureView>(address(&f.views[1])));
  wgpu::RenderPassDepthStencilAttachment depth{};depth.depthLoadOp=wgpu::LoadOp::Load;depth.depthStoreOp=wgpu::StoreOp::Store;
  depth.depthClearValue=std::bit_cast<float>(uint32_t{0x80000000});depth.stencilClearValue=UINT32_MAX;
  depth.view=wgpu::TextureView::Acquire(reinterpret_cast<WGPUTextureView>(address(&f.views[2])));
  f.base.observer->observe_render_pass(frame,0,pass,colors,&depth);
  (void)pass.colorAttachments[0].view.MoveToCHandle();(void)pass.colorAttachments[0].resolveView.MoveToCHandle();
  (void)colors[0].view.MoveToCHandle();(void)colors[0].resolveTarget.MoveToCHandle();(void)depth.view.MoveToCHandle();
}
bool persistent_attachment_controls() {
  using O=BrowserFrameEffectObserver;using Access=aurora::gfx::detail::BrowserFrameEffectObserverControls;
  {
    AttachmentFixture f;STATIC_GROUP_CONTROL_CHECK(f.start());auto& o=*f.base.observer;
    std::array<uint64_t,6> created{};
    for(size_t slot=0;slot<3;++slot){created[slot*2]=Access::attachment_serial(o,O::PersistentAttachmentTextureRole,slot);created[slot*2+1]=Access::attachment_serial(o,O::PersistentAttachmentViewRole,slot);}
    STATIC_GROUP_CONTROL_CHECK(f.base.begin());
    for(size_t slot=0;slot<3;++slot)STATIC_GROUP_CONTROL_CHECK(created[slot*2]==Access::attachment_serial(o,O::PersistentAttachmentTextureRole,slot));
    STATIC_GROUP_CONTROL_CHECK(f.base.arm_and_begin_frame());observe_attachment_load_pass(f);
    const auto observed=o.snapshot();STATIC_GROUP_CONTROL_CHECK(observed.schemaVersion==5&&observed.firstBlocker==AURORA_BROWSER_FRAME_EFFECT_LOAD_ATTACHMENT);
    AuroraBrowserFrameEffectAttachmentFact color{},depth{};
    STATIC_GROUP_CONTROL_CHECK(observed.attachmentFactCount==2&&o.attachment_fact(0,&color)&&o.attachment_fact(1,&depth)&&!o.attachment_fact(2,&depth)&&!o.attachment_fact(0,nullptr));
    STATIC_GROUP_CONTROL_CHECK(color.kind==1&&color.aspects==1&&color.colorClearBits[0]==0x8000000000000000ULL&&color.colorClearBits[1]==0x7ff8000000000001ULL&&color.colorClearBits[3]==0x3ff0000000000000ULL);
    STATIC_GROUP_CONTROL_CHECK(depth.kind==2&&depth.aspects==6&&depth.depthClearBits==0x80000000u&&depth.stencilClearValue==UINT32_MAX);
    AuroraBrowserFrameEffectEvent ce{},de{};STATIC_GROUP_CONTROL_CHECK(o.event(color.eventIndex,&ce)&&o.event(depth.eventIndex,&de)&&ce.kind==3&&de.kind==4&&ce.handleA==color.viewId&&de.handleA==depth.viewId);
    STATIC_GROUP_CONTROL_CHECK(observed.resourceCount==6&&observed.provenanceCount==6&&observed.layoutEntryCount==0&&observed.bindingEntryCount==0);
    for(uint32_t i=0;i<6;i+=2){AuroraBrowserFrameEffectResource texture{},view{};AuroraBrowserFrameEffectProvenance tp{},vp{};
      STATIC_GROUP_CONTROL_CHECK(o.resource(i,&texture)&&o.resource(i+1,&view)&&o.provenance(i,&tp)&&o.provenance(i+1,&vp));
      STATIC_GROUP_CONTROL_CHECK(texture.kind==AURORA_BROWSER_FRAME_EFFECT_HANDLE_TEXTURE&&texture.wordCount==10&&texture.words[0]==640&&texture.words[1]==480&&texture.words[2]==1);
      STATIC_GROUP_CONTROL_CHECK(view.kind==AURORA_BROWSER_FRAME_EFFECT_HANDLE_TEXTURE_VIEW&&view.wordCount==10&&view.ownerId==texture.captureId&&view.words[0]==texture.captureId&&view.words[1]==0);
      STATIC_GROUP_CONTROL_CHECK(tp.role==15&&vp.role==16&&tp.slot==i/2&&vp.slot==tp.slot&&tp.rendererEpoch==vp.rendererEpoch&&tp.rendererEpoch!=0);
      STATIC_GROUP_CONTROL_CHECK(tp.creationSerial==created[i]&&vp.creationSerial==created[i+1]&&tp.deviceId==vp.deviceId);
    }
    STATIC_GROUP_CONTROL_CHECK(o.promote_persistent_attachment_view(address(&f.views[0]))&&o.snapshot().resourceCount==6&&o.snapshot().provenanceCount==6);
    STATIC_GROUP_CONTROL_CHECK((observed.findingMask&(uint64_t{1}<<AURORA_BROWSER_FRAME_EFFECT_LOAD_ATTACHMENT))!=0);
  }
  for(unsigned mode=0;mode<12;++mode){
    AttachmentFixture f;STATIC_GROUP_CONTROL_CHECK(f.start(1));auto& o=*f.base.observer;
    uint32_t expected=AURORA_BROWSER_FRAME_EFFECT_RESOURCE_ID_REUSE;
    switch(mode){
      case 0:f.create(0);break; // duplicate actual creation, never cache observation
      case 1:f.release(0);f.release(0);break;
      case 2:o.release_persistent_attachment(0,address(&wrongOwnerToken),address(&f.textures[0]),address(&f.views[0]));break;
      case 3:Access::stale_attachment_parent(o,0);expected=AURORA_BROWSER_FRAME_EFFECT_UNKNOWN_RESOURCE;break;
      case 4:o.end_persistent_attachment_lifetime(address(&wrongDeviceToken));break;
      case 5:o.begin_persistent_attachment_lifetime(address(&wrongDeviceToken));break;
      case 6:o.end_persistent_attachment_lifetime(f.base.ids.device);expected=AURORA_BROWSER_FRAME_EFFECT_RESOURCE_ID_REUSE;break;
      case 7:o.remember_persistent_attachment_view(1,address(&f.owners[1]),f.base.ids.device,address(&f.views[1]),address(&wrongVertexToken),f.view);expected=AURORA_BROWSER_FRAME_EFFECT_UNKNOWN_RESOURCE;break;
      case 8:o.remember_persistent_attachment_view(1,address(&f.owners[1]),address(&wrongDeviceToken),address(&f.views[1]),address(&f.textures[0]),f.view);expected=AURORA_BROWSER_FRAME_EFFECT_UNKNOWN_RESOURCE;break;
      case 9:{auto d=f.texture;d.nextInChain=reinterpret_cast<const wgpu::ChainedStruct*>(&wrongGroupToken);o.remember_persistent_attachment_texture(1,address(&f.owners[1]),f.base.ids.device,address(&f.textures[1]),d);o.remember_persistent_attachment_texture(1,address(&f.owners[1]),f.base.ids.device,address(&f.textures[1]),f.texture);expected=AURORA_BROWSER_FRAME_EFFECT_UNKNOWN_RESOURCE;break;}
      case 10:{auto d=f.view;d.nextInChain=reinterpret_cast<const wgpu::ChainedStruct*>(&wrongGroupToken);o.remember_persistent_attachment_view(1,address(&f.owners[1]),f.base.ids.device,address(&f.views[1]),address(&f.textures[1]),d);o.remember_persistent_attachment_texture(1,address(&f.owners[1]),f.base.ids.device,address(&f.textures[1]),f.texture);expected=AURORA_BROWSER_FRAME_EFFECT_UNKNOWN_RESOURCE;break;}
      case 11:o.remember_persistent_attachment_texture(3,address(&wrongOwnerToken),f.base.ids.device,address(&wrongVertexToken),f.texture);expected=AURORA_BROWSER_FRAME_EFFECT_UNKNOWN_RESOURCE;break;
    }
    STATIC_GROUP_CONTROL_CHECK(f.base.begin()&&f.base.arm_and_begin_frame());
    STATIC_GROUP_CONTROL_CHECK(!o.promote_persistent_attachment_view(address(&f.views[0])));
    const auto r=o.snapshot();STATIC_GROUP_CONTROL_CHECK(r.firstBlocker==expected&&r.resourceCount==0&&r.provenanceCount==0&&r.bindingEntryCount==0&&r.layoutEntryCount==0);
  }
  for(unsigned mode=0;mode<4;++mode){
    AttachmentFixture f;STATIC_GROUP_CONTROL_CHECK(f.start(1)&&f.base.begin()&&f.base.arm_and_begin_frame());auto& o=*f.base.observer;
    if(mode==0)f.release(0);
    else if(mode==1)f.create(0); // reused raw during active window refuses before allocating a serial
    else if(mode==2)o.end_persistent_attachment_lifetime(f.base.ids.device);
    else o.end_persistent_attachment_lifetime(address(&wrongDeviceToken));
    STATIC_GROUP_CONTROL_CHECK(!o.promote_persistent_attachment_view(address(&f.views[0]))&&o.snapshot().firstBlocker==AURORA_BROWSER_FRAME_EFFECT_RESOURCE_ID_REUSE&&o.snapshot().resourceCount==0&&o.snapshot().provenanceCount==0);
  }
  {AttachmentFixture f;STATIC_GROUP_CONTROL_CHECK(f.start(1));const auto prior=Access::attachment_serial(*f.base.observer,O::PersistentAttachmentTextureRole,0);
    f.release(0);f.create(0);STATIC_GROUP_CONTROL_CHECK(Access::attachment_serial(*f.base.observer,O::PersistentAttachmentTextureRole,0)!=prior);
    STATIC_GROUP_CONTROL_CHECK(f.base.begin()&&f.base.arm_and_begin_frame());STATIC_GROUP_CONTROL_CHECK(f.base.observer->promote_persistent_attachment_view(address(&f.views[0])));}
  {AttachmentFixture f;STATIC_GROUP_CONTROL_CHECK(f.start(1)&&f.base.begin()&&f.base.arm_and_begin_frame());
    STATIC_GROUP_CONTROL_CHECK(!f.base.observer->promote_persistent_attachment_view(address(&wrongVertexToken))&&f.base.observer->snapshot().firstBlocker==AURORA_BROWSER_FRAME_EFFECT_UNKNOWN_RESOURCE&&f.base.observer->snapshot().resourceCount==0);}
  {AttachmentFixture f;STATIC_GROUP_CONTROL_CHECK(f.start(1));f.base.ids.device=address(&wrongDeviceToken);
    STATIC_GROUP_CONTROL_CHECK(f.base.begin()&&f.base.arm_and_begin_frame());STATIC_GROUP_CONTROL_CHECK(!f.base.observer->promote_persistent_attachment_view(address(&f.views[0]))&&f.base.observer->snapshot().firstBlocker==AURORA_BROWSER_FRAME_EFFECT_RESOURCE_ID_REUSE&&f.base.observer->snapshot().resourceCount==0);}
  {AttachmentFixture f;STATIC_GROUP_CONTROL_CHECK(f.start(1));auto& o=*f.base.observer;
    const auto prior=Access::attachment_serial(o,O::PersistentAttachmentTextureRole,0);
    f.release(0);o.end_persistent_attachment_lifetime(f.base.ids.device);
    f.base.ids.device=address(&wrongDeviceToken);o.begin_persistent_attachment_lifetime(f.base.ids.device);f.create(0);
    STATIC_GROUP_CONTROL_CHECK(prior!=Access::attachment_serial(o,O::PersistentAttachmentTextureRole,0));
    STATIC_GROUP_CONTROL_CHECK(f.base.begin()&&f.base.arm_and_begin_frame()&&o.promote_persistent_attachment_view(address(&f.views[0])));
    AuroraBrowserFrameEffectProvenance parent{};STATIC_GROUP_CONTROL_CHECK(o.provenance(0,&parent)&&parent.role==15&&parent.creationSerial!=prior);}
  for(bool parentKnown:{false,true}){AttachmentFixture f;STATIC_GROUP_CONTROL_CHECK(f.base.begin()&&f.base.arm_and_begin_frame());auto& o=*f.base.observer;
    if(parentKnown)o.observe_texture_descriptor(address(&f.textures[0]),f.base.ids.device,f.texture);
    o.observe_texture_view_descriptor(address(&f.views[0]),address(&f.textures[0]),f.base.ids.device,&f.view);
    const auto before=o.snapshot();STATIC_GROUP_CONTROL_CHECK(o.promote_persistent_attachment_view(address(&f.views[0]))==parentKnown);
    STATIC_GROUP_CONTROL_CHECK(o.snapshot().resourceCount==before.resourceCount&&o.snapshot().provenanceCount==0);
    STATIC_GROUP_CONTROL_CHECK(parentKnown?o.snapshot().firstBlocker==0:o.snapshot().firstBlocker==AURORA_BROWSER_FRAME_EFFECT_UNKNOWN_RESOURCE);}
  {AttachmentFixture f;STATIC_GROUP_CONTROL_CHECK(f.start(1)&&f.base.begin()&&f.base.arm_and_begin_frame());auto& o=*f.base.observer;
    Access::exhaust_provenance_rows(o);const auto before=o.snapshot();
    STATIC_GROUP_CONTROL_CHECK(!o.promote_persistent_attachment_view(address(&f.views[0]))&&o.snapshot().firstBlocker==AURORA_BROWSER_FRAME_EFFECT_DESCRIPTOR_OVERFLOW);
    STATIC_GROUP_CONTROL_CHECK(o.snapshot().resourceCount==0&&o.snapshot().provenanceCount==before.provenanceCount&&o.snapshot().bindingEntryCount==0&&o.snapshot().layoutEntryCount==0);}
  std::puts("actual persistent EFB attachment pre-renderer creation/LOAD parent provenance and lifecycle refusals passed");
  return true;
}
}

namespace {
using TextureOwnerPair=aurora::gfx::detail::TextureOwnerPair;
using TextureCreation=aurora::gfx::detail::TextureCreation;
struct TextureOwnerFixture {
  Fixture base;
  std::array<int,8> textures{},views{},owners{};
  wgpu::TextureDescriptor descriptor{.usage=wgpu::TextureUsage::TextureBinding|wgpu::TextureUsage::RenderAttachment,
      .dimension=wgpu::TextureDimension::e2D,.size={640,480,1},.format=wgpu::TextureFormat::RGBA8Unorm,.mipLevelCount=1,.sampleCount=1};
  wgpu::TextureViewDescriptor viewDescriptor{.dimension=wgpu::TextureViewDimension::e2D};
  bool start(){if(!base.observer)return false;base.observer->begin_persistent_attachment_lifetime(base.ids.device);return true;}
  TextureOwnerPair create(size_t index,uint32_t role=17,uint32_t slot=0) {
    auto& o=*base.observer;
    const auto texture=o.observe_owned_texture_creation(role,slot,address(&textures[index]),base.ids.device,descriptor);
    auto pair=o.observe_owned_texture_view_creation(texture,address(&views[index]),address(&textures[index]),base.ids.device,&viewDescriptor);
    if(role==17)o.bind_owned_texture_ref(address(&owners[index]),pair,address(&textures[index]),address(&views[index]),address(&views[index]));
    return pair;
  }
  bool promote(const TextureOwnerPair& pair,size_t index,uint32_t role=17,uint32_t frameIndex=3158,uint64_t frameId=3159) {
    return base.observer->promote_owned_texture_pair(pair,role==17?address(&owners[index]):0,address(&textures[index]),address(&views[index]),frameIndex,frameId);
  }
};
bool copied_texture_owner_controls(){
  using Access=aurora::gfx::detail::BrowserFrameEffectObserverControls;
  // Actual constructor/destructor and exact extracted production bridge. Opaque
  // wrappers are relinquished before final destruction; no WebGPU calls occur.
  {
    int textureRaw,viewRaw,ownerRaw,deviceRaw;
    auto& o=aurora::gfx::detail::browser_frame_effect_observer();
    const auto device=address(&deviceRaw);
    o.begin_persistent_attachment_lifetime(device);
    const wgpu::TextureDescriptor d{.usage=wgpu::TextureUsage::TextureBinding,.dimension=wgpu::TextureDimension::e2D,
      .size={8,8,1},.format=wgpu::TextureFormat::RGBA8Unorm,.mipLevelCount=1,.sampleCount=1};
    const auto t=o.observe_owned_texture_creation(17,0,address(&textureRaw),device,d);
    const auto pair=o.observe_owned_texture_view_creation(t,address(&viewRaw),address(&textureRaw),device,nullptr);
    auto ref=std::make_shared<aurora::gfx::TextureRef>(
      wgpu::Texture::Acquire(reinterpret_cast<WGPUTexture>(address(&textureRaw))),
      wgpu::TextureView::Acquire(reinterpret_cast<WGPUTextureView>(address(&viewRaw))),
      wgpu::TextureView::Acquire(reinterpret_cast<WGPUTextureView>(address(&viewRaw))),d.size,d.format,1,0,pair);
    auto alias=ref;const auto actualOwner=address(ref.get());
    o.begin_static_resource_lifetime(address(&ownerRaw),device);
    STATIC_GROUP_CONTROL_CHECK(o.arm_first_draw(3158,3159));
    STATIC_GROUP_CONTROL_CHECK(o.promote_owned_texture_pair(ref->frameEffectOwnerFacts,actualOwner,address(&textureRaw),address(&viewRaw),3158,3159));
    ref.reset();STATIC_GROUP_CONTROL_CHECK(o.snapshot().firstBlocker==0);
    (void)alias->texture.MoveToCHandle();(void)alias->sampleTextureView.MoveToCHandle();(void)alias->attachmentTextureView.MoveToCHandle();
    alias.reset();STATIC_GROUP_CONTROL_CHECK(o.snapshot().firstBlocker==AURORA_BROWSER_FRAME_EFFECT_RESOURCE_ID_REUSE);
  }
  for(uint32_t role:{17u,19u,21u,23u,25u})for(bool armed:{false,true}) {
    TextureOwnerFixture f;STATIC_GROUP_CONTROL_CHECK(f.start());const uint32_t slot=role==19||role==21?3:0;
    const auto pair=f.create(0,role,slot);const auto other=f.create(1,role,slot);
    STATIC_GROUP_CONTROL_CHECK(f.base.begin());
    if(armed)STATIC_GROUP_CONTROL_CHECK(f.base.observer->arm_first_draw(3158,3159));
    else STATIC_GROUP_CONTROL_CHECK(f.base.arm_and_begin_frame());
    STATIC_GROUP_CONTROL_CHECK(f.promote(pair,0,role)&&f.promote(other,1,role)&&f.promote(pair,0,role));
    auto& o=*f.base.observer;STATIC_GROUP_CONTROL_CHECK(o.snapshot().resourceCount==4&&o.snapshot().provenanceCount==4);
    AuroraBrowserFrameEffectResource t{},v{};AuroraBrowserFrameEffectProvenance tp{},vp{},op{};
    STATIC_GROUP_CONTROL_CHECK(o.resource(0,&t)&&o.resource(1,&v)&&o.provenance(0,&tp)&&o.provenance(1,&vp)&&o.provenance(2,&op));
    STATIC_GROUP_CONTROL_CHECK(tp.role==role&&vp.role==role+1&&tp.slot==slot&&vp.slot==slot&&tp.creationSerial!=op.creationSerial&&vp.creationSerial!=tp.creationSerial);
    STATIC_GROUP_CONTROL_CHECK(v.ownerId==t.captureId&&v.words[0]==t.captureId&&tp.deviceId==vp.deviceId&&tp.rendererEpoch==vp.rendererEpoch);
    if(armed){o.observe_command(3158,3159,0,0,0,false);STATIC_GROUP_CONTROL_CHECK(o.snapshot().resourceCount==4&&o.snapshot().state==AURORA_BROWSER_FRAME_EFFECT_COLLECTING);}
  }
  // Exact token mutations are refusals, never a descriptor-to-provenance upgrade.
  for(unsigned mutation=0;mutation<9;++mutation){
    TextureOwnerFixture f;STATIC_GROUP_CONTROL_CHECK(f.start());auto pair=f.create(0);
    pair=Access::corrupt_texture_owner(pair,mutation);STATIC_GROUP_CONTROL_CHECK(f.base.begin()&&f.base.arm_and_begin_frame());
    STATIC_GROUP_CONTROL_CHECK(!f.promote(pair,0));const auto s=f.base.observer->snapshot();
    STATIC_GROUP_CONTROL_CHECK(s.firstBlocker==AURORA_BROWSER_FRAME_EFFECT_RESOURCE_ID_REUSE&&s.resourceCount==0&&s.provenanceCount==0&&s.bindingEntryCount==0&&s.layoutEntryCount==0);
  }
  for(unsigned mode=0;mode<10;++mode){
    TextureOwnerFixture f;STATIC_GROUP_CONTROL_CHECK(f.start());const auto pair=f.create(0,mode==2||mode==3?19:17);
    auto& o=*f.base.observer;
    if(mode==0){o.end_persistent_attachment_lifetime(f.base.ids.device);o.begin_persistent_attachment_lifetime(f.base.ids.device);}
    if(mode==2)o.invalidate_texture_owner_domain(1);
    STATIC_GROUP_CONTROL_CHECK(f.base.begin()&&f.base.arm_and_begin_frame());const auto before=o.snapshot();
    if(mode==1)o.detach_owned_texture_ref(address(&f.owners[0]),pair);
    if(mode==3)o.invalidate_texture_owner_domain(1);
    if(mode==4)o.detach_owned_texture_ref(address(&wrongOwnerToken),pair);
    if(mode==5)Access::exhaust_provenance_rows(o);
    if(mode==6)o.observe_texture_descriptor(address(&f.textures[0]),f.base.ids.device,f.descriptor);
    bool accepted=false;
    if(mode==7)accepted=o.promote_owned_texture_pair(pair,address(&f.owners[0]),address(&wrongVertexToken),address(&f.views[0]),3158,3159);
    else if(mode==8)accepted=f.promote(pair,0,17,3157,3159);
    else if(mode==9)accepted=f.promote(pair,0,17,3158,3160);
    else accepted=f.promote(pair,0,mode==2||mode==3?19:17);
    STATIC_GROUP_CONTROL_CHECK(!accepted);
    const auto after=o.snapshot();const auto expected=mode==5?AURORA_BROWSER_FRAME_EFFECT_DESCRIPTOR_OVERFLOW:mode>=8?AURORA_BROWSER_FRAME_EFFECT_FRAME_ID_MISMATCH:AURORA_BROWSER_FRAME_EFFECT_RESOURCE_ID_REUSE;
    STATIC_GROUP_CONTROL_CHECK(after.firstBlocker==expected&&after.resourceCount==(mode==6?1:before.resourceCount)&&after.provenanceCount==(mode==5?BrowserFrameEffectObserver::ResourceCapacity:before.provenanceCount)&&after.bindingEntryCount==0&&after.layoutEntryCount==0);
  }
  {TextureOwnerFixture f;STATIC_GROUP_CONTROL_CHECK(f.start());const auto pair=f.create(0);STATIC_GROUP_CONTROL_CHECK(f.base.begin()&&f.base.arm_and_begin_frame()&&f.promote(pair,0));
    const auto replacement=f.create(0);const auto before=f.base.observer->snapshot();STATIC_GROUP_CONTROL_CHECK(!f.promote(replacement,0));
    const auto after=f.base.observer->snapshot();STATIC_GROUP_CONTROL_CHECK(after.firstBlocker==AURORA_BROWSER_FRAME_EFFECT_RESOURCE_ID_REUSE&&after.resourceCount==before.resourceCount&&after.provenanceCount==before.provenanceCount);}
  {TextureOwnerFixture f;STATIC_GROUP_CONTROL_CHECK(f.start());auto t=f.base.observer->observe_owned_texture_creation(19,4,address(&f.textures[0]),f.base.ids.device,f.descriptor);
    auto bad=f.base.observer->observe_owned_texture_view_creation(t,address(&f.views[0]),address(&f.textures[0]),f.base.ids.device,&f.viewDescriptor);
    STATIC_GROUP_CONTROL_CHECK(f.base.begin()&&f.base.arm_and_begin_frame()&&!f.promote(bad,0,19)&&f.base.observer->snapshot().firstBlocker==AURORA_BROWSER_FRAME_EFFECT_UNKNOWN_RESOURCE);}
  {TextureOwnerFixture f;STATIC_GROUP_CONTROL_CHECK(f.start());const auto t=f.base.observer->observe_owned_texture_creation(17,0,address(&f.textures[0]),f.base.ids.device,f.descriptor);
    const auto bad=f.base.observer->observe_owned_texture_view_creation(t,address(&f.views[0]),address(&wrongVertexToken),f.base.ids.device,&f.viewDescriptor);
    STATIC_GROUP_CONTROL_CHECK(f.base.begin()&&f.base.arm_and_begin_frame()&&!f.promote(bad,0)&&f.base.observer->snapshot().firstBlocker==AURORA_BROWSER_FRAME_EFFECT_UNKNOWN_RESOURCE);}
  {TextureOwnerFixture f;STATIC_GROUP_CONTROL_CHECK(f.start());Access::exhaust_serials(*f.base.observer);const auto bad=f.create(0);
    STATIC_GROUP_CONTROL_CHECK(f.base.begin()&&f.base.arm_and_begin_frame()&&!f.promote(bad,0)&&f.base.observer->snapshot().firstBlocker==AURORA_BROWSER_FRAME_EFFECT_RESOURCE_ID_REUSE);}
  std::puts("actual TextureRef final-owner detach, live copied/pool token ARMED/COLLECTING provenance and exact identity/domain/budget refusals passed");
  return true;
}
}

int main(){
 if(!copied_texture_owner_controls() || !persistent_attachment_controls() || !renderer_owner_sampler_order_controls() || !renderer_owner_layout_and_budget_refusals() || !renderer_owner_graph_positive() || !renderer_owner_release_and_malformed_controls() || static_group_descriptor_controls() || selected_completion(false)||selected_completion(true)||unknown_identity())return 1;
 std::puts("first-draw actual collector issuing identity and selected map-owner controls passed");
}
