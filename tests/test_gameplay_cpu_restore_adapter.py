"""Compile the actual private CPU adapter include against explicit leaf models."""
import ast
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPO_ROOT = Path(os.environ.get(
    "MELEE_CPU_RESTORE_REPO_ROOT", Path(__file__).resolve().parents[1])).resolve()
CANDIDATE_ROOT = Path(os.environ.get(
    "MELEE_CPU_RESTORE_CANDIDATE_ROOT", Path(__file__).resolve().parents[1])).resolve()
OUTPUT_ROOT = Path(os.environ.get(
    "MELEE_CPU_RESTORE_OUTPUT_ROOT", REPO_ROOT / "work")).resolve()
sys.path.insert(0,str(REPO_ROOT / "tests"))
from owned_test_workspace import OwnedWorkspaceTests

STUB_CHECKPOINT_HEADER = r'''#pragma once
#include "gameplay_cpu_commit.hpp"
#include "gameplay_cpu_statics.hpp"
#include <array>
#include <cstdint>
#include <memory>

struct CpuRestoreFixtureSnapshot {
    std::uint32_t action, audio, flow, bootstrap, context, payload, rng;
    std::array<std::uint8_t,44> pad;
    std::array<std::int32_t,9> transfer;
};
CpuRestoreFixtureSnapshot melee_web_cpu_restore_model_snapshot();
void melee_web_cpu_restore_model_apply(const CpuRestoreFixtureSnapshot&);
bool melee_web_cpu_restore_model_equal(const CpuRestoreFixtureSnapshot&);
void melee_web_cpu_restore_mark_disarm();
void melee_web_cpu_restore_mark_destroy();

namespace melee_web {
class GameplayMatchSession;
class GameplayCpuCheckpoint final {
public:
    static std::unique_ptr<GameplayCpuCheckpoint> capture(
        const GameplayMatchSession&,std::unique_ptr<GameplayCpuStatics>) noexcept;
    ~GameplayCpuCheckpoint();
    bool begin_record(const GameplayMatchSession&) noexcept;
    bool seal_post(const GameplayMatchSession&) noexcept;
    GameplayCpuCommitResult restore(const GameplayMatchSession&) noexcept;
    bool finish_replay(const GameplayMatchSession&) noexcept;
    void disarm() noexcept;
    const CpuComparison& comparison() const noexcept {return comparison_;}
    std::uint32_t checked_domains() const noexcept {return checked_domains_;}
    std::uint32_t matched_domains() const noexcept {return matched_domains_;}
    bool source_owners_retired(const GameplayMatchSession*) noexcept;
private:
    enum class Phase { Captured, Recording, Sealed, Replaying, Finished, Failed, Retired };
    GameplayCpuCheckpoint()=default;
    std::unique_ptr<GameplayCpuStatics> statics_;
    GameplayCpuCommitOwners commit_{};
    MeleeWebActionInterval* actions_{};
    CpuRestoreFixtureSnapshot pre_{},post_{};
    std::uint64_t effect_token_{};
    std::uint32_t pre_frame_{},post_frame_{};
    Phase phase_=Phase::Captured;
    bool source_step_possible_=false,retired_=false;
    CpuComparison comparison_{};
    std::uint32_t checked_domains_=0,matched_domains_=0;
};
}
'''


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_record(path, command, result):
    Path(path).write_text(json.dumps({
        "command": [str(part) for part in command],
        "returncode": result.returncode,
        "stdout": result.stdout,
        "stderr": result.stderr,
    }, indent=2) + "\n")


def source_overlay(output_dir):
    overlay = output_dir / "src"
    candidate_src = CANDIDATE_ROOT / "src"
    candidate_paths = {
        path.relative_to(candidate_src).as_posix()
        for path in candidate_src.rglob("*") if path.is_file()
    }
    source_root = REPO_ROOT / "src"
    for source in source_root.rglob("*"):
        relative = source.relative_to(source_root)
        destination = overlay / relative
        if source.is_dir():
            destination.mkdir(parents=True, exist_ok=True)
            continue
        destination.parent.mkdir(parents=True, exist_ok=True)
        candidate = candidate_src / relative
        target = candidate if relative.as_posix() in candidate_paths else source
        destination.symlink_to(target)
    # Candidate-only files may not exist in the frozen checkout yet. Add those
    # exact candidate paths to the overlay without changing the checkout or
    # broadening the compile input set.
    for relative_name in candidate_paths:
        candidate = candidate_src / relative_name
        if not candidate.is_file():
            continue
        destination = overlay / relative_name
        if not destination.exists() and not destination.is_symlink():
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.symlink_to(candidate)
    return overlay


