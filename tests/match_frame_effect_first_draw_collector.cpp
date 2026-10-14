// Actual private collector control. No WebGPU service or command encoding is
// fabricated: these are diagnostic event/callback association inputs only.
#include <array>
#include <limits>
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
  STATIC_GROUP_CONTROL_CHECK(summary.resourceCount == 3 && summary.bindingEntryCount == 2);
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

int main(){
 if(static_group_descriptor_controls() || selected_completion(false)||selected_completion(true)||unknown_identity())return 1;
 std::puts("first-draw actual collector issuing identity and selected map-owner controls passed");
}
