from __future__ import annotations

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).parents[1]
PATCH = ROOT / "reference-capture" / "dolphin" / "patches" / "0001-jitarm64-reference-observer.patch"
SOURCE = ROOT / "reference-capture" / "dolphin" / "source" / "Core" / "PowerPC" / "ReferenceCaptureObserver.cpp"


class ReferenceDolphinObserverTests(unittest.TestCase):
    def test_fighter_create_preserves_paired_fighter_entities_per_source_slot(self) -> None:
        compiler = shutil.which("clang++") or shutil.which("g++")
        if compiler is None:
            self.skipTest("A native C++ compiler is not installed")
        source = SOURCE.read_text(encoding="utf-8")
        methods = source[source.index("  bool ReadFighterSourceSlot("):
                         source.index("  bool ReadU32(")]
        create_start = source.index("    else if (boundary == Boundary::FighterCreate)")
        create_end = source.index("    else if (boundary == Boundary::SourceTick", create_start)
        boundary = source[create_start:create_end]
        self.assertIn("FighterEntitySliceFlags(slot, entity_index)", boundary)
        self.assertIn("RegisterFighterEntity(slot, fighter, kind, &entity_index)", boundary)
        self.assertIn("ReadU32(system, gobj + 0x2c, &fighter)", boundary)
        self.assertIn("ReadU32(system, fighter + 0x4, &kind)", boundary)
        self.assertIn("SliceTag::FighterCreateContext, gobj, 0x30, flags", boundary)
        self.assertIn("SliceTag::FighterHead, fighter, 0x100, flags", boundary)
        self.assertNotIn("fighter_present[slot])", boundary)
        match_slices = source[source.index("  bool AddMatchSlices("):
                              source.index("  bool ReadProfileRoot(")]
        self.assertIn("entity_index < fighter_entity_count[slot]", match_slices)
        self.assertIn("fighter_entity_pointers[slot][entity_index]", match_slices)
        harness = r"""
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
using u8 = uint8_t;
using u32 = uint32_t;
using u16 = uint16_t;
namespace Core { struct System {}; }
struct Reader {
  std::array<u8, 256> memory{};
  std::array<u32, 4> fighter_pointers{};
  std::array<bool, 4> fighter_present{};
  std::array<std::array<u32, 2>, 4> fighter_entity_pointers{};
  std::array<std::array<u32, 2>, 4> fighter_entity_kinds{};
  std::array<u8, 4> fighter_entity_count{};
  bool IsMem1Range(u32 address, size_t size) const {
    return address >= 0x100 && address <= 0x200 && size <= 0x200 - address;
  }
  bool ReadBytes(Core::System*, u32 address, size_t size, u8* output) const {
    if (!IsMem1Range(address, size)) return false;
    for (size_t i = 0; i < size; ++i) output[i] = memory[address - 0x100 + i];
    return true;
  }
""" + methods + r"""
};
int main() {
  Reader reader;
  Core::System system;
  const std::array<u32, 5> addresses = {0x100, 0x120, 0x140, 0x160, 0x180};
  const std::array<u32, 5> kinds = {13, 14, 19, 7, 20};
  const std::array<u8, 5> slots = {0, 1, 2, 2, 3};
  for (size_t i = 0; i < addresses.size(); ++i) {
    const u32 base = addresses[i] - 0x100;
    reader.memory[base + 4] = static_cast<u8>(kinds[i] >> 24);
    reader.memory[base + 5] = static_cast<u8>(kinds[i] >> 16);
    reader.memory[base + 6] = static_cast<u8>(kinds[i] >> 8);
    reader.memory[base + 7] = static_cast<u8>(kinds[i]);
    reader.memory[base + 12] = slots[i];
    u8 slot = 0xff;
    assert(reader.ReadFighterSourceSlot(&system, addresses[i], &slot));
    assert(slot == slots[i]);
    u32 entity_index = 0xff;
    assert(reader.RegisterFighterEntity(slot, addresses[i], kinds[i], &entity_index));
    assert(entity_index == (i == 3 ? 1u : 0u));
    assert(reader.FighterEntitySliceFlags(slot, entity_index) ==
           static_cast<u16>(slot | (entity_index << 8)));
  }
  assert(reader.fighter_entity_count[2] == 2);
  assert(reader.fighter_entity_pointers[2][0] == addresses[2]);
  assert(reader.fighter_entity_pointers[2][1] == addresses[3]);
  assert(reader.fighter_entity_kinds[2][0] == 19);
  assert(reader.fighter_entity_kinds[2][1] == 7);
  assert(reader.fighter_pointers[2] == addresses[2]);
  assert(reader.fighter_present[2]);
  assert(reader.FighterEntitySliceFlags(2, 0) == 2);
  assert(reader.FighterEntitySliceFlags(2, 1) == 0x102);
  u32 entity_index = 0;
  assert(!reader.RegisterFighterEntity(0, 0x1a0, 13, &entity_index));
  assert(!reader.RegisterFighterEntity(0, addresses[0], 23, &entity_index));
  assert(!reader.RegisterFighterEntity(2, 0x1a0, 23, &entity_index));
  assert(!reader.RegisterFighterEntity(4, 0x1a0, 23, &entity_index));
  u8 invalid_slot = 0xff;
  assert(!reader.ReadFighterSourceSlot(&system, 0x1f4, &invalid_slot));
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / "slot.cpp").write_text(harness)
            built = subprocess.run([compiler, "-std=c++17", "-Wall", "-Werror",
                                    str(path / "slot.cpp"), "-o", str(path / "slot")],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            checked = subprocess.run([str(path / "slot")], capture_output=True, text=True)
            self.assertEqual(checked.returncode, 0, checked.stderr)

    def test_scene_reset_classifier_distinguishes_menu_match_results_and_prize(self) -> None:
        compiler = shutil.which("clang++") or shutil.which("g++")
        if compiler is None:
            self.skipTest("A native C++ compiler is not installed")
        source = SOURCE.read_text(encoding="utf-8")
        classifier = source[source.index("  enum class SceneResetAction"):
                            source.index("  void Observe(Core::System*")]
        harness = r"""
