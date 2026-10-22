"""Verify the Aurora browser-owner diagnostic is opt-in at the build boundary.

The source projection test deliberately starts from the pinned Aurora git tree,
applies this checkout's downstream patch, and preprocesses the resulting
``frame.cpp`` observer.  It therefore does not depend on a historical commit
remaining reachable in a rebased checkout.  Its compiler invocation is a
bounded ``-E`` source projection check; it does not establish object, link, or
whole-binary equivalence.
"""

from pathlib import Path
import importlib.util
import io
import json
import shutil
import subprocess
import sys
import tarfile
import unittest
from contextlib import contextmanager
from unittest.mock import patch

from owned_test_workspace import OwnedWorkspaceTests


ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / "patches/aurora-browser.patch"
AURORA = ROOT / ".deps/aurora"
BASELINE = ROOT / "tests/aurora_browser_off_observer_baseline.inc"
sys.path.insert(0, str(ROOT / "scripts"))
BUILD_SPEC = importlib.util.spec_from_file_location("aurora_quiescence_build", ROOT / "scripts/build.py")
assert BUILD_SPEC is not None and BUILD_SPEC.loader is not None
BUILD = importlib.util.module_from_spec(BUILD_SPEC)
BUILD_SPEC.loader.exec_module(BUILD)


