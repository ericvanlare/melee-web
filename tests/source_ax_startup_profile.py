"""Compile pinned AX source with same-TU read-only layout accessors.

This helper deliberately stops at object compilation. AXInit reaches DSP and
OS services that are owned by the integration target; inventing those services
here would turn a layout check into an audio-runtime claim.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / ".deps/emsdk"
AX_ROOT = ROOT / ".deps/melee/extern/dolphin/src/dolphin/ax"
EMCC = SDK / "upstream/emscripten/emcc.py"
LLVM_NM = SDK / "upstream/bin/llvm-nm"
ACCESSOR_HEADER = ROOT / "tests/source_ax_startup_accessors.h"
ALIGNMENT_PATCH = ROOT / "patches/source-ax-startup-alignment.patch"
SERVICES_PATCH = ROOT / "patches/source-ax-startup-services.patch"

SOURCE_NAMES = (
    "AX.c",
    "AXAlloc.c",
    "AXVPB.c",
    "AXSPB.c",
    "AXAux.c",
    "AXCL.c",
    "AXOut.c",
    "AXProf.c",
    "DSPCode.c",
)
ACCESSOR_NAMES = tuple(name for name in SOURCE_NAMES if name not in ("AX.c", "AXProf.c"))

EXPECTED_SOURCE_SHA256 = {
    "AX.c": "0ae9762d57ac25806d391302996ec68222514872678147635694127c350ed5df",
    "AXAlloc.c": "d73f91a4b5f88bb5be3b51cf3e0c2e1c0951d7f3b7e6e31047609eca4ec951f3",
    "AXVPB.c": "b7042d79ba6afd7aac1a475f78330fc8a6e9252a5f23ac281c086cf568b11c3b",
    "AXSPB.c": "a07eff99ee0d751d80fe735dca3e8cca799b6be404b9cbd17b211e125aca49c1",
    "AXAux.c": "13659fb4e81025a7acc82baa9afcdf5691bb589b89dfb3e9c8d7e42c5d6cfbe5",
    "AXCL.c": "43294bf7887a327da3a2aedff42cc826e916c280ff67c12f7512a427a2272d25",
    "AXOut.c": "8a6d6c138c6262a2a97a08759cddb984d86e85979d990e6bc1455a1eb7207aa4",
    "AXProf.c": "ab820c816778df4d21be244647724e4ee54638fe0eedd4f5667ec30ebe854b23",
    "DSPCode.c": "8807c54266535396ea190e40e370fa045a3461ddbe52c5a5ef09913e4baaddb9",
}


def _node_environment() -> dict[str, str]:
    env = dict(os.environ)
    env.update(
        {
            "EMSDK": str(SDK),
            "EM_CONFIG": str(SDK / ".emscripten"),
            "EM_CACHE": str(SDK / "upstream/emscripten/cache"),
            "EMSDK_PYTHON": sys.executable,
        }
    )
    return env


def _pointer_helpers() -> str:
    return r'''
#include "source_ax_startup_accessors.h"

_Static_assert(sizeof(u8) == 1, "source u8 ABI must be one byte");
_Static_assert(sizeof(u16) == 2, "source u16 ABI must be two bytes");
_Static_assert(sizeof(u32) == 4, "source u32 ABI must be four bytes");
_Static_assert(sizeof(long) == 4, "source long ABI must be four bytes");
_Static_assert(sizeof(unsigned long) == 4,
               "source unsigned long ABI must be four bytes");
_Static_assert(sizeof(void*) == 4, "source pointer ABI must be Wasm32");

static u32 melee_web_ax_address(const void* pointer)
{
    return (u32) (unsigned long) pointer;
}

#define melee_web_ax_function_address(callback) \
    ((u32) (unsigned long) (callback))

static u32 melee_web_ax_alignment(const void* pointer)
{
    u32 address = melee_web_ax_address(pointer);
    return address == 0 ? 0 : address & (0U - address);
}

static void melee_web_ax_reset_state(MeleeWebAxState* state)
{
    if (state != 0) {
        *state = (MeleeWebAxState) { 0 };
        state->version = MELEE_WEB_AX_ACCESSOR_VERSION;
    }
}

static void melee_web_ax_span(MeleeWebAxDescriptor* out, u32 index,
                              u32 kind, u32 tag, const void* address,
                              u32 bytes, u32 element_bytes,
                              u32 element_count)
{
    out[index].kind = kind;
    out[index].tag = tag;
    out[index].address = melee_web_ax_address(address);
    out[index].bytes = bytes;
    out[index].alignment = melee_web_ax_alignment(address);
    out[index].element_bytes = element_bytes;
    out[index].element_count = element_count;
}

static void melee_web_ax_callback(MeleeWebAxDescriptor* out, u32 index,
                                  u32 tag, u32 address)
{
    out[index].kind = MELEE_WEB_AX_CALLBACK;
    out[index].tag = tag;
    out[index].address = address;
    out[index].bytes = 0;
    out[index].alignment = 0;
    out[index].element_bytes = 0;
    out[index].element_count = 1;
}
'''


def _accessor_source(name: str) -> str:
    common = _pointer_helpers()
    if name == "AX.c":
        return common + r'''
u32 melee_web_ax_init_describe(MeleeWebAxDescriptor* out, u32 cap,
                               MeleeWebAxState* state)
{
    (void) out;
    (void) cap;
    melee_web_ax_reset_state(state);
    return 0;
}
'''
    if name == "AXAlloc.c":
        body = r'''
    const u32 required = 3;
    melee_web_ax_reset_state(state);
    if (out == 0 || cap < required) return required;
    melee_web_ax_span(out, 0, MELEE_WEB_AX_SPAN, MELEE_WEB_AX_TAG_ALLOC_HEAD,
                      &__AXStackHead[0], sizeof(__AXStackHead),
                      sizeof(__AXStackHead[0]),
                      sizeof(__AXStackHead) / sizeof(__AXStackHead[0]));
    melee_web_ax_span(out, 1, MELEE_WEB_AX_SPAN, MELEE_WEB_AX_TAG_ALLOC_TAIL,
                      &__AXStackTail[0], sizeof(__AXStackTail),
                      sizeof(__AXStackTail[0]),
                      sizeof(__AXStackTail) / sizeof(__AXStackTail[0]));
    melee_web_ax_span(out, 2, MELEE_WEB_AX_SPAN, MELEE_WEB_AX_TAG_ALLOC_CALLBACK_STACK,
                      &__AXCallbackStack, sizeof(__AXCallbackStack),
                      sizeof(__AXCallbackStack), 1);
    return required;
'''
        return common + "\nu32 melee_web_ax_alloc_describe(MeleeWebAxDescriptor* out, u32 cap, MeleeWebAxState* state)\n{\n" + body + "}\n"
    if name == "AXVPB.c":
        body = r'''
    const u32 required = 6;
    melee_web_ax_reset_state(state);
    if (out == 0 || cap < required) return required;
    melee_web_ax_span(out, 0, MELEE_WEB_AX_SPAN, MELEE_WEB_AX_TAG_SRC_CYCLES,
                      &__AXSrcCycles[0], sizeof(__AXSrcCycles),
                      sizeof(__AXSrcCycles[0]),
                      sizeof(__AXSrcCycles) / sizeof(__AXSrcCycles[0]));
    melee_web_ax_span(out, 1, MELEE_WEB_AX_SPAN, MELEE_WEB_AX_TAG_MIX_CYCLES,
                      &__AXMixCycles[0], sizeof(__AXMixCycles),
                      sizeof(__AXMixCycles[0]),
                      sizeof(__AXMixCycles) / sizeof(__AXMixCycles[0]));
    melee_web_ax_span(out, 2, MELEE_WEB_AX_SPAN, MELEE_WEB_AX_TAG_PB,
                      &__AXPB[0], sizeof(__AXPB), sizeof(__AXPB[0]),
                      sizeof(__AXPB) / sizeof(__AXPB[0]));
    melee_web_ax_span(out, 3, MELEE_WEB_AX_SPAN, MELEE_WEB_AX_TAG_ITD,
                      &__AXITD[0], sizeof(__AXITD), sizeof(__AXITD[0]),
                      sizeof(__AXITD) / sizeof(__AXITD[0]));
    melee_web_ax_span(out, 4, MELEE_WEB_AX_SPAN, MELEE_WEB_AX_TAG_UPDATES,
                      &__AXUpdates[0], sizeof(__AXUpdates),
                      sizeof(__AXUpdates[0]),
                      sizeof(__AXUpdates) / sizeof(__AXUpdates[0]));
    melee_web_ax_span(out, 5, MELEE_WEB_AX_SPAN, MELEE_WEB_AX_TAG_VPB,
                      &__AXVPB[0], sizeof(__AXVPB), sizeof(__AXVPB[0]),
                      sizeof(__AXVPB) / sizeof(__AXVPB[0]));
    return required;
'''
        return common + "\nu32 melee_web_ax_vpb_describe(MeleeWebAxDescriptor* out, u32 cap, MeleeWebAxState* state)\n{\n" + body + "}\n"
    if name == "AXSPB.c":
        body = r'''
    const u32 required = 1;
    melee_web_ax_reset_state(state);
    if (out == 0 || cap < required) return required;
    melee_web_ax_span(out, 0, MELEE_WEB_AX_SPAN, MELEE_WEB_AX_TAG_STUDIO,
                      &__AXStudio, sizeof(__AXStudio), sizeof(__AXStudio),
                      sizeof(__AXStudio) / sizeof(__AXStudio));
    return required;
'''
        return common + "\nu32 melee_web_ax_spb_describe(MeleeWebAxDescriptor* out, u32 cap, MeleeWebAxState* state)\n{\n" + body + "}\n"
    if name == "AXAux.c":
        body = r'''
    const u32 required = 4;
    melee_web_ax_reset_state(state);
    if (out == 0 || cap < required) return required;
    melee_web_ax_span(out, 0, MELEE_WEB_AX_SPAN, MELEE_WEB_AX_TAG_AUX_A,
                      &__AXBufferAuxA[0][0], sizeof(__AXBufferAuxA),
                      sizeof(__AXBufferAuxA[0][0]),
                      sizeof(__AXBufferAuxA) / sizeof(__AXBufferAuxA[0][0]));
    melee_web_ax_span(out, 1, MELEE_WEB_AX_SPAN, MELEE_WEB_AX_TAG_AUX_B,
                      &__AXBufferAuxB[0][0], sizeof(__AXBufferAuxB),
                      sizeof(__AXBufferAuxB[0][0]),
                      sizeof(__AXBufferAuxB) / sizeof(__AXBufferAuxB[0][0]));
    melee_web_ax_callback(out, 2, MELEE_WEB_AX_TAG_AUX_CALLBACK_A,
                          melee_web_ax_function_address(__AXCallbackAuxA));
    melee_web_ax_callback(out, 3, MELEE_WEB_AX_TAG_AUX_CALLBACK_B,
                          melee_web_ax_function_address(__AXCallbackAuxB));
    return required;
'''
        return common + "\nu32 melee_web_ax_aux_describe(MeleeWebAxDescriptor* out, u32 cap, MeleeWebAxState* state)\n{\n" + body + "}\n"
    if name == "AXCL.c":
        body = r'''
    const u32 required = 2;
    melee_web_ax_reset_state(state);
    if (state != 0) {
        state->command_list_position = __AXCommandListPosition;
        state->command_list_write = melee_web_ax_address(__AXClWrite);
        state->command_list_cycles = __AXCommandListCycles;
        state->command_list_mode = __AXClMode;
        state->command_list_capacity_words =
            sizeof(__AXCommandList) / sizeof(__AXCommandList[0][0]);
        state->command_list_flush_bytes = sizeof(__AXCommandList[0]);
    }
    if (out == 0 || cap < required) return required;
    melee_web_ax_span(out, 0, MELEE_WEB_AX_SPAN, MELEE_WEB_AX_TAG_HRTF_HISTORY,
                      &__AXHRTFHistory[0], sizeof(__AXHRTFHistory),
                      sizeof(__AXHRTFHistory[0]),
                      sizeof(__AXHRTFHistory) / sizeof(__AXHRTFHistory[0]));
    melee_web_ax_span(out, 1, MELEE_WEB_AX_SPAN, MELEE_WEB_AX_TAG_COMMAND_LIST,
                      &__AXCommandList[0][0], sizeof(__AXCommandList),
                      sizeof(__AXCommandList[0][0]),
                      sizeof(__AXCommandList) / sizeof(__AXCommandList[0][0]));
    return required;
'''
        return common + "\nu32 melee_web_ax_cl_describe(MeleeWebAxDescriptor* out, u32 cap, MeleeWebAxState* state)\n{\n" + body + "}\n"
    if name == "AXOut.c":
        body = r'''
    const u32 required = 9;
    melee_web_ax_reset_state(state);
    if (state != 0) {
        state->out_frame = __AXOutFrame;
        state->out_dsp_ready = __AXOutDspReady;
        state->dsp_init_flag = __AXDSPInitFlag;
        state->dsp_done_flag = __AXDSPDoneFlag;
        state->dsp_task_state = task.state;
        state->dsp_task_priority = task.priority;
        state->dsp_task_flags = task.flags;
        state->dsp_task_iram_address = melee_web_ax_address(task.iram_mmem_addr);
        state->dsp_task_iram_length = task.iram_length;
        state->dsp_task_dram_address = melee_web_ax_address(task.dram_mmem_addr);
        state->dsp_task_dram_length = task.dram_length;
        state->dsp_task_dram_size_bytes = sizeof(ax_dram_image);
        state->dsp_task_init_vector = task.dsp_init_vector;
        state->dsp_task_resume_vector = task.dsp_resume_vector;
    }
    if (out == 0 || cap < required) return required;
    melee_web_ax_span(out, 0, MELEE_WEB_AX_SPAN, MELEE_WEB_AX_TAG_OUT_BUFFER,
                      &__AXOutBuffer[0][0], sizeof(__AXOutBuffer),
                      sizeof(__AXOutBuffer[0][0]),
                      sizeof(__AXOutBuffer) / sizeof(__AXOutBuffer[0][0]));
    melee_web_ax_span(out, 1, MELEE_WEB_AX_SPAN, MELEE_WEB_AX_TAG_OUT_SBUFFER,
                      &__AXOutSBuffer[0], sizeof(__AXOutSBuffer),
                      sizeof(__AXOutSBuffer[0]),
                      sizeof(__AXOutSBuffer) / sizeof(__AXOutSBuffer[0]));
    melee_web_ax_span(out, 2, MELEE_WEB_AX_SPAN, MELEE_WEB_AX_TAG_PROFILE,
                      &__AXLocalProfile, sizeof(__AXLocalProfile),
                      sizeof(__AXLocalProfile), 1);
    melee_web_ax_span(out, 3, MELEE_WEB_AX_SPAN, MELEE_WEB_AX_TAG_DSP_TASK,
                      &task, sizeof(task), sizeof(task), 1);
    melee_web_ax_span(out, 4, MELEE_WEB_AX_SPAN, MELEE_WEB_AX_TAG_DRAM,
                      &ax_dram_image[0], sizeof(ax_dram_image),
                      sizeof(ax_dram_image[0]),
                      sizeof(ax_dram_image) / sizeof(ax_dram_image[0]));
    melee_web_ax_callback(out, 5, MELEE_WEB_AX_TAG_DSP_INIT_CALLBACK,
                          melee_web_ax_function_address(__AXDSPInitCallback));
    melee_web_ax_callback(out, 6, MELEE_WEB_AX_TAG_DSP_RESUME_CALLBACK,
                          melee_web_ax_function_address(__AXDSPResumeCallback));
    melee_web_ax_callback(out, 7, MELEE_WEB_AX_TAG_DSP_DONE_CALLBACK,
                          melee_web_ax_function_address(__AXDSPDoneCallback));
    melee_web_ax_callback(out, 8, MELEE_WEB_AX_TAG_USER_FRAME_CALLBACK,
                          melee_web_ax_function_address(__AXUserFrameCallback));
    return required;
'''
        return common + "\nu32 melee_web_ax_out_describe(MeleeWebAxDescriptor* out, u32 cap, MeleeWebAxState* state)\n{\n" + body + "}\n"
    if name == "DSPCode.c":
        body = r'''
    const u32 required = 2;
    melee_web_ax_reset_state(state);
    if (out == 0 || cap < required) return required;
    melee_web_ax_span(out, 0, MELEE_WEB_AX_SPAN, MELEE_WEB_AX_TAG_DSP_SLAVE,
                      &axDspSlave[0], sizeof(axDspSlave),
                      sizeof(axDspSlave[0]),
                      sizeof(axDspSlave) / sizeof(axDspSlave[0]));
    melee_web_ax_span(out, 1, MELEE_WEB_AX_SPAN, MELEE_WEB_AX_TAG_DSP_SLAVE_LENGTH,
                      &axDspSlaveLength, sizeof(axDspSlaveLength),
                      sizeof(axDspSlaveLength),
                      sizeof(axDspSlaveLength) / sizeof(axDspSlaveLength));
    return required;
'''
        return common + "\nu32 melee_web_ax_dsp_code_describe(MeleeWebAxDescriptor* out, u32 cap, MeleeWebAxState* state)\n{\n" + body + "}\n"
    raise ValueError(name)


def _hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _available() -> list[Path]:
    return [
        path for path in (SDK / ".emscripten", EMCC, LLVM_NM,
                          ACCESSOR_HEADER, ALIGNMENT_PATCH, SERVICES_PATCH, *[AX_ROOT / n for n in SOURCE_NAMES])
        if not path.is_file()
    ]


def build_profile() -> dict:
    missing = _available()
    if missing:
        raise unittest.SkipTest("missing pinned AX dependency: " + ", ".join(map(str, missing)))
    evidence_root = ROOT / "work"
    evidence_root.mkdir(parents=True, exist_ok=True)
    evidence = Path(tempfile.mkdtemp(prefix="source-ax-startup-profile-", dir=evidence_root))
    tree = evidence / "extern/dolphin/src/dolphin/ax"
    tree.mkdir(parents=True)
    originals = {name: AX_ROOT / name for name in SOURCE_NAMES}
    source_hashes = {name: _hash(path) for name, path in originals.items()}
    if source_hashes != EXPECTED_SOURCE_SHA256:
        raise AssertionError(
            "pinned AX source identity changed before generation: "
            + json.dumps(source_hashes, sort_keys=True)
        )
    for name, path in originals.items():
        destination = tree / name
        destination.write_bytes(path.read_bytes())
    # Apply explained alignment declarations and the declared bus-clock service seam.
    for patch_name, patch_path in (("alignment", ALIGNMENT_PATCH), ("services", SERVICES_PATCH)):
        patch_command = ["git", "apply", "--check", "--unsafe-paths", "--directory", str(evidence), str(patch_path)]
        checked = subprocess.run(patch_command, cwd=ROOT, text=True, capture_output=True)
        (evidence / f"{patch_name}-patch-check.stdout").write_text(checked.stdout)
        (evidence / f"{patch_name}-patch-check.stderr").write_text(checked.stderr)
        if checked.returncode:
            raise AssertionError(f"{patch_name} patch check failed: {evidence}\n{checked.stderr}")
        applied = subprocess.run(["git", "apply", "--unsafe-paths", "--directory", str(evidence), str(patch_path)], cwd=ROOT, text=True, capture_output=True)
        (evidence / f"{patch_name}-patch-apply.stdout").write_text(applied.stdout)
        (evidence / f"{patch_name}-patch-apply.stderr").write_text(applied.stderr)
        if applied.returncode:
            raise AssertionError(f"{patch_name} patch apply failed: {evidence}\n{applied.stderr}")

    generated = {}
    for name in SOURCE_NAMES:
        destination = tree / name
        if name in ACCESSOR_NAMES:
            generated_source = destination.read_text() + "\n" + _accessor_source(name)
            destination.write_text(generated_source)
            generated[name] = str(destination)

    include = [
        f"-I{ROOT / '.deps/melee/extern/dolphin/include'}",
        f"-I{ROOT / '.deps/melee/extern/dolphin/src/dolphin/gx'}",
        f"-I{AX_ROOT}",
        f"-I{ROOT / '.deps/melee/include'}",
        f"-I{ROOT / '.deps/melee/src'}",
        f"-I{ROOT / 'tests'}",
    ]
    env = _node_environment()
    objects = {}
    commands = {}
    for name in SOURCE_NAMES:
        source = tree / name
        output = evidence / (Path(name).stem + ".o")
        command = [sys.executable, str(EMCC), "-c", "-std=gnu11", "-O0",
                   "-ffp-contract=off", "-DDEBUG=1", "-Wall", "-Wextra", "-Werror",
                   "-Wno-array-parameter", "-Wno-visibility",
                   "-Wno-unused-value", "-Wno-unused-but-set-global",
                   "-Wno-unused-parameter", "-Wno-unused-variable",
                   "-Wno-unused-function", *include, str(source), "-o", str(output)]
        commands[name] = command
        (evidence / (Path(name).stem + ".command.json")).write_text(
            json.dumps(command, indent=2) + "\n")
        try:
            completed = subprocess.run(command, cwd=ROOT, env=env, text=True,
                                       capture_output=True, timeout=60)
        except subprocess.TimeoutExpired as exc:
            stdout = exc.stdout.decode() if isinstance(exc.stdout, bytes) else (exc.stdout or "")
            stderr = exc.stderr.decode() if isinstance(exc.stderr, bytes) else (exc.stderr or "")
            (evidence / (Path(name).stem + ".stdout")).write_text(stdout)
            (evidence / (Path(name).stem + ".stderr")).write_text(stderr)
            (evidence / (Path(name).stem + ".failure")).write_text(
                "AX source compile timed out after 60 seconds\n"
            )
            raise AssertionError(
                f"AX source compile timed out for {name}; evidence at {evidence}"
            ) from exc
        (evidence / (Path(name).stem + ".stdout")).write_text(completed.stdout)
        (evidence / (Path(name).stem + ".stderr")).write_text(completed.stderr)
        if completed.returncode:
            raise AssertionError(f"AX source compile failed for {name}; evidence at {evidence}\n{completed.stderr}")
        objects[name] = {"path": str(output), "bytes": output.stat().st_size,
                         "sha256": _hash(output)}
    symbols = {}
    for name in SOURCE_NAMES:
        if name not in ACCESSOR_NAMES:
            continue
        output = evidence / (Path(name).stem + ".o")
        listed = subprocess.run([str(LLVM_NM), "--defined-only", str(output)],
                                cwd=ROOT, text=True, capture_output=True,
                                timeout=10)
        (evidence / (Path(name).stem + ".nm")).write_text(listed.stdout)
        expected = "melee_web_ax_" + {
            "AXAlloc.c": "alloc_describe", "AXVPB.c": "vpb_describe",
            "AXSPB.c": "spb_describe", "AXAux.c": "aux_describe",
            "AXCL.c": "cl_describe", "AXOut.c": "out_describe",
            "DSPCode.c": "dsp_code_describe",
        }[name]
        if listed.returncode or expected not in listed.stdout:
            raise AssertionError(f"accessor symbol missing for {name}; evidence at {evidence}")
        symbols[name] = expected
    after_hashes = {name: _hash(path) for name, path in originals.items()}
    if after_hashes != source_hashes:
        raise AssertionError("pinned AX source changed during profile generation")
    receipt = {
        "kind": "source_ax_startup_compile",
        "accessor_version": 1,
        "source_hashes_before": source_hashes,
        "source_hashes_after": after_hashes,
        "alignment_patch_sha256": _hash(ALIGNMENT_PATCH),
        "services_patch_sha256": _hash(SERVICES_PATCH),
        "services_patch_sha256": _hash(SERVICES_PATCH),
        "accessor_header_sha256": _hash(ACCESSOR_HEADER),
        "patched_source_hashes": {name: _hash(tree / name) for name in SOURCE_NAMES},
        "objects": objects,
        "accessor_symbols": symbols,
        "commands": commands,
        "evidence_dir": str(evidence),
        "host_services_linked": False,
        "runtime_claim": False,
    }
    (evidence / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return receipt


class SourceAxStartupProfileTest(unittest.TestCase):
    def test_source_hashes_alignment_patch_accessors_and_object_compile(self) -> None:
        receipt = build_profile()
        self.assertEqual(receipt["source_hashes_before"], receipt["source_hashes_after"])
        self.assertFalse(receipt["host_services_linked"])
        self.assertFalse(receipt["runtime_claim"])
        self.assertEqual(len(receipt["objects"]), len(SOURCE_NAMES))
        self.assertEqual(len(receipt["accessor_symbols"]), len(ACCESSOR_NAMES))

    def test_alignment_patch_changes_only_required_declarations(self) -> None:
        source_cl = (AX_ROOT / "AXCL.c").read_text()
        source_out = (AX_ROOT / "AXOut.c").read_text()
        self.assertIn("static u16 __AXCommandList[2][384];", source_cl)
        self.assertIn("static s16 __AXOutBuffer[2][320];", source_out)
        self.assertIn("static long __AXOutSBuffer[160];", source_out)
        patch = ALIGNMENT_PATCH.read_text()
        self.assertEqual(patch.count("ATTRIBUTE_ALIGN(32)"), 3)
        self.assertNotIn("__AXCommandList[2][385]", patch)
        self.assertNotIn("__AXOutBuffer[2][321]", patch)


if __name__ == "__main__":
    unittest.main()
