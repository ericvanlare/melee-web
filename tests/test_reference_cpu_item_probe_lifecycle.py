"""Exercise the bounded Link Arrow observer path with an isolated guest-memory model."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp"


def _method(source: str, start: str, end: str) -> str:
    return source[source.index(start):source.index(end, source.index(start))]


class ReferenceCpuItemProbeLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang++") or shutil.which("g++")
        if compiler is None:
            raise unittest.SkipTest("A native C++ compiler is required")
        source = SOURCE.read_text(encoding="utf-8")
        constants = "\n".join(
            line.strip() for line in source.splitlines()
            if line.strip().startswith("constexpr") and any(f" {name} " in line for name in (
                "ITEM_PROBE_MAX_RECORDS", "ITEM_PROBE_MAX_PROCESSES", "ITEM_PROBE_ARROW_GOBJ",
                "ITEM_PROBE_ARROW_ITEM", "ITEM_PROBE_ARROW_KIND",
                "ITEM_PROBE_MAX_JSON_BYTES", "CPU_PROBE_MAX_JSON_BYTES"))
        )
        records = source[
            source.index("enum class ItemProbeEvent"):
            source.index("constexpr bool IsGuestRange", source.index("enum class ItemProbeEvent"))
        ]
        ranges = _method(source, "constexpr bool IsGuestRange", "void PutU16")
        json_helpers = "\n".join((
            _method(source, "std::string JsonEscape", "std::string Env"),
            _method(source, "bool AppendBounded", "bool AppendHex"),
            _method(source, "bool AppendHex", "bool AppendHexBytes"),
            _method(source, "bool AppendHexBytes", "}  // namespace"),
        ))
        item_methods = _method(source, "  void CloseItemProbe", "  bool AddSlice")
        build_json = _method(source, "  bool BuildItemProbeJson", "  bool WriteItemProbe")
        harness = r'''
#include <array>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

namespace Core {
struct Memory {
  std::vector<u8> bytes = std::vector<u8>(0x01800000);
  u8* GetPointerForRange(u32 address, size_t size) {
    if (address < 0x80000000U ||
        static_cast<u64>(address) - 0x80000000U + size > bytes.size())
      return nullptr;
    return bytes.data() + (address - 0x80000000U);
  }
};
struct System { Memory memory; Memory& GetMemory() { return memory; } };
}
namespace PowerPC {
struct PairedSingle { u64 value = 0; u64 PS0AsU64() const { return value; } };
struct PowerPCState { u32 gpr[32]{}; u32 spr[9]{}; PairedSingle ps[32]{}; };
}

namespace ReferenceCapture {
namespace {
''' + constants + records + ranges + json_helpers + r'''
}

constexpr char EXPECTED_DOL_SHA256[] =
    "dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646";
struct ProbeHarness {
  void SetInvalid(const std::string& reason) {
    if (!invalid) error = reason;
    invalid = true;
  }
  bool ReadU32(Core::System* system, u32 address, u32* value) const {
    u8* bytes = system->GetMemory().GetPointerForRange(address, 4);
    if (!IsGuestRange(address, 4) || !bytes) return false;
    *value = ReadBE32(bytes);
    return true;
  }
  bool ReadMem1(Core::System* system, u32 address, size_t size, u8* destination) const {
    u8* bytes = system->GetMemory().GetPointerForRange(address, size);
    if (!IsMem1Range(address, size) || !bytes) return false;
    std::memcpy(destination, bytes, size);
    return true;
  }
  bool ReadBytes(Core::System* system, u32 address, size_t size, u8* destination) const {
    u8* bytes = system->GetMemory().GetPointerForRange(address, size);
    if (!IsGuestRange(address, size) || !bytes) return false;
    std::memcpy(destination, bytes, size);
    return true;
  }

  bool invalid = false;
  std::string error;
  bool item_probe_configured = true;
  bool item_probe_valid = true;
  bool item_probe_closed = false;
  std::atomic<bool> item_probe_published{false};
  u32 item_probe_match = 1;
  u32 item_probe_first_tick = 11144;
  u32 item_probe_last_tick = 11159;
  bool item_probe_trigger_on_arrow_creation = false;
  bool item_probe_trigger_on_arrow_launch = false;
  bool item_probe_window_triggered = false;
  u32 item_probe_capture_ticks = 0;
  size_t item_probe_record_count = 0;
  std::unique_ptr<ItemProbeRecord[]> item_probe_records =
      std::make_unique<ItemProbeRecord[]>(ITEM_PROBE_MAX_RECORDS);
  std::array<bool, 16> item_probe_pair_active{};
  std::array<u32, 16> item_probe_pair_lr{};
  bool match_active = true;
  bool setup_ready = true;
  u32 match_index = 1;
  std::string capture_id = "item-capture";
  std::string sequence_id = "item-sequence";
  std::array<u32, 4> fighter_pointers{};
  std::array<bool, 4> fighter_present{};

''' + item_methods + r'''
''' + build_json + r'''
};
}

static void Store16(Core::System* system, u32 address, u16 value) {
  u8* bytes = system->GetMemory().GetPointerForRange(address, 2);
  assert(bytes);
  bytes[0] = static_cast<u8>(value >> 8); bytes[1] = static_cast<u8>(value);
}
static void Store32(Core::System* system, u32 address, u32 value) {
  u8* bytes = system->GetMemory().GetPointerForRange(address, 4);
  assert(bytes);
  bytes[0] = static_cast<u8>(value >> 24); bytes[1] = static_cast<u8>(value >> 16);
  bytes[2] = static_cast<u8>(value >> 8); bytes[3] = static_cast<u8>(value);
}
static void Prepare(Core::System* system, ReferenceCapture::ProbeHarness* probe) {
  using namespace ReferenceCapture;
  const u32 list_root = 0x80020000, first_gobj = 0x80030000;
  const u32 slot0_fighter = 0x80042000, slot0_gobj = 0x80052000;
  const u32 slot2_fighter = 0x80041000, slot3_fighter = 0x80040000;
  const u32 slot2_gobj = 0x80051000, slot3_gobj = 0x80050000;
  const u32 item_proc = 0x80060000;
  Store32(system, 0x804d782c, list_root);
  Store32(system, 0x804d7834, 4);
  Store32(system, list_root + 0x24, first_gobj);
  Store32(system, first_gobj + 0x8, ITEM_PROBE_ARROW_GOBJ);
  Store16(system, first_gobj, 0);
  Store16(system, ITEM_PROBE_ARROW_GOBJ, 0x0006);
  system->GetMemory().GetPointerForRange(ITEM_PROBE_ARROW_GOBJ + 2, 1)[0] = 9;
  Store32(system, ITEM_PROBE_ARROW_GOBJ + 0x2c, ITEM_PROBE_ARROW_ITEM);
  Store32(system, ITEM_PROBE_ARROW_GOBJ + 0x18, item_proc);
  Store32(system, item_proc + 0x0c, 0x04000000);
  Store32(system, item_proc + 0x10, ITEM_PROBE_ARROW_GOBJ);
  Store32(system, ITEM_PROBE_ARROW_ITEM + 0x10, ITEM_PROBE_ARROW_KIND);
  Store32(system, ITEM_PROBE_ARROW_ITEM + 0xc34, 0x3f800000);
  Store32(system, ITEM_PROBE_ARROW_ITEM + 0xc50, 8);
  Store32(system, ITEM_PROBE_ARROW_ITEM + 0xb8, 0x80071000);
  Store32(system, 0x80071000 + 0x30, 0x80301234);
  Store32(system, 0x80071000 + 0x34, 0x80305678);
  Store32(system, ITEM_PROBE_ARROW_ITEM + 0xc0, 1);
  Store32(system, ITEM_PROBE_ARROW_ITEM + 0xc54, 0x3f000001);
  Store32(system, ITEM_PROBE_ARROW_ITEM + 0xcf4, slot0_gobj);
  system->GetMemory().GetPointerForRange(ITEM_PROBE_ARROW_ITEM + 0xdce, 1)[0] = 0xa5;
  Store32(system, 0x804d6d28, 0x80072000);
  Store32(system, 0x80072000 + 0xe0, 0x42c80000);
  Store32(system, ITEM_PROBE_ARROW_ITEM + 0x518, slot3_gobj);
  Store32(system, slot0_fighter, slot0_gobj);
  Store32(system, slot2_fighter, slot2_gobj);
  Store32(system, slot3_fighter, slot3_gobj);
  probe->fighter_pointers[0] = slot0_fighter;
  probe->fighter_pointers[2] = slot2_fighter;
  probe->fighter_pointers[3] = slot3_fighter;
  probe->fighter_present[0] = true;
  probe->fighter_present[2] = probe->fighter_present[3] = true;
  const u32 cpu = slot2_fighter + 0x1a88;
  Store32(system, cpu + 0xf4, 0x80abcdef);
  Store32(system, cpu + 0xf8, 0x00000002);
  Store32(system, ITEM_PROBE_ARROW_ITEM + 0x28, 0x12345678);
  for (u32 axis = 0; axis < 3; ++axis) {
    Store32(system, ITEM_PROBE_ARROW_ITEM + 0x40 + axis * 4, 0x3f000001 + axis);
    Store32(system, ITEM_PROBE_ARROW_ITEM + 0x4c + axis * 4, 0x40000001 + axis);
    Store32(system, ITEM_PROBE_ARROW_ITEM + 0x620 + axis * 4, 0x41000001 + axis);
    Store32(system, ITEM_PROBE_ARROW_ITEM + 0x62c + axis * 4, 0x42000001 + axis);
  }
  Store32(system, ITEM_PROBE_ARROW_ITEM + 0x5d4, 2);
}

int main() {
  using namespace ReferenceCapture;
  Core::System system;
  ProbeHarness probe;
  PowerPC::PowerPCState state;
  Prepare(&system, &probe);
  const ItemProbePoint* entry = FindItemProbePoint(0x802697d4);
  const ItemProbePoint* exit = FindItemProbePoint(0x80269974);
  assert(entry && exit);
  Store32(&system, entry->address, entry->expected_word);
  Store32(&system, exit->address, exit->expected_word);
  state.gpr[3] = ITEM_PROBE_ARROW_GOBJ;
  state.spr[8] = 0x80381234;
  probe.RecordItemProbe(&system, entry->address, *entry, &state, 11144);
  assert(!probe.invalid && probe.item_probe_record_count == 1);
  const auto& first = probe.item_probe_records[0];
  assert(first.arrow_present && first.arrow_gobj == ITEM_PROBE_ARROW_GOBJ);
  assert(first.arrow_item == ITEM_PROBE_ARROW_ITEM && first.arrow_kind == ITEM_PROBE_ARROW_KIND);
  assert(first.arrow_damage_dealt == 0x3f800000);
  assert(first.arrow_pending_shield_damage == 8);
  assert(first.arrow_shield_target_gobj == 0x80052000 && first.arrow_damage_flags == 0xa5);
  assert(first.arrow_ground_or_air == 1 && first.arrow_shield_angle_bits == 0x3f000001);
  assert(first.arrow_common_shield_degrees_bits == 0x42c80000);
  assert(first.arrow_shield_bounced_present && first.arrow_hit_shield_present);
  assert(first.arrow_owner_gobj == 0x80050000 && first.arrow_list_order == 1);
  assert(first.scheduler_priority == 4 && first.arrow_p_link == 9);
  assert(first.arrow_process_count == 1 && first.arrow_process_priorities[0] == 4);
  assert(first.fighter_slot2_pointer == 0x80041000);
  assert(first.fighter_slot2_gobj == 0x80051000 && first.decision_item == 0x80abcdef);
  assert(first.decision_item_status == 2 && first.velocity_bits[0] == 0x3f000001);
  assert(first.position_bits[2] == 0x40000003 && first.hitbox0_state == 2);
  assert(first.hitbox0_previous_endpoint_bits[1] == 0x42000002);
  assert(first.hitbox0_current_endpoint_bits[2] == 0x41000003);

  state.gpr[3] = 0x1234;  // The function epilogue may repurpose r3.
  probe.RecordItemProbe(&system, exit->address, *exit, &state, 11144);
  assert(!probe.invalid && probe.item_probe_record_count == 2);
  assert(!probe.item_probe_pair_active[1]);

  const ItemProbePoint* updater_call = FindItemProbePoint(0x80269bc0);
  const ItemProbePoint* updater_return = FindItemProbePoint(0x80269bc4);
  assert(updater_call && updater_return);
  Store32(&system, updater_call->address, updater_call->expected_word);
  Store32(&system, updater_return->address, updater_return->expected_word);
  state.gpr[3] = ITEM_PROBE_ARROW_GOBJ;
  state.spr[8] = 0x80269b9c;
  probe.RecordItemProbe(&system, updater_call->address, *updater_call, &state, 11144);
  state.gpr[3] = 0x1234;
  state.spr[8] = 0x80269bc4;  // The branch-and-link instruction replaced LR.
  probe.RecordItemProbe(&system, updater_return->address, *updater_return, &state, 11144);
  assert(!probe.invalid && probe.item_probe_record_count == 4);
  assert(!probe.item_probe_pair_active[4]);

  const ItemProbePoint* cpu_entry = FindItemProbePoint(0x800bb9b4);
  const ItemProbePoint* cpu_return = FindItemProbePoint(0x800bbb88);
  assert(cpu_entry && cpu_return);
  Store32(&system, cpu_entry->address, cpu_entry->expected_word);
  Store32(&system, cpu_return->address, cpu_return->expected_word);
  state.gpr[3] = probe.fighter_pointers[2];
  state.spr[8] = 0x80384567;
  probe.RecordItemProbe(&system, cpu_entry->address, *cpu_entry, &state, 11159);
  state.gpr[3] = 0x1234;
  probe.RecordItemProbe(&system, cpu_return->address, *cpu_return, &state, 11159);
  assert(!probe.invalid && probe.item_probe_record_count == 6);
  assert(probe.item_probe_records[4].decision_item == 0x80abcdef);
  assert(probe.item_probe_records[5].decision_item_status == 2);
  assert(!probe.item_probe_pair_active[5]);

  const ItemProbePoint* shield_call = FindItemProbePoint(0x802a9c08);
  const ItemProbePoint* shield_return = FindItemProbePoint(0x802a9c0c);
  assert(shield_call && shield_return);
  Store32(&system, shield_call->address, shield_call->expected_word);
  Store32(&system, shield_return->address, shield_return->expected_word);
  state.gpr[3] = ITEM_PROBE_ARROW_GOBJ;
  state.gpr[4] = 2;  // Link Arrow state id at the verified DOL call site.
  state.gpr[5] = 2;  // ITEM_ANIM_UPDATE at the verified DOL call site.
  probe.RecordItemProbe(&system, shield_call->address, *shield_call, &state, 11159);
  assert(!probe.invalid && probe.item_probe_record_count == 7);
  assert(probe.item_probe_records[6].gpr4 == 2 && probe.item_probe_records[6].gpr5 == 2);
  probe.RecordItemProbe(&system, shield_return->address, *shield_return, &state, 11159);
  assert(!probe.invalid && probe.item_probe_record_count == 8);
  assert(!probe.item_probe_pair_active[9]);

  const ItemProbePoint* overlap_entry = FindItemProbePoint(0x80079810);
  const ItemProbePoint* overlap_return = FindItemProbePoint(0x80079814);
  assert(overlap_entry && overlap_return);
  Store32(&system, overlap_entry->address, overlap_entry->expected_word);
  Store32(&system, overlap_return->address, overlap_return->expected_word);
  state.gpr[3] = ITEM_PROBE_ARROW_ITEM + 0x5d4;
  state.gpr[4] = 0x80042000 + 0x19c0;
  state.gpr[5] = 0x80083000;
  state.gpr[6] = 2;
  state.ps[0].value = 0x3ff0000000000000ULL;
  state.ps[1].value = 0x4000000000000000ULL;
  state.ps[2].value = 0x4008000000000000ULL;
  for (u32 i = 0; i < 0x64; ++i)
    system.memory.GetPointerForRange(state.gpr[3] + i, 1)[0] = static_cast<u8>(i);
  for (u32 i = 0; i < 0x24; ++i)
    system.memory.GetPointerForRange(state.gpr[4] + i, 1)[0] = static_cast<u8>(0x40 + i);
  for (u32 i = 0; i < 0x30; ++i)
    system.memory.GetPointerForRange(state.gpr[5] + i, 1)[0] = static_cast<u8>(0x80 + i);
  state.spr[8] = 0x80381234;
  probe.RecordItemProbe(&system, overlap_entry->address, *overlap_entry, &state, 11158);
  assert(!probe.invalid && probe.item_probe_record_count == 9);
  const auto& overlap = probe.item_probe_records[8];
  assert(overlap.shield_inputs_present && overlap.shield_transform_present);
  assert(overlap.gpr3 == ITEM_PROBE_ARROW_ITEM + 0x5d4 && overlap.gpr4 == 0x800439c0);
  assert(overlap.gpr5 == 0x80083000 && overlap.gpr6 == 2);
  assert(overlap.fighter_slot0_pointer == 0x80042000 &&
         overlap.fighter_slot0_gobj == 0x80052000);
  assert(overlap.gpr4 == overlap.fighter_slot0_pointer + 0x19c0);
  assert(overlap.shield_fpr[0] == 0x3ff0000000000000ULL);
  assert(overlap.shield_capsule[0] == 0 && overlap.shield_capsule[0x63] == 0x63);
  assert(overlap.shield_result[0] == 0x40 && overlap.shield_result[0x23] == 0x63);
  assert(overlap.shield_transform[0] == 0x80 && overlap.shield_transform[0x2f] == 0xaf);
  state.gpr[3] = 0;
  state.spr[8] = overlap_return->address;  // `bl` replaced LR with the return PC.
  probe.RecordItemProbe(&system, overlap_return->address, *overlap_return, &state, 11158);
  assert(!probe.invalid && probe.item_probe_record_count == 10);
  assert(!probe.item_probe_pair_active[11]);

  const ItemProbePoint* assignment_entry = FindItemProbePoint(0x80077688);
  const ItemProbePoint* assignment_return = FindItemProbePoint(0x8007796c);
  const ItemProbePoint* dispatch_entry = FindItemProbePoint(0x80269dc8);
  const ItemProbePoint* dispatch_return = FindItemProbePoint(0x80269f10);
  const ItemProbePoint* shield_entry = FindItemProbePoint(0x802a9b08);
  const ItemProbePoint* shield_callback_return = FindItemProbePoint(0x802a9cdc);
  const ItemProbePoint* target_call = FindItemProbePoint(0x802a9b30);
  const ItemProbePoint* target_return = FindItemProbePoint(0x802a9b34);
  assert(assignment_entry && assignment_return && dispatch_entry && dispatch_return &&
         shield_entry && shield_callback_return && target_call && target_return);
  for (const ItemProbePoint* point : {assignment_entry, assignment_return, dispatch_entry,
                                      dispatch_return, shield_entry, shield_callback_return,
                                      target_call, target_return})
    Store32(&system, point->address, point->expected_word);

  state.gpr[3] = ITEM_PROBE_ARROW_ITEM;
  state.gpr[5] = 0x80042000;
  state.gpr[6] = 0x80086000;
  state.ps[1].value = 0x400a651100000000ULL;
  Store32(&system, state.gpr[6], 0x3f800001);
  Store32(&system, state.gpr[6] + 4, 0x40000002);
  Store32(&system, state.gpr[6] + 8, 0x40400003);
  Store32(&system, state.gpr[5] + 0x19c0, 0x80085000);
  for (u32 i = 4; i < 0x24; ++i)
    system.memory.GetPointerForRange(state.gpr[5] + 0x19c0 + i, 1)[0] =
        static_cast<u8>(i + 0x10);
  for (u32 i = 0; i < 0x30; ++i)
    system.memory.GetPointerForRange(0x80085000 + 0x44 + i, 1)[0] =
        static_cast<u8>(i + 0x20);
  state.spr[8] = 0x80381234;
  probe.RecordItemProbe(&system, assignment_entry->address, *assignment_entry, &state, 11158);
  assert(!probe.invalid && probe.item_probe_record_count == 11);
  assert(probe.item_probe_pair_active[12]);
  assert(probe.item_probe_records[10].arrow_damage_dealt == 0x3f800000);
  assert(probe.item_probe_records[10].arrow_pending_shield_damage == 8);
  assert(probe.item_probe_records[10].arrow_shield_target_gobj == 0x80052000);
  assert(probe.item_probe_records[10].arrow_ftcoll_inputs_present);
  assert(probe.item_probe_records[10].arrow_ftcoll_angle == 0x400a651100000000ULL);
  assert(probe.item_probe_records[10].arrow_ftcoll_position[0] == 0x3f &&
         probe.item_probe_records[10].arrow_ftcoll_position[11] == 0x03);
  assert(probe.item_probe_records[10].arrow_shield_hit[0] == 0x80 &&
         probe.item_probe_records[10].arrow_shield_hit[3] == 0x00);
  assert(probe.item_probe_records[10].arrow_shield_bone_matrix[0] == 0x20 &&
         probe.item_probe_records[10].arrow_shield_bone_matrix[0x2f] == 0x4f);
  probe.RecordItemProbe(&system, assignment_return->address, *assignment_return, &state, 11158);
  assert(!probe.invalid && probe.item_probe_record_count == 12);
  assert(!probe.item_probe_pair_active[12]);

  state.gpr[3] = ITEM_PROBE_ARROW_GOBJ;
  state.spr[8] = 0x80381238;
  probe.RecordItemProbe(&system, dispatch_entry->address, *dispatch_entry, &state, 11158);
  assert(!probe.invalid && probe.item_probe_record_count == 13);
  assert(probe.item_probe_pair_active[13]);
  state.gpr[3] = 0x1234;
  probe.RecordItemProbe(&system, dispatch_return->address, *dispatch_return, &state, 11158);
  assert(!probe.invalid && probe.item_probe_record_count == 14);
  assert(!probe.item_probe_pair_active[13]);

  state.gpr[3] = ITEM_PROBE_ARROW_GOBJ;
  state.spr[8] = 0x8038123c;
  probe.RecordItemProbe(&system, shield_entry->address, *shield_entry, &state, 11158);
  assert(!probe.invalid && probe.item_probe_record_count == 15);
  assert(probe.item_probe_pair_active[14]);
  state.gpr[3] = 0x80052000;
  probe.RecordItemProbe(&system, target_call->address, *target_call, &state, 11158);
  assert(!probe.invalid && probe.item_probe_record_count == 16);
  assert(probe.item_probe_pair_active[15]);
  state.gpr[3] = 0;
  state.spr[8] = target_return->address;
  probe.RecordItemProbe(&system, target_return->address, *target_return, &state, 11158);
  assert(!probe.invalid && probe.item_probe_record_count == 17);
  assert(!probe.item_probe_pair_active[15]);
  state.spr[8] = 0x8038123c;
  probe.RecordItemProbe(&system, shield_callback_return->address,
                        *shield_callback_return, &state, 11158);
  assert(!probe.invalid && probe.item_probe_record_count == 18);
  assert(!probe.item_probe_pair_active[14]);

  Core::System prewindow_system;
  ProbeHarness prewindow_probe;
  PowerPC::PowerPCState prewindow_state;
  Prepare(&prewindow_system, &prewindow_probe);
  prewindow_probe.item_probe_first_tick = 11158;
  prewindow_probe.item_probe_last_tick = 11158;
  Store32(&prewindow_system, overlap_entry->address, overlap_entry->expected_word);
  Store32(&prewindow_system, overlap_return->address, overlap_return->expected_word);
  prewindow_state.gpr[3] = ITEM_PROBE_ARROW_ITEM + 0x5d4;
  prewindow_state.gpr[4] = 0x80042000 + 0x19c0;
  prewindow_state.spr[8] = 0x80381234;
  prewindow_probe.RecordItemProbe(&prewindow_system, overlap_entry->address,
                                  *overlap_entry, &prewindow_state, 4184);
  prewindow_state.spr[8] = overlap_return->address;
  prewindow_probe.RecordItemProbe(&prewindow_system, overlap_return->address,
                                  *overlap_return, &prewindow_state, 4184);
  assert(!prewindow_probe.invalid && prewindow_probe.item_probe_record_count == 0);
  assert(!prewindow_probe.item_probe_pair_active[11]);

  const ItemProbePoint* source_tick = FindItemProbePoint(0x80390eb4);
  assert(source_tick);
  Store32(&system, source_tick->address, source_tick->expected_word);
  probe.RecordItemProbe(&system, source_tick->address, *source_tick, &state, 11160);
  assert(probe.item_probe_published.load() && probe.item_probe_record_count == 18);
  std::string json;
  assert(probe.BuildItemProbeJson(&json));
  assert(json.find("arrow_shield_overlap_call") != std::string::npos);
  assert(json.find("\"shield_overlap_inputs\":{\"fpr1\":\"0x3ff0000000000000\"") !=
         std::string::npos);
  assert(json.find("melee-web-cpu-item-boundary-probe") != std::string::npos);
  assert(json.find("hitbox0_previous_endpoint_bits") != std::string::npos);
  assert(json.find("cpu_item_decision_entry") != std::string::npos);
  assert(json.find("arrow_hit_shield_state_call") != std::string::npos);
  assert(json.find("arrow_ftcoll_shield_assignment_entry") != std::string::npos);
  assert(json.find("arrow_item_damage_dispatch_entry") != std::string::npos);
  assert(json.find("arrow_hit_shield_target_test_return") != std::string::npos);
  assert(json.find("\"arrow_damage_dealt\":\"0x3f800000\"") != std::string::npos);
  assert(json.find("\"arrow_pending_shield_damage\":\"0x00000008\"") !=
         std::string::npos);
  assert(json.find("\"arrow_shield_target_gobj\":\"0x80052000\"") !=
         std::string::npos);
  assert(json.find("\"version\":5") != std::string::npos);
  assert(json.find("\"arrow_ftcoll_inputs\":{\"angle_fpr1\":\"0x400a651100000000\"") !=
         std::string::npos);
  assert(json.find("\"arrow_damage_flags\":165") != std::string::npos);
  assert(json.find("\"arrow_dispatch_state\":{\"ground_or_air\":1") !=
         std::string::npos);
  assert(json.find("\"xDCE_raw\":165") != std::string::npos);
  assert(json.find("\"xC54_angle_bits\":\"0x3f000001\"") != std::string::npos);
  assert(json.find("\"unk_degrees_bits\":\"0x42c80000\"") != std::string::npos);
  assert(json.find("\"shield_bounced_present\":true") != std::string::npos);
  assert(json.find("\"hit_shield_present\":true}") != std::string::npos);
  assert(json.find("\"gpr4\":\"0x00000002\"") != std::string::npos);
  assert(json.find("\"gpr5\":\"0x00000002\"") != std::string::npos);
  assert(json.find("fighter_slot2_pointer") != std::string::npos);
  assert(json.find("fighter_slot2_gobj") != std::string::npos);
  assert(json.find("fighter_slot0_pointer") != std::string::npos);
  assert(json.find("fighter_slot0_gobj") != std::string::npos);
  assert(json.find("scheduler_priority") != std::string::npos);
  assert(json.find("arrow_process_priorities") != std::string::npos);
  assert(json.find("bytes_0xAC4") == std::string::npos);

  Core::System creation_system;
  ProbeHarness creation_probe;
  PowerPC::PowerPCState creation_state;
  Prepare(&creation_system, &creation_probe);
  creation_probe.item_probe_trigger_on_arrow_creation = true;
  creation_probe.item_probe_capture_ticks = 3;
  creation_probe.item_probe_first_tick = 0;
  creation_probe.item_probe_last_tick = 0;
  const ItemProbePoint* creation_return = FindItemProbePoint(0x802a8508);
  assert(creation_return && creation_return->event == ItemProbeEvent::CreationReturn);
  Store32(&creation_system, creation_return->address, creation_return->expected_word);
  creation_state.gpr[3] = ITEM_PROBE_ARROW_GOBJ;
  creation_probe.RecordItemProbe(&creation_system, creation_return->address,
                                 *creation_return, &creation_state, 9000);
  assert(!creation_probe.invalid && creation_probe.item_probe_window_triggered);
  assert(creation_probe.item_probe_first_tick == 9000 &&
         creation_probe.item_probe_last_tick == 9002);
  assert(creation_probe.item_probe_record_count == 1);
  assert(creation_probe.item_probe_records[0].scheduler_priority == 4);
  const ItemProbePoint* creation_window_end = FindItemProbePoint(0x80390eb4);
  assert(creation_window_end);
  Store32(&creation_system, creation_window_end->address,
          creation_window_end->expected_word);
  creation_probe.RecordItemProbe(&creation_system, creation_window_end->address,
                                 *creation_window_end, &creation_state, 9003);
  assert(creation_probe.item_probe_published.load());
  std::string creation_json;
  assert(creation_probe.BuildItemProbeJson(&creation_json));
  assert(creation_json.find("young_link_arrow_creation") != std::string::npos);
  assert(creation_json.find("\"first_tick\":9000") != std::string::npos);
  assert(creation_json.find("\"last_tick\":9002") != std::string::npos);

  Core::System launch_system;
  ProbeHarness launch_probe;
  PowerPC::PowerPCState launch_state;
  Prepare(&launch_system, &launch_probe);
  launch_probe.item_probe_trigger_on_arrow_launch = true;
  launch_probe.item_probe_capture_ticks = 3;
  launch_probe.item_probe_first_tick = 0;
  launch_probe.item_probe_last_tick = 0;
  const ItemProbePoint* launch_entry = FindItemProbePoint(0x802a850c);
  const ItemProbePoint* launch_return = FindItemProbePoint(0x802a8980);
  assert(launch_entry && launch_return && launch_entry->pair == launch_return->pair);
  Store32(&launch_system, launch_entry->address, launch_entry->expected_word);
  Store32(&launch_system, launch_return->address, launch_return->expected_word);
  launch_state.gpr[3] = ITEM_PROBE_ARROW_GOBJ;
  launch_state.spr[8] = 0x80123456;
  launch_probe.RecordItemProbe(&launch_system, launch_entry->address, *launch_entry,
                               &launch_state, 9100);
  assert(!launch_probe.invalid && launch_probe.item_probe_window_triggered);
  assert(launch_probe.item_probe_first_tick == 9100 &&
         launch_probe.item_probe_last_tick == 9102);
  assert(launch_probe.item_probe_pair_active[10]);
  launch_state.gpr[3] = 1;
  launch_probe.RecordItemProbe(&launch_system, launch_return->address, *launch_return,
                               &launch_state, 9100);
  assert(!launch_probe.invalid && !launch_probe.item_probe_pair_active[10]);
  assert(launch_probe.item_probe_record_count == 2);
  Store32(&launch_system, creation_window_end->address,
          creation_window_end->expected_word);
  launch_probe.RecordItemProbe(&launch_system, creation_window_end->address,
                               *creation_window_end, &launch_state, 9103);
  assert(launch_probe.item_probe_published.load());
  std::string launch_json;
  assert(launch_probe.BuildItemProbeJson(&launch_json));
  assert(launch_json.find("young_link_arrow_launch") != std::string::npos);

  Core::System invalid_system;
  ProbeHarness invalid_probe;
  PowerPC::PowerPCState invalid_state;
  Prepare(&invalid_system, &invalid_probe);
  Store32(&invalid_system, entry->address, entry->expected_word);
  Store32(&invalid_system, ITEM_PROBE_ARROW_ITEM + 0x518, 0x80050004);
  invalid_state.gpr[3] = ITEM_PROBE_ARROW_GOBJ;
  invalid_state.spr[8] = 0x80381234;
  invalid_probe.RecordItemProbe(&invalid_system, entry->address, *entry,
                                &invalid_state, 11144);
  assert(invalid_probe.invalid && invalid_probe.item_probe_record_count == 0);

  Core::System mismatched_return_system;
  ProbeHarness mismatched_return_probe;
  PowerPC::PowerPCState mismatched_return_state;
  Prepare(&mismatched_return_system, &mismatched_return_probe);
  Store32(&mismatched_return_system, entry->address, entry->expected_word);
  Store32(&mismatched_return_system, exit->address, exit->expected_word);
  mismatched_return_state.gpr[3] = ITEM_PROBE_ARROW_GOBJ;
  mismatched_return_state.spr[8] = 0x80381234;
  mismatched_return_probe.RecordItemProbe(&mismatched_return_system, entry->address,
      *entry, &mismatched_return_state, 11144);
  mismatched_return_state.spr[8] = 0x80381238;
  mismatched_return_probe.RecordItemProbe(&mismatched_return_system, exit->address,
      *exit, &mismatched_return_state, 11144);
  assert(mismatched_return_probe.invalid &&
         mismatched_return_probe.item_probe_record_count == 1);
  return 0;
}
'''
        path = Path(tempfile.mkdtemp()) / "item_probe.cpp"
        cls.addClassCleanup(lambda: shutil.rmtree(path.parent, ignore_errors=True))
        path.write_text(harness, encoding="utf-8")
        cls.binary = path.with_suffix("")
        built = subprocess.run([compiler, "-std=c++17", "-Wall", "-Werror", str(path),
                                "-o", str(cls.binary)], capture_output=True, text=True)
        if built.returncode:
            raise RuntimeError(built.stderr)

    def test_item_probe_identity_boundaries_and_window(self):
        result = subprocess.run([str(self.binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
