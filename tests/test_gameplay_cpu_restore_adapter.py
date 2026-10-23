"""Compile the actual private CPU adapter include against explicit leaf models."""
import ast
import hashlib
import json
import os
import shutil
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
enum class CpuCheckpointSpan : std::uint32_t {OneTick=1,TwoTicks=2};
class GameplayMatchSession;
struct CpuCaptureDiagnostic {
    CpuComparison comparison{};
    std::array<char,160> message{};
};
class GameplayCpuCheckpoint final {
public:
    static std::unique_ptr<GameplayCpuCheckpoint> capture(
        const GameplayMatchSession&,std::unique_ptr<GameplayCpuStatics>,CpuCaptureDiagnostic&,CpuCheckpointSpan=CpuCheckpointSpan::OneTick) noexcept;
    ~GameplayCpuCheckpoint();
    bool begin_record(const GameplayMatchSession&) noexcept;
    bool interval_boundary(const GameplayMatchSession&) noexcept;
    bool seal_post(const GameplayMatchSession&) noexcept;
    GameplayCpuCommitResult restore(const GameplayMatchSession&) noexcept;
    bool finish_replay(const GameplayMatchSession&) noexcept;
    bool seal_drawn_reference(const GameplayMatchSession&) noexcept;
    bool matches_drawn_reference(const GameplayMatchSession&) noexcept;
    void disarm() noexcept;
    const CpuComparison& comparison() const noexcept {return comparison_;}
    const MeleeWebSourcePayloadFailureWitness& payload_failure_witness() const noexcept {return payload_failure_witness_;}
    std::uint32_t checked_domains() const noexcept {return checked_domains_;}
    std::uint32_t matched_domains() const noexcept {return matched_domains_;}
    bool source_owners_retired(const GameplayMatchSession*) noexcept;
private:
    enum class Phase { Captured, Recording, Sealed, Replaying, Finished, Failed, Retired };
    explicit GameplayCpuCheckpoint(CpuCheckpointSpan span):span_(span) {}
    const CpuCheckpointSpan span_;
    std::unique_ptr<GameplayCpuStatics> statics_;
    GameplayCpuCommitOwners commit_{};
    MeleeWebActionInterval* actions_{};
    CpuRestoreFixtureSnapshot pre_{},post_{},drawn_{};
    std::vector<std::byte> drawn_statics_;
    std::uint64_t effect_token_{};
    std::uint32_t pre_frame_{},post_frame_{};
    Phase phase_=Phase::Captured;
    bool source_step_possible_=false,retired_=false;
    CpuComparison comparison_{};
    MeleeWebSourcePayloadFailureWitness payload_failure_witness_{};
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
        if target.suffix in (".h", ".hpp"):
            shutil.copy2(target,destination)
        else:
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
            if candidate.suffix in (".h", ".hpp"):
                shutil.copy2(candidate,destination)
            else:
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
        fixture_probe = CANDIDATE_ROOT / "tests/gameplay_snapshot_probe.cpp"
        if not fixture_probe.is_file():fixture_probe=REPO_ROOT / "tests/gameplay_snapshot_probe.cpp"
        if not fixture_probe.is_file(): fixture_probe = REPO_ROOT / "tests/gameplay_snapshot_probe.cpp"
        actual_fixture = extract_fixture_observations(fixture_probe)
        generated = template.replace("@ACTUAL_FIXTURE_OBSERVATIONS@", actual_fixture)
        self.assertNotIn("@ACTUAL_FIXTURE_OBSERVATIONS@", generated)
        control_source = output_dir / "cpu_restore_adapter_control.cpp"
        control_source.write_text(generated)

