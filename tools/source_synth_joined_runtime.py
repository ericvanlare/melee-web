"""Build the checked joined Synth startup runtime from reviewed objects.

This is a bounded integration builder.  It composes the checked HSD heap
prefix, source Synth/DevCom/AX objects, AR/AI/DSP services, and the existing
Aurora archive closure.  It never runs a shell command, accepts captured
addresses as inputs, or claims audio/runtime equivalence.  Runtime execution
is an explicit follow-up using independently derived boot, Synth-parameter,
and SRAM inputs.
"""

from __future__ import annotations

import ast
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from typing import Any

from tools import source_synth_joined_startup as startup


ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / ".deps/emsdk"
MELEE = ROOT / ".deps/melee"
BUILD_DEFAULT = ROOT / "build/browser"
SOURCE_SERVICES = ROOT / "tests/source_synth_joined_services.cpp"
SOURCE_SERVICES_HEADER = ROOT / "tests/source_synth_joined_services.h"
SOURCE_TRACE = ROOT / "tests/source_synth_joined_trace.cpp"
SOURCE_AR_SERVICE = ROOT / "src/source_audio_ar_services.c"
AI_PATCH = ROOT / "patches/source-ai-callback-stack.patch"

# This is the reviewed CMake archive closure from the existing browser
# configuration.  Keeping it explicit prevents a command-file parser from
# silently changing library order or pulling a host-installed dependency.
ARCHIVE_CLOSURE = (
    "libhsd_native_runtime.a",
    "libgameplay_runtime.a",
    ".deps/aurora/libaurora_os.a",
    ".deps/aurora/libaurora_gx.a",
    "_deps/png-build/libpng16.a",
    ".deps/aurora/libaurora_mtx.a",
    ".deps/aurora/libaurora_vi.a",
    ".deps/aurora/libaurora_core.a",
    "_deps/fmt-build/libfmt.a",
    "_deps/xxhash-build/libxxhash.a",
    "_deps/abseil-cpp-build/absl/strings/libabsl_cord.a",
    "_deps/abseil-cpp-build/absl/strings/libabsl_cordz_info.a",
    "_deps/abseil-cpp-build/absl/strings/libabsl_cord_internal.a",
    "_deps/abseil-cpp-build/absl/strings/libabsl_cordz_functions.a",
    "_deps/abseil-cpp-build/absl/strings/libabsl_cordz_handle.a",
    "_deps/abseil-cpp-build/absl/crc/libabsl_crc_cord_state.a",
    "_deps/abseil-cpp-build/absl/crc/libabsl_crc32c.a",
    "_deps/abseil-cpp-build/absl/crc/libabsl_crc_internal.a",
    "_deps/abseil-cpp-build/absl/crc/libabsl_crc_cpu_detect.a",
    "_deps/abseil-cpp-build/absl/strings/libabsl_str_format_internal.a",
    "_deps/abseil-cpp-build/absl/container/libabsl_raw_hash_set.a",
    "_deps/abseil-cpp-build/absl/hash/libabsl_hash.a",
    "_deps/abseil-cpp-build/absl/types/libabsl_bad_optional_access.a",
    "_deps/abseil-cpp-build/absl/hash/libabsl_city.a",
    "_deps/abseil-cpp-build/absl/types/libabsl_bad_variant_access.a",
    "_deps/abseil-cpp-build/absl/hash/libabsl_low_level_hash.a",
    "_deps/abseil-cpp-build/absl/container/libabsl_hashtablez_sampler.a",
    "_deps/abseil-cpp-build/absl/profiling/libabsl_exponential_biased.a",
    "_deps/abseil-cpp-build/absl/synchronization/libabsl_synchronization.a",
    "_deps/abseil-cpp-build/absl/numeric/libabsl_int128.a",
    "_deps/abseil-cpp-build/absl/base/libabsl_throw_delegate.a",
    "_deps/abseil-cpp-build/absl/debugging/libabsl_debugging_internal.a",
    "_deps/abseil-cpp-build/absl/base/libabsl_malloc_internal.a",
    "_deps/abseil-cpp-build/absl/debugging/libabsl_stacktrace.a",
    "_deps/abseil-cpp-build/absl/synchronization/libabsl_graphcycles_internal.a",
    "_deps/abseil-cpp-build/absl/synchronization/libabsl_kernel_timeout_internal.a",
    "_deps/abseil-cpp-build/absl/time/libabsl_time.a",
    "_deps/abseil-cpp-build/absl/time/libabsl_civil_time.a",
    "_deps/abseil-cpp-build/absl/time/libabsl_time_zone.a",
    "_deps/abseil-cpp-build/absl/debugging/libabsl_symbolize.a",
    "_deps/abseil-cpp-build/absl/strings/libabsl_strings.a",
    "_deps/abseil-cpp-build/absl/strings/libabsl_strings_internal.a",
    "_deps/abseil-cpp-build/absl/strings/libabsl_string_view.a",
    "_deps/abseil-cpp-build/absl/base/libabsl_base.a",
    "_deps/abseil-cpp-build/absl/base/libabsl_raw_logging_internal.a",
    "_deps/abseil-cpp-build/absl/base/libabsl_log_severity.a",
    "_deps/abseil-cpp-build/absl/base/libabsl_spinlock_wait.a",
    "_deps/abseil-cpp-build/absl/debugging/libabsl_demangle_internal.a",
    "_deps/abseil-cpp-build/absl/debugging/libabsl_demangle_rust.a",
    "_deps/abseil-cpp-build/absl/debugging/libabsl_decode_rust_punycode.a",
    "_deps/abseil-cpp-build/absl/debugging/libabsl_utf8_for_code_point.a",
    ".deps/aurora/extern/libsqlite3.a",
    "_deps/tracy-build/libTracyClient.a",
    ".deps/aurora/extern/libimgui.a",
    ".deps/aurora/extern/libimgui_backends.a",
    "_deps/freetype-build/libfreetype.a",
    "_deps/zlib-build/libz.a",
    "_deps/sdl-build/libSDL3.a",
)


