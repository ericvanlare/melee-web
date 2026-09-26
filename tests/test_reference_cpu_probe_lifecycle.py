"""Compile the observer's CPU-probe window and memory-safety paths.

The production observer is deliberately not linked into the normal test binary:
it depends on the full Dolphin checkout.  This test extracts the small probe
recording methods verbatim and supplies only a bounded guest-memory/System
stand-in.  The assertions therefore exercise the production control flow,
rather than checking that source text contains particular guards.
"""

from pathlib import Path
import json
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp"


def _method(source: str, start: str, end: str) -> str:
    return source[source.index(start) : source.index(end, source.index(start))]


class ReferenceCpuProbeLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang++") or shutil.which("g++")
        if compiler is None:
            raise unittest.SkipTest("A native C++ compiler is required")

        source = SOURCE.read_text(encoding="utf-8")
        constant_names = (
            "CPU_PROBE_MAX_RECORDS",
            "CPU_PROBE_MAX_JSON_BYTES",
            "CPU_PROBE_STACK_SIZE",
            "CPU_PROBE_FIGHTER_HEAD_SIZE",
            "CPU_PROBE_FIGHTER_CPU_SIZE",
            "CPU_PROBE_FIGHTER_FLAGS_OFFSET",
            "CPU_PROBE_RANDOM_ADDRESS",
        )
        constants = "\n".join(
            next(
                line.strip()
                for line in source.splitlines()
                if line.strip().startswith("constexpr") and f" {name} " in line
            )
            for name in constant_names
        )
        records = source[
            source.index("struct CpuProbeFighterRecord") : source.index(
                "constexpr bool IsGuestRange", source.index("struct CpuProbeFighterRecord")
            )
        ]
        ranges = source[
            source.index("constexpr bool IsGuestRange") : source.index(
                "void PutU16", source.index("constexpr bool IsGuestRange")
            )
        ]
        points = source[
            source.index("struct CpuProbePoint") : source.index(
                "bool ParseBoundedDecimal", source.index("struct CpuProbePoint")
            )
        ]
        json_escape = _method(source, "std::string JsonEscape", "std::string Env")
        bounded_helpers = "\n".join(
            [
                _method(source, "bool AppendBounded", "bool AppendHex"),
                _method(source, "bool AppendHex", "bool AppendHexBytes"),
                _method(source, "bool AppendHexBytes", "}  // namespace"),
            ]
        )
        read_methods = "\n".join(
            [
                _method(source, "  bool ReadBytes", "  bool ReadU32"),
                _method(source, "  bool ReadU32", "  bool ReadMem1"),
                _method(source, "  bool ReadMem1", "  void CloseCpuProbe"),
                _method(source, "  void CloseCpuProbe", "  void RecordCpuProbe"),
                _method(source, "  void RecordCpuProbe", "  bool AddSlice"),
                _method(source, "  bool BuildCpuProbeJson", "  bool WriteCpuProbe"),
                _method(source, "  bool WriteCpuProbe", "  void WriterMain"),
            ]
        )

        harness = r'''
#include <array>
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
using s64 = std::int64_t;

namespace Core {
constexpr u32 MEM1_BASE = 0x80000000U;
struct Memory {
  std::vector<u8> bytes = std::vector<u8>(0x01800000);
  const u8* GetPointerForRange(u32 address, size_t size) const {
    if (address < MEM1_BASE ||
        static_cast<u64>(address) - MEM1_BASE + size > bytes.size())
      return nullptr;
    return bytes.data() + (address - MEM1_BASE);
  }
  u8* GetPointerForRange(u32 address, size_t size) {
    return const_cast<u8*>(static_cast<const Memory&>(*this).GetPointerForRange(address, size));
  }
};
struct System {
  Memory memory;
  Memory& GetMemory() { return memory; }
};
}  // namespace Core

namespace PowerPC {
struct Fpr {
  u64 value = 0;
  u64 PS0AsU64() const { return value; }
};
struct PowerPCState {
  u32 gpr[32]{};
  u32 spr[9]{};
  Fpr ps[7]{};
};
}  // namespace PowerPC

namespace File {
enum class AccessMode { Write };
enum class OpenMode { Create };
class DirectIOFile {
public:
  DirectIOFile(const std::string&, AccessMode, OpenMode) : open(true) {}
  bool IsOpen() const { return open; }
  bool Write(const u8* bytes, size_t size) {
    if (!open) return false;
    contents.assign(bytes, bytes + size);
    return true;
  }
  bool Flush() const { return open; }
  bool Close() { open = false; return true; }
  std::vector<u8> contents;
private:
  bool open;
};
}  // namespace File

namespace ReferenceCapture {
namespace {
''' + constants + records + ranges + points + json_escape + bounded_helpers + r'''
}  // namespace

constexpr char EXPECTED_DOL_SHA256[] =
    "dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646";

struct ProbeHarness {
  void SetInvalid(std::string reason) {
    if (!invalid) error = std::move(reason);
    invalid = true;
  }

  bool invalid = false;
  std::string error;
  bool cpu_probe_configured = true;
  bool cpu_probe_valid = true;
  bool cpu_probe_closed = false;
  bool cpu_probe_effect_group_found = false;
  std::atomic<bool> cpu_probe_published{false};
  std::string cpu_probe_output_path = "probe.json";
  u32 cpu_probe_match = 2;
  u32 cpu_probe_first_tick = 100;
  u32 cpu_probe_last_tick = 103;
  size_t cpu_probe_record_count = 0;
  std::unique_ptr<CpuProbeRecord[]> cpu_probe_records =
      std::make_unique<CpuProbeRecord[]>(CPU_PROBE_MAX_RECORDS);
  std::array<u32, 4> fighter_pointers{};
  std::array<bool, 4> fighter_present{};
  std::array<std::array<u32, 2>, 4> fighter_entity_pointers{};
  std::array<std::array<u32, 2>, 4> fighter_entity_kinds{};
  std::array<u8, 4> fighter_entity_count{};
  u32 cpu_probe_samus_effect_palette_address = 0;
  bool match_active = true;
  bool setup_ready = true;
  u32 match_index = 2;
  std::string capture_id = "capture";
  std::string sequence_id = "sequence";

''' + read_methods + r'''
};
}  // namespace ReferenceCapture

static void StoreBE32(Core::System* system, u32 address, u32 value) {
  u8* bytes = system->GetMemory().GetPointerForRange(address, 4);
  assert(bytes != nullptr);
  bytes[0] = static_cast<u8>(value >> 24);
  bytes[1] = static_cast<u8>(value >> 16);
  bytes[2] = static_cast<u8>(value >> 8);
  bytes[3] = static_cast<u8>(value);
}

static const ReferenceCapture::CpuProbePoint& ProbePoint() {
  return ReferenceCapture::CPU_PROBE_POINTS[0];
}

static const ReferenceCapture::CpuProbePoint& SamusEffectProbePoint() {
  assert(ReferenceCapture::CPU_PROBE_POINTS[32].expected_word == 0xa0c30000);
  return ReferenceCapture::CPU_PROBE_POINTS[32];
}

static const ReferenceCapture::CpuProbePoint& SamusEffectBankLoadProbePoint() {
  assert(ReferenceCapture::CPU_PROBE_POINTS[33].address == 0x803984f4);
  assert(ReferenceCapture::CPU_PROBE_POINTS[33].expected_word == 0x7c0802a6);
  return ReferenceCapture::CPU_PROBE_POINTS[33];
}

static const ReferenceCapture::CpuProbePoint& SamusEffectTlutProbePoint() {
  assert(ReferenceCapture::CPU_PROBE_POINTS[34].address == 0x8033f024);
  assert(ReferenceCapture::CPU_PROBE_POINTS[34].expected_word == 0x38000000);
  return ReferenceCapture::CPU_PROBE_POINTS[34];
}

static const ReferenceCapture::CpuProbePoint& SamusEffectParticleProbePoint() {
  assert(ReferenceCapture::CPU_PROBE_POINTS[35].address == 0x80398c04);
  assert(ReferenceCapture::CPU_PROBE_POINTS[35].expected_word == 0x7c0802a6);
  return ReferenceCapture::CPU_PROBE_POINTS[35];
}

static void Prepare(ReferenceCapture::ProbeHarness* harness, Core::System* system,
                    PowerPC::PowerPCState* state) {
  StoreBE32(system, ProbePoint().address, ProbePoint().expected_word);
  state->gpr[1] = 0x80010000;
  state->spr[8] = 0x80381234;
  state->gpr[3] = 0x12345678;
  state->ps[0].value = 0x0123456789abcdefULL;
  (void)harness;
}

int main() {
  using ReferenceCapture::ProbeHarness;

  // The window is inert until all three production predicates hold.
  {
    Core::System system;
    PowerPC::PowerPCState state;
    ProbeHarness harness;
    Prepare(&harness, &system, &state);
    harness.cpu_probe_configured = false;
    harness.RecordCpuProbe(&system, ProbePoint().address, ProbePoint(), &state, 100);
    assert(harness.cpu_probe_record_count == 0);
    assert(!harness.cpu_probe_published.load());

    harness.cpu_probe_configured = true;
    harness.setup_ready = false;
    harness.RecordCpuProbe(&system, ProbePoint().address, ProbePoint(), &state, 100);
    assert(harness.cpu_probe_record_count == 0);

    harness.setup_ready = true;
    harness.match_index = harness.cpu_probe_match + 1;
    harness.RecordCpuProbe(&system, ProbePoint().address, ProbePoint(), &state, 1000);
    assert(harness.cpu_probe_record_count == 0);
    assert(!harness.cpu_probe_closed);
  }

  // A reset counter and an event from another match are ignored; neither can
  // close the target window before the target match reaches its final tick.
  {
    Core::System system;
    PowerPC::PowerPCState state;
    ProbeHarness harness;
    Prepare(&harness, &system, &state);
    harness.match_index = harness.cpu_probe_match + 1;
    harness.RecordCpuProbe(&system, ProbePoint().address, ProbePoint(), &state, 1000);
    assert(!harness.cpu_probe_closed);
    harness.match_index = harness.cpu_probe_match;
    harness.RecordCpuProbe(&system, ProbePoint().address, ProbePoint(), &state, 99);
    assert(!harness.cpu_probe_closed);
    assert(harness.cpu_probe_record_count == 0);
    harness.RecordCpuProbe(&system, ProbePoint().address, ProbePoint(), &state, 104);
    assert(harness.cpu_probe_closed);
    assert(harness.cpu_probe_published.load(std::memory_order_acquire));
  }

  // A valid record closes into an immutable published snapshot.  Calling the
  // production recorder again after close cannot append or alter serialization.
  {
    Core::System system;
    PowerPC::PowerPCState state;
    ProbeHarness harness;
    Prepare(&harness, &system, &state);
    harness.RecordCpuProbe(&system, ProbePoint().address, ProbePoint(), &state, 100);
    assert(harness.cpu_probe_record_count == 1);
    std::string before_close;
    harness.CloseCpuProbe();
    assert(harness.cpu_probe_closed);
    assert(harness.cpu_probe_published.load(std::memory_order_acquire));
    assert(harness.BuildCpuProbeJson(&before_close));
    std::cout << before_close;
    state.gpr[3] = 0;
    harness.RecordCpuProbe(&system, ProbePoint().address, ProbePoint(), &state, 101);
    harness.CloseCpuProbe();
    std::string after_close;
    assert(harness.BuildCpuProbeJson(&after_close));
    assert(before_close == after_close);
    assert(harness.cpu_probe_record_count == 1);
  }

  // Verified instruction mismatch and all guest ranges outside MEM1 fail the
  // capture instead of publishing a partial CPU record.
  {
    Core::System system;
    PowerPC::PowerPCState state;
    ProbeHarness wrong_instruction;
    Prepare(&wrong_instruction, &system, &state);
    StoreBE32(&system, ProbePoint().address, 0);
    wrong_instruction.RecordCpuProbe(&system, ProbePoint().address, ProbePoint(), &state, 100);
    assert(wrong_instruction.invalid);
    assert(wrong_instruction.error.find("instruction differs") != std::string::npos);
    assert(wrong_instruction.cpu_probe_record_count == 0);

    Prepare(&wrong_instruction, &system, &state);
    wrong_instruction.invalid = false;
    wrong_instruction.error.clear();
    state.gpr[1] = 0x81800000;
    wrong_instruction.RecordCpuProbe(&system, ProbePoint().address, ProbePoint(), &state, 100);
    assert(wrong_instruction.invalid);
    assert(wrong_instruction.error.find("outside MEM1") != std::string::npos);
    assert(wrong_instruction.cpu_probe_record_count == 0);
  }

  // The fixed record bound is checked by the compiled production guard.
  {
    Core::System system;
    PowerPC::PowerPCState state;
    ProbeHarness overflow;
    Prepare(&overflow, &system, &state);
    overflow.cpu_probe_record_count = ReferenceCapture::CPU_PROBE_MAX_RECORDS;
    overflow.RecordCpuProbe(&system, ProbePoint().address, ProbePoint(), &state, 100);
    assert(overflow.invalid);
    assert(overflow.error.find("record bound exceeded") != std::string::npos);
    assert(overflow.cpu_probe_record_count == ReferenceCapture::CPU_PROBE_MAX_RECORDS);
  }

  // Capture completion cannot silently succeed with an unfinalized window.
  {
    Core::System system;
    PowerPC::PowerPCState state;
    ProbeHarness missing_close;
    Prepare(&missing_close, &system, &state);
    missing_close.RecordCpuProbe(&system, ProbePoint().address, ProbePoint(), &state, 100);
    assert(missing_close.cpu_probe_record_count == 1);
    assert(!missing_close.WriteCpuProbe());
    assert(missing_close.invalid);
    assert(missing_close.error.find("did not close") != std::string::npos);
  }

  // The Samus-only diagnostic reads the original relative group and palette
  // target without mutating guest memory; low MEM1 aliases are sampled through
  // their equivalent cached address.
  {
    Core::System system;
    PowerPC::PowerPCState state;
    ProbeHarness effect_probe;
    Prepare(&effect_probe, &system, &state);
    const auto& point = SamusEffectProbePoint();
    StoreBE32(&system, point.address, point.expected_word);
    state.gpr[4] = 0x80020000;
    StoreBE32(&system, 0x80020000, 1);
    StoreBE32(&system, 0x80020004, 0x100);
    StoreBE32(&system, 0x80020100, 1);
    StoreBE32(&system, 0x80020104, 9);
    StoreBE32(&system, 0x8002010c, 64);
    StoreBE32(&system, 0x80020110, 64);
    StoreBE32(&system, 0x80020114, 0);
    StoreBE32(&system, 0x80020118, 0x40);
    StoreBE32(&system, 0x8002011c, 0x80a8812a);
    std::fill(system.GetMemory().GetPointerForRange(0x80aa812a, 512),
              system.GetMemory().GetPointerForRange(0x80aa812a, 512) + 512, 0x5a);
    std::fill(system.GetMemory().GetPointerForRange(0x80a8812a, 512),
              system.GetMemory().GetPointerForRange(0x80a8812a, 512) + 512, 0x6b);
    effect_probe.RecordCpuProbe(&system, point.address, point, &state, 100);
    assert(effect_probe.cpu_probe_record_count == 1);
    const auto& record = effect_probe.cpu_probe_records[0];
    assert(record.effect_group_present && record.effect_palette_readable);
    assert(record.effect_bank_base == 0x80020000);
    assert(record.effect_group_address == 0x80020100);
    assert(record.effect_palette_address == 0x00aa812a);
    assert(record.effect_palette[0] == 0x5a && record.effect_palette[511] == 0x5a);
    assert(record.effect_literal_palette_readable);
    assert(record.effect_literal_palette[0] == 0x6b && record.effect_literal_palette[511] == 0x6b);
    std::string json;
    effect_probe.CloseCpuProbe();
    assert(effect_probe.BuildCpuProbeJson(&json));
    assert(json.find("\"samus_effect_group\"") != std::string::npos);
    assert(json.find("\"palette_address\":\"0x00aa812a\",\"palette_readable\":true") !=
           std::string::npos);
    assert(json.find("\"literal_palette_address\":\"0x80a8812a\",\"literal_palette_readable\":true") !=
           std::string::npos);
    assert(json.find("\"literal_palette_512\":\"0x6b6b") != std::string::npos);
    assert(json.find("\"palette_readable\":true") != std::string::npos);
    assert(json.find("\"palette_512\":\"0x5a5a") != std::string::npos);
  }

  // The Samus bank can be located before match setup or outside the selected
  // CPU-register window. Preserve the first exact authored group there without
  // admitting unrelated pre-match calls into the bounded probe stream.
  {
    Core::System system;
    PowerPC::PowerPCState state;
    ProbeHarness effect_probe;
    Prepare(&effect_probe, &system, &state);
    const auto& point = SamusEffectProbePoint();
    StoreBE32(&system, point.address, point.expected_word);
    state.gpr[4] = 0x80020000;
    StoreBE32(&system, 0x80020000, 1);
    StoreBE32(&system, 0x80020004, 0x100);
    StoreBE32(&system, 0x80020100, 1);
    StoreBE32(&system, 0x80020104, 9);
    StoreBE32(&system, 0x8002010c, 64);
    StoreBE32(&system, 0x80020110, 64);
    StoreBE32(&system, 0x80020114, 0);
    StoreBE32(&system, 0x80020118, 0x40);
    StoreBE32(&system, 0x8002011c, 0x80a8812a);
    std::fill(system.GetMemory().GetPointerForRange(0x80aa812a, 512),
              system.GetMemory().GetPointerForRange(0x80aa812a, 512) + 512, 0x7c);
    std::fill(system.GetMemory().GetPointerForRange(0x80a8812a, 512),
              system.GetMemory().GetPointerForRange(0x80a8812a, 512) + 512, 0x3d);
    effect_probe.match_active = false;
    effect_probe.setup_ready = false;
    effect_probe.match_index = 0;
    effect_probe.RecordCpuProbe(&system, point.address, point, &state, 0);
    assert(effect_probe.cpu_probe_record_count == 1);
    assert(effect_probe.cpu_probe_effect_group_found);
    const auto& record = effect_probe.cpu_probe_records[0];
    assert(record.match == 0 && record.source_tick == 0);
    assert(record.effect_group_present && record.effect_palette_readable);
    assert(record.effect_palette[0] == 0x7c && record.effect_palette[511] == 0x7c);
    assert(record.effect_literal_palette_readable);
    assert(record.effect_literal_palette[0] == 0x3d && record.effect_literal_palette[511] == 0x3d);
  }

  // Trace the post-Locate texture-table pointer at bank registration, then
  // require GXInitTlutObj to consume that exact pointer before reporting use.
  {
    Core::System system;
    PowerPC::PowerPCState state;
    ProbeHarness effect_probe;
    Prepare(&effect_probe, &system, &state);
    const auto& load = SamusEffectBankLoadProbePoint();
    const auto& tlut = SamusEffectTlutProbePoint();
    const auto& particle = SamusEffectParticleProbePoint();
    StoreBE32(&system, load.address, load.expected_word);
    StoreBE32(&system, tlut.address, tlut.expected_word);
    StoreBE32(&system, particle.address, particle.expected_word);
    state.gpr[3] = 34;
    state.gpr[5] = 0x80030000;
    StoreBE32(&system, state.gpr[5], 1);
    StoreBE32(&system, state.gpr[5] + 4, 0x80030100);
    StoreBE32(&system, 0x80030100, 1);
    StoreBE32(&system, 0x80030104, 9);
    StoreBE32(&system, 0x8003010c, 64);
    StoreBE32(&system, 0x80030110, 64);
    StoreBE32(&system, 0x80030114, 0);
    StoreBE32(&system, 0x80030118, 0x80030200);
    StoreBE32(&system, 0x8003011c, 0x01d3ac8a);
    effect_probe.RecordCpuProbe(&system, load.address, load, &state, 99);
    assert(effect_probe.cpu_probe_record_count == 1);
    const auto& loaded = effect_probe.cpu_probe_records[0];
    assert(loaded.samus_effect_loaded_group_present);
    assert(loaded.samus_effect_loaded_bank == 34);
    assert(loaded.samus_effect_loaded_texture_base == 0x80030000);
    assert(loaded.samus_effect_loaded_group_address == 0x80030100);
    assert(loaded.samus_effect_loaded_image_address == 0x80030200);
    assert(loaded.samus_effect_loaded_palette_address == 0x01d3ac8a);

    state.gpr[5] = 34;
    state.gpr[6] = 34000;
    state.gpr[7] = 0;
    effect_probe.RecordCpuProbe(&system, particle.address, particle, &state, 102);
    assert(effect_probe.cpu_probe_record_count == 2);
    const auto& spawned = effect_probe.cpu_probe_records[1];
    assert(spawned.samus_effect_particle_spawn);
    assert(spawned.samus_effect_particle_bank == 34);
    assert(spawned.samus_effect_particle_kind == 34000);
    assert(spawned.samus_effect_particle_group == 0);

    state.gpr[5] = 0x80030000;
    state.gpr[4] = loaded.samus_effect_loaded_palette_address;
    effect_probe.RecordCpuProbe(&system, tlut.address, tlut, &state, 102);
    assert(effect_probe.cpu_probe_record_count == 3);
    const auto& consumed = effect_probe.cpu_probe_records[2];
    assert(consumed.samus_effect_gx_tlut_call);
    assert(consumed.samus_effect_gx_tlut_address == 0x01d3ac8a);

    effect_probe.CloseCpuProbe();
    std::string json;
    assert(effect_probe.BuildCpuProbeJson(&json));
    assert(json.find("\"samus_effect_loaded_group\":{\"bank\":34") !=
           std::string::npos);
    assert(json.find("\"palette_address\":\"0x01d3ac8a\"}") !=
           std::string::npos);
    assert(json.find("\"samus_effect_gx_tlut\":{\"palette_address\":\"0x01d3ac8a\"}") !=
           std::string::npos);
    assert(json.find("\"samus_effect_particle_spawn\":{\"bank\":34,\"kind\":34000,\"texture_group\":0}") !=
           std::string::npos);
  }
  return 0;
}
'''

        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name)
        source_path = directory / "cpu_probe_lifecycle.cpp"
        source_path.write_text(harness, encoding="utf-8")
        cls.binary = directory / "cpu_probe_lifecycle"
        built = subprocess.run(
            [compiler, "-std=c++17", "-Wall", "-Werror", str(source_path), "-o", str(cls.binary)],
            capture_output=True,
            text=True,
        )
        if built.returncode:
            raise RuntimeError(built.stderr)

    def test_compiled_probe_window_lifecycle_and_memory_guards(self):
        checked = subprocess.run([str(self.binary)], capture_output=True, text=True)
        self.assertEqual(checked.returncode, 0, checked.stderr)
        # Parse the production serializer's output independently, including raw
        # bits and the binding fields needed by an external run receipt.
        document = json.loads(checked.stdout)
        self.assertEqual(document["schema"], "melee-web-cpu-register-probe")
        self.assertTrue(document["diagnostic_only"])
        self.assertTrue(document["window_complete"])
        self.assertEqual(document["capture_id"], "capture")
        self.assertEqual(document["sequence_id"], "sequence")
        self.assertEqual(document["dol_sha256"],
                         "dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646")
        self.assertEqual(document["record_count"], len(document["records"]))
        record, = document["records"]
        self.assertEqual(record["source_tick"], 100)
        self.assertEqual(record["lr"], "0x80381234")
        self.assertEqual(len(record["gpr"]), 32)
        self.assertEqual(record["gpr"][3], "0x12345678")
        self.assertEqual(len(record["fpr"]), 7)
        self.assertEqual(record["fpr"][0], "0x0123456789abcdef")


if __name__ == "__main__":
    unittest.main()