def extract_fixture_observations(probe):
    text = Path(probe).read_text()
    start = text.index("constexpr uint32_t rng_profile_offset=")
    end = text.index("struct InitializerDiagnostic", start)
    block = text[start:end].rstrip()
    required = ("struct RngProfileObservation", "struct Observation", "struct TransferDiagnostic",
                "sizeof(Observation)==1280", "MELEE_WEB_PAD_STATE_BYTES==822")
    if any(token not in block for token in required):
        raise ValueError("The actual snapshot fixture declaration block changed shape")
    prefix = "uint32_t rng_profile_expected_callback_frame="
    expected = [line for line in text.splitlines() if line.startswith(prefix)]
    if len(expected) != 1:
        raise ValueError("Actual fixture expected-callback declaration changed")
    return block + "\n" + expected[0] + "\n"


class GameplayCpuRestoreAdapterTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.ordinary_workspace = None
        if "MELEE_CPU_RESTORE_OUTPUT_ROOT" not in os.environ:
            cls.ordinary_workspace = cls.new_workspace(REPO_ROOT,"cpu-restore-adapter-")

    def test_actual_adapter_fixed_interval_controls(self):
        sdk = REPO_ROOT / ".deps/emsdk"
        compiler = sdk / "upstream/emscripten"
        config = sdk / ".emscripten"
        if not (compiler / "emcc.py").is_file() or not config.is_file():
            self.skipTest("Pinned Emscripten SDK unavailable for the focused Wasm32 adapter control")
        node_setting = None
        for statement in ast.parse(config.read_text()).body:
            if (isinstance(statement, ast.Assign) and
                    any(isinstance(target, ast.Name) and target.id == "NODE_JS" for target in statement.targets)):
                node_setting = ast.literal_eval(statement.value)
        self.assertIsInstance(node_setting, str)
        node = Path(node_setting.replace("$CFGDIR", str(sdk))).resolve()
        self.assertTrue(node.is_relative_to(sdk.resolve()))
        lock = json.loads((REPO_ROOT / "dependencies.lock.json").read_text())
        self.assertEqual((compiler / "emscripten-version.txt").read_text().strip().strip('"'),
                         lock["emscripten"])
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(config),
                   EM_CACHE=str(compiler / "cache"), EMSDK_PYTHON=sys.executable)
        sys.path.insert(0, str(REPO_ROOT / "scripts"))
        # Never prepare or mutate the checkout's generated dependency here.
        source = REPO_ROOT / "build/gameplay-source/src"
        if not (source / "melee/ft/forward.h").is_file():
            self.skipTest("Existing generated source headers unavailable; prepare via the supported build entry point")
        generated_head = subprocess.run(["git","rev-parse","HEAD"],cwd=source,
            text=True,capture_output=True,check=True).stdout.strip()
        self.assertEqual(generated_head,lock["repositories"]["melee"]["commit"])

        OUTPUT_ROOT.mkdir(parents=True, exist_ok=True)
        output_dir = self.ordinary_workspace or Path(tempfile.mkdtemp(prefix="cpu-restore-adapter-", dir=OUTPUT_ROOT))
        temporary = output_dir / "tmp"
        temporary.mkdir()
        env["TMPDIR"] = str(temporary)
        stubs = output_dir / "stubs"
        stubs.mkdir()
        (stubs / "gameplay_cpu_checkpoint.hpp").write_text(STUB_CHECKPOINT_HEADER)
        overlay_src = source_overlay(output_dir)
        template = (CANDIDATE_ROOT / "tests/cpu_restore_adapter_control.cpp.in").read_text()
        actual_fixture = extract_fixture_observations(CANDIDATE_ROOT / "tests/gameplay_snapshot_probe.cpp")
        generated = template.replace("@ACTUAL_FIXTURE_OBSERVATIONS@", actual_fixture)
        self.assertNotIn("@ACTUAL_FIXTURE_OBSERVATIONS@", generated)
        control_source = output_dir / "cpu_restore_adapter_control.cpp"
        control_source.write_text(generated)

        include_dirs = [stubs, overlay_src, overlay_src / "slippi_profile/tests",
                        REPO_ROOT / ".deps/aurora/include", source,
                        REPO_ROOT / "tests"]
        common = ["-O1", "-fexceptions", "-DTARGET_PC", "-DMELEE_WEB_CPU_INTERVAL_ENABLED=1",
                  "-ffunction-sections", "-fdata-sections", "-ffp-contract=off"]
        for directory in include_dirs:
            common.extend(["-I", str(directory)])
        common.extend(["-include", str(REPO_ROOT / "src/gameplay_compat.h")])

        commands = []
        cpp_sources = [overlay_src / "gameplay_cpu_commit.cpp",
                       overlay_src / "gameplay_cpu_statics.cpp", control_source]
        for index, path in enumerate(cpp_sources):
            commands.append([sys.executable, str(compiler / "em++.py"), *common,
                             "-std=c++20", "-c", str(path), "-o", str(output_dir / f"cpp-{index}.o")])
        for index, command in enumerate(commands):
            result = subprocess.run(command, cwd=output_dir, env=env,
                                    capture_output=True, text=True, timeout=120)
            write_record(output_dir / f"compile-{index}.json", command, result)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

        output = output_dir / "cpu_restore_adapter_control.js"
        link = [sys.executable, str(compiler / "em++.py"), "-O1", "-fexceptions",
                *map(str, sorted(output_dir.glob("*.o"))), "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1",
                "-sASSERTIONS=2", "-sSAFE_HEAP=1", "-sALLOW_MEMORY_GROWTH=1", "-o", str(output)]
        result = subprocess.run(link, cwd=output_dir, env=env,
                                capture_output=True, text=True, timeout=120)
        write_record(output_dir / "link.json", link, result)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        run = [str(node), str(output)]
        result = subprocess.run(run, cwd=output_dir, env=env,
                                 capture_output=True, text=True, timeout=30)
        write_record(output_dir / "run.json", run, result)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Actual CPU adapter control passed", result.stdout)
        receipt = {
            "schema": "actual-cpu-restore-adapter-focused-control-v1",
            "result": "PASS",
            "scope": "Actual cpu_restore_adapter.inc and actual fixture observation declarations; modeled named component/session/provider leaves, not a complete source-world restore.",
            "adapter_sha256": sha256(CANDIDATE_ROOT / "src/slippi_profile/tests/cpu_restore_adapter.inc"),
            "control_template_sha256": sha256(CANDIDATE_ROOT / "tests/cpu_restore_adapter_control.cpp.in"),
            "fixture_probe_sha256": sha256(CANDIDATE_ROOT / "tests/gameplay_snapshot_probe.cpp"),
            "generated_control_sha256": sha256(control_source),
            "receipt_path": str(output_dir),
            "commands": [f"compile-{index}.json" for index in range(len(commands))] + ["link.json", "run.json"],
            "checks": [
                "sample300 fixed while modeled source frame resets 176→177→176→177",
                "actual fixed coordinator admissions and writes execute in named order",
                "exact fixture observation/PCM/PAD/profile/transfer equality gates one draw",
                "admission and replay refusals block draw; first failure remains latched",
                "draw return permits pending submission; a later idle boundary follows",
                "source-close reentry disarms; capsule leases release only after owner retirement",
                "evidence ABI and null/misaligned/out-of-range/overflow/forbidden configuration refusals",
            ],
        }
        (output_dir / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print(result.stdout, end="")
        print(f"Focused adapter control receipt: {output_dir / 'receipt.json'}")


if __name__ == "__main__":
    unittest.main()
