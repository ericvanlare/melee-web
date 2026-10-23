"""Descriptor allocations only; not rendered-world rollback acceptance."""
from pathlib import Path
import shutil
import subprocess
import unittest

from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).resolve().parents[1]


class NativeDatCheckpoint(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.work = cls.new_workspace(ROOT, "native-dat-checkpoint-")

    def test_owner_topology_atomic_refusal_and_byte_restoration(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        self.assertIsNotNone(compiler, "C++20 compiler required")
        binary = self.work / "checkpoint"
        build = subprocess.run([
            compiler, "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror",
            "-UNDEBUG", "-Isrc", "src/dat_archive.cpp", "src/native_dat.cpp",
            "tests/native_dat_checkpoint_test.cpp", "-o", str(binary),
        ], cwd=ROOT, capture_output=True, text=True, timeout=120)
        self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
        run = subprocess.run([str(binary)], cwd=ROOT, capture_output=True,
                             text=True, timeout=20)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("checkpoint ownership controls passed", run.stdout)

    @staticmethod
    def _extract_function(source, signature):
        start = source.index(signature)
        opening = source.index("{", start)
        depth = 0
        state = "code"
        escaped = False
        index = opening
        while index < len(source):
            char = source[index]
            next_char = source[index + 1] if index + 1 < len(source) else ""
            if state == "line":
                if char == "\n": state = "code"
            elif state == "block":
                if char == "*" and next_char == "/": state = "code"; index += 1
            elif state in ("string", "char"):
                if escaped: escaped = False
                elif char == "\\": escaped = True
                elif (state == "string" and char == '"') or (state == "char" and char == "'"):
                    state = "code"
            else:
                if char == "/" and next_char == "/": state = "line"; index += 1
                elif char == "/" and next_char == "*": state = "block"; index += 1
                elif char == '"': state = "string"
                elif char == "'": state = "char"
                elif char == "{": depth += 1
                elif char == "}":
                    depth -= 1
                    if depth == 0: return source[start:index + 1]
            index += 1
        raise AssertionError(f"unterminated extracted function: {signature}")

    @staticmethod
    def _extract_struct(source, signature):
        start = source.index(signature)
        end = source.index("};", start) + 2
        return source[start:end]

    def test_actual_private_stage_arena_query_source_fails_closed_without_writes(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        self.assertIsNotNone(compiler, "C++20 compiler required")
        source = (ROOT / "src/slippi_profile/tests/synctest_browser.cpp").read_text()
        actual = self._extract_struct(source, "struct StageArenaPreflight {") + "\n\n" + "\n\n".join(
            self._extract_function(source, signature) for signature in (
            "bool stage_arena_preflight_row(",
            "bool stage_arena_wire_anchor_matches(",
            "EMSCRIPTEN_KEEPALIVE const void *melee_web_synctest_stage_arena_inventory_query(",
        ))
        harness = r'''#include "native_dat.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <cstdio>

#define EMSCRIPTEN_KEEPALIVE
namespace melee_web {
struct GameplayWorldStageArenaInventory {
    NativeDatAllocationVisitStatus status = NativeDatAllocationVisitStatus::InvalidOwner;
    const void* world_storage = nullptr;
    uint64_t world_generation = 0;
    NativeDatAllocationInventory arena{};
};
}
uint32_t melee_web_synctest_stage_arena_diagnostic_storage[24]{};

bool wire_u32(const void* pointer, uint32_t& value) {
    if (!pointer) return false;
    if (pointer == melee_web_synctest_stage_arena_diagnostic_storage) { value = 0x1000; return true; }
    if (pointer == melee_web_synctest_stage_arena_diagnostic_storage + 8) { value = 0x1020; return true; }
    const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
    if (address > UINT32_MAX) return false;
    value = static_cast<uint32_t>(address);
    return true;
}

struct FakeMatch {
    melee_web::GameplayWorldStageArenaInventory inventory{};
    std::array<melee_web::NativeDatAllocationRange, 2> rows{};
    std::array<unsigned char, 16> owner_bytes{};
    bool live = true;
    bool abort_visit = false;
    unsigned queries = 0;
    melee_web::GameplayWorldStageArenaInventory diagnostic_stage_arena_allocations(
        std::size_t first, std::size_t count,
        bool (*visitor)(const melee_web::NativeDatAllocationRange&, void*) noexcept,
        void* context) noexcept {
        ++queries;
        auto result = inventory;
        if (!live || inventory.status != melee_web::NativeDatAllocationVisitStatus::Complete) {
            result.status = melee_web::NativeDatAllocationVisitStatus::InvalidOwner;
            return result;
        }
        if (first > inventory.arena.allocation_count ||
            count > inventory.arena.allocation_count - first ||
            (count == 0 && visitor) || (count != 0 && !visitor)) {
            result.status = melee_web::NativeDatAllocationVisitStatus::InvalidArgument;
            return result;
        }
        result.arena.visited_count = 0;
        for (std::size_t offset = 0; offset < count; ++offset) {
            if (abort_visit) {
                result.status = melee_web::NativeDatAllocationVisitStatus::Aborted;
                result.arena.status = melee_web::NativeDatAllocationVisitStatus::Aborted;
                return result;
            }
            const auto& row = rows[first + offset];
            if (!visitor(row, context)) {
                result.status = melee_web::NativeDatAllocationVisitStatus::Aborted;
                result.arena.status = melee_web::NativeDatAllocationVisitStatus::Aborted;
                return result;
            }
            ++result.arena.visited_count;
        }
        return result;
    }
};
std::unique_ptr<FakeMatch> match;

'''+actual+r'''

static std::unique_ptr<FakeMatch> make_match() {
    auto value = std::make_unique<FakeMatch>();
    value->inventory.status = melee_web::NativeDatAllocationVisitStatus::Complete;
    value->inventory.world_storage = reinterpret_cast<void*>(0x100);
    value->inventory.world_generation = UINT64_C(0x1234567887654321);
    value->inventory.arena.status = melee_web::NativeDatAllocationVisitStatus::Complete;
    value->inventory.arena.arena_storage = reinterpret_cast<void*>(0x200);
    value->inventory.arena.identity_token = reinterpret_cast<void*>(0x300);
    value->inventory.arena.allocation_count = 2;
    value->inventory.arena.total_bytes = 40;
    value->rows[0] = {0, reinterpret_cast<void*>(0x4000), 16};
    value->rows[1] = {1, reinterpret_cast<void*>(0x4200), 24};
    value->owner_bytes.fill(0x5a);
    return value;
}

static bool storage_equals(const std::array<uint32_t, 24>& expected) {
    return std::memcmp(melee_web_synctest_stage_arena_diagnostic_storage,
                       expected.data(), sizeof(expected)) == 0;
}
static std::array<uint32_t, 24> storage_snapshot() {
    std::array<uint32_t, 24> bytes{};
    std::memcpy(bytes.data(), melee_web_synctest_stage_arena_diagnostic_storage,
                sizeof(bytes));
    return bytes;
}
static std::array<unsigned char, 16> payload_snapshot() { return match->owner_bytes; }
[[maybe_unused]] static void healthy_metadata() {
    assert(melee_web_synctest_stage_arena_inventory_query(UINT32_MAX) != nullptr);
}
[[maybe_unused]] static void refuse_then_recover() {
    const auto wire = storage_snapshot();
    const auto payload = payload_snapshot();
    assert(melee_web_synctest_stage_arena_inventory_query(UINT32_MAX) == nullptr);
    assert(storage_equals(wire));
    assert(match->owner_bytes == payload);
    match = make_match();
    healthy_metadata();
}

int main() {
#ifdef __EMSCRIPTEN_PTHREADS__
    std::memset(melee_web_synctest_stage_arena_diagnostic_storage, 0xa5,
                sizeof(melee_web_synctest_stage_arena_diagnostic_storage));
    match = make_match();
    const auto threaded_wire = storage_snapshot();
    const auto threaded_owner = payload_snapshot();
    assert(melee_web_synctest_stage_arena_inventory_query(UINT32_MAX) == nullptr);
    assert(melee_web_synctest_stage_arena_inventory_query(0) == nullptr);
    assert(storage_equals(threaded_wire));
    assert(match->owner_bytes == threaded_owner && match->queries == 0);
    std::puts("actual extracted pthread query refused before owner/output mutation");
#else
    std::memset(melee_web_synctest_stage_arena_diagnostic_storage, 0,
                sizeof(melee_web_synctest_stage_arena_diagnostic_storage));
    match = make_match();
    const auto empty = storage_snapshot();
    assert(melee_web_synctest_stage_arena_inventory_query(0) == nullptr);
    assert(storage_equals(empty) && match->queries == 0);
    match.reset();
    assert(melee_web_synctest_stage_arena_inventory_query(UINT32_MAX) == nullptr);
    assert(storage_equals(empty));

    match = make_match();
    assert(melee_web_synctest_stage_arena_inventory_query(UINT32_MAX) != nullptr);
    auto* wire = melee_web_synctest_stage_arena_diagnostic_storage + 8;
    assert(wire[0] == 1 && wire[1] == 64 && wire[2] == 1 && wire[3] == 1);
    assert(wire[4] == 0x87654321 && wire[5] == 0x12345678 &&
           wire[6] == 0x100 && wire[7] == 0x200 && wire[8] == 0x300 &&
           wire[9] == 2 && wire[10] == 40 && wire[11] == UINT32_MAX &&
           wire[12] == 0 && wire[13] == 0 && wire[14] == 0 && wire[15] == 0);
    assert(melee_web_synctest_stage_arena_inventory_query(0) != nullptr);
    assert(wire[11] == 0 && wire[12] == 0x4000 && wire[13] == 16);
    const auto before_bad_index = storage_snapshot();
    const auto before_bad_payload = payload_snapshot();
    assert(melee_web_synctest_stage_arena_inventory_query(2) == nullptr);
    assert(storage_equals(before_bad_index) && match->owner_bytes == before_bad_payload);
    healthy_metadata();
    const auto valid_anchor_and_wire = storage_snapshot();
    melee_web_synctest_stage_arena_diagnostic_storage[0] = 0;
    const auto partial_anchor = storage_snapshot();
    assert(melee_web_synctest_stage_arena_inventory_query(UINT32_MAX) == nullptr);
    assert(storage_equals(partial_anchor) && match->owner_bytes == before_bad_payload);
    std::memcpy(melee_web_synctest_stage_arena_diagnostic_storage,
                valid_anchor_and_wire.data(), sizeof(valid_anchor_and_wire));
    healthy_metadata();

    match->rows[0].address = reinterpret_cast<void*>(0x1010); // aliases the 96-byte observer
    refuse_then_recover();
    match->rows[0].address = reinterpret_cast<void*>(UINT64_C(0xfffffff0));
    assert(melee_web_synctest_stage_arena_inventory_query(UINT32_MAX) != nullptr);
    assert(melee_web_synctest_stage_arena_inventory_query(0) != nullptr);
    assert(wire[12] == UINT32_C(0xfffffff0) && wire[13] == 16);
    match = make_match();
    healthy_metadata();
    match->abort_visit = true;
    refuse_then_recover();
    match->inventory.world_generation++;
    refuse_then_recover();
    match->inventory.world_storage = reinterpret_cast<void*>(0x104);
    refuse_then_recover();
    match->inventory.arena.arena_storage = reinterpret_cast<void*>(0x204);
    refuse_then_recover();
    match->inventory.arena.identity_token = reinterpret_cast<void*>(0x304);
    refuse_then_recover();
    match->inventory.arena.total_bytes++;
    refuse_then_recover();
    match->inventory.arena.allocation_count = 1;
    refuse_then_recover();
    match->inventory.world_storage = reinterpret_cast<void*>(UINT64_C(0x100000000));
    refuse_then_recover();
    match->inventory.world_storage = reinterpret_cast<void*>(0x100);
    match->rows[0].address = reinterpret_cast<void*>(UINT64_C(0xfffffff1));
    refuse_then_recover();
    match->live = false;
    refuse_then_recover();
    std::puts("actual stage-arena query source nonmutation controls passed");
#endif
}
'''
        source_file = self.work / "stage_arena_query_controls.cpp"
        source_file.write_text(harness)
        for label, extra_flags, expected in [
            ("ordinary", [], "actual stage-arena query source nonmutation controls passed"),
            ("pthread", ["-D__EMSCRIPTEN_PTHREADS__"],
             "actual extracted pthread query refused before owner/output mutation"),
        ]:
            binary = self.work / f"stage_arena_query_controls_{label}"
            build = subprocess.run([
                compiler, "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror",
                "-UNDEBUG", *extra_flags, "-Isrc", str(source_file), "-o", str(binary),
            ], cwd=ROOT, capture_output=True, text=True, timeout=120)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(binary)], cwd=ROOT, capture_output=True,
                                 text=True, timeout=20)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn(expected, run.stdout)


if __name__ == "__main__":
    unittest.main()
