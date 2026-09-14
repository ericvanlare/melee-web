#!/usr/bin/env python3
"""Generate a finite C++ pipeline-preparation union from certified sidecars.

The input is an explicit JSON list of ``{"requirements": ..., "coverage":
...}`` file pairs.  Every requirements sidecar and coverage manifest is
validated before descriptors are joined.  The generated metadata and header
contain only finite descriptor identities, configuration sizes/versions and
binding hashes; they do not contain input paths, route inventories, capture
IDs, provenance records, descriptor bytes or an exhaustive coverage claim.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Any, Iterable, Mapping, Sequence


SCHEMA = "melee-web-pipeline-preparation-v1"
VERSION = 1
REQUIREMENTS_SCHEMA = "melee-web-pipeline-requirements-v1"
COVERAGE_SCHEMA = "melee-web-pipeline-coverage-v1"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
REF_RE = re.compile(r"^[0-9a-f]{16}$")
IDENTIFIER_RE = re.compile(r"^[^/\\\x00]{1,256}$")
UINT32_MAX = (1 << 32) - 1
MAX_DESCRIPTORS = 1024
MAX_CONFIG_BYTES = 16 * 1024 * 1024

SCENE_BOOT = 1
SCENE_CSS = 2
SCENE_SSS = 3
SCENE_MATCH = 4
SCENE_TEARDOWN = 5
SCENE_RETURN = 6
CLEAR_DESCRIPTOR_TYPE = 0  # Aurora gfx::ShaderType::Clear


class PipelinePreparationError(ValueError):
    """Input evidence is not safe to turn into a preparation union."""

    def __init__(self, code: str, message: str):
        self.code = code
        self.message = message
        super().__init__(f"{code}: {message}")


Descriptor = tuple[int, str, int, int, str]


def canonical_json(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode("utf-8")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_json(value: Any) -> str:
    return sha256_bytes(canonical_json(value))


def _require(condition: bool, code: str, message: str) -> None:
    if not condition:
        raise PipelinePreparationError(code, message)


def _safe_identifier(value: Any, label: str) -> str:
    _require(isinstance(value, str) and IDENTIFIER_RE.fullmatch(value) is not None,
             "identifier", f"{label} is not a bounded path-free identifier")
    _require(all(ord(char) >= 0x20 and char != '"' for char in value), "identifier",
             f"{label} contains a control character or C++ quote")
    return value


def _sha256(value: Any, label: str) -> str:
    _require(isinstance(value, str) and SHA256_RE.fullmatch(value) is not None,
             "hash", f"{label} is not a lowercase SHA-256 digest")
    return value


def _bounded_number(value: Any, label: str, *, maximum: int = UINT32_MAX) -> int:
    _require(isinstance(value, int) and not isinstance(value, bool) and 0 <= value <= maximum,
             "coverage_field", f"{label} must be a bounded non-negative integer")
    return value


def _case_inventory(sidecar_case: Mapping[str, Any], case_id: str) -> dict[str, Any]:
    """Validate and normalize the finite fields copied to preparation output."""

    def sequence(name: str) -> list[Any]:
        _require(name in sidecar_case, "coverage_field", f"case {case_id} lacks {name}")
        value = sidecar_case[name]
        _require(isinstance(value, list), "coverage_field", f"case {case_id} {name} must be a list")
        return value

    expected_phases = [_bounded_number(value, f"case {case_id} expected phase")
                       for value in sequence("expected_phases")]
    expected_actions: list[int | str] = []
    for value in sequence("expected_actions"):
        if isinstance(value, int) and not isinstance(value, bool):
            expected_actions.append(_bounded_number(value, f"case {case_id} expected action"))
        else:
            expected_actions.append(_safe_identifier(value, f"case {case_id} expected action"))
    expected_contexts: list[dict[str, int]] = []
    for index, context in enumerate(sequence("expected_contexts")):
        _require(isinstance(context, Mapping), "coverage_field",
                 f"case {case_id} expected context {index} is not an object")
        expected_contexts.append({
            "scene": _bounded_number(context.get("scene"), f"case {case_id} context scene"),
            "phase": _bounded_number(context.get("phase"), f"case {case_id} context phase"),
        })
    expected_costumes = [_bounded_number(value, f"case {case_id} expected costume")
                        for value in sequence("expected_costumes")]
    lifecycle = [_safe_identifier(value, f"case {case_id} lifecycle entry")
                 for value in sequence("lifecycle")]
    expected_source_ticks = [_bounded_number(value, f"case {case_id} source tick", maximum=(1 << 63) - 1)
                             for value in sequence("expected_source_ticks")]
    return {
        "expected_phases": expected_phases,
        "expected_contexts": expected_contexts,
        "expected_actions": expected_actions,
        "expected_costumes": expected_costumes,
        "lifecycle": lifecycle,
        "expected_source_ticks": expected_source_ticks,
    }


def _read_json(path: Path, label: str) -> tuple[Any, str]:
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise PipelinePreparationError("read", f"cannot read {label} input") from exc
    try:
        value = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise PipelinePreparationError("json", f"{label} input is not UTF-8 JSON") from exc
    return value, sha256_bytes(data)


def _descriptor(value: Any, label: str) -> Descriptor:
    _require(isinstance(value, Mapping), "descriptor_shape", f"{label} descriptor must be an object")
    kind = value.get("type")
    ref_hex = value.get("ref_hex")
    version = value.get("config_version")
    size = value.get("size")
    digest = value.get("sha256")
    _require(isinstance(kind, int) and not isinstance(kind, bool) and kind >= 0,
             "descriptor_type", f"{label} descriptor type is invalid")
    _require(kind <= 2, "descriptor_type", f"{label} descriptor type is outside Aurora's ABI")
    _require(isinstance(ref_hex, str) and REF_RE.fullmatch(ref_hex) is not None,
             "descriptor_ref", f"{label} descriptor ref_hex is invalid")
    _require(isinstance(version, int) and not isinstance(version, bool) and version >= 0,
             "descriptor_config", f"{label} descriptor config_version is invalid")
    _require(version <= UINT32_MAX, "descriptor_config", f"{label} descriptor config_version is too large")
    _require(isinstance(size, int) and not isinstance(size, bool) and size > 0,
             "descriptor_config", f"{label} descriptor size is invalid")
    _require(size <= MAX_CONFIG_BYTES, "descriptor_config", f"{label} descriptor size is too large")
    _sha256(digest, f"{label} descriptor sha256")
    return (kind, ref_hex, version, size, digest)


def _descriptor_map(requirements: Mapping[str, Any]) -> dict[tuple[int, str], Descriptor]:
    catalog = requirements.get("descriptors")
    _require(isinstance(catalog, list), "descriptor_catalog", "requirements descriptors must be a list")
    result: dict[tuple[int, str], Descriptor] = {}
    for index, value in enumerate(catalog):
        descriptor = _descriptor(value, f"requirements descriptor {index}")
        key = descriptor[:2]
        prior = result.get(key)
        _require(prior is None or prior == descriptor, "descriptor_conflict",
                 f"descriptor {key} has conflicting metadata in the dictionary")
        result[key] = descriptor
    return result


def _validate_route(route: Any, label: str) -> None:
    _require(isinstance(route, Mapping), "route_shape", f"{label} route must be an object")
    fighters = route.get("fighter_numeric_ids")
    costumes = route.get("costume_numeric_ids")
    stage = route.get("stage_numeric_id")
    ground = route.get("ground_numeric_id")
    _require(isinstance(fighters, list) and bool(fighters), "route_shape",
             f"{label} fighter_numeric_ids must be a non-empty list")
    _require(isinstance(costumes, list) and bool(costumes), "route_shape",
             f"{label} costume_numeric_ids must be a non-empty list")
    _require(len(fighters) == len(costumes), "route_shape",
             f"{label} fighter and costume lists must have equal lengths")
    for field, values in (("fighter_numeric_ids", fighters), ("costume_numeric_ids", costumes)):
        for value in values:
            _require(isinstance(value, int) and not isinstance(value, bool) and value >= 0,
                     "route_shape", f"{label} {field} contains an invalid numeric ID")
            _require(value <= UINT32_MAX, "route_shape", f"{label} {field} exceeds the header integer range")
    for field, value in (("stage_numeric_id", stage), ("ground_numeric_id", ground)):
        _require(isinstance(value, int) and not isinstance(value, bool) and value >= 0,
                 "route_shape", f"{label} {field} must be a non-negative integer")
        _require(value <= UINT32_MAX, "route_shape", f"{label} {field} exceeds the header integer range")


def _binding(requirements: Mapping[str, Any], coverage_digest: str) -> dict[str, Any]:
    _require(requirements.get("schema") == REQUIREMENTS_SCHEMA, "requirements_schema",
             "requirements schema is unsupported")
    _require(requirements.get("version") == VERSION, "requirements_version",
             "requirements version is unsupported")
    status = requirements.get("status")
    _require(isinstance(status, Mapping) and status.get("kind") == "certificate" and
             status.get("valid") is True and
             status.get("certified") is True and status.get("errors") == [] and
             status.get("incomplete_scopes") == [], "certificate_status",
             "requirements sidecar is not a complete valid certificate")
    binding = requirements.get("binding")
    _require(isinstance(binding, Mapping), "binding", "requirements binding is missing")
    required = ("seed_decoded_sha256", "source_head", "dirty_overlay_sha256",
                "dependencies", "renderer", "registry_sha256", "coverage_manifest_sha256")
    for key in required:
        _require(key in binding, "binding", f"requirements binding lacks {key}")
    _sha256(binding["seed_decoded_sha256"], "seed_decoded_sha256")
    _sha256(binding["dirty_overlay_sha256"], "dirty_overlay_sha256")
    _require(isinstance(binding["source_head"], str) and
             re.fullmatch(r"[0-9a-fA-F]{40,64}", binding["source_head"]) is not None,
             "binding", "source_head is not a revision")
    _sha256(binding["registry_sha256"], "registry_sha256")
    _require(isinstance(binding["dependencies"], Mapping) and bool(binding["dependencies"]),
             "binding", "dependency revisions are missing")
    for name, revision in binding["dependencies"].items():
        _safe_identifier(name, "dependency name")
        _require(isinstance(revision, str) and re.fullmatch(r"[0-9a-fA-F]{40,64}", revision) is not None,
                 "binding", f"dependency {name} revision is invalid")
    renderer = binding["renderer"]
    _require(isinstance(renderer, Mapping) and "version" in renderer and "config_layout" in renderer,
             "binding", "renderer binding is incomplete")
    for key in ("version", "config_layout"):
        value = renderer[key]
        _require((isinstance(value, int) and not isinstance(value, bool) and 0 <= value <= UINT32_MAX) or
                 (isinstance(value, str) and IDENTIFIER_RE.fullmatch(value) is not None),
                 "binding", f"renderer {key} is invalid")
    if "config_layout_sha256" in renderer:
        _sha256(renderer["config_layout_sha256"], "renderer config layout digest")
    _require(binding["coverage_manifest_sha256"] == coverage_digest, "coverage_binding",
             "coverage raw SHA-256 differs from requirements binding")
    normalized = {
        "seed_decoded_sha256": binding["seed_decoded_sha256"],
        "source_head": binding["source_head"],
        "dirty_overlay_sha256": binding["dirty_overlay_sha256"],
        "dependencies": {str(name): revision for name, revision in binding["dependencies"].items()},
        "renderer": {
            "version": renderer["version"],
            "config_layout": renderer["config_layout"],
        },
        "registry_sha256": binding["registry_sha256"],
        "coverage_manifest_sha256": binding["coverage_manifest_sha256"],
    }
    if "config_layout_sha256" in renderer:
        normalized["renderer"]["config_layout_sha256"] = renderer["config_layout_sha256"]
    return normalized


def _validate_coverage(requirements: Mapping[str, Any], coverage: Mapping[str, Any],
                       coverage_digest: str) -> dict[str, str]:
    _require(isinstance(coverage, Mapping) and
             coverage.get("schema") == COVERAGE_SCHEMA and coverage.get("version") == VERSION,
             "coverage_schema", "coverage schema or version is unsupported")
    requirements_coverage = requirements.get("coverage")
    _require(isinstance(requirements_coverage, Mapping), "coverage", "requirements coverage is missing")
    _require(requirements_coverage.get("manifest_sha256") == coverage_digest, "coverage_binding",
             "coverage raw SHA-256 differs from requirements manifest SHA")
    raw_cases = coverage.get("cases")
    sidecar_cases = requirements_coverage.get("cases")
    _require(isinstance(raw_cases, list) and bool(raw_cases) and isinstance(sidecar_cases, list),
             "coverage_cases", "coverage cases are missing")
    raw_by_id: dict[str, Mapping[str, Any]] = {}
    for index, raw_case in enumerate(raw_cases):
        _require(isinstance(raw_case, Mapping), "coverage_case", f"coverage case {index} is not an object")
        case_id = _safe_identifier(raw_case.get("case_id"), "coverage case_id")
        _require(case_id not in raw_by_id, "coverage_case", f"coverage case {case_id} is duplicated")
        raw_by_id[case_id] = raw_case
    seen_case_ids: set[str] = set()
    for index, sidecar_case in enumerate(sidecar_cases):
        _require(isinstance(sidecar_case, Mapping), "coverage_case", f"requirements case {index} is not an object")
        case_id = _safe_identifier(sidecar_case.get("case_id"), "requirements case_id")
        raw_case = raw_by_id.get(case_id)
        _require(raw_case is not None, "coverage_case", f"requirements case {case_id} is absent from coverage")
        route = raw_case.get("route")
        _validate_route(route, f"coverage case {case_id}")
        _require(sidecar_case.get("route_id") == raw_case.get("route_id"), "route_binding",
                 f"case {case_id} route_id differs between certificate and coverage")
        _require(sidecar_case.get("route_identity_sha256") == sha256_json(route), "route_binding",
                 f"case {case_id} route identity hash differs from coverage route")
        input_binding = raw_case.get("input")
        _require(isinstance(input_binding, Mapping), "coverage_input", f"case {case_id} input binding is missing")
        input_sha = _sha256(input_binding.get("sha256"), f"case {case_id} input SHA")
        capture_id = _safe_identifier(input_binding.get("capture_id"), f"case {case_id} capture_id")
        _require(sidecar_case.get("input_sha256") == input_sha and
                 sidecar_case.get("capture_id") == capture_id, "input_binding",
                 f"case {case_id} input binding differs between certificate and coverage")
        coverage_case_id = sidecar_case.get("coverage_case_id")
        if coverage_case_id is not None:
            _require(isinstance(coverage_case_id, int) and not isinstance(coverage_case_id, bool) and
                     0 <= coverage_case_id <= UINT32_MAX, "coverage_case",
                     f"case {case_id} coverage_case_id is invalid")
        capture = requirements.get("capture")
        _require(isinstance(capture, Mapping), "capture", "requirements capture identity is missing")
        _sha256(capture.get("capture_sha256"), "capture SHA")
        _require(case_id not in seen_case_ids, "coverage_case", f"case {case_id} is duplicated")
        seen_case_ids.add(case_id)
        _safe_identifier(sidecar_case.get("route_id"), f"case {case_id} route_id")
        # Validate the finite certificate-side fields even though they are
        # deliberately omitted from the compact shipped artifact.
        _case_inventory(sidecar_case, case_id)
    _require(set(raw_by_id) == seen_case_ids,
             "coverage_case", "certificate and coverage case inventories differ")
    return {
        _safe_identifier(case.get("case_id"), "coverage case_id"):
        _safe_identifier(case.get("route_id"), "coverage route_id")
        for case in sidecar_cases
    }


def _join_descriptors(target: dict[tuple[int, str], Descriptor], descriptors: Iterable[Descriptor],
                      label: str) -> None:
    for descriptor in descriptors:
        key = descriptor[:2]
        prior = target.get(key)
        _require(prior is None or prior == descriptor, "descriptor_conflict",
                 f"{label} descriptor {key} has conflicting metadata")
        target[key] = descriptor


def _validate_group_member(member: Any, catalog: Mapping[tuple[int, str], Descriptor], label: str) -> Descriptor:
    descriptor = _descriptor(member, label)
    _require(isinstance(member.get("provenance"), list) and bool(member["provenance"]),
             "provenance", f"{label} descriptor has no provenance membership")
    _require(catalog.get(descriptor[:2]) == descriptor, "descriptor_catalog",
             f"{label} descriptor differs from its requirements dictionary entry")
    return descriptor


def _process_groups(requirements: Mapping[str, Any], route_ids: set[str],
                    union: dict[tuple[int, str], Descriptor]) -> None:
    catalog = _descriptor_map(requirements)
    groups = requirements.get("groups")
    _require(isinstance(groups, list), "groups", "requirements groups must be a list")
    for group_index, group in enumerate(groups):
        _require(isinstance(group, Mapping), "group_shape", f"requirements group {group_index} is not an object")
        members = group.get("members")
        _require(isinstance(members, list), "group_shape", f"requirements group {group_index} members are not a list")
        if not members:
            continue
        route_id = _safe_identifier(group.get("route_id"), f"group {group_index} route_id")
        _require(route_id in route_ids, "unknown_route", f"group {group_index} route is absent from coverage cases")
        scene = group.get("scene")
        _require(isinstance(scene, int) and not isinstance(scene, bool) and
                 scene in {SCENE_BOOT, SCENE_CSS, SCENE_SSS, SCENE_MATCH, SCENE_TEARDOWN, SCENE_RETURN},
                 "group_scene", f"requirements group {group_index} has no recognized numeric scene")
        for member_index, member in enumerate(members):
            descriptor = _validate_group_member(member, catalog,
                                                f"group {group_index} member {member_index}")
            # Every member of every nonempty certified demand group is part of
            # the conservative union. Empty groups and unused dictionary rows
            # remain excluded, while per-draw provenance stays private.
            _join_descriptors(union, (descriptor,), f"group {group_index}")


def _descriptor_list(values: Mapping[tuple[int, str], Descriptor]) -> list[dict[str, Any]]:
    return [{"type": descriptor[0], "ref_hex": descriptor[1],
             "config_version": descriptor[2], "size": descriptor[3], "sha256": descriptor[4]}
            for descriptor in sorted(values.values(), key=lambda item: (item[0], item[1]))]


def _metadata(inputs: Sequence[Mapping[str, Any]], binding: Mapping[str, Any],
              union: Mapping[tuple[int, str], Descriptor]) -> dict[str, Any]:
    # Only certificate and coverage digests survive into the output.  Case IDs,
    # capture IDs and raw per-draw provenance remain evidence-side data.
    output_inputs = [{
        "certificate_sha256": item["certificate_sha256"],
        "coverage_sha256": item["coverage_sha256"],
    } for item in sorted(inputs, key=lambda value: (value["certificate_sha256"], value["coverage_sha256"]))]
    public_binding = {key: binding[key] for key in (
        "seed_decoded_sha256", "source_head", "dirty_overlay_sha256", "dependencies",
        "renderer", "registry_sha256")}
    descriptors = _descriptor_list(union)
    return {
        "schema": SCHEMA,
        "version": VERSION,
        "finite": True,
        "exhaustive": False,
        "kind": "conservative_certified_union",
        "bindings": public_binding,
        "inputs": output_inputs,
        "descriptor_count": len(descriptors),
        "config_bytes": sum(descriptor["size"] for descriptor in descriptors),
        "descriptor_union": descriptors,
    }


def _cpp_u32(value: int) -> str:
    return f"UINT32_C({value})"


def _cpp_u64(ref_hex: str) -> str:
    return f"UINT64_C(0x{ref_hex})"


def _cpp_descriptor_array(name: str, values: Sequence[Mapping[str, Any]]) -> list[str]:
    lines = [f"static const MeleeWebPipelinePreparationDescriptor {name}[] = {{"]
    for descriptor in values:
        lines.append("    {%s, %s, %s, %s, \"%s\"}," % (
            _cpp_u32(descriptor["type"]), _cpp_u64(descriptor["ref_hex"]),
            _cpp_u32(descriptor["config_version"]), _cpp_u32(descriptor["size"]), descriptor["sha256"]))
    lines.append("};")
    lines.append(f"static const size_t {name}Count = sizeof({name}) / sizeof({name}[0]);")
    return lines


def generate_header(metadata: Mapping[str, Any]) -> str:
    """Return a deterministic C++ header for the already validated metadata."""

    descriptors = metadata["descriptor_union"]
    metadata_digest = sha256_json(metadata)
    bindings = metadata.get("bindings")
    _require(isinstance(bindings, Mapping), "binding", "compact metadata bindings are missing")
    seed_digest = _sha256(bindings.get("seed_decoded_sha256"), "seed_decoded_sha256")
    lines = [
        "#ifndef MELEE_WEB_PIPELINE_PREPARATION_GENERATED_H",
        "#define MELEE_WEB_PIPELINE_PREPARATION_GENERATED_H",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        f"#define MELEE_WEB_PIPELINE_PREPARATION_SCHEMA \"{SCHEMA}\"",
        f"#define MELEE_WEB_PIPELINE_PREPARATION_VERSION {_cpp_u32(VERSION)}",
        f"#define MELEE_WEB_PIPELINE_PREPARATION_BINDING_SHA256 \"{metadata_digest}\"",
        f"#define MELEE_WEB_PIPELINE_PREPARATION_SEED_DECODED_SHA256 \"{seed_digest}\"",
        "/* One finite conservative certified union; this header is not exhaustive. */",
        "",
        "typedef struct MeleeWebPipelinePreparationDescriptor {",
        "    uint32_t type;",
        "    uint64_t pipeline_ref;",
        "    uint32_t config_version;",
        "    uint32_t size;",
        "    const char* sha256;",
        "} MeleeWebPipelinePreparationDescriptor;",
        "",
    ]
    lines.extend([
        "/* Each entry contains only a typed identity, config bounds and SHA-256. */",
    ])
    lines.extend(_cpp_descriptor_array("melee_web_pipeline_preparation_union", descriptors))
    lines.extend([
        "#define melee_web_pipeline_preparation_union_count \\",
        "    (sizeof(melee_web_pipeline_preparation_union) / sizeof(melee_web_pipeline_preparation_union[0]))",
        "",
        "#endif",
        "",
    ])
    return "\n".join(lines)


def generate_preparation(inputs: Sequence[Mapping[str, Any]]) -> tuple[dict[str, Any], str]:
    """Validate input certificate pairs and return metadata plus C++ header."""

    _require(isinstance(inputs, Sequence) and not isinstance(inputs, (str, bytes)) and bool(inputs),
             "inputs", "preparation input list must be non-empty")
    common_binding: dict[str, Any] | None = None
    seen_certificates: set[str] = set()
    prepared_inputs: list[dict[str, Any]] = []
    seen_case_ids: set[str] = set()
    union: dict[tuple[int, str], Descriptor] = {}

    for input_index, item in enumerate(inputs):
        _require(isinstance(item, Mapping), "inputs", f"input {input_index} is not an object")
        _require(set(item) == {"requirements", "coverage"}, "inputs",
                 f"input {input_index} must contain only requirements and coverage")
        requirements_path = item.get("requirements")
        coverage_path = item.get("coverage")
        _require(isinstance(requirements_path, (str, Path)) and isinstance(coverage_path, (str, Path)),
                 "inputs", f"input {input_index} requires requirements and coverage paths")
        requirements, certificate_digest = _read_json(Path(requirements_path), "requirements")
        coverage, coverage_digest = _read_json(Path(coverage_path), "coverage")
        _require(isinstance(requirements, Mapping), "requirements_shape",
                 f"input {input_index} requirements is not an object")
        _require(isinstance(coverage, Mapping), "coverage_shape",
                 f"input {input_index} coverage is not an object")
        _require(certificate_digest not in seen_certificates, "inputs",
                 f"input {input_index} duplicates a requirements certificate")
        seen_certificates.add(certificate_digest)
        binding = _binding(requirements, coverage_digest)
        case_routes = _validate_coverage(requirements, coverage, coverage_digest)
        if common_binding is None:
            common_binding = {key: binding[key] for key in (
                "seed_decoded_sha256", "source_head", "dirty_overlay_sha256", "dependencies",
                "renderer", "registry_sha256")}
        else:
            current = {key: binding[key] for key in common_binding}
            _require(current == common_binding, "binding_conflict",
                     f"input {input_index} does not match the common source/renderer binding")
        for case_id in case_routes:
            _require(case_id not in seen_case_ids, "coverage_case",
                     f"case {case_id} is duplicated across certificates")
            seen_case_ids.add(case_id)
        _process_groups(requirements, set(case_routes.values()), union)
        prepared_inputs.append({"certificate_sha256": certificate_digest,
                                "coverage_sha256": coverage_digest})

    _require(common_binding is not None, "binding", "no common binding was established")
    _require(bool(union), "missing_union", "no certified preparation descriptors were observed")
    _require(any(descriptor[0] == CLEAR_DESCRIPTOR_TYPE for descriptor in union.values()),
             "bootstrap_missing", "no explicitly demanded Clear descriptor has provenance membership")
    _require(len(union) <= MAX_DESCRIPTORS and
             sum(row[3] for row in union.values()) <= MAX_CONFIG_BYTES,
             "descriptor_limit", "preparation registry exceeds Aurora's bounds")
    # GPU objects are keyed globally by PipelineRef even though certificates
    # carry typed identities. Reject aliases before emitting a header.
    _require(len({row[1] for row in union.values()}) == len(union),
             "descriptor_conflict", "typed descriptors alias one global pipeline reference")
    metadata = _metadata(prepared_inputs, common_binding, union)
    header = generate_header(metadata)
    return json.loads(canonical_json(metadata).decode("utf-8")), header


def _load_input_list(path: Path) -> list[dict[str, Any]]:
    value, _digest = _read_json(path, "preparation input list")
    if isinstance(value, list):
        inputs = value
    elif isinstance(value, Mapping) and "inputs" in value:
        _require(value.get("schema") == SCHEMA and value.get("version") == VERSION,
                 "inputs", "preparation input-list schema or version is unsupported")
        inputs = value.get("inputs")
    elif isinstance(value, Mapping):
        inputs = None
    else:
        inputs = None
    _require(isinstance(inputs, list), "inputs", "preparation input list must contain inputs")
    return inputs


def write_outputs(inputs: Sequence[Mapping[str, Any]], metadata_path: Path,
                  header_path: Path) -> None:
    metadata, header = generate_preparation(inputs)
    metadata_path.parent.mkdir(parents=True, exist_ok=True)
    header_path.parent.mkdir(parents=True, exist_ok=True)
    metadata_path.write_bytes(canonical_json(metadata) + b"\n")
    header_path.write_text(header, encoding="utf-8")


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--metadata-output", type=Path, required=True)
    parser.add_argument("--header-output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        write_outputs(_load_input_list(args.inputs), args.metadata_output, args.header_output)
    except PipelinePreparationError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