def staging_observer_fixture(frame_source, header_source):
    status_end = header_source.index("} AuroraBrowserSubmissionStatus;") + len("} AuroraBrowserSubmissionStatus;")
    status_start = header_source.rfind("typedef struct {", 0, status_end)
    types = header_source[status_start:status_end] + "\n" + header_source[
        header_source.index("/* Diagnostic-only browser ownership view."):
        header_source.index("size_t aurora_browser_quiescence_descriptor_bytes();")]
    lease = frame_source[frame_source.index("AuroraBrowserQuiescenceLease quiescence_lease("):
                         frame_source.index("AuroraBrowserByteBufferDescriptor describe_buffer(")]
    actual = frame_source[frame_source.index("// BEGIN PURE STAGING OBSERVER"):
                          frame_source.index("// END PURE STAGING OBSERVER")]
    return r"""
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
static size_t allocationCalls = 0;
void* operator new(std::size_t size) { ++allocationCalls; if (void* p=std::malloc(size)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
""" + types + r"""
constexpr size_t FrameSlotCount=4, StagingBufferCount=4;
struct TestStats { uint32_t queuedPipelines=0,createdPipelines=7,drawCallCount=8; uint64_t lastBeginFrameId=9,lastEndFrameId=9; };
struct { TestStats stats; } g_resources;
struct Pool { size_t free=4; size_t free_count() const {return free;} } g_frameSlots,g_stagingSlots;
uint32_t g_frameIndex=0, cacheStatus=0;
std::atomic_uint64_t g_browserStagingGeneration{1},g_browserSubmissionSequence{2},g_browserCallbackSequence{2},g_browserStaleCallbackCount{0};
size_t frameCount=4, stagingCount=4;
size_t active_frame_slot_count(){return frameCount;}
size_t active_staging_buffer_count(){return stagingCount;}
namespace render_worker { bool idle=true; bool is_idle(){return idle;} }
uint32_t browser_pipeline_cache_status(){return cacheStatus;}
std::array<AuroraBrowserFrameStagingDescriptor,4> ownedFrames{};
std::array<AuroraBrowserSubmissionDescriptor,4> ownedSubmissions{};
std::array<unsigned char,64> payload{};
bool corruptHeader=false, advanceGeneration=false;
void fill_quiescence_header(AuroraBrowserQuiescenceDescriptor& out,const AuroraBrowserSubmissionStatus& status){
  out={}; out.schemaVersion=corruptHeader?4:5; out.frameSlotCount=frameCount;out.stagingSlotCount=stagingCount;
  out.frameDescriptorOffset=sizeof(out);out.frameDescriptorStride=sizeof(ownedFrames[0]);out.frameDescriptorCount=frameCount;
  out.submissionDescriptorOffset=sizeof(out)+frameCount*sizeof(ownedFrames[0]);
  out.submissionDescriptorStride=sizeof(ownedSubmissions[0]);out.submissionDescriptorCount=stagingCount;
  out.byteSize=out.submissionDescriptorOffset+stagingCount*sizeof(ownedSubmissions[0]);
  out.pendingFramePackets=status.pendingFramePackets;out.pendingStagingBuffers=status.pendingStagingBuffers;out.workerBusy=status.workerBusy;
  out.pipelineCacheStatus=cacheStatus;out.queuedPipelines=g_resources.stats.queuedPipelines;
  out.createdPipelines=g_resources.stats.createdPipelines;out.drawCallCount=g_resources.stats.drawCallCount;
  out.currentFrameIndex=g_frameIndex;out.stagingGeneration=g_browserStagingGeneration.load();
}
void fill_staging_descriptor(AuroraBrowserFrameStagingDescriptor& out,size_t slot){out=ownedFrames[slot];if(advanceGeneration){++g_browserStagingGeneration;advanceGeneration=false;}}
void fill_submission_descriptor(AuroraBrowserSubmissionDescriptor& out,size_t slot){out=ownedSubmissions[slot];}
static unsigned pumps=0;
void enqueue_process_events(){++pumps;assert(false);}
void process_events(){++pumps;assert(false);}
""" + lease + actual + r"""
void reset(){
 g_frameIndex=0;cacheStatus=0;frameCount=stagingCount=4;g_frameSlots.free=g_stagingSlots.free=4;
 render_worker::idle=true;g_resources.stats=TestStats{};g_browserStagingGeneration=1;
 corruptHeader=advanceGeneration=false;ownedFrames={};ownedSubmissions={};payload.fill(0x6A);
 for(size_t i=0;i<4;++i){auto& f=ownedFrames[i];f.frameSlot=i;f.packetStagingBuffer=UINT32_MAX;
  for(size_t j=0;j<5;++j){f.packetBuffers[j].owned=1;f.packetBuffers[j].maxCapacity=UINT32_MAX;
    f.shadowBuffers[j].owned=1;f.shadowBuffers[j].maxCapacity=UINT32_MAX;}
  ownedSubmissions[i].stagingSlot=i;}
 auto& b=ownedFrames[0].shadowBuffers[0];b.dataAddress=reinterpret_cast<uintptr_t>(payload.data());
 b.capacity=64;b.length=4;b.maxCapacity=64;ownedFrames[0].shadowPreviousUsed[0]=4;
}
struct Observed {
 decltype(ownedFrames) frames;decltype(ownedSubmissions) submissions;decltype(payload) bytes;
 TestStats stats;size_t freeFrames,freeStaging,framesN,stagingN;uint32_t frame,cache;bool idle;
 uint64_t generation,registration,callback,stale;
};
Observed read_owners(){Observed x{};x.frames=ownedFrames;x.submissions=ownedSubmissions;x.bytes=payload;
 x.stats=g_resources.stats;x.freeFrames=g_frameSlots.free;x.freeStaging=g_stagingSlots.free;x.framesN=frameCount;x.stagingN=stagingCount;
 x.frame=g_frameIndex;x.cache=cacheStatus;x.idle=render_worker::idle;x.generation=g_browserStagingGeneration;
 x.registration=g_browserSubmissionSequence;x.callback=g_browserCallbackSequence;x.stale=g_browserStaleCallbackCount;return x;}
void refusal(bool injectedMutation=false){
 std::array<unsigned char,BrowserObserverStorageBytes> before{};
 std::memcpy(before.data(),aurora_browser_quiescence_observer_storage,before.size());
 const auto owners=read_owners();const auto allocations=allocationCalls;
 assert(browser_quiescence_observe_descriptor_impl()==nullptr);
 assert(std::memcmp(before.data(),aurora_browser_quiescence_observer_storage,before.size())==0);
 const auto after=read_owners();if(!injectedMutation)assert(std::memcmp(&owners,&after,sizeof(owners))==0);
 assert(allocationCalls==allocations&&pumps==0);
}
int main(){
 reset();std::memset(aurora_browser_quiescence_observer_storage,0xA5,BrowserObserverStorageBytes);
#ifdef __EMSCRIPTEN_PTHREADS__
 refusal();return 0;
#else
 const auto before=read_owners();const auto allocations=allocationCalls;
 assert(browser_quiescence_observe_descriptor_impl()==aurora_browser_quiescence_observer_storage);
 const auto after=read_owners();assert(std::memcmp(&before,&after,sizeof(before))==0);assert(allocationCalls==allocations&&pumps==0);
 std::array<unsigned char,BrowserObserverStorageBytes> saved{};std::memcpy(saved.data(),aurora_browser_quiescence_observer_storage,saved.size());
 assert(browser_quiescence_observe_descriptor_impl()!=nullptr);assert(std::memcmp(saved.data(),aurora_browser_quiescence_observer_storage,saved.size())==0);
 for(int failure=0;failure<23;++failure){reset();
  auto& f=ownedFrames[0];auto& b=f.shadowBuffers[0];
  switch(failure){
  case 0:g_frameSlots.free=3;break;case 1:g_stagingSlots.free=3;break;
  case 2:render_worker::idle=false;break;case 3:cacheStatus=1;break;case 4:g_resources.stats.queuedPipelines=1;break;
  case 5:g_frameIndex=UINT32_MAX;break;case 6:g_browserStagingGeneration=0;break;
  case 7:frameCount=0;break;case 8:frameCount=5;break;case 9:stagingCount=5;break;
  case 10:f.packetActive=1;break;case 11:f.packetFrameId=1;break;
  case 12:f.packetBuffers[0].dataAddress=reinterpret_cast<uintptr_t>(payload.data());break;
  case 13:b.owned=0;break;case 14:b.length=65;break;case 15:b.maxCapacity=63;break;
  case 16:f.shadowPreviousUsed[0]=65;break;case 17:b.capacity=0;b.length=0;break;
  case 18:ownedFrames[1].shadowBuffers[0]=b;break;
  case 19:b.dataAddress=reinterpret_cast<uintptr_t>(aurora_browser_quiescence_observer_storage);break;
  case 20:ownedSubmissions[0].state=AURORA_BROWSER_SUBMISSION_REGISTERED;break;
  case 21:corruptHeader=true;break;case 22:b.dataAddress=UINTPTR_MAX-16;b.capacity=32;b.length=0;b.maxCapacity=64;break;
  }refusal();
 }
 reset();advanceGeneration=true;refusal(true);
 // Full header/rows are the actual source ABI, not a handwritten wire.
 reset();assert(browser_quiescence_observe_descriptor_impl()!=nullptr);
 const auto* header=reinterpret_cast<const AuroraBrowserQuiescenceDescriptor*>(aurora_browser_quiescence_observer_storage);
 assert(header->schemaVersion==5&&header->byteSize==BrowserObserverStorageBytes);
 return 0;
#endif
}
"""


class AuroraBrowserQuiescenceOptionTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.scratch = cls.new_workspace(ROOT, "aurora-browser-quiescence-option-")
        cls._source_serial = 0
        cls._projection_serial = 0

    def test_cmake_and_build_profile_default_off_and_diagnostic_only_on(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        build = (ROOT / "scripts/build.py").read_text(encoding="utf-8")
        self.assertIn(
            'option(MELEE_WEB_AURORA_QUIESCENCE_DIAGNOSTIC\n'
            '  "Enable opt-in Aurora browser submission-owner/quiescence diagnostics" OFF)',
            cmake,
        )
        self.assertIn(
            "target_compile_definitions(aurora_gx PRIVATE AURORA_BROWSER_QUIESCENCE_DIAGNOSTIC=1)",
            cmake,
        )
        self.assertIn('AURORA_QUIESCENCE_DIAGNOSTIC_TARGET = "aurora-browser-quiescence"', build)
        self.assertIn(
            "MELEE_WEB_AURORA_QUIESCENCE_DIAGNOSTIC={'ON' if target == AURORA_QUIESCENCE_DIAGNOSTIC_TARGET or aurora_staging_shadow_diagnostic else 'OFF'}",
            build,
        )
        self.assertIn(
            "MELEE_WEB_AURORA_STAGING_SHADOW_INVENTORY={'ON' if aurora_staging_shadow_diagnostic else 'OFF'}",
            build,
        )
        self.assertEqual(
            BUILD.build_directory(Path("/fixture/repo"),
                                  target=BUILD.AURORA_QUIESCENCE_DIAGNOSTIC_TARGET,
                                  configuration="Release"),
            Path("/fixture/repo/build/browser-quiescence-release"),
        )
        self.assertNotEqual(
            BUILD.build_directory(Path("/fixture/repo"), target="graphics", configuration="Release"),
            Path("/fixture/repo/build/browser-quiescence-release"),
        )

    @contextmanager
    def _patched_frame(self):
        """Yield a clean pinned Aurora tree with this patch applied."""
        if not AURORA.is_dir() or not (AURORA / ".git").exists():
            self.skipTest("pinned Aurora source checkout is unavailable")
        lock = json.loads((ROOT / "dependencies.lock.json").read_text(encoding="utf-8"))
        expected_commit = lock["repositories"]["aurora"]["commit"]
        head = subprocess.run(
            ["git", "-C", str(AURORA), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
            timeout=10,
        ).stdout.strip()
        self.assertEqual(head, expected_commit, "pinned Aurora checkout HEAD differs from dependencies.lock.json")
        self.__class__._source_serial += 1
        base = self.scratch / f"patched-aurora-{self._source_serial}"
        base.mkdir()
        (base / "archive-command.txt").write_text(
            f"git -C {AURORA} archive HEAD\nexpected_commit={expected_commit}\nactual_commit={head}\n",
            encoding="utf-8",
        )
        try:
            archive_result = subprocess.run(
                ["git", "-C", str(AURORA), "archive", "HEAD"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=30,
            )
            (base / "archive.stderr").write_bytes(archive_result.stderr)
            if archive_result.returncode:
                raise subprocess.CalledProcessError(
                    archive_result.returncode,
                    ["git", "-C", str(AURORA), "archive", "HEAD"],
                    stderr=archive_result.stderr,
                )
            with tarfile.open(fileobj=io.BytesIO(archive_result.stdout)) as source:
                source.extractall(base, filter="data")
            patch_result = subprocess.run(
                ["patch", "--batch", "--forward", "-p1"],
                cwd=base,
                input=PATCH.read_text(encoding="utf-8"),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=30,
            )
            (base / "patch-apply.stdout").write_text(patch_result.stdout, encoding="utf-8")
            (base / "patch-apply.stderr").write_text(patch_result.stderr, encoding="utf-8")
            (base / "patch-command.txt").write_text(
                f"(cd {base} && patch --batch --forward -p1 < {PATCH})\n", encoding="utf-8"
            )
            if patch_result.returncode:
                raise subprocess.CalledProcessError(
                    patch_result.returncode,
                    ["patch", "--batch", "--forward", "-p1"],
                    output=patch_result.stdout,
                    stderr=patch_result.stderr,
                )
            yield base / "lib/gfx/frame.cpp"
        except BaseException as error:
            (base / "materialize-error.txt").write_text(repr(error) + "\n", encoding="utf-8")
            raise

    @staticmethod
    def _observer_branches(frame):
        source = frame.read_text(encoding="utf-8")
        diagnostic = "#if defined(AURORA_BROWSER_QUIESCENCE_DIAGNOSTIC)\n"
        function_start = source.index("void end_frame")
        function_end = source.index(
            "\n#ifdef __EMSCRIPTEN__\nAuroraBrowserSubmissionStatus",
            function_start,
        )
        function = source[function_start:function_end]
        position = source.index(diagnostic, function_start)
        start = source.rfind("#ifdef __EMSCRIPTEN__\n", 0, position)
        end = source.index("    stats.lastEndFrameCallbackResidualMs =", position)
        block = source[start:end]
        conditional = block[block.index(diagnostic) + len(diagnostic):]
        on, off = conditional.split("#else\n", 1)
        off = off.split("#endif", 1)[0]
        return source, function, on.rstrip() + "\n", off.rstrip() + "\n"

    def _preprocess(self, source, *, diagnostic, include_dirs=(), label="projection"):
        compiler = shutil.which("clang++") or shutil.which("g++")
        if compiler is None:
            raise unittest.SkipTest("a C++ preprocessor is unavailable")
        self.__class__._projection_serial += 1
        directory = self.scratch / f"preprocess-{label}-{self._projection_serial}"
        directory.mkdir()
        fixture = directory / "projection.cpp"
        fixture.write_text(source, encoding="utf-8")
        command = [compiler, "-E", "-P", "-std=c++20", "-D__EMSCRIPTEN__"]
        if diagnostic:
            command.append("-DAURORA_BROWSER_QUIESCENCE_DIAGNOSTIC=1")
        for include_dir in include_dirs:
            command.extend(("-I", str(include_dir)))
        command.append(str(fixture))
        (directory / "command.json").write_text(json.dumps(command) + "\n", encoding="utf-8")
        try:
            result = subprocess.run(
                command,
                check=False,
                capture_output=True,
                text=True,
                timeout=30,
            )
        except BaseException as error:
            (directory / "error.txt").write_text(repr(error) + "\n", encoding="utf-8")
            raise
        (directory / "stdout.txt").write_text(result.stdout, encoding="utf-8")
        (directory / "stderr.txt").write_text(result.stderr, encoding="utf-8")
        if result.returncode:
            raise subprocess.CalledProcessError(
                result.returncode, command, output=result.stdout, stderr=result.stderr
            )
        return result.stdout

    def test_actual_patched_frame_off_projection_matches_reviewed_baseline(self):
        with self._patched_frame() as frame:
            source, function, on, off = self._observer_branches(frame)
            self.assertIn(
                "#if defined(__EMSCRIPTEN__) && defined(AURORA_BROWSER_QUIESCENCE_DIAGNOSTIC)\n"
                '#include "browser_submission_owner.hpp"',
                source,
            )
            include_start = source.index(
                "#if defined(__EMSCRIPTEN__) && defined(AURORA_BROWSER_QUIESCENCE_DIAGNOSTIC)\n"
                '#include "browser_submission_owner.hpp"'
            )
            include_end = source.index("#endif", include_start) + len("#endif")
            header_off = self._preprocess(
                source[include_start:include_end],
                diagnostic=False,
                include_dirs=(frame.parent,),
            )
            header_on = self._preprocess(
                source[include_start:include_end],
                diagnostic=True,
                include_dirs=(frame.parent,),
            )
            self.assertNotIn("std::mutex", header_off)
            self.assertIn("std::mutex", header_on)
            # Keep the historical fixture. Project out only the reviewed staging
            # telemetry; every other ordinary-path delta still fails comparison.
            registration = (
                "      const double completionRegistrationMs =\n"
                "          g_browserStagingDiagnosticsEnabled ? phase_now_ms() : 0.0;\n"
                "      const int32_t diagnosticSourceFrame =\n"
                "          g_browserDiagnosticSourceFrame.load(std::memory_order_acquire);\n"
                "      if (g_browserStagingDiagnosticsEnabled) {\n"
                "        g_browserQueueCompletionRegistrations.fetch_add(1, std::memory_order_acq_rel);\n"
                "      }\n"
            )
            capture = "[stagingSlot, generation, completionRegistrationMs,\n           diagnosticSourceFrame]"
            completion = "            observe_browser_queue_completion(completionRegistrationMs, diagnosticSourceFrame);\n"
            for addition in (registration, capture, completion):
                self.assertEqual(off.count(addition), 1)
            projected = off.replace(registration, "").replace(
                capture, "[stagingSlot, generation]"
            ).replace(completion, "")
            self.assertEqual(projected.strip(), BASELINE.read_text(encoding="utf-8").split("\n", 2)[2].strip())
            guard = "if (!g_browserStagingDiagnosticsEnabled) {\n    return;\n  }"
            observer_start = source.index("void observe_browser_queue_completion(")
            observer_end = source.index("\n}\n", observer_start)
            observer = source[observer_start:observer_end]
            self.assertEqual(observer.count(guard), 1)
            self.assertLess(observer.index(guard), observer.index("phase_now_ms()"))
            positions = [off.index(token) for token in (
                "if (generation != g_browserStagingGeneration.load",
                "return;",
                "observe_browser_queue_completion(",
                "if (status != wgpu::QueueWorkDoneStatus::Success)",
                'Log.fatal("Browser staging submission failed:',
                "g_stagingSlots.release(stagingSlot)",
            )]
            self.assertEqual(positions, sorted(positions))
            # This is a bounded source projection, not binary/default-path
            # equivalence: disabled diagnostics still capture an atomic frame read.

            off_preprocessed = self._preprocess(function, diagnostic=False)
            on_preprocessed = self._preprocess(function, diagnostic=True)

            # The ordinary browser path retains its queue observer, generation
            # guard and release, while the opt-in owner book/counters/fatal path
            # are absent from its preprocessed translation unit.
            for token in (
                "g_queue.OnSubmittedWorkDone",
                "g_browserStagingGeneration",
                "g_stagingSlots.release",
            ):
                self.assertIn(token, off_preprocessed)
            for token in (
                "g_browserSubmissionOwners",
                "g_browserSubmissionSequence",
                "g_browserCallbackSequence",
                "BrowserOwnerCallbackOutcome",
                "OwnershipMismatch",
                "frameEncoderHandle",
            ):
                self.assertNotIn(token, off_preprocessed)
                self.assertIn(token, on_preprocessed)
            self.assertIn("Browser staging owner mismatch", on_preprocessed)
            self.assertNotIn("Browser staging owner mismatch", off_preprocessed)

    def test_build_profile_controls_reach_the_real_configure_graph(self):
        for target, expected_profile, expected_shadow_profile, expected_directory in (
            (
                BUILD.AURORA_QUIESCENCE_DIAGNOSTIC_TARGET,
                "ON",
                "OFF",
                "build/browser-quiescence-release",
            ),
            ("graphics", "OFF", "OFF", "build/browser-release"),
        ):
            with self.subTest(target=target):
                root = self.scratch / f"build-graph-{target}"
                root.mkdir()
                bins = root / ".venv/bin"
                emscripten = root / ".deps/emsdk/upstream/emscripten"
                bins.mkdir(parents=True)
                emscripten.mkdir(parents=True)
                for path in (bins / "cmake", bins / "ninja", emscripten / "emcmake"):
                    path.write_text("fixture\n", encoding="utf-8")
                (root / ".deps/emsdk/.emscripten").write_text("fixture\n", encoding="utf-8")
                (emscripten / "emscripten-version.txt").write_text("6.0.9\n", encoding="utf-8")
                generated = root / "build/gameplay-source/src"
                lock = {"repositories": {}, "emscripten": "6.0.9"}
                with patch.object(BUILD, "read_lock", return_value=lock), \
                        patch.object(BUILD, "verify_sources"), \
                        patch.object(BUILD, "prepare_sources", return_value=generated), \
                        patch.object(BUILD.subprocess, "run") as run:
                    BUILD.build(2, root=root, target=target, configuration="Release")
                configure_calls = [call for call in run.call_args_list if "-S" in call.args[0]]
                self.assertEqual(len(configure_calls), 1)
                configure = configure_calls[0].args[0]
                self.assertIn(
                    f"-DMELEE_WEB_AURORA_QUIESCENCE_DIAGNOSTIC={expected_profile}",
                    configure,
                )
                self.assertIn(
                    f"-DMELEE_WEB_AURORA_STAGING_SHADOW_INVENTORY={expected_shadow_profile}",
                    configure,
                )
                build_calls = [call for call in run.call_args_list if "--build" in call.args[0]]
                self.assertEqual(len(build_calls), 1)
                self.assertEqual(build_calls[0].args[0][2], str(root / expected_directory))
                self.assertEqual(
                    build_calls[0].args[0][build_calls[0].args[0].index("--target") + 1],
                    "gx_probe",
                )

    def test_staging_shadow_build_flag_reaches_only_the_private_trace(self):
        root = self.scratch / "aurora-shadow-build-graph"
        root.mkdir()
        bins = root / ".venv/bin"
        emscripten = root / ".deps/emsdk/upstream/emscripten"
        bins.mkdir(parents=True)
        emscripten.mkdir(parents=True)
        for path in (bins / "cmake", bins / "ninja", emscripten / "emcmake"):
            path.write_text("fixture\n", encoding="utf-8")
        (root / ".deps/emsdk/.emscripten").write_text("fixture\n", encoding="utf-8")
        (emscripten / "emscripten-version.txt").write_text("6.0.9\n", encoding="utf-8")
        (root / "assets-local/snapshot-mario").mkdir(parents=True)
        generated = root / "build/gameplay-source/src"
        with patch.object(BUILD, "read_lock", return_value={"repositories": {}, "emscripten": "6.0.9"}), \
                patch.object(BUILD, "verify_sources"), \
                patch.object(BUILD, "prepare_sources", return_value=generated), \
                patch.object(BUILD.subprocess, "run") as run:
            BUILD.build(2, root=root, configuration="Release",
                        trace_targets=("gameplay_slippi_synctest",),
                        aurora_staging_shadow_diagnostic=True)
        configure_calls = [call for call in run.call_args_list if "-S" in call.args[0]]
        self.assertEqual(len(configure_calls), 1)
        configure = configure_calls[0].args[0]
        self.assertIn("-DMELEE_WEB_AURORA_QUIESCENCE_DIAGNOSTIC=ON", configure)
        self.assertIn("-DMELEE_WEB_AURORA_STAGING_SHADOW_INVENTORY=ON", configure)
        build_calls = [call for call in run.call_args_list if "--build" in call.args[0]]
        self.assertEqual(len(build_calls), 1)
        self.assertEqual(build_calls[0].args[0][2],
                         str(root / "build/slippi-synctest-aurora-staging-release"))

    def test_staging_shadow_opt_in_is_private_and_release_only(self):
        root = Path("/fixture/repo")
        self.assertEqual(
            BUILD.build_directory(root, target="gameplay_slippi_synctest", configuration="Release"),
            root / "build/slippi-synctest-release",
        )
        self.assertEqual(
            BUILD.build_directory(root, target="gameplay_slippi_synctest", configuration="Release",
                                  aurora_staging_shadow_diagnostic=True),
            root / "build/slippi-synctest-aurora-staging-release",
        )
        with self.assertRaisesRegex(ValueError, "Release Slippi synctest"):
            BUILD.build_directory(root, target="runtime", configuration="Release",
                                  aurora_staging_shadow_diagnostic=True)
        with self.assertRaisesRegex(ValueError, "Release Slippi synctest"):
            BUILD.build_directory(root, target="gameplay_slippi_synctest", configuration="Debug",
                                  aurora_staging_shadow_diagnostic=True)
        build = (ROOT / "scripts/build.py").read_text(encoding="utf-8")
        cmake = (ROOT / "cmake/FighterRuntime.cmake").read_text(encoding="utf-8")
        self.assertIn('"--aurora-staging-shadow-diagnostic"', build)
        self.assertIn('build/slippi-synctest-aurora-staging-release', build)
        self.assertIn('if(MELEE_WEB_AURORA_STAGING_SHADOW_INVENTORY)', cmake)
        self.assertIn('NOT MELEE_WEB_AURORA_QUIESCENCE_DIAGNOSTIC', cmake)
        self.assertIn('MELEE_WEB_AURORA_STAGING_SHADOW_INVENTORY=1', cmake)
        self.assertIn('_melee_web_synctest_aurora_staging_shadow_inventory_query', cmake)

    def test_staging_shadow_observer_is_pure_and_rejects_unsafe_output(self):
        with self._patched_frame() as frame:
            source = frame.read_text(encoding="utf-8")
            start = source.index("// BEGIN PURE STAGING OBSERVER")
            end = source.index("// END PURE STAGING OBSERVER", start)
            observer = source[start:end]
            for forbidden in (
                "browser_submission_status(", "enqueue_process_events(", "process_events(",
                "browser_quiescence_descriptor(", "browser_quiescence_validate_descriptor(",
                "browser_quiescence_capture_lease(", "browser_quiescence_validate_lease(",
                "synchronize(", "malloc(", "calloc(", "realloc(", ".exchange(", "queue.submit(",
            ):
                self.assertNotIn(forbidden, observer)
            self.assertIn("const void* browser_quiescence_observe_descriptor_impl()", observer)
            self.assertIn("quiescence_lease_equal(before, after)", observer)
            self.assertIn("aurora_browser_quiescence_observer_storage", observer)
            compiler = shutil.which("clang++") or shutil.which("g++")
            if compiler is None:
                self.skipTest("C++ compiler unavailable")
            header = (frame.parents[2] / "include/aurora/gfx.h").read_text(encoding="utf-8")
            fixture_source = staging_observer_fixture(source, header)
            for threaded in (False, True):
                directory = self.scratch / ("pure-observer-threaded" if threaded else "pure-observer-inline")
                directory.mkdir()
                fixture = directory / "control.cpp"
                fixture.write_text(fixture_source, encoding="utf-8")
                command = [compiler, "-std=c++20", "-O0", str(fixture), "-o", str(directory / "control")]
                if threaded:
                    command.append("-D__EMSCRIPTEN_PTHREADS__=1")
                (directory / "command.json").write_text(json.dumps(command) + "\n", encoding="utf-8")
                for ordinal, arguments in enumerate((command, [str(directory / "control")])):
                    result = subprocess.run(arguments, capture_output=True, text=True, timeout=30)
                    (directory / f"{ordinal}.stdout").write_text(result.stdout, encoding="utf-8")
                    (directory / f"{ordinal}.stderr").write_text(result.stderr, encoding="utf-8")
                    self.assertEqual(result.returncode, 0, result.stderr)

    def test_disabled_public_api_is_explicit_zero_write(self):
        current = PATCH.read_text(encoding="utf-8")
        self.assertIn(
            "+  // This status poll is part of the ordinary browser preparation path. Keep it\n"
            "+  // independent of the opt-in owner book and descriptor/lease diagnostics.\n",
            current,
        )
        self.assertIn(
            "+#if defined(AURORA_BROWSER_QUIESCENCE_DIAGNOSTIC) && !defined(__EMSCRIPTEN_PTHREADS__)\n"
            "+  return true;\n"
            "+#else\n"
            "+  return false;\n",
            current,
        )
        with self._patched_frame() as frame:
            source = frame.read_text(encoding="utf-8")
            start = source.index("#else  // AURORA_BROWSER_QUIESCENCE_DIAGNOSTIC")
            end = source.index("#endif  // AURORA_BROWSER_QUIESCENCE_DIAGNOSTIC", start)
            disabled = source[start:end]
            for signature in (
                "bool browser_quiescence_supported() noexcept { return false; }",
                "size_t browser_quiescence_descriptor_bytes() noexcept { return 0; }",
                "size_t browser_quiescence_lease_bytes() noexcept { return 0; }",
                "int browser_quiescence_descriptor(void*, size_t) noexcept { return 0; }",
                "const void* browser_quiescence_observe_descriptor() noexcept { return nullptr; }",
                "int browser_quiescence_validate_descriptor(const void*, size_t) noexcept { return 0; }",
                "int browser_quiescence_capture_lease(AuroraBrowserQuiescenceLease*) noexcept { return 0; }",
                "int browser_quiescence_validate_lease(const AuroraBrowserQuiescenceLease*) noexcept { return 0; }",
            ):
                self.assertIn(signature, disabled)
            self.assertNotIn("memcpy", disabled)


if __name__ == "__main__":
    unittest.main()
