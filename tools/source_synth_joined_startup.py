"""Compile the original Synth/DevCom/AX startup closure to Wasm objects.

This is a bounded compile probe, not an audio-runtime implementation.  It
prepares the reviewed downstream source tree, compiles the original bodies
with the source's 32-bit ABI, and attempts one strict link with no service
stubs.  The link is expected to stop at the real OS/AI/DSP/AR/DVD and
unadapted AXFX boundaries.  The DevCom ARQ callback uses a separately staged,
source-reviewed typed header so its request-pointer ABI is compiled strictly;
the header is recorded and never treated as a generic host compatibility
header.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / ".deps/emsdk"
MELEE = ROOT / ".deps/melee"
PREPARED = ROOT / "build/gameplay-source"
EMCC = SDK / "upstream/emscripten/emcc.py"
EMXX = SDK / "upstream/emscripten/em++.py"
LLVM_NM = SDK / "upstream/bin/llvm-nm"
SOURCE_REVISION = "b43912cc78606f96c9569f5d6229bc9d7e265ea5"
AR_HEADER_SOURCE = ROOT / "tests/source_audio_ar_services_include/dolphin/ar.h"
AR_HEADER_SHA256 = "b71e4f29625409283aa32678c03f266324531b2e0a20375adcabd561ee4fe6a1"
OSRTC_SOURCE = MELEE / "extern/dolphin/src/dolphin/os/OSRtc.c"
OSRTC_SOURCE_SHA256 = "17bd3582268008fce8441e2278dce461d2d9b7759f762f08a2074b10f76a8c16"
OSRTC_SOUND_MODE_BODY_SHA256 = "1d9cbd98a5f5703e1efc53fbb2ea16d4f1bffc7f47b7363eb6fb7ead3fb09dcc"
PREFIX_ALLOC_SOURCE = ROOT / "tests/original_startup_alloc_trace.cpp"
PREFIX_MEMORY_SOURCE = ROOT / "tests/original_startup_osmemory.cpp"
PREFIX_AURORA_MEMORY_SOURCE = ROOT / ".deps/aurora/lib/dolphin/os/OSMemory.cpp"
JOINED_ACCESSOR_SOURCE = ROOT / "tests/source_synth_joined_accessors.h"
JOINED_SRAM_TEMPLATE = ROOT / "tests/source_synth_joined_sram.c"
FMT_INCLUDE = ROOT / "build/browser/_deps/fmt-src/include"
FMT_FILES = {
    "base.h": FMT_INCLUDE / "fmt/base.h",
    "format.h": FMT_INCLUDE / "fmt/format.h",
}
SOURCE_FILES = {
    "synth": MELEE / "src/sysdolphin/baselib/synth.c",
    "devcom": MELEE / "src/sysdolphin/baselib/devcom.c",
    "axdriver": MELEE / "src/sysdolphin/baselib/axdriver.c",
    "axfx": MELEE / "extern/dolphin/src/dolphin/axfx/axfx.c",
    "delay": MELEE / "extern/dolphin/src/dolphin/axfx/delay.c",
    "reverb_std": MELEE / "extern/dolphin/src/dolphin/axfx/reverb_std.c",
}
PREPARED_FILES = {
    "synth": PREPARED / "src/sysdolphin/baselib/synth.c",
    "devcom": PREPARED / "src/sysdolphin/baselib/devcom.c",
    "axdriver": PREPARED / "src/sysdolphin/baselib/axdriver.c",
    "axfx": PREPARED / "extern/dolphin/src/dolphin/axfx/axfx.c",
    "delay": PREPARED / "extern/dolphin/src/dolphin/axfx/delay.c",
    "reverb_std": PREPARED / "extern/dolphin/src/dolphin/axfx/reverb_std.c",
}

EXPECTED_SOURCE_SHA256 = {
    "synth": "a8a45a3b66ea559ad945979ccfeb1eeb873d11210bd241ad0dfffaa14beefca6",
    "devcom": "aea7c93eb7be895f2c159568e0a9a1ed91f914aad5ed11130aa677c02d1d18cb",
    "axdriver": "c818fc3131c92ce7a3734cbac8d6ddc4074ba5a01fcf2ec3c1937ffc26c1ff3f",
    "axfx": "2e0966d1d29f3dde0ea94345ca4df734fa39ebc554b5c0bbb1e1a49dd8b2f85f",
    "delay": "e4765d6fd3e4322f9d0ef622ed7f13a72b03bd004f3b22d0a066eabb01aec3ef",
    "reverb_std": "a9a8e512656a86aca59da72c083ec56b27b63c7614332af76630a87bbed97de9",
}
REQUIRED_SYMBOLS = {
    "synth": "HSD_SynthInit",
    "devcom": "HSD_DevComARAMWakeUp",
    "axdriver": "AXDriver_8038E498",
    "axfx": "AXFXSetHooks",
    "delay": "AXFXDelayInit",
    "reverb_std": "AXFXReverbStdInit",
}


class JoinedStartupError(RuntimeError):
    """Raised when the compile probe cannot establish its bounded evidence."""


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


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


def _run(
    command: list[str],
    *,
    cwd: Path,
    env: dict[str, str],
    stdout_path: Path,
    stderr_path: Path,
    timeout: int,
) -> subprocess.CompletedProcess[str]:
    """Run one bounded command while retaining timeout diagnostics."""

    (stdout_path.parent).mkdir(parents=True, exist_ok=True)
    try:
        result = subprocess.run(
            command,
            cwd=cwd,
            env=env,
            text=True,
            capture_output=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as error:
        out = error.stdout.decode() if isinstance(error.stdout, bytes) else (error.stdout or "")
        err = error.stderr.decode() if isinstance(error.stderr, bytes) else (error.stderr or "")
        stdout_path.write_text(out, encoding="utf-8")
        stderr_path.write_text(err, encoding="utf-8")
        raise JoinedStartupError(f"command timed out after {timeout}s: {' '.join(command)}") from error
    stdout_path.write_text(result.stdout, encoding="utf-8")
    stderr_path.write_text(result.stderr, encoding="utf-8")
    return result


def _verify_pristine_sources() -> dict[str, str]:
    if not MELEE.is_dir() or not (MELEE / ".git").is_dir():
        raise JoinedStartupError(f"pinned melee source checkout is unavailable: {MELEE}")
    revision = subprocess.check_output(
        ["git", "-C", str(MELEE), "rev-parse", "HEAD"], text=True
    ).strip()
    if revision != SOURCE_REVISION:
        raise JoinedStartupError(f"pinned source revision changed: {revision} != {SOURCE_REVISION}")
    dirty = subprocess.check_output(
        ["git", "-C", str(MELEE), "status", "--porcelain", "--untracked-files=all"], text=True
    )
    if dirty:
        raise JoinedStartupError("pinned melee source checkout is dirty")
    hashes: dict[str, str] = {}
    for name, path in SOURCE_FILES.items():
        if not path.is_file():
            raise JoinedStartupError(f"missing source input: {path}")
        actual = sha256(path)
        if actual != EXPECTED_SOURCE_SHA256[name]:
            raise JoinedStartupError(
                f"source identity changed for {name}: {actual} != {EXPECTED_SOURCE_SHA256[name]}"
            )
        hashes[name] = actual
    return {"revision": revision, "files": hashes}


def _prepare_sources(evidence: Path, env: dict[str, str]) -> None:
    missing = [path for path in PREPARED_FILES.values() if not path.is_file()]
    if not missing:
        return
    # Import the preparer with the explicit checkout root.  Executing the
    # script path alone uses its source-file parent as ROOT and can prepare a
    # different checkout when this helper is invoked from another worktree.
    command = [
        sys.executable,
        "-c",
        (
            "import sys; from pathlib import Path; "
            "sys.path.insert(0, 'scripts'); "
            "from gameplay_sources import prepare_sources; "
            "prepare_sources(Path.cwd())"
        ),
    ]
    result = _run(
        command,
        cwd=ROOT,
        env=env,
        stdout_path=evidence / "prepare.stdout",
        stderr_path=evidence / "prepare.stderr",
        timeout=120,
    )
    if result.returncode:
        raise JoinedStartupError(f"prepared source generation failed; see {evidence / 'prepare.stderr'}")
    missing = [path for path in PREPARED_FILES.values() if not path.is_file()]
    if missing:
        raise JoinedStartupError("prepared source tree is incomplete: " + ", ".join(map(str, missing)))


def _check_prepared_markers(patch_path: Path) -> dict[str, str]:
    prepared_revision = subprocess.check_output(
        ["git", "-C", str(PREPARED), "rev-parse", "HEAD"], text=True
    ).strip()
    if prepared_revision != SOURCE_REVISION:
        raise JoinedStartupError(
            f"prepared source revision changed: {prepared_revision} != {SOURCE_REVISION}"
        )
    scripts_path = str(ROOT / "scripts")
    if scripts_path not in sys.path:
        sys.path.insert(0, scripts_path)
    from gameplay_bool import composed_patch

    composed = composed_patch(PREPARED, patch_path)
    prepared_patch = PREPARED / ".git/melee-web-gameplay.patch"
    if not prepared_patch.is_file() or prepared_patch.read_bytes() != composed.read_bytes():
        raise JoinedStartupError("prepared source patch provenance does not match the reviewed patch")
    markers = {
        "synth": "MELEE_WEB_ORIGINAL_STARTUP_FIXTURE",
        "devcom": "HSD_DevComARAMCallback",
        "axdriver": "MELEE_WEB_GAMEPLAY",
        "axfx": "AXFXSetHooks",
        "delay": "return AXFXDelaySettings(delay);",
        "reverb_std": "melee_web_audio_handle_reverb",
    }
    hashes: dict[str, str] = {}
    for name, path in PREPARED_FILES.items():
        text = path.read_text(encoding="utf-8")
        if markers[name] not in text:
            raise JoinedStartupError(f"prepared source marker missing for {name}: {markers[name]}")
        hashes[name] = sha256(path)
    hashes["composed_patch"] = sha256(composed)
    return hashes


def _ar_header() -> tuple[Path, str]:
    source = Path(os.environ.get("MELEE_WEB_SOURCE_AUDIO_AR_HEADER", str(AR_HEADER_SOURCE)))
    if not source.is_file():
        raise JoinedStartupError(
            "reviewed typed AR header is unavailable: "
            f"{source} (set MELEE_WEB_SOURCE_AUDIO_AR_HEADER to its checked path)"
        )
    actual = sha256(source)
    if actual != AR_HEADER_SHA256:
        raise JoinedStartupError(
            f"typed AR header identity changed: {actual} != {AR_HEADER_SHA256}"
        )
    return source, actual


def _write_abi_headers(include_dir: Path, ar_source: Path) -> dict[str, str]:
    """Stage only reviewed ABI declarations into the ignored evidence tree."""

    (include_dir / "dolphin").mkdir(parents=True, exist_ok=True)
    staged_ar = include_dir / "dolphin/ar.h"
    staged_ar.write_bytes(ar_source.read_bytes())
    (include_dir / "source_abi.h").write_text(
        "#ifndef MELEE_WEB_SOURCE_ABI_H\n"
        "#define MELEE_WEB_SOURCE_ABI_H\n"
        "typedef signed int ssize_t;\n"
        "#define __DEFINED_ssize_t 1\n"
        "_Static_assert(sizeof(bool) == 4, \"retail MSL bool must be four bytes\");\n"
        "_Static_assert(sizeof(unsigned int) == 4, \"retail u32 must be four bytes\");\n"
        "_Static_assert(sizeof(long) == 4, \"retail long must be four bytes\");\n"
        "_Static_assert(sizeof(void*) == 4, \"retail pointer ABI must be Wasm32\");\n"
        "#endif\n",
        encoding="utf-8",
    )
    (include_dir / "abi_asserts.h").write_text(
        "#ifndef MELEE_WEB_JOINED_ABI_ASSERTS_H\n"
        "#define MELEE_WEB_JOINED_ABI_ASSERTS_H\n"
        "#include <dolphin/ar.h>\n"
        "#include <dolphin/ax.h>\n"
        "typedef struct ARQRequest ARQRequest;\n"
        "_Static_assert(sizeof(struct ARQRequest) == 0x20, \"ARQ request ABI changed\");\n"
        "_Static_assert(sizeof(AXPB) == 0xc0, \"AXPB ABI changed\");\n"
        "_Static_assert(sizeof(AXVPB) == 0x1f8, \"AXVPB ABI changed\");\n"
        "_Static_assert(sizeof(AXSPB) == 0x36, \"AXSPB ABI changed\");\n"
        "#endif\n",
        encoding="utf-8",
    )
    (include_dir / "initialize_compat.h").write_text(
        "#ifndef MELEE_WEB_JOINED_INITIALIZE_COMPAT_H\n"
        "#define MELEE_WEB_JOINED_INITIALIZE_COMPAT_H\n"
        "#include <dolphin/mtx.h>\n"
        "typedef Vec Vec3;\n"
        "typedef enum { GX_TC_LINEAR = 0, GX_TC_GE = 1, GX_TC_EQ = 2, GX_TC_LE = 3 } GXTevClampMode;\n"
        "#endif\n",
        encoding="utf-8",
    )
    return {
        "typed_ar_header_sha256": sha256(ar_source),
        "staged_ar_header_sha256": sha256(staged_ar),
        "source_abi_sha256": sha256(include_dir / "source_abi.h"),
        "abi_asserts_sha256": sha256(include_dir / "abi_asserts.h"),
        "initialize_compat_sha256": sha256(include_dir / "initialize_compat.h"),
    }


def _write_joined_devcom_source(evidence: Path) -> tuple[Path, str]:
    """Compose DevCom with the reviewed same-TU relay accessor.

    The relay declaration must precede devcom.c, because the source's static
    object is intentionally private.  This wrapper keeps the declaration and
    accessor in the same translation unit without changing the pinned body.
    """

    if not JOINED_ACCESSOR_SOURCE.is_file():
        raise JoinedStartupError(f"joined DevCom accessor is unavailable: {JOINED_ACCESSOR_SOURCE}")
    wrapper = evidence / "source_synth_joined_devcom.c"
    wrapper.write_text(
        '#include "sysdolphin/baselib/devcom.static.h"\n'
        "_Alignas(ARQ_DMA_ALIGNMENT) static __typeof__(HSD_DevCom_804C6330_bufs)\n"
        "    HSD_DevCom_804C6330_bufs;\n"
        '#include "sysdolphin/baselib/devcom.c"\n'
        '#include "source_synth_joined_accessors.h"\n',
        encoding="utf-8",
    )
    return wrapper, sha256(JOINED_ACCESSOR_SOURCE)


def _write_joined_sram_source(evidence: Path) -> tuple[Path, dict[str, str]]:
    """Inject only the pinned OSGetSoundMode body into the SRAM fixture."""

    if not JOINED_SRAM_TEMPLATE.is_file():
        raise JoinedStartupError(f"joined SRAM template is unavailable: {JOINED_SRAM_TEMPLATE}")
    if not OSRTC_SOURCE.is_file() or sha256(OSRTC_SOURCE) != OSRTC_SOURCE_SHA256:
        raise JoinedStartupError("pinned OSRtc.c source identity changed or is unavailable")
    source = OSRTC_SOURCE.read_bytes()
    marker = b"unsigned long OSGetSoundMode()"
    start = source.find(marker)
    if start < 0:
        raise JoinedStartupError("OSGetSoundMode definition is missing from pinned OSRtc.c")
    end = source.find(b"\n}\n", start)
    if end < 0:
        raise JoinedStartupError("OSGetSoundMode body has no bounded closing brace")
    body = source[start : end + len(b"\n}\n")]
    body_hash = hashlib.sha256(body).hexdigest()
    if body_hash != OSRTC_SOUND_MODE_BODY_SHA256:
        raise JoinedStartupError(f"OSGetSoundMode body identity changed: {body_hash}")
    template = JOINED_SRAM_TEMPLATE.read_text(encoding="utf-8")
    marker_text = "/* MELEE_WEB_PINNED_SOUND_MODE */"
    if template.count(marker_text) != 1:
        raise JoinedStartupError("joined SRAM template must contain one sound-mode marker")
    generated = evidence / "source_synth_joined_sram.c"
    generated.write_text(template.replace(marker_text, body.decode("utf-8").rstrip("\n")), encoding="utf-8")
    return generated, {
        "template_sha256": sha256(JOINED_SRAM_TEMPLATE),
        "source_sha256": sha256(OSRTC_SOURCE),
        "body_sha256": body_hash,
        "generated_sha256": sha256(generated),
    }


def _write_cpp_probe_headers(include_dir: Path) -> dict[str, str]:
    """Stage the compile-only C++ ABI assertions."""

    cpp_include = include_dir.parent / "cpp_include"
    cpp_abi = cpp_include / "cpp_abi.h"
    cpp_include.mkdir(parents=True, exist_ok=True)
    cpp_abi.write_text(
        "#ifndef MELEE_WEB_JOINED_CPP_ABI_H\n"
        "#define MELEE_WEB_JOINED_CPP_ABI_H\n"
        "static_assert(sizeof(unsigned int) == 4, \"retail C++ u32 ABI changed\");\n"
        "static_assert(sizeof(long) == 4, \"retail C++ long ABI changed\");\n"
        "static_assert(sizeof(void*) == 4, \"retail C++ pointer ABI must be Wasm32\");\n"
        "#endif\n",
        encoding="utf-8",
    )
    return {"cpp_abi_sha256": sha256(cpp_abi)}


def _fmt_provenance() -> dict[str, object]:
    """Require the configured CMake fmt dependency and bind its headers."""

    missing = [path for path in FMT_FILES.values() if not path.is_file()]
    if missing:
        raise JoinedStartupError(
            "configured fmt dependency is unavailable: " + ", ".join(map(str, missing))
        )
    return {
        "include": str(FMT_INCLUDE),
        "files": {name: sha256(path) for name, path in FMT_FILES.items()},
    }


def _source_includes(evidence: Path, *, axfx: bool, melee_first: bool = False) -> list[str]:
    # Source AXFX files include the retail dolphin.h first.  Gameplay baselib
    # files use Aurora's host service declarations first, matching the existing
    # checked source probes and leaving the ARQ ABI mismatch visible.
    if axfx:
        directories = [
            evidence / "include",
            ROOT / "tests",
            ROOT / "src",
            PREPARED / "src",
            PREPARED,
            MELEE / "extern/dolphin/include",
            ROOT / ".deps/aurora/include",
            MELEE / "extern/dolphin/src/dolphin/gx",
            MELEE / "include",
            MELEE / "src",
        ]
    else:
        directories = [
            evidence / "include",
            ROOT / "tests",
            ROOT / "src",
            PREPARED / "src",
            PREPARED,
            ROOT / ".deps/aurora/include",
            MELEE / "extern/dolphin/include",
            MELEE / "extern/dolphin/src/dolphin/gx",
            MELEE / "include",
            MELEE / "src",
        ]
    if melee_first and not axfx:
        # OSRtc's authored declaration is unsigned long.  The Aurora host
        # header uses u32 for this ABI-equivalent function, which is a C type
        # conflict even under Wasm32.  This narrow source leaf must therefore
        # see the pinned retail declaration first.
        directories = [
            evidence / "include", ROOT / "tests", PREPARED / "src", PREPARED,
            MELEE / "extern/dolphin/include", ROOT / ".deps/aurora/include",
            MELEE / "extern/dolphin/src/dolphin/gx", MELEE / "include", MELEE / "src",
        ]
    return [item for directory in directories for item in ("-I", str(directory))]


def _compile_flags(evidence: Path, *, axfx: bool, melee_first: bool = False) -> list[str]:
    flags = [
        "-c", "-std=gnu11", "-O0", "-ffp-contract=off", "-DDEBUG=1",
        "-DTARGET_PC", "-DMELEE_WEB_GAMEPLAY", "-DMELEE_WEB_ORIGINAL_STARTUP_FIXTURE",
        "-Wall", "-Wextra", "-Werror", "-Wno-array-parameter", "-Wno-visibility",
        "-Wno-macro-redefined", "-Wno-unused-value", "-Wno-unused-but-set-global",
        "-Wno-unused-parameter", "-Wno-unused-variable", "-Wno-unused-function",
        "-Wno-sign-compare", "-Wno-incompatible-library-redeclaration",
        "-Wno-builtin-requires-header", "-include", str(evidence / "include/stdbool.h"),
        "-include", str(evidence / "include/source_abi.h"),
        "-include", str(evidence / "include/abi_asserts.h"),
    ]
    return flags + _source_includes(evidence, axfx=axfx, melee_first=melee_first)


def _compile_cpp_flags(evidence: Path) -> list[str]:
    return [
        "-c", "-std=c++20", "-O0", "-ffp-contract=off", "-DDEBUG=1",
        "-DTARGET_PC", "-DMELEE_WEB_GAMEPLAY", "-DMELEE_WEB_ORIGINAL_STARTUP_FIXTURE",
        "-Wall", "-Wextra", "-Werror", "-Wno-writable-strings",
        "-Wno-unused-parameter", "-Wno-unused-variable", "-Wno-unused-function",
        "-Wno-sign-compare", "-Wno-unused-but-set-variable",
        "-include", str(ROOT / "src/original_startup_compat.h"),
        "-include", str(evidence / "cpp_include/cpp_abi.h"),
        "-I", str(evidence / "cpp_include"),
        "-I", str(FMT_INCLUDE),
        "-I", str(ROOT / "tests"),
        "-I", str(ROOT / "src"),
        "-I", str(PREPARED / "src"),
        "-I", str(PREPARED),
        "-I", str(ROOT / ".deps/aurora/include"),
        "-I", str(MELEE / "extern/dolphin/include"),
        "-I", str(MELEE / "extern/dolphin/src/dolphin/gx"),
        "-I", str(MELEE / "include"),
        "-I", str(MELEE / "src"),
    ]


def _defined_symbols(path: Path, evidence: Path) -> list[str]:
    if not LLVM_NM.is_file():
        raise JoinedStartupError(f"pinned llvm-nm is unavailable: {LLVM_NM}")
    listed = subprocess.run(
        [str(LLVM_NM), "--defined-only", str(path)],
        cwd=ROOT,
        text=True,
        capture_output=True,
        timeout=15,
    )
    (evidence / f"{path.stem}.nm").write_text(listed.stdout + listed.stderr, encoding="utf-8")
    if listed.returncode:
        raise JoinedStartupError(f"llvm-nm failed for {path}")
    return re.findall(r"\s+[A-Za-z]\s+(\S+)$", listed.stdout, re.MULTILINE)


def _probe_source() -> str:
    return r'''
#include <stdint.h>
extern void HSD_SynthInit(int, int, int, int);
extern void HSD_DevComARAMWakeUp(void);
extern void AXDriver_8038E498(int, int, int, int);
extern int AXDriverSetupAux(int, int, void*);
extern int AXFXDelayInit(void*);
extern int AXFXReverbStdInit(void*);
extern void AXFXSetHooks(void*, void*);
static void* const entrypoints[] = {
    (void*)HSD_SynthInit, (void*)HSD_DevComARAMWakeUp,
    (void*)AXDriver_8038E498, (void*)AXDriverSetupAux,
    (void*)AXFXDelayInit, (void*)AXFXReverbStdInit, (void*)AXFXSetHooks,
};
__attribute__((used, noinline)) void melee_web_joined_entrypoints_anchor(void) {
    uintptr_t checksum = 0;
    for (unsigned i = 0; i < sizeof(entrypoints) / sizeof(entrypoints[0]); ++i)
        checksum += (uintptr_t)entrypoints[i];
    if (checksum == 1) __builtin_trap();
}
'''


def header_inventory(evidence: Path) -> dict[str, str]:
    """Bind the exact generated include trees, rejecting redirected entries."""
    if evidence.is_symlink() or not evidence.is_dir():
        raise JoinedStartupError("profile evidence must be a regular directory")
    result = {}
    for name in ("include", "cpp_include"):
        directory = evidence / name
        if directory.is_symlink() or not directory.is_dir():
            raise JoinedStartupError("profile include tree must be a regular directory")
        for path in sorted(directory.rglob("*")):
            if path.is_symlink():
                raise JoinedStartupError("profile include tree contains a symlink")
            if path.is_file():
                result[path.relative_to(evidence).as_posix()] = sha256(path)
    return result


def build_profile(work_dir: Path | None = None) -> dict:
    """Build the bounded object/link profile and retain all diagnostics."""

    if not EMCC.is_file() or not LLVM_NM.is_file():
        raise JoinedStartupError("pinned Emscripten compiler or llvm-nm is unavailable")
    (ROOT / "work").mkdir(parents=True, exist_ok=True)
    if work_dir is None:
        work_dir = Path(tempfile.mkdtemp(prefix="source-synth-joined-startup-", dir=ROOT / "work"))
    work_dir = work_dir.absolute()
    if work_dir.is_symlink():
        raise JoinedStartupError("artifact directory must not be a symlink")
    if work_dir.exists() and any(work_dir.iterdir()):
        raise JoinedStartupError(f"artifact directory must be new or empty: {work_dir}")
    work_dir.mkdir(parents=True, exist_ok=True)
    evidence = work_dir
    env = _node_environment()
    patch_path = ROOT / "patches/melee-gameplay.patch"
    if not patch_path.is_file():
        raise JoinedStartupError(f"reviewed gameplay patch is missing: {patch_path}")
    patch_hash = sha256(patch_path)
    source_before = _verify_pristine_sources()
    _prepare_sources(evidence, env)
    prepared_hashes = _check_prepared_markers(patch_path)
    ar_source, ar_hash = _ar_header()
    fmt_before = _fmt_provenance()

    include_dir = evidence / "include"
    include_dir.mkdir()
    msl_bool = MELEE / "src/MSL/stdbool.h"
    (include_dir / "stdbool.h").write_bytes(msl_bool.read_bytes())
    abi_headers = _write_abi_headers(include_dir, ar_source)
    cpp_headers = _write_cpp_probe_headers(include_dir)
    devcom_source, devcom_accessor_hash = _write_joined_devcom_source(evidence)
    sram_source, sram_provenance = _write_joined_sram_source(evidence)
    tests_path = str(ROOT / "tests")
    if tests_path not in sys.path:
        sys.path.insert(0, tests_path)
    import source_ax_startup_profile as ax_profile

    # Reuse the reviewed nine-object AX profile.  It compiles the authored AX
    # units and accessors with its own exact source/patched-tree receipt; this
    # probe only composes those objects into the unresolved joined link.
    headers_before = header_inventory(evidence)
    helper_before = sha256(Path(__file__))
    ax_receipt = ax_profile.build_profile()
    ax_objects = [entry.get("object", entry.get("path")) for entry in ax_receipt["objects"].values()]
    if len(ax_objects) != len(ax_profile.SOURCE_NAMES):
        raise JoinedStartupError("AX profile did not produce all nine reviewed objects")
    objects: dict[str, dict] = {}
    compile_sources = dict(PREPARED_FILES)
    compile_sources["devcom"] = devcom_source
    for name, source in compile_sources.items():
        output = evidence / f"{name}.o"
        command = [sys.executable, str(EMCC), *_compile_flags(evidence, axfx=name in {"axfx", "delay", "reverb_std"}), str(source), "-o", str(output)]
        (evidence / f"{name}.command.json").write_text(json.dumps(command, indent=2) + "\n")
        result = _run(command, cwd=ROOT, env=env,
                      stdout_path=evidence / f"{name}.stdout",
                      stderr_path=evidence / f"{name}.stderr", timeout=60)
        symbols = _defined_symbols(output, evidence) if result.returncode == 0 and output.is_file() else []
        if result.returncode == 0:
            required = REQUIRED_SYMBOLS[name]
            if required not in symbols:
                raise JoinedStartupError(f"{name} object lacks required symbol {required}")
            objects[name] = {"status": "pass", "object": str(output), "bytes": output.stat().st_size,
                             "sha256": sha256(output), "required_symbol": required}
            continue
        raise JoinedStartupError(
            f"{name} strict source compile failed; see {evidence / (name + '.stderr')}"
        )

    support_source = ROOT / "src/gameplay_audio_reverb.c"
    support_object = evidence / "gameplay_audio_reverb.o"
    support_command = [sys.executable, str(EMCC), *_compile_flags(evidence, axfx=False), str(support_source), "-o", str(support_object)]
    (evidence / "gameplay_audio_reverb.command.json").write_text(json.dumps(support_command, indent=2) + "\n")
    support_result = _run(support_command, cwd=ROOT, env=env,
                          stdout_path=evidence / "gameplay_audio_reverb.stdout",
                          stderr_path=evidence / "gameplay_audio_reverb.stderr", timeout=60)
    if support_result.returncode:
        raise JoinedStartupError("reviewed reverb support source failed; no replacement stub is allowed")

    sram_object = evidence / "source_synth_joined_sram.o"
    sram_command = [sys.executable, str(EMCC), *_compile_flags(evidence, axfx=False, melee_first=True), str(sram_source), "-o", str(sram_object)]
    (evidence / "source_synth_joined_sram.command.json").write_text(json.dumps(sram_command, indent=2) + "\n")
    sram_result = _run(sram_command, cwd=ROOT, env=env,
                       stdout_path=evidence / "source_synth_joined_sram.stdout",
                       stderr_path=evidence / "source_synth_joined_sram.stderr", timeout=60)
    if sram_result.returncode:
        raise JoinedStartupError("joined SRAM source failed; see source_synth_joined_sram.stderr")
    sram_symbols = _defined_symbols(sram_object, evidence)
    if "OSGetSoundMode" not in sram_symbols:
        raise JoinedStartupError("joined SRAM object lacks exact OSGetSoundMode")

    prefix_headers = cpp_headers
    prefix_objects: dict[str, dict] = {}
    cpp_sources = {
        "alloc_trace": PREFIX_ALLOC_SOURCE,
        "osmemory": PREFIX_MEMORY_SOURCE,
        "initialize": PREPARED / "src/sysdolphin/baselib/initialize.c",
    }
    for name, source in cpp_sources.items():
        if not source.is_file():
            raise JoinedStartupError(f"original startup prefix source is unavailable: {source}")
        output = evidence / f"{name}.o"
        if name == "initialize":
            command = [sys.executable, str(EMCC), *_compile_flags(evidence, axfx=False),
                       "-include", str(evidence / "include/initialize_compat.h"),
                       str(source), "-o", str(output)]
        else:
            command = [sys.executable, str(EMXX), *_compile_cpp_flags(evidence), str(source), "-o", str(output)]
        (evidence / f"{name}.command.json").write_text(json.dumps(command, indent=2) + "\n")
        result = _run(command, cwd=ROOT, env=env,
                      stdout_path=evidence / f"{name}.stdout",
                      stderr_path=evidence / f"{name}.stderr", timeout=90)
        symbols = _defined_symbols(output, evidence) if result.returncode == 0 and output.is_file() else []
        if result.returncode:
            raise JoinedStartupError(f"{name} original startup prefix compile failed; see {evidence / (name + '.stderr')}")
        prefix_objects[name] = {"status": "pass", "source": str(source),
                                "source_sha256": sha256(source), "object": str(output),
                                "bytes": output.stat().st_size, "sha256": sha256(output),
                                "defined_symbols": symbols}

    probe = evidence / "joined_probe.c"
    probe.write_text(_probe_source(), encoding="utf-8")
    probe_object = evidence / "joined_probe.o"
    probe_command = [sys.executable, str(EMCC), *_compile_flags(evidence, axfx=False), str(probe), "-o", str(probe_object)]
    (evidence / "joined_probe.command.json").write_text(json.dumps(probe_command, indent=2) + "\n")
    probe_result = _run(probe_command, cwd=ROOT, env=env,
                        stdout_path=evidence / "joined_probe.stdout",
                        stderr_path=evidence / "joined_probe.stderr", timeout=60)
    if probe_result.returncode:
        raise JoinedStartupError("joined probe failed; see joined_probe.stderr")

    link_output = evidence / "joined_startup.js"
    link_command = [sys.executable, str(EMXX), "-O0", "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1",
                    "-sSAFE_HEAP=1", "-sERROR_ON_UNDEFINED_SYMBOLS=1", "-Wl,--error-limit=0",
                    "-Wl,-u,melee_web_joined_entrypoints_anchor",
                    str(prefix_objects["alloc_trace"]["object"]),
                    str(prefix_objects["osmemory"]["object"]),
                    str(prefix_objects["initialize"]["object"]),
                    str(probe_object), str(sram_object), *ax_objects,
                    *(str(objects[name]["object"]) for name in SOURCE_FILES),
                    str(support_object), "-o", str(link_output)]
    (evidence / "link.command.json").write_text(json.dumps(link_command, indent=2) + "\n")
    link_result = _run(link_command, cwd=ROOT, env=env,
                       stdout_path=evidence / "link.stdout",
                       stderr_path=evidence / "link.stderr", timeout=120)
    link_text = (evidence / "link.stderr").read_text(encoding="utf-8")
    # Keep C++ namespace-qualified service names intact; truncating at ':'
    # would turn aurora::g_config into the misleading symbol "aurora".
    undefined = sorted(set(
        match.strip() for match in re.findall(r"undefined symbol: ([^\r\n]+)", link_text)
        if match.strip()
    ))
    (evidence / "link-undefined.json").write_text(json.dumps(undefined, indent=2) + "\n")
    if link_result.returncode and not undefined:
        raise JoinedStartupError("joined link failed without an unresolved-symbol boundary")
    source_after = _verify_pristine_sources()
    if source_after != source_before:
        raise JoinedStartupError("pinned source changed during joined startup profile")
    prepared_after = _check_prepared_markers(patch_path)
    if prepared_after != prepared_hashes:
        raise JoinedStartupError("prepared source changed during joined startup profile")
    if sha256(JOINED_ACCESSOR_SOURCE) != devcom_accessor_hash:
        raise JoinedStartupError("joined DevCom accessor changed during profile")
    if sha256(JOINED_SRAM_TEMPLATE) != sram_provenance["template_sha256"]:
        raise JoinedStartupError("joined SRAM template changed during profile")
    fmt_after = _fmt_provenance()
    if fmt_after != fmt_before:
        raise JoinedStartupError("configured fmt dependency changed during joined startup profile")
    platform_symbols = {
        "os_ai_dsp_ar_dvd": sorted(
            symbol for symbol in undefined
            if symbol.startswith(("OS", "AI", "DSP", "AR", "DVD", "__OS"))
            or symbol in {"__assert", "OSAllocFromHeap", "OSFreeToHeap", "__OSCurrHeap"}
        ),
        "axfx": sorted(symbol for symbol in undefined if symbol.startswith("AXFX")),
        "hsd_gx": sorted(symbol for symbol in undefined if symbol.startswith(("HSD_", "GX"))),
        "gameplay_services": sorted(symbol for symbol in undefined if symbol.startswith("melee_web_")),
        "aurora_host": sorted(symbol for symbol in undefined if symbol.startswith("aurora::")),
    }
    platform_path = evidence / "shared-platform-symbols.json"
    platform_path.write_text(json.dumps({"undefined_symbols": undefined, "categories": platform_symbols}, indent=2) + "\n", encoding="utf-8")
    receipt = {
        "kind": "source_synth_joined_startup_compile",
        "source_revision": SOURCE_REVISION,
        "source_before": source_before,
        "source_after": source_after,
        "gameplay_patch_sha256": patch_hash,
        "builder_sha256": helper_before,
        "generated_headers": headers_before,
        "typed_ar_header_sha256": ar_hash,
        "prepared_source_sha256": prepared_hashes,
        "prepared_source_sha256_after": prepared_after,
        "abi_headers": abi_headers,
        "cpp_headers": cpp_headers,
        "fmt_dependency": {
            "before": fmt_before,
            "after": fmt_after,
        },
        "joined_devcom": {
            "generated_source_sha256": sha256(devcom_source),
            "accessor_source_sha256": devcom_accessor_hash,
        },
        "joined_sram": sram_provenance,
        "ax_profile": {
            "source_hashes_before": ax_receipt.get("source_hashes_before"),
            "source_hashes_after": ax_receipt.get("source_hashes_after"),
            "evidence_dir": ax_receipt.get("evidence_dir"),
            "objects": ax_receipt.get("objects"),
            "accessor_symbols": ax_receipt.get("accessor_symbols"),
        },
        "msl_bool_header_sha256": sha256(msl_bool),
        "objects": objects,
        "prefix_objects": prefix_objects,
        "sram_object": {"status": "pass", "object": str(sram_object),
                        "bytes": sram_object.stat().st_size, "sha256": sha256(sram_object),
                        "required_symbol": "OSGetSoundMode", "defined_symbols": sram_symbols},
        "support_object": {"path": str(support_object), "sha256": sha256(support_object)},
        "shared_platform_symbols": {"path": str(platform_path), "categories": platform_symbols},
        "runtime_claim": False,
        "link": {"status": "expected_unresolved" if undefined else "pass",
                 "undefined_symbols": undefined, "command": link_command,
                 "safe_heap": True, "runtime_claim": False},
        "evidence_dir": str(evidence),
        "limits": [
            "No executable/runtime startup was run; the C++ heap prefix is linked only for unresolved ownership inventory.",
            "OS/AI/DSP/AR/DVD/DVD and unadapted chorus/high-reverb symbols remain unresolved.",
            "No host-service or audio-output success stubs are linked.",
        ],
    }
    if header_inventory(evidence) != headers_before or sha256(Path(__file__)) != helper_before:
        raise JoinedStartupError("generated headers or profile builder changed during compilation")
    (evidence / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    return receipt


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("--artifact-dir", type=Path)
    args = parser.parse_args()
    print(json.dumps(build_profile(args.artifact_dir), indent=2))
