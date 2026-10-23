"""Native value-ownership and refusal controls for the source-memory mirror."""
from pathlib import Path
import shutil
import subprocess
import unittest

from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).resolve().parents[1]


class SourceMemorySnapshotTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.directory = cls.new_workspace(ROOT, "source-memory-snapshot-")

    def test_extracted_browser_payload_admission_adapter_lifecycle(self):
        compiler = shutil.which("c++")
        if not compiler:
            self.skipTest("native C++ compiler unavailable")
        browser_source = (ROOT / "src/slippi_profile/tests/synctest_browser.cpp").read_text()
        start = "// BEGIN MELEE_WEB_SOURCE_PAYLOAD_ADMISSION_ADAPTER"
        end = "// END MELEE_WEB_SOURCE_PAYLOAD_ADMISSION_ADAPTER"
        self.assertEqual(browser_source.count(start), 1)
        self.assertEqual(browser_source.count(end), 1)
        adapter = browser_source.split(start, 1)[1].split(end, 1)[0]
        template = (ROOT / "tests/payload_admission_adapter_control.cpp.in").read_text()
        self.assertEqual(template.count("// ADAPTER_BLOCK"), 1)
        frame_source = (ROOT / ".deps/aurora/lib/gfx/frame.cpp").read_text()
        header_producer = frame_source[frame_source.index("void fill_quiescence_header("):
            frame_source.index("AuroraBrowserQuiescenceLease quiescence_lease(")]
        size_producer = frame_source[frame_source.index("size_t browser_quiescence_descriptor_bytes() noexcept {"):
            frame_source.index("size_t browser_quiescence_lease_bytes() noexcept {")]
        self.assertEqual(template.count("// AURORA_HEADER_PRODUCER"), 1)
        template = template.replace("// AURORA_HEADER_PRODUCER", header_producer + size_producer)
        source = self.directory / "payload-admission-control.cpp"
        source.write_text(template.replace("// ADAPTER_BLOCK", adapter))
        binary = self.directory / "payload-admission-control"
        built = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                "-I" + str(ROOT / ".deps/aurora/include"),
                                str(source), "-o", str(binary)],
                               capture_output=True, text=True, timeout=60)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        for scenario in ["producer-layout", "success", "draw-pending-then-idle", "postdraw-admit-early-latches", "close-before-capture", "shutdown-fallback", "reentrant-shutdown", "order", "duplicate-capture", "capture-failure",
                         "capture-failure-pointer", "capture-admission-failure",
                         "step-admission-failure", "postdraw-admission-failure",
                         "step-failure", "draw-failure", "transfer-busy",
                         "aurora-missing", "aurora-busy", "aurora-one-slot",
                         "aurora-layout-schema", "aurora-layout-header-only", "aurora-layout-truncated",
                         "aurora-layout-offset", "aurora-layout-stride", "aurora-layout-count",
                         "aurora-layout-zero-count", "aurora-layout-overflow", "aurora-layout-size-unavailable",
                         "owner-unhealthy",
                         "frame-mismatch", "not-main-thread", "asyncify-disabled", "asyncify-jspi",
                         "asyncify-busy", "asyncify-entry-not-idle", "asyncify-boundary-after-step",
                         "not-runtime-thread",
                         "reentrant-close", "unknown"]:
            run = subprocess.run([str(binary), scenario], capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 0, f"{scenario}: {run.stdout}{run.stderr}")
        pthread_binary = self.directory / "payload-admission-pthread-control"
        pthread_built = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                "-I" + str(ROOT / ".deps/aurora/include"),
                                        "-D__EMSCRIPTEN_PTHREADS__=1", "-Wno-unused-function",
                                        "-Wno-unused-variable", "-Wno-unused-const-variable",
                                        str(source), "-o", str(pthread_binary)],
                                       capture_output=True, text=True, timeout=60)
        self.assertEqual(pthread_built.returncode, 0, pthread_built.stdout + pthread_built.stderr)
        pthread_run = subprocess.run([str(pthread_binary), "pthread"],
                                     capture_output=True, text=True, timeout=10)
        self.assertEqual(pthread_run.returncode, 0, pthread_run.stdout + pthread_run.stderr)

    def test_value_capture_restore_replay_and_owner_refusal(self):
        compiler = shutil.which("c++")
        if not compiler:
            self.skipTest("native C++ compiler unavailable")
        binary = self.directory / "snapshot-control"
        sources = ["tests/source_memory_snapshot_trace.cpp",
                   "src/gameplay_source_memory_runtime.cpp",
                   "src/source_address_context.cpp", "src/source_game_heap_context.cpp"]
        built = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                "-I" + str(ROOT / ".deps/aurora/include"),
                                "-I", str(ROOT / "src"),
                                *[str(ROOT / path) for path in sources], "-o", str(binary)],
                               capture_output=True, text=True, timeout=60)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("Source-memory mirror snapshot controls: passed", run.stdout)


if __name__ == "__main__":
    unittest.main()