        include_dirs = [stubs, overlay_src, overlay_src / "slippi_profile/tests",
                        REPO_ROOT / ".deps/aurora/include", source,
                        REPO_ROOT / "tests"]
        common = ["-O1", "-fexceptions", "-DTARGET_PC", "-DMELEE_WEB_CPU_INTERVAL_ENABLED=1", "-DMELEE_WEB_SLIPPI_LOGICAL_FRAME_CLOCK=1",
                  "-ffunction-sections", "-fdata-sections", "-ffp-contract=off"]
        for directory in include_dirs:
            common.extend(["-I", str(directory)])
        common.extend(["-include", str(overlay_src / "gameplay_compat.h")])

        actual_includes=[directory for directory in include_dirs if directory!=stubs]
        syntax=[sys.executable,str(compiler/'em++.py'),'-std=c++20','-fsyntax-only',
                '-DTARGET_PC','-DMELEE_WEB_CPU_INTERVAL_ENABLED=1','-ffp-contract=off']
        for directory in actual_includes:syntax.extend(['-I',str(directory)])
        syntax.extend(['-include',str(overlay_src/'gameplay_compat.h'),str(overlay_src/'gameplay_cpu_checkpoint.cpp')])
        result=subprocess.run(syntax,cwd=output_dir,env=env,capture_output=True,text=True,timeout=120)
        write_record(output_dir/'actual-checkpoint-syntax.json',syntax,result)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
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
        # Decode actual Wasm-emitted evidence through the real JS consumer.
        # The native source/session/component leaves remain modeled; this is
        # an ABI bridge, not a real browser/source-world replay claim.
        marker="CPU_TWO_ACTUAL_WASM_EVIDENCE_BASE64="
        emitted=[line[len(marker):] for line in result.stdout.splitlines() if line.startswith(marker)]
        self.assertEqual(len(emitted),1,"Actual adapter did not emit exactly one ABI record")
        import base64
        raw=base64.b64decode(emitted[0],validate=True)
        self.assertEqual(len(raw),35368)
        native_bytes=output_dir/"actual-two-tick-evidence.bin";native_bytes.write_bytes(raw)
        helper=Path(os.environ.get("MELEE_CPU_TWO_TICK_JS_HELPER",
            REPO_ROOT/"src/slippi_profile/tests/cpu_two_tick_runner.mjs")).resolve()
        self.assertTrue(helper.is_file(),"Actual joined two-tick JS consumer is unavailable")
        js="""const fs=await import('node:fs');const {pathToFileURL}=await import('node:url');
const {decodeCpuTwoTickEvidence,validateCpuTwoTickFinalEvidence}=await import(pathToFileURL(process.argv[1]));
const raw=new Uint8Array(fs.readFileSync(process.argv[2]));const decoded=decodeCpuTwoTickEvidence(raw);
validateCpuTwoTickFinalEvidence(decoded,176);
console.log(JSON.stringify({status:'PASS',bytes:raw.length,preFrame:decoded.preFrame,postFrame:decoded.postFrame,
 drawnPreFrame:decoded.drawnPair301.preFrame,drawnPostFrame:decoded.drawnPair301.postFrame}));"""
        bridge=[str(node),"--input-type=module","-e",js,str(helper),str(native_bytes)]
        bridged=subprocess.run(bridge,cwd=output_dir,env=env,capture_output=True,text=True,timeout=30)
        write_record(output_dir/"actual-js-bridge.json",bridge,bridged)
        self.assertEqual(bridged.returncode,0,bridged.stdout+bridged.stderr)
        witness_marker="CPU_PAYLOAD_ACTUAL_WASM_WITNESS_BASE64="
        witness_rows=[line[len(witness_marker):] for line in result.stdout.splitlines() if line.startswith(witness_marker)]
        self.assertEqual(len(witness_rows),1,"Actual adapter omitted fixed failure witness")
        witness_raw=base64.b64decode(witness_rows[0],validate=True)
        self.assertEqual(len(witness_raw),256)
        witness_bytes=output_dir/"actual-payload-failure-witness.bin";witness_bytes.write_bytes(witness_raw)
        witness_js="""const fs=await import('node:fs');const {pathToFileURL}=await import('node:url');
const {decodeCpuPayloadFailureWitness}=await import(pathToFileURL(process.argv[1]));
const d=decodeCpuPayloadFailureWitness(new Uint8Array(fs.readFileSync(process.argv[2])));
if(d.originalStatus!==2||d.firstDifferenceMatches!==1||d.window.row!==7||d.window.offset!==5||d.attribution!==0||d.reason!==2)throw Error('actual adapter witness drift');
console.log(JSON.stringify({status:'PASS',bytes:256,scope:'actual adapter; modeled payload leaf',row:d.window.row,offset:d.window.offset}));"""
        witness_bridge=[str(node),"--input-type=module","-e",witness_js,str(helper),str(witness_bytes)]
        witness_result=subprocess.run(witness_bridge,cwd=output_dir,env=env,capture_output=True,text=True,timeout=30)
        write_record(output_dir/"actual-witness-js-bridge.json",witness_bridge,witness_result)
        self.assertEqual(witness_result.returncode,0,witness_result.stdout+witness_result.stderr)
        # Recompile only the actual adapter fixture in default-v5 mode; real
        # coordinator/statics objects and compiler settings stay identical.
        v5_object=output_dir / "control-v5.o"
        v5_common=[part for part in common if part!="-DMELEE_WEB_SLIPPI_LOGICAL_FRAME_CLOCK=1"]
        v5_compile=[sys.executable,str(compiler/"em++.py"),*v5_common,
                    "-DMELEE_WEB_SLIPPI_LOGICAL_FRAME_CLOCK=0","-std=c++20","-c",
                    str(control_source),"-o",str(v5_object)]
        result=subprocess.run(v5_compile,cwd=output_dir,env=env,capture_output=True,text=True,timeout=120)
        write_record(output_dir/"compile-v5.json",v5_compile,result)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        v5_output=output_dir/"control-v5.js"
        v5_link=[sys.executable,str(compiler/"em++.py"),"-O1","-fexceptions",
                 str(output_dir/"cpp-0.o"),str(output_dir/"cpp-1.o"),str(v5_object),
                 "-sENVIRONMENT=node","-sEXIT_RUNTIME=1","-sASSERTIONS=2","-sSAFE_HEAP=1",
                 "-sALLOW_MEMORY_GROWTH=1","-o",str(v5_output)]
        result=subprocess.run(v5_link,cwd=output_dir,env=env,capture_output=True,text=True,timeout=120)
        write_record(output_dir/"link-v5.json",v5_link,result)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        result=subprocess.run([str(node),str(v5_output)],cwd=output_dir,env=env,capture_output=True,text=True,timeout=30)
        write_record(output_dir/"run-v5.json",[str(node),str(v5_output)],result)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        receipt = {
            "schema": "actual-cpu-restore-adapter-focused-control-v1",
            "result": "PASS",
            "scope": "Actual cpu_restore_adapter.inc and actual fixture observation declarations; modeled named component/session/provider leaves, not a complete source-world restore.",
            "adapter_sha256": sha256(CANDIDATE_ROOT / "src/slippi_profile/tests/cpu_restore_adapter.inc"),
            "control_template_sha256": sha256(CANDIDATE_ROOT / "tests/cpu_restore_adapter_control.cpp.in"),
            "fixture_probe_sha256": sha256(fixture_probe),
            "js_consumer": {"path":str(helper),"sha256":sha256(helper)},
            "actual_wasm_evidence_sha256":sha256(native_bytes),
            "actual_witness_sha256":sha256(witness_bytes),
            "generated_control_sha256": sha256(control_source),
            "receipt_path": str(output_dir),
            "commands": [f"compile-{index}.json" for index in range(len(commands))] + ["link.json", "run.json", "actual-js-bridge.json", "actual-witness-js-bridge.json", "compile-v5.json", "link-v5.json", "run-v5.json"],
            "checks": [
                "actual two-tick adapter 300/301 baseline and replay; idle waits, raw/typed evidence and first replay refusal stop before next tick",
                "default-v5 compile rejects new route before source work while legacy routes still pass",
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
