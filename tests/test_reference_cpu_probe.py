"""Compile the observer's opt-in configuration and serialization boundaries."""
from pathlib import Path
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp"


class ReferenceCpuProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang++") or shutil.which("g++")
        if not compiler:
            raise unittest.SkipTest("A native C++ compiler is required")
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name)
        source = SOURCE.read_text()
        constants = source[source.index("constexpr size_t CPU_PROBE_TICK_WINDOW_MAX"):
                           source.index("constexpr std::array<u8, 32> EXPECTED_DOL_SHA256_BYTES")]
        constants = "\n".join(line for line in constants.splitlines()
                              if "TICK_WINDOW_MAX" in line or "MAX_JSON_BYTES" in line)
        helpers = source[source.index("struct CpuProbePoint"):
                         source.index("}  // namespace", source.index("struct CpuProbePoint"))]
        harness = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
using u8 = uint8_t; using u32 = uint32_t; using u64 = uint64_t;
constexpr u32 WHOLE_SESSION_MAX_MATCHES = 64;
std::string Env(const char* key) { const char* value = std::getenv(key); return value ? value : ""; }
bool ActivationRequested() { return Env("MWRC_ENABLE") == "1"; }
''' + constants + helpers + r'''
int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string action(argv[1]);
  if (action == "helpers") {
    u32 result = 42;
    for (const auto bad : {"", "-1", "+1", "1 ", " 1", "0x10", "1.0", "4294967296"})
      assert(!ParseBoundedDecimal(bad, UINT32_MAX, &result));
    assert(ParseBoundedDecimal("4294967295", UINT32_MAX, &result) && result == UINT32_MAX);
    assert(ParseBoundedDecimal("0", 0, &result) && result == 0);
    assert(!ParseBoundedDecimal("1", 0, &result));
    assert(!ParseBoundedDecimal("64", 63, &result));
    assert(ParseBoundedDecimal("63", 63, &result) && result == 63);
    std::string output;
    assert(AppendHex(&output, 0xfedcba9876543210ULL, 16));
    assert(output == "fedcba9876543210");
    assert(!AppendHex(&output, 0, 17));
    const u8 bytes[] = {0, 0x80, 0xff};
    assert(AppendHexBytes(&output, bytes, 3));
    assert(output == "fedcba98765432100080ff");
    output.assign(CPU_PROBE_MAX_JSON_BYTES - 1, 'x');
    assert(!AppendHexBytes(&output, bytes, 1));
    assert(AppendBounded(&output, "y"));
    assert(!AppendBounded(&output, "z"));
    assert(output.back() == 'y');
    for (const auto& point : CPU_PROBE_POINTS)
      assert(FindCpuProbePoint(point.address) == &point);
    assert(!FindCpuProbePoint(0x80000000));
    return 0;
  }
  const auto& settings = CpuProbeEnvironment();
  if (action == "absent") assert(!settings.present && !CpuProbeEnabled());
  else if (action == "invalid") assert(settings.present && !settings.valid && !CpuProbeEnabled() && !settings.error.empty());
  else if (action == "disabled") assert(settings.valid && !CpuProbeEnabled());
  else if (action == "enabled") assert(settings.valid && CpuProbeEnabled());
  else assert(false);
}
'''
        path = directory / "probe.cpp"
        path.write_text(harness)
        cls.binary = directory / "probe"
        built = subprocess.run([compiler, "-std=c++17", "-Wall", "-Werror", str(path),
                                "-o", str(cls.binary)], capture_output=True, text=True)
        if built.returncode:
            raise RuntimeError(built.stderr)

    def run_probe(self, action, **settings):
        environment = {k: v for k, v in os.environ.items() if not k.startswith("MWRC_")}
        environment.update(settings)
        result = subprocess.run([str(self.binary), action], env=environment,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_compiled_bounds_and_bit_serialization(self):
        self.run_probe("helpers")

    def test_opt_in_requires_complete_bounded_configuration(self):
        self.run_probe("absent")
        good = dict(MWRC_ENABLE="1", MWRC_CPU_PROBE_OUTPUT="probe.json",
                    MWRC_CPU_PROBE_MATCH="0", MWRC_CPU_PROBE_FIRST_TICK="100",
                    MWRC_CPU_PROBE_LAST_TICK="163")
        self.run_probe("enabled", **good)
        self.run_probe("disabled", **dict(good, MWRC_ENABLE="0"))
        for patch in ({"MWRC_CPU_PROBE_OUTPUT": ""}, {"MWRC_CPU_PROBE_MATCH": "64"},
                      {"MWRC_CPU_PROBE_FIRST_TICK": "-1"}, {"MWRC_CPU_PROBE_LAST_TICK": "99"},
                      {"MWRC_CPU_PROBE_LAST_TICK": "164"}, {"MWRC_CPU_PROBE_MATCH": ""},
                      {"MWRC_CPU_PROBE_LAST_TICK": "4294967296"}):
            with self.subTest(patch=patch):
                self.run_probe("invalid", **dict(good, **patch))

    def test_compiled_probe_inventory_matches_verified_profile(self):
        source = SOURCE.read_text()
        found = re.findall(r'\{"([a-z0-9_]+)", (0x[0-9a-f]+), (0x[0-9a-f]+)\}', source)
        expected = json.loads((ROOT / "tools/cpu-register-gale01r2.json").read_text())["probes"]
        self.assertEqual([(label, int(pc, 16), int(word, 16)) for label, pc, word in found],
                         [(row["label"], int(row["address"], 16), int(row["expected_word"], 16))
                          for row in expected])