class JoinedRuntimeError(RuntimeError):
    """Raised when a source, object, input, or runtime boundary is invalid."""


# This is the reviewed fail-closed matrix.  The retained v2 JSON is evidence
# of one run of these cases; it is deliberately not a builder dependency.
EXPECTED_NEGATIVE_DIAGNOSTICS = {
    "bad-dma": "AI DMA points outside AX output buffer",
    "bad-image": "AX DSP image mutation did not reach the source validator",
    "bad-mail": "DSP boot mail mismatch",
    "masked-pump": "DSP interrupt dispatched without masked interrupt owner",
    "missing-cache": "AX output buffer was not published before DMA",
    "bad-task-callback": "DSP task callback identity does not match AXOut source",
    "bad-task-span": "DSP task IRAM span does not match source image",
    "global-masked-pump": "DSP interrupt dispatched while globally or locally masked",
    "local-masked-pump": "DSP interrupt dispatched while globally or locally masked",
    "status-masked-pump": "DSP interrupt pending without enabled DSP status bits",
    "unknown": "unknown joined Synth startup mode",
}


LBAUDIO_NEGATIVE_DIAGNOSTICS = {
    "unsupported-chorus-init": "Native audio effect unavailable: AXFXChorusInit",
    "unsupported-chorus-shutdown": "Native audio effect unavailable: AXFXChorusShutdown",
    "unsupported-chorus-callback": "Native audio effect unavailable: AXFXChorusCallback",
    "unsupported-reverb-hi-init": "Native audio effect unavailable: AXFXReverbHiInit",
    "unsupported-reverb-hi-shutdown": "Native audio effect unavailable: AXFXReverbHiShutdown",
    "unsupported-reverb-hi-callback": "Native audio effect unavailable: AXFXReverbHiCallback",
    "unexpected-sfx-command": "source lbAudio startup cannot execute an SFX command stream",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _relative(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return path.name


def _environment() -> dict[str, str]:
    return startup._node_environment()


def _run(command: list[str], *, cwd: Path, work: Path, label: str,
         timeout: int) -> subprocess.CompletedProcess[str]:
    """Run argv directly and retain every stream, exit, and timeout record."""

    if not command or any(not isinstance(item, str) for item in command):
        raise JoinedRuntimeError("commands must be nonempty argv lists")
    (work / f"{label}.command.json").write_text(
        json.dumps(command, indent=2) + "\n", encoding="utf-8"
    )
    try:
        result = subprocess.run(
            command, cwd=cwd, env=_environment(), text=True,
            capture_output=True, timeout=timeout, shell=False,
        )
    except subprocess.TimeoutExpired as error:
        for stream in ("stdout", "stderr"):
            value = getattr(error, stream) or ""
            if isinstance(value, bytes):
                value = value.decode(errors="replace")
            (work / f"{label}.{stream}").write_text(value, encoding="utf-8")
        (work / f"{label}.timeout").write_text(f"{timeout}\n", encoding="utf-8")
        raise JoinedRuntimeError(f"{label} timed out; retained {work}") from error
    (work / f"{label}.stdout").write_text(result.stdout, encoding="utf-8")
    (work / f"{label}.stderr").write_text(result.stderr, encoding="utf-8")
    (work / f"{label}.exit").write_text(f"{result.returncode}\n", encoding="utf-8")
    return result


def _json_object(path: Path, label: str) -> dict[str, Any]:
    if path.is_symlink() or not path.is_file():
        raise JoinedRuntimeError(f"{label} must be a regular file: {path}")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise JoinedRuntimeError(f"{label} is not valid JSON: {path}") from error
    if not isinstance(value, dict):
        raise JoinedRuntimeError(f"{label} must be a JSON object")
    return value


def _file_record(path: Path, *, required: bool = True) -> dict[str, Any]:
    if required and (path.is_symlink() or not path.is_file()):
        raise JoinedRuntimeError(f"required runtime input is not a regular file: {path}")
    return {"path": _relative(path), "bytes": path.stat().st_size, "sha256": sha256(path)}


def validate_profile(path: Path) -> dict[str, Any]:
    profile = _json_object(path, "Synth object profile")
    if profile.get("kind") != "source_synth_joined_startup_compile":
        raise JoinedRuntimeError("profile is not the reviewed joined startup receipt")
    if profile.get("source_revision") != startup.SOURCE_REVISION:
        raise JoinedRuntimeError("profile source revision is not the pinned source")
    if profile.get("runtime_claim") is not False:
        raise JoinedRuntimeError("object profile must not claim runtime equivalence")
    if profile.get("source_before") != profile.get("source_after"):
        raise JoinedRuntimeError("pinned source changed during object profile")
    if profile.get("prepared_source_sha256") != profile.get("prepared_source_sha256_after"):
        raise JoinedRuntimeError("prepared gameplay source changed during object profile")
    if profile.get("source_after") != startup._verify_pristine_sources():
        raise JoinedRuntimeError("profile no longer matches the pinned source checkout")
    if profile.get("prepared_source_sha256_after") != startup._check_prepared_markers(ROOT / "patches/melee-gameplay.patch"):
        raise JoinedRuntimeError("profile no longer matches current prepared source")
    evidence = Path(profile.get("evidence_dir", ""))
    if evidence.absolute() != path.absolute().parent:
        raise JoinedRuntimeError("profile evidence directory does not own this receipt")
    try:
        headers = startup.header_inventory(evidence)
    except startup.JoinedStartupError as error:
        raise JoinedRuntimeError(str(error)) from error
    if headers != profile.get("generated_headers"):
        raise JoinedRuntimeError("profile generated headers changed")
    if profile.get("builder_sha256") != sha256(Path(startup.__file__)):
        raise JoinedRuntimeError("profile builder changed since object compilation")
    objects = profile.get("objects")
    if not isinstance(objects, dict) or set(objects) != set(startup.SOURCE_FILES):
        raise JoinedRuntimeError("profile does not contain all six joined source objects")
    for name, row in objects.items():
        if row.get("status") != "pass":
            raise JoinedRuntimeError(f"profile object {name} did not compile")
        path_value = row.get("object")
        object_path = Path(path_value) if isinstance(path_value, str) else Path()
        if not object_path.is_file() or sha256(object_path) != row.get("sha256"):
            raise JoinedRuntimeError(f"profile object hash is stale: {name}")
    prefix = profile.get("prefix_objects")
    if not isinstance(prefix, dict) or set(prefix) != {"alloc_trace", "osmemory", "initialize"}:
        raise JoinedRuntimeError("profile heap prefix object set is incomplete")
    for name, row in prefix.items():
        object_path = Path(row.get("object", ""))
        if row.get("status") != "pass" or not object_path.is_file() or sha256(object_path) != row.get("sha256"):
            raise JoinedRuntimeError(f"profile prefix object hash is stale: {name}")
    sram = profile.get("sram_object")
    sram_path = Path(sram.get("object", "")) if isinstance(sram, dict) else Path()
    if (not isinstance(sram, dict) or sram.get("status") != "pass" or
            sram.get("required_symbol") != "OSGetSoundMode" or
            not sram_path.is_file() or sha256(sram_path) != sram.get("sha256")):
        raise JoinedRuntimeError("profile SRAM object is missing or stale")
    ax = profile.get("ax_profile", {}).get("objects")
    if not isinstance(ax, dict) or len(ax) != 9:
        raise JoinedRuntimeError("profile does not contain all nine AX objects")
    for name, row in ax.items():
        object_path = Path(row.get("object", row.get("path", "")))
        expected = row.get("sha256")
        if not object_path.is_file() or not isinstance(expected, str) or sha256(object_path) != expected:
            raise JoinedRuntimeError(f"AX profile object hash is stale: {name}")
    return profile


def _configured_node() -> Path:
    config = SDK / ".emscripten"
    if not config.is_file():
        raise JoinedRuntimeError("configured Emscripten settings are unavailable")
    for statement in ast.parse(config.read_text(encoding="utf-8")).body:
        if isinstance(statement, ast.Assign) and any(
                isinstance(target, ast.Name) and target.id == "NODE_JS"
                for target in statement.targets):
            value = ast.literal_eval(statement.value)
            if not isinstance(value, str):
                break
            node = Path(value.replace("$CFGDIR", str(SDK))).resolve()
            if node.is_file() and node.is_relative_to(SDK.resolve()):
                return node
            break
    raise JoinedRuntimeError("configured Node is missing or outside the pinned SDK")


def validate_sram(settings_path: Path, envelope_path: Path) -> dict[str, Any]:
    """Validate the explicit synthetic JSON envelope plus its 64-byte view.

    Owned execution uses :func:`read_owned_sram_envelope`, which requires the
    actual 68-byte SRAM envelope.  Keeping this JSON form here is useful for
    small negative/unit tests and is never accepted as owned provenance.
    """
    envelope = _json_object(envelope_path, "SRAM envelope")
    data = settings_path.read_bytes() if settings_path.is_file() and not settings_path.is_symlink() else b""
    if envelope.get("bytes") != 68 or envelope.get("settings_offset") != 4 or envelope.get("settings_bytes") != 64:
        raise JoinedRuntimeError("SRAM envelope does not describe the authored 68-byte Dolphin layout")
    if envelope.get("flags_offset") != 19 or len(data) != 64:
        raise JoinedRuntimeError("SRAM settings must be exactly 64 bytes")
    if envelope.get("settings_sha256") != hashlib.sha256(data).hexdigest():
        raise JoinedRuntimeError("SRAM settings do not match the owned envelope")
    return {"settings": _file_record(settings_path), "envelope": _file_record(envelope_path),
            "flags_offset": 19}


def read_owned_sram_envelope(envelope_path: Path, output_dir: Path) -> tuple[Path, dict[str, Any]]:
    """Read an owned 68-byte SRAM image and publish only its 64-byte settings.

    The first four bytes are Dolphin's envelope prefix; the SDK's ``OSSram``
    object begins at offset four and keeps ``flags`` at byte 19 of that view.
    No JSON labels are accepted for this owned path.
    """

    if envelope_path.is_symlink() or not envelope_path.is_file():
        raise JoinedRuntimeError("owned SRAM envelope must be a regular file")
    raw = envelope_path.read_bytes()
    if len(raw) != 68:
        raise JoinedRuntimeError("owned SRAM envelope must contain exactly 68 bytes")
    settings = raw[4:]
    if len(settings) != 64:
        raise JoinedRuntimeError("owned SRAM settings view must contain exactly 64 bytes")
    if output_dir.is_symlink():
        raise JoinedRuntimeError("SRAM output directory must not be a symlink")
    output_dir.mkdir(parents=True, exist_ok=True)
    settings_path = output_dir / "owned-sram-settings.bin"
    if settings_path.exists() or settings_path.is_symlink():
        raise JoinedRuntimeError("owned SRAM settings output already exists")
    settings_path.write_bytes(settings)
    return settings_path, {
        "envelope": _file_record(envelope_path),
        "settings": _file_record(settings_path),
        "envelope_bytes": 68,
        "settings_offset": 4,
        "settings_bytes": 64,
        "flags_offset": 19,
        "settings_sha256": hashlib.sha256(settings).hexdigest(),
        "sdk_layout": "OSSram offset=4, flags=19, asserted by source_synth_joined_sram.c",
    }


def derive_runtime_inputs(boot_path: Path, parameters_path: Path,
                          settings_path: Path, envelope_path: Path,
                          *, synthetic: bool = False) -> dict[str, Any]:
    """Produce synthetic-test argv from already materialized JSON receipts.

    This function is intentionally not the owned execution path.  Callers
    with a disc/DOL must use :func:`derive_owned_runtime_inputs`, so matching
    identity strings in a JSON receipt cannot substitute for derivation.
    """

    if not synthetic:
        raise JoinedRuntimeError(
            "JSON receipts are synthetic-only; derive owned inputs from disc/DOL"
        )

    boot = _json_object(boot_path, "boot context")
    try:
        from tools.original_startup_fixture import fixture_arguments
        boot_args = fixture_arguments(boot, require_identity=False)
    except (ValueError, OSError) as error:
        raise JoinedRuntimeError(f"boot context rejected: {error}") from error
    parameters = _json_object(parameters_path, "Synth parameter receipt")
    if parameters.get("schema") != "melee-web-source-synth-parameters" or parameters.get("status") != "derived":
        raise JoinedRuntimeError("Synth parameters are not the reviewed source-derived receipt")
    if parameters.get("source_revision") != startup.SOURCE_REVISION:
        raise JoinedRuntimeError("Synth parameter source revision is not pinned")
    result = parameters.get("result")
    call = result.get("driver_call") if isinstance(result, dict) else None
    sizes = result.get("bank_sizes") if isinstance(result, dict) else None
    total = result.get("bank_size_total") if isinstance(result, dict) else None
    if (not isinstance(call, list) or len(call) != 4 or
            any(type(value) is not int or value < 0 or value > 0x7FFFFFFF for value in call) or
            not isinstance(sizes, list) or len(sizes) != 3 or
            any(type(value) is not int or value <= 0 for value in sizes) or
            total != sum(sizes) or call[3] != total):
        raise JoinedRuntimeError("Synth parameter driver call is malformed or inconsistent")
    sram = validate_sram(settings_path, envelope_path)
    arguments = ["valid", *(str(value) for value in call), str(settings_path), *boot_args]
    return {
        "arguments": arguments,
        "boot": {"path": _relative(boot_path), "sha256": sha256(boot_path)},
        "parameters": {"path": _relative(parameters_path), "sha256": sha256(parameters_path)},
        "sram": sram,
        "driver_call": call,
        "synthetic": synthetic,
    }


def derive_owned_runtime_inputs(*, dol_path: Path, disc_path: Path,
                                symbols_path: Path, source_root: Path,
                                sram_envelope_path: Path,
                                work_dir: Path) -> dict[str, Any]:
    """Derive all joined-runtime inputs from owned source/disc material.

    The JSON receipts used by the focused synthetic tests are not consulted.
    ``derive_boot_context`` validates the original disc/apploader/DOL and
    ``derive_parameters`` reruns the source Synth table/selection fixture.
    """

    try:
        from tools.original_boot_context import derive_boot_context
        from tools.original_startup_fixture import fixture_arguments
        from tools.source_synth_parameters import derive_parameters

        boot = derive_boot_context(Path(dol_path), Path(disc_path), Path(symbols_path), Path(source_root))
        boot_args = fixture_arguments(boot, require_identity=True)
        source_path = Path(source_root) / "src/melee/lb/lbaudio_ax.c"
        parameter_work = Path(work_dir) / "synth-parameters"
        parameters = derive_parameters(Path(dol_path), Path(symbols_path), source_path, parameter_work)
    except (OSError, ValueError, RuntimeError) as error:
        raise JoinedRuntimeError(f"owned source input derivation failed: {error}") from error

    if parameters.get("schema") != "melee-web-source-synth-parameters" or parameters.get("status") != "derived":
        raise JoinedRuntimeError("owned Synth derivation did not produce a derived receipt")
    result = parameters.get("result")
    call = result.get("driver_call") if isinstance(result, dict) else None
    sizes = result.get("bank_sizes") if isinstance(result, dict) else None
    total = result.get("bank_size_total") if isinstance(result, dict) else None
    if (parameters.get("source_revision") != startup.SOURCE_REVISION or
            not isinstance(call, list) or len(call) != 4 or
            any(type(value) is not int or value < 0 or value > 0x7FFFFFFF for value in call) or
            not isinstance(sizes, list) or len(sizes) != 3 or
            any(type(value) is not int or value <= 0 for value in sizes) or
            total != sum(sizes) or call[3] != total):
        raise JoinedRuntimeError("owned Synth derivation has malformed or unpinned parameters")

    settings_path, sram = read_owned_sram_envelope(Path(sram_envelope_path), Path(work_dir))
    arguments = ["valid", *(str(value) for value in call), str(settings_path), *boot_args]
    return {
        "arguments": arguments,
        "boot": {"derivation": boot.get("derivation"), "sha256": hashlib.sha256(
            json.dumps(boot, sort_keys=True).encode("utf-8")).hexdigest()},
        "parameters": {"source_revision": parameters["source_revision"], "sha256": hashlib.sha256(
            json.dumps(parameters, sort_keys=True).encode("utf-8")).hexdigest()},
        "sram": sram,
        "driver_call": call,
        "synthetic": False,
        "owned_inputs": {
            "dol": _file_record(Path(dol_path)),
            "disc": _file_record(Path(disc_path)),
            "symbols": _file_record(Path(symbols_path)),
            "source_root": _relative(Path(source_root)),
        },
    }


def validate_negative_results(path: Path | None = None) -> list[dict[str, Any]]:
    """Validate retained observations against the code-owned matrix.

    With no path this returns the expected case/diagnostic contract and does
    not depend on ignored work evidence.  A path is optional observed evidence
    and must contain exactly the same modes and diagnostic substrings.
    """

    if path is None:
        return [{"mode": mode, "diagnostic_contains": diagnostic}
                for mode, diagnostic in EXPECTED_NEGATIVE_DIAGNOSTICS.items()]
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise JoinedRuntimeError(f"joined runtime negative matrix is unreadable: {path}") from error
    if not isinstance(value, list) or len(value) != len(EXPECTED_NEGATIVE_DIAGNOSTICS):
        raise JoinedRuntimeError("joined runtime negative matrix must contain exactly 11 cases")
    modes: set[str] = set()
    for row in value:
        if (not isinstance(row, dict) or not isinstance(row.get("mode"), str) or
                row.get("mode") in modes or row.get("exit") != 1 or
                row.get("mode") not in EXPECTED_NEGATIVE_DIAGNOSTICS or
                not isinstance(row.get("diagnostic"), str) or
                EXPECTED_NEGATIVE_DIAGNOSTICS[row["mode"]] not in row["diagnostic"]):
            raise JoinedRuntimeError("joined runtime negative matrix has malformed diagnostics")
        modes.add(row["mode"])
    return value


def run_negative_matrix(node: Path, runtime_path: Path,
                        input_plan: dict[str, Any], work: Path,
                        diagnostics: dict[str, str] = EXPECTED_NEGATIVE_DIAGNOSTICS) -> list[dict[str, Any]]:
    """Execute every current fail-closed mode against the newly linked binary."""

    rows: list[dict[str, Any]] = []
    for mode, expected in diagnostics.items():
        arguments = list(input_plan["arguments"])
        arguments[0] = mode
        result = _run([str(node), str(runtime_path), *arguments], cwd=work,
                      work=work, label=f"negative-{mode}", timeout=60)
        if result.returncode != 1:
            raise JoinedRuntimeError(
                f"negative mode {mode} returned {result.returncode}, expected fail-closed exit 1"
            )
        diagnostic = (result.stderr.strip() or result.stdout.strip())
        if expected not in diagnostic:
            raise JoinedRuntimeError(
                f"negative mode {mode} omitted expected diagnostic: {expected}"
            )
        rows.append({"mode": mode, "exit": result.returncode,
                     "diagnostic": diagnostic,
                     "stdout_sha256": sha256(work / f"negative-{mode}.stdout"),
                     "stderr_sha256": sha256(work / f"negative-{mode}.stderr")})
    return rows


def _source_hashes() -> dict[str, str]:
    """Hash all source bodies injected into the runtime services."""

    paths = [SOURCE_SERVICES, SOURCE_SERVICES_HEADER, SOURCE_TRACE, SOURCE_AR_SERVICE, AI_PATCH,
             ROOT / "src/source_audio_ar_services.h",
             ROOT / "tests/original_startup_alloc_trace.cpp",
             ROOT / "tests/original_startup_osmemory.cpp",
             ROOT / "tests/source_synth_joined_sram.c",
             ROOT / "tests/source_synth_joined_accessors.h",
             ROOT / "tools/source_synth_joined_startup.py",
             ROOT / "tools/source_synth_joined_runtime.py",
             ROOT / "CMakeLists.txt",
             ROOT / "src/gameplay_audio_fx.c",
             MELEE / "extern/dolphin/src/dolphin/ai/ai.c",
             MELEE / "extern/dolphin/src/dolphin/dsp/dsp.c",
             MELEE / "extern/dolphin/src/dolphin/dsp/dsp_task.c",
             MELEE / "extern/dolphin/src/dolphin/ar/ar.c",
             MELEE / "extern/dolphin/src/dolphin/ar/arq.c",
             MELEE / "extern/dolphin/src/dolphin/os/OSInterrupt.c"]
    return {_relative(path): sha256(path) for path in paths}


def _replace_markers(work: Path) -> Path:
    """Generate the services TU from exact pinned source bodies."""

    sys.path.insert(0, str(ROOT / "tests"))
    import test_source_ax_startup_services as ax
    import test_source_audio_ar_services as ar

    ax.source_hashes()
    ar.source_hashes()
    (work / "ai.c").write_bytes((ax.AIROOT / "ai.c").read_bytes())
    for label, options in (("ai-patch-check", ["--check"]), ("ai-patch-apply", [])):
        result = _run(["git", "apply", *options, "--unsafe-paths", "--directory",
                       str(work), str(AI_PATCH)], cwd=ROOT, work=work, label=label, timeout=60)
        if result.returncode:
            raise JoinedRuntimeError(f"{label} failed; see {work / (label + '.stderr')}")
    generated = SOURCE_SERVICES.read_text(encoding="utf-8")
    replacements = {
        "/* MELEE_WEB_PINNED_OS_INTERRUPT */": ax.interrupt_source(ax.OSINT.read_text(encoding="utf-8")),
        "/* MELEE_WEB_PINNED_AR_SOURCE */": ar.SourceAudioArServiceTests.compatibility_copy(ar.AR_SOURCE.read_text(encoding="utf-8")),
        "/* MELEE_WEB_PINNED_ARQ_SOURCE */": ar.ARQ_SOURCE.read_text(encoding="utf-8"),
        "/* MELEE_WEB_PINNED_AI_SOURCE */": (work / "ai.c").read_text(encoding="utf-8"),
        "/* MELEE_WEB_PINNED_DSP_TASK_SOURCE */": (MELEE / "extern/dolphin/src/dolphin/dsp/dsp_task.c").read_text(encoding="utf-8"),
        "/* MELEE_WEB_PINNED_DSP_SOURCE */": (MELEE / "extern/dolphin/src/dolphin/dsp/dsp.c").read_text(encoding="utf-8"),
    }
    for marker, body in replacements.items():
        if generated.count(marker) != 1:
            raise JoinedRuntimeError(f"services marker count changed: {marker}")
        generated = generated.replace(marker, body)
    path = work / "services_generated.cpp"
    path.write_text(generated, encoding="utf-8")
    return path


def _runtime_archives(build_dir: Path) -> list[Path]:
    archives = [build_dir / relative for relative in ARCHIVE_CLOSURE]
    missing = [path for path in archives if not path.is_file()]
    if missing:
        raise JoinedRuntimeError("configured runtime archive closure is incomplete: " + ", ".join(_relative(path) for path in missing))
    return archives


def _refresh_hsd_native_runtime(build_dir: Path, work: Path, jobs: int = 2) -> None:
    """Refresh the checked native archive using the local configured CMake."""

    cmake = ROOT / ".venv/bin/cmake"
    if not cmake.is_file() or not (build_dir / "CMakeCache.txt").is_file():
        raise JoinedRuntimeError(
            "configured local CMake build is unavailable; run scripts/build.py --configure-only"
        )
    cache = (build_dir / "CMakeCache.txt").read_text()
    homes = [line.split("=", 1)[1] for line in cache.splitlines()
             if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL=")]
    if homes != [str(ROOT)] or build_dir.is_symlink():
        raise JoinedRuntimeError("CMake build is not owned by this source checkout")
    if jobs < 1:
        raise JoinedRuntimeError("CMake parallelism must be positive")
    result = _run(
        [str(cmake), "--build", str(build_dir), "--target", "hsd_native_runtime",
         "--parallel", str(jobs)],
        cwd=ROOT, work=work, label="hsd-native-runtime", timeout=600,
    )
    if result.returncode:
        raise JoinedRuntimeError(
            f"hsd_native_runtime refresh failed; see {work / 'hsd-native-runtime.stderr'}"
        )


def _compile_services(profile: dict[str, Any], work: Path, *, lbaudio: bool = False,
                      post_audio: bool = False) -> dict[str, Path]:
    evidence = Path(profile["evidence_dir"])
    if not evidence.is_dir():
        raise JoinedRuntimeError("profile evidence directory is unavailable")
    generated = _replace_markers(work)
    includes = [evidence / "include", ROOT / "src", MELEE / "extern/dolphin/include",
                MELEE / "extern/dolphin/src/dolphin/gx", MELEE / "src", MELEE / "include",
                MELEE / "extern/dolphin/src/dolphin/ax", MELEE / "extern/dolphin/src/dolphin/dsp",
                MELEE / "extern/dolphin/src/dolphin/ar", ROOT / "tests"]
    cxx_flags = ["-std=gnu++17", "-O0", "-g", "-ffp-contract=off", "-DDEBUG=1",
                 "-Wall", "-Wextra", "-Werror", "-Wno-writable-strings",
                 "-Wno-array-parameter", "-Wno-unused-parameter", "-Wno-unused-variable",
                 "-Wno-unused-function", "-Wno-unused-but-set-variable",
                 *(item for include in includes for item in (f"-I{include}",))]
    if lbaudio:
        cxx_flags.append("-DMELEE_WEB_SOURCE_LBAUDIO_STARTUP")
    services_object = work / "services.o"
    command = [sys.executable, str(startup.EMXX), *cxx_flags, "-c", str(generated), "-o", str(services_object)]
    result = _run(command, cwd=ROOT, work=work, label="services", timeout=180)
    if result.returncode:
        raise JoinedRuntimeError(f"services compile failed; see {work / 'services.stderr'}")
    ar_object = work / "ar_service.o"
    command = [sys.executable, str(startup.EMCC), "-c", "-std=gnu11", "-O0", "-g", "-ffp-contract=off",
               "-Wall", "-Wextra", "-Werror", f"-I{ROOT / 'src'}", f"-I{ROOT / 'tests'}",
               str(SOURCE_AR_SERVICE), "-o", str(ar_object)]
    result = _run(command, cwd=ROOT, work=work, label="ar-service", timeout=120)
    if result.returncode:
        raise JoinedRuntimeError(f"AR service compile failed; see {work / 'ar-service.stderr'}")
    trace_object = work / "trace.o"
    trace_flags = startup._compile_cpp_flags(evidence)
    if post_audio:
        trace_flags.append("-DMELEE_WEB_SOURCE_POST_AUDIO_STARTUP")
    command = [sys.executable, str(startup.EMXX), *trace_flags, str(SOURCE_TRACE), "-o", str(trace_object)]
    result = _run(command, cwd=ROOT, work=work, label="trace", timeout=180)
    if result.returncode:
        raise JoinedRuntimeError(f"trace compile failed; see {work / 'trace.stderr'}")
    objects = {"services": services_object, "ar_service": ar_object, "trace": trace_object}
    if lbaudio:
        fx_object = work / "fx_unreachable.o"
        command = [sys.executable, str(startup.EMCC),
                   *startup._compile_flags(evidence, axfx=False),
                   str(ROOT / "src/gameplay_audio_fx.c"), "-o", str(fx_object)]
        result = _run(command, cwd=ROOT, work=work, label="fx-unreachable", timeout=120)
        if result.returncode:
            raise JoinedRuntimeError(f"unreachable FX provider compile failed; see {work / 'fx-unreachable.stderr'}")
        objects["fx_unreachable"] = fx_object
    return objects


def _profile_objects(profile: dict[str, Any]) -> list[Path]:
    rows: list[dict[str, Any]] = []
    rows.extend(profile["objects"].values())
    rows.extend(profile["ax_profile"]["objects"].values())
    # source_synth_joined_trace.cpp includes original_startup_alloc_trace.cpp
    # and renames its main, so linking alloc_trace.o again would define the
    # prefix twice.  Keep the other two prefix objects as direct dependencies.
    rows.extend(profile["prefix_objects"][name] for name in ("osmemory", "initialize"))
    rows.extend([profile["sram_object"], profile["support_object"]])
    result = []
    for row in rows:
        path = Path(row.get("object", row.get("path", "")))
        if not path.is_file() or not isinstance(row.get("sha256"), str) or sha256(path) != row["sha256"]:
            raise JoinedRuntimeError(f"profile object changed before link: {path}")
        result.append(path)
    return result


def build_runtime(profile_path: Path, *, build_dir: Path = BUILD_DEFAULT,
                  artifact_dir: Path | None = None, run: bool = False,
                  inputs: dict[str, Path] | None = None,
                  synthetic_inputs: bool = False,
                  owned_inputs: dict[str, Path] | None = None,
                  jobs: int = 2,
                  lbaudio_profile: Path | None = None,
                  post_audio_profile: Path | None = None) -> dict[str, Any]:
    """Compile/link the joined runtime and optionally run its input-bound trace."""

    profile_path = profile_path.absolute()
    build_dir = build_dir.absolute()
    profile = validate_profile(profile_path)
    (ROOT / "work").mkdir(parents=True, exist_ok=True)
    artifact = artifact_dir or Path(tempfile.mkdtemp(prefix="source-synth-joined-runtime-", dir=ROOT / "work"))
    artifact = artifact.absolute()
    if artifact.is_symlink():
        raise JoinedRuntimeError("artifact directory must not be a symlink")
    if artifact.exists() and any(artifact.iterdir()):
        raise JoinedRuntimeError(f"artifact directory must be new or empty: {artifact}")
    artifact.mkdir(parents=True, exist_ok=True)
    _refresh_hsd_native_runtime(build_dir, artifact, jobs=jobs)
    archive_records = [_file_record(path) for path in _runtime_archives(build_dir)]
    source_before = _source_hashes()
    lbaudio = None
    if lbaudio_profile is not None:
        from tools import source_lbaudio_startup
        lbaudio = source_lbaudio_startup.validate_profile(lbaudio_profile)
    post_audio = None
    if post_audio_profile is not None:
        if lbaudio is None:
            raise JoinedRuntimeError("post-audio startup requires the original complete lbAudio owner")
        from tools import source_post_audio_allocations
        post_audio = source_post_audio_allocations.validate_profile(post_audio_profile)
    objects = _compile_services(profile, artifact, lbaudio=lbaudio is not None,
                                post_audio=post_audio is not None)
    direct_objects = [objects["trace"], objects["services"], objects["ar_service"]]
    profile_objects = _profile_objects(profile)
    if lbaudio is not None:
        direct_objects.extend([Path(lbaudio["object"]["path"]), objects["fx_unreachable"]])
    if post_audio is not None:
        direct_objects.extend(Path(row["object"]) for row in post_audio["units"].values())
    link = [sys.executable, str(startup.EMXX), "-O2", "-g", "-DNDEBUG", "-fexceptions",
            "-sASYNCIFY=1", "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2",
            "-sSAFE_HEAP=1", "-sSTACK_SIZE=8388608", "-sINITIAL_MEMORY=134217728",
            "-sNODERAWFS=1", "--use-port=emdawnwebgpu", "-Wl,--wrap=OSGetTime", *map(str, direct_objects),
            *map(str, profile_objects), *map(str, _runtime_archives(build_dir)), "-lm",
            "-o", str(artifact / "joined.js")]
    result = _run(link, cwd=build_dir, work=artifact, label="link", timeout=300)
    if result.returncode:
        raise JoinedRuntimeError(f"joined runtime link failed; see {artifact / 'link.stderr'}")
    if post_audio is not None and source_post_audio_allocations.validate_profile(post_audio_profile) != post_audio:
        raise JoinedRuntimeError("post-audio profile changed during link")
    if lbaudio is not None and source_lbaudio_startup.validate_profile(lbaudio_profile) != lbaudio:
        raise JoinedRuntimeError("lbAudio profile changed during link")
    if [_file_record(path) for path in _runtime_archives(build_dir)] != archive_records:
        raise JoinedRuntimeError("runtime archives changed during link")
    source_after = _source_hashes()
    if source_after != source_before:
        raise JoinedRuntimeError("pinned runtime source changed during build")
    receipt: dict[str, Any] = {
        "kind": "source_synth_joined_runtime_build",
        "profile": {"path": _relative(profile_path), "sha256": sha256(profile_path)},
        "build": {"path": _relative(build_dir), "archives": archive_records},
        "source_hashes_before": source_before,
        "source_hashes_after": source_after,
        "generated_services_sha256": sha256(artifact / "services_generated.cpp"),
        "objects": {name: _file_record(path) for name, path in {**objects}.items()},
        "runtime": {"path": _relative(artifact / "joined.js"), "js_sha256": sha256(artifact / "joined.js"),
                    "wasm_sha256": sha256(artifact / "joined.wasm"), "runtime_claim": False},
        "negative_results": {"expected_cases": validate_negative_results()},
    }
    if lbaudio is not None:
        receipt["lbaudio_profile"] = _file_record(lbaudio_profile)
        receipt["scope"] = "original lbAudioAx initialization and first deferred completion"
        receipt["driver_argument_role"] = "Expected values only; the raw source routine owns the actual driver call"
    if post_audio is not None:
        receipt["post_audio_profile"] = _file_record(post_audio_profile)
        receipt["scope"] = "original lbAudioAx, deferred completion, lbMemory and lbHeap descriptor startup"
    if run:
        if synthetic_inputs:
            if not inputs:
                raise JoinedRuntimeError("synthetic runtime execution requires explicit receipt inputs")
            input_plan = derive_runtime_inputs(**inputs, synthetic=True)
        else:
            if not owned_inputs:
                raise JoinedRuntimeError(
                    "owned runtime execution requires disc, DOL, symbols, source, and SRAM envelope"
                )
            input_plan = derive_owned_runtime_inputs(work_dir=artifact, **owned_inputs)
        node = _configured_node()
        command = [str(node), str(artifact / "joined.js"), *input_plan["arguments"]]
        run_result = _run(command, cwd=artifact, work=artifact, label="run", timeout=180)
        if run_result.returncode:
            raise JoinedRuntimeError(f"joined runtime valid run failed; see {artifact / 'run.stderr'}")
        # Keep absolute paths out of the retained receipt while preserving the
        # exact argv used for the run in run.command.json.
        receipt_plan = dict(input_plan)
        receipt_plan["arguments"] = [
            _relative(Path(value)) if index == 5 else value
            for index, value in enumerate(input_plan["arguments"])
        ]
        receipt["input_plan"] = receipt_plan
        receipt["run"] = {"status": "pass", "stdout_sha256": sha256(artifact / "run.stdout"),
                           "stderr_sha256": sha256(artifact / "run.stderr")}
        diagnostics = dict(EXPECTED_NEGATIVE_DIAGNOSTICS)
        if lbaudio is not None:
            diagnostics.update(LBAUDIO_NEGATIVE_DIAGNOSTICS)
        receipt["negative_results"]["expected_cases"] = [
            {"mode": mode, "diagnostic_contains": diagnostic}
            for mode, diagnostic in diagnostics.items()]
        receipt["negative_results"]["runs"] = run_negative_matrix(
            node, artifact / "joined.js", input_plan, artifact, diagnostics
        )
    (artifact / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    return receipt


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--lbaudio-profile", type=Path)
    parser.add_argument("--post-audio-profile", type=Path)
    parser.add_argument("--build-dir", type=Path, default=BUILD_DEFAULT)
    parser.add_argument("--artifact-dir", type=Path)
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--synthetic-inputs", action="store_true",
                        help="use explicit JSON receipts for a bounded synthetic test")
    parser.add_argument("--owned-dol", type=Path)
    parser.add_argument("--owned-disc", type=Path)
    parser.add_argument("--owned-symbols", type=Path)
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--owned-sram-envelope", type=Path)
    args = parser.parse_args()
    owned = None
    if args.run and not args.synthetic_inputs:
        required = (args.owned_dol, args.owned_disc, args.owned_symbols,
                    args.source_root, args.owned_sram_envelope)
        if any(value is None for value in required):
            parser.error("owned execution requires --owned-dol, --owned-disc, --owned-symbols, --source-root, and --owned-sram-envelope")
        owned = {"dol_path": args.owned_dol, "disc_path": args.owned_disc,
                 "symbols_path": args.owned_symbols, "source_root": args.source_root,
                 "sram_envelope_path": args.owned_sram_envelope}
    print(json.dumps(build_runtime(args.profile, build_dir=args.build_dir,
                                   artifact_dir=args.artifact_dir, run=args.run,
                                   synthetic_inputs=args.synthetic_inputs,
                                   owned_inputs=owned, jobs=args.jobs,
                                   lbaudio_profile=args.lbaudio_profile,
                                   post_audio_profile=args.post_audio_profile), indent=2))