#include <cassert>
struct ObserverState {
  bool match_active = false, result_seen = false, vs_exit_seen = false;
  bool vs_exit_return_seen = false, vs_mode_exit_seen = false;
  bool results_enter_seen = false, results_gobj_seen = false;
  bool results_exit_seen = false, results_mode_exit_seen = false;
  bool prize_scene_exit_seen = false, prize_mode_exit_seen = false;
  int whole_phase = 0;
""" + classifier + r"""
};
int main() {
  using Action = ObserverState::SceneResetAction;
  ObserverState state;
  for (int phase : {0, 2, 4, 8}) {
    state.whole_phase = phase;
    assert(state.ClassifyWholeSceneReset() == Action::Ignore);
  }
  for (int phase : {1, 3, 5, 6, 7}) {
    state.whole_phase = phase;
    assert(state.ClassifyWholeSceneReset() == Action::Invalid);
  }
  state.match_active = true; state.whole_phase = 5;
  state.result_seen = state.vs_exit_seen = state.vs_exit_return_seen = true;
  assert(state.ClassifyWholeSceneReset() == Action::Invalid);
  state.vs_mode_exit_seen = true;
  assert(state.ClassifyWholeSceneReset() == Action::BeginResults);
  state.results_enter_seen = true;
  assert(state.ClassifyWholeSceneReset() == Action::Invalid);
  state.results_gobj_seen = state.results_exit_seen = true;
  assert(state.ClassifyWholeSceneReset() == Action::Invalid);
  state.results_mode_exit_seen = true;
  assert(state.ClassifyWholeSceneReset() == Action::FinishResults);
  state.match_active = false; state.whole_phase = 6;
  assert(state.ClassifyWholeSceneReset() == Action::Invalid);
  state.prize_scene_exit_seen = state.prize_mode_exit_seen = true;
  assert(state.ClassifyWholeSceneReset() == Action::Ignore);
}
"""
        harness = "#include <initializer_list>\n" + harness
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / "reset.cpp").write_text(harness)
            built = subprocess.run([compiler, "-std=c++17", "-Wall", "-Werror",
                                    str(path / "reset.cpp"), "-o", str(path / "reset")],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            checked = subprocess.run([str(path / "reset")], capture_output=True, text=True)
            self.assertEqual(checked.returncode, 0, checked.stderr)

    def test_css_joint_reader_preserves_source_traversal_and_rejects_cycles(self) -> None:
        compiler = shutil.which("clang++") or shutil.which("g++")
        if compiler is None:
            self.skipTest("A native C++ compiler is not installed")
        source = SOURCE.read_text(encoding="utf-8")
        method = source[source.index("  bool ReadCssJoint("):
                        source.index("  bool AddCssCpuSteeringSlices(")]
        harness = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <unordered_map>
using u8 = uint8_t;
using u32 = uint32_t;
namespace Core { struct System {}; }
struct Reader {
  std::unordered_map<u32, u32> memory;
  bool ReadU32(Core::System*, u32 address, u32* result) const {
    const auto found = memory.find(address);
    if (found == memory.end()) return false;
    *result = found->second;
    return true;
  }
  void node(u32 address, u32 parent, u32 child, u32 next, u32 flags = 0) {
    memory[address + 8] = next;
    memory[address + 12] = parent;
    memory[address + 16] = child;
    memory[address + 20] = flags;
  }
''' + method + r'''
};
int main() {
  Reader reader;
  // Root -> A -> C,D ; root's second child B is an instance whose child E
  // must be skipped, as in lb_80011E24's authored joint numbering.
  reader.node(0x100, 0, 0x200, 0);
  reader.node(0x200, 0x100, 0x400, 0x300);
  reader.node(0x300, 0x100, 0x600, 0, 1u << 12);
  reader.node(0x400, 0x200, 0, 0x500);
  reader.node(0x500, 0x200, 0, 0);
  reader.node(0x600, 0x300, 0, 0);
  const u32 expected[] = {0x100, 0x200, 0x400, 0x500, 0x300};
  for (u8 index = 0; index < 5; ++index) {
    u32 found = 0;
    assert(reader.ReadCssJoint(nullptr, 0x100, index, &found));
    assert(found == expected[index]);
  }
  u32 found = 0;
  assert(!reader.ReadCssJoint(nullptr, 0x100, 5, &found));
  assert(!reader.ReadCssJoint(nullptr, 0x100, 255, &found));
  reader.memory[0x500 + 12] = 0x500; // Parent cycle.
  assert(!reader.ReadCssJoint(nullptr, 0x100, 4, &found));
  reader.memory[0x500 + 12] = 0x200;
  reader.memory[0x500 + 8] = 0x200; // Child/sibling cycle.
  assert(!reader.ReadCssJoint(nullptr, 0x100, 4, &found));
  reader.memory[0x500 + 8] = 0;
  reader.memory[0x500 + 12] = 0x700; // Unvisited parent.
  assert(!reader.ReadCssJoint(nullptr, 0x100, 4, &found));
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / "reader.cpp").write_text(harness)
            built = subprocess.run([compiler, "-std=c++17", "-Wall", "-Werror",
                                    str(path / "reader.cpp"), "-o", str(path / "reader")],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            checked = subprocess.run([str(path / "reader")], capture_output=True, text=True)
            self.assertEqual(checked.returncode, 0, checked.stderr)

    def test_complete_observer_and_input_patch_series_applies_in_order(self) -> None:
        checkout = ROOT / ".deps" / "reference-dolphin"
        if not checkout.exists():
            self.skipTest("Optional pinned Dolphin source checkout is not installed")
        patches = sorted(PATCH.parent.glob("*.patch"))
        with tempfile.TemporaryDirectory() as temporary:
            staged = Path(temporary)
            names = set()
            for patch in patches:
                names.update(re.findall(r"^--- a/(.+)$", patch.read_text(), re.MULTILINE))
            for name in names:
                target = staged / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(checkout / name, target)
            for patch in patches:
                result = subprocess.run(["git", "apply", str(patch)], cwd=staged,
                                        text=True, capture_output=True, check=False)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_observer_is_read_only_and_source_identity_is_internal(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("ValidateDiscDOL", source)
        self.assertIn("mbedtls_sha256_ret", source)
        self.assertIn("!status.Flush()", source)
        self.assertIn("File::OpenMode::Create", source)
        self.assertNotIn("Write_U", source)
        self.assertNotIn("WriteToEmu", source)

    def test_host_save_writes_are_guarded_by_session_flag(self) -> None:
        patch = PATCH.read_text(encoding="utf-8")
        self.assertIn("GCMemcardDirectory::FlushToFile", patch)
        self.assertIn("if (!Config::Get(Config::SESSION_SAVE_DATA_WRITABLE))", patch)
        self.assertIn("ExpansionInterfaceManager::Shutdown", patch)

    def test_boundary_blocks_retain_complete_guest_register_state(self) -> None:
        patch = PATCH.read_text(encoding="utf-8")
        self.assertIn("const bool observer_enabled", patch)
        self.assertIn("bool observer_block", patch)
        self.assertIn("!bJITRegisterCacheOff && !observer_block", patch)

    def test_pad_poll_contains_scene_routing_context(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        pad_poll = source[source.index("if (boundary == Boundary::PadPoll)") :
                           source.index("else if (boundary == Boundary::PadConsume)")]
        self.assertIn("SliceTag::SceneRouting, 0x80479d30, 6", pad_poll)

    def test_entry_arms_only_original_vs_setups(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        entry = source[source.index("if (boundary == Boundary::Entry)") :
                       source.index("else\n      {\n        if (!match_active", source.index("if (boundary == Boundary::Entry)"))]
        self.assertIn("match_active = (setup[4] & 0x40) != 0", entry)
        self.assertIn("if (match_active)", entry)
        self.assertIn("!match_active || !setup_pointer", source)

    def test_menu_steering_slices_read_the_authored_menu_sources(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("STAGE_SELECT_TABLE = 0x803F06D0", source)
        self.assertIn("STAGE_SELECT_STRIDE = 0x1C", source)
        self.assertIn("STAGE_SELECT_INDEX = 0x804D6CAE", source)
        self.assertIn("CSS_CURSOR_POINTERS = 0x804A0BC0", source)
        steering = source.split("bool AddMenuSteeringSlices", 1)[1].split("bool AddProfileContextSlices", 1)[0]
        self.assertIn("AddSlice(system, SliceTag::StageSelectIndex, STAGE_SELECT_INDEX, 1)", steering)
        self.assertIn("AddSlice(system, SliceTag::MenuCssCursor, cursor, CSS_CURSOR_BYTES", steering)
        self.assertIn("!AddMenuSteeringSlices(system))", source)

    def test_typed_first_css_context_reads_authored_profile_ranges(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        for authored in ("PROFILE_ROOT_GLOBAL = 0x804D3EE0",
                         "PROFILE_GAME_RULES_OFFSET = 0x1850",
                         "PROFILE_GAME_RULES_SIZE = 0x18",
                         "PROFILE_SAVE_DATA_OFFSET = 0x1868",
                         "PROFILE_SAVE_DATA_SIZE = 0x55E8"):
            self.assertIn(authored, source)
        context = source.split("bool AddProfileContextSlices", 1)[1].split("bool AddSessionSlices", 1)[0]
        self.assertIn("main_data > UINT32_MAX - PROFILE_LAST_BYTE_OFFSET", context)
        self.assertIn("SliceTag::ProfileGameRules", context)
        self.assertIn("SliceTag::ProfileSaveData", context)
        # The save block already carries the persistent fighter records and
        # name banks, so the context captures each authored range once.
        self.assertNotIn("ProfileNameBank", source)
        # Only CSS entry publishes the typed context, and the observer copies
        # the authored ranges instead of reinterpreting their fields.
        self.assertIn("(css && entering && !AddProfileContextSlices(system))", source)
        self.assertNotIn("ProfileSaveData", source.split("bool AddSessionSlices", 1)[1])

    def test_whole_session_is_opt_in_and_has_pinned_source_boundaries(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("MWRC_WHOLE_SESSION_MATCHES", source)
        self.assertIn("WHOLE_SESSION_MIN_MATCHES = 3", source)
        for address in ("0x8026688c", "0x80266d70", "0x8025a998",
                        "0x8025bbd0", "0x801a5af0", "0x80177368",
                        "0x80177704", "0x801a5f64", "0x80179350",
                        "0x801bfcfc", "0x802febe0", "0x802fed10",
                        "0x801a6308", "0x801bff7c"):
            self.assertIn(address, source)
        self.assertIn("ResultsGObjProcess", source)
        self.assertIn("ReturnCss", source)
        self.assertIn("CssCancelEnter", source)
        self.assertIn("MenuSssRoute", source)
        self.assertIn("ProfileCharacters", source)
        self.assertIn("ProfileStages", source)
        self.assertIn("gmMainLib_804D3EE0", source)
        self.assertIn("completed_match_pending_prize", source)
        self.assertIn("StartupPrizeModeExit", source)
        self.assertIn("startup Prize mode exit", source)
        hook_predicate = source[source.index("static bool IsCaptureBoundary"):
                                source.index("void Observer::OnBoundary")]
        self.assertIn("case 0x801BFF7C:", hook_predicate)
        self.assertIn("return IsCaptureBoundary(guest_pc) ||", hook_predicate)
        self.assertIn("MWRC_CAPTURE_ID", source)
        self.assertIn("MWRC_SEQUENCE_ID", source)
        self.assertIn("MENU_AUDIO_STREAM_START", source)
        self.assertIn("audio_owner_epoch", source)
        self.assertIn("return word == 0x7c0802a6", source)
        self.assertIn("SliceTag::Result, 0x80479d98 + 0xc, 0x28", source)
        self.assertIn("AddSessionSlices(system)", source)
        self.assertIn("whole_session_enabled() ? WHOLE_SESSION_FLAG : 0", source)
        self.assertIn("PutU16(out, static_cast<u16>(match_index))", source)
        self.assertIn("PutU32(out, audio_owner_epoch)", source)
        self.assertNotIn("Write_U", source)
        self.assertNotIn("WriteToEmu", source)


if __name__ == "__main__":
    unittest.main()
