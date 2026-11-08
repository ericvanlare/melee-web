#!/usr/bin/env python3
"""Project an exact reviewed staging link into the page's compact statics table."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

from source_state_manifest import ManifestError, _parse_map, _validate_match_history_cmake_profile
from stage_public import StageError, _load_json

PROFILE = "runtime-rollback-staging"
TABLE_NAME = "linked-statics.json"
POLICIES = {
    "ORIGINAL_SOURCE_CPU_VALUE_BUNDLE_RESTORE": (1, 1),
    "INCLUDED_ORIGINAL_MPLIB_CPU_VALUE_BUNDLE_RESTORE": (1, 2),
    "AUTHORED_COLLISION_TABLE_EQUALITY_GUARD": (2, 2),
    "SOURCE_SELECTED_HEAP_EQUALITY_GUARD_NO_REWIND": (2, 1),
    "PATCHED_SCOPE_OR_BASELINE_EQUALITY_GUARD_NO_REWIND": (2, 1),
    "PROJECT_COLLISION_OWNER_IDENTITY_EQUALITY_GUARD": (2, 2),
    "TYPED_CLOCK_WRITER_EXCLUDE_FROM_BYTE_BUNDLE": None,
    "TYPED_CADENCE_WRITER_EXCLUDE_FROM_BYTE_BUNDLE": None,
}
NEW_SOURCE_POLICY = "PATCHED_SCOPE_OR_BASELINE_EQUALITY_GUARD_NO_REWIND"
ROW_KEY = ("producer_target", "source", "input_object", "input_section")
ROW_FACTS = ("size", "decision", "exclusion_reason")


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def index_rows(rows, name, *, excluded_without_source=False):
    require(isinstance(rows, list), f"{name} rows must be a list")
    indexed = {}
    for row in rows:
        require(isinstance(row, dict), f"Invalid {name} row")
        key = tuple(row.get(field) for field in ROW_KEY)
        require(all(isinstance(value, str) and value for value in key[2:]) and
                all((isinstance(value, str) and value) or
                    (value is None and excluded_without_source and row.get("decision") == "excluded")
                    for value in key[:2]), f"Invalid {name} row identity")
        require(key not in indexed, f"Duplicate {name} row identity: {key}")
        indexed[key] = row
    return indexed


def compact_rows(manifest_rows, reviewed_rows, map_rows):
    current = index_rows(manifest_rows, "manifest", excluded_without_source=True)
    reviewed = index_rows(reviewed_rows, "reviewed")
    require(reviewed, "Reviewed policy has no rows")
    actual_ranges = {(row["object"], row["section"], row["address"], row["size"])
                     for row in map_rows}
    manifest_ranges = {(row["input_object"], row["input_section"], row["address"],
                        row["size"]) for row in current.values()}
    require(len(actual_ranges) == len(map_rows) == len(manifest_ranges) == len(current) and
            actual_ranges == manifest_ranges, "Manifest does not exactly join the retained mutable map")
    classified = {}
    for key, old in reviewed.items():
        require(key in current, f"Reviewed source row missing from staging link: {key}")
        row = current[key]
        require(all(field in old and field in row and old[field] == row[field] for field in ROW_FACTS),
                f"Reviewed source row facts changed: {key}")
        policy = old.get("aggregate_policy")
        require(isinstance(policy, str) and policy in POLICIES, f"Unknown reviewed source policy: {key}")
        classified[key] = policy
    for key, row in current.items():
        if key not in classified and row["decision"] == "source-c-static-candidate":
            classified[key] = NEW_SOURCE_POLICY
    rows = []
    for key, policy in classified.items():
        mapping = POLICIES[policy]
        if mapping is not None:
            row = current[key]
            rows.append([row["address"], row["size"], *mapping])
    rows.sort()
    end = 0
    for address, size, *_ in rows:
        require(type(address) is int and type(size) is int and
                0 < address <= 0xFFFFFFFF and 0 < size <= 0xFFFFFFFF and
                address >= end and address + size <= 0x100000000, "Invalid compact statics range")
        end = address + size
    require(rows, "Empty compact statics table")
    return rows


def pinned_json(path, expected, label):
    value, raw = _load_json(path, label)
    require(re.fullmatch(r"[0-9a-f]{64}", expected) and sha256(raw) == expected,
            f"{label} digest does not match the reviewed pin")
    return value


def prepare(root, build, policy_path, output, *, source_sha, manifest_sha256, policy_sha256):
    def git(*args):
        return subprocess.check_output(["git", *args], cwd=root, text=True).strip()

    def check_source():
        require(re.fullmatch(r"[0-9a-f]{40}", source_sha) and git("rev-parse", "HEAD") == source_sha and
                not git("status", "--porcelain"), "Staging producer requires the exact clean source revision")

    check_source()
    manifest_path = build / "gameplay_menu_browser_match_history.source-state.json"
    manifest = pinned_json(manifest_path, manifest_sha256, "linked source-state manifest")
    policy = pinned_json(policy_path, policy_sha256, "reviewed linked policy")
    require(policy.get("schema") == "aggregate-source-static-review-contract-v2" and
            policy.get("revision") == 61, "Expected the reviewed v61 source policy")
    identity = manifest["configuration_identity"]
    require(manifest.get("schema") == "melee-web-source-state-manifest-v1" and
            manifest.get("target") == "gameplay_menu_browser" and
            manifest.get("complete_game_state") is False and
            identity.get("execution_profile") == PROFILE and
            identity.get("cmake_build_type") == "Release" and
            identity.get("source_profile") == "slippi-render-dependencies-overlay" and
            sha256(canonical(identity)) == manifest.get("configuration_identity_sha256"),
            "Expected the exact staging Release source-state identity")
    pins = {
        "wasm_sha256": "gameplay_menu_browser.wasm",
        "link_map_sha256": "gameplay_menu_browser_match_history.map",
        "compile_commands_sha256": "compile_commands.json",
        "cmake_cache_sha256": "CMakeCache.txt",
        "build_graph_sha256": "build.ninja",
        "producer_sources_sha256": "gameplay_menu_browser_match_history_producers.tsv",
    }
    for field, name in pins.items():
        require(sha256((build / name).read_bytes()) == identity.get(field), f"Linked producer pin mismatch: {name}")
    _validate_match_history_cmake_profile(build / pins["cmake_cache_sha256"], rollback_staging=True)
    for name, digest in {
        "dependencies.lock.json": identity["dependencies_lock_sha256"],
        "scripts/source_state_manifest.py": identity["manifest_script_sha256"],
        "patches/melee-gameplay.patch": identity["source_patch_sha256"]["melee-gameplay.patch"],
        "src/slippi_profile/patches/melee-profile-hooks.patch":
            identity["source_patch_sha256"]["melee-profile-hooks.patch"],
        **identity["cmake_source_sha256"],
    }.items():
        require(sha256((root / name).read_bytes()) == digest, f"Linked source pin mismatch: {name}")
    rows = compact_rows(manifest["mutable_input_sections"], policy["rows"],
                        _parse_map(build / pins["link_map_sha256"]))
    table = canonical({"schema": "melee-web-rollback-linked-statics-v1",
                       "wasmSha256": identity["wasm_sha256"], "policySha256": policy_sha256, "rows": rows})
    artifacts = {name: sha256((build / name).read_bytes()) for name in (
        "gameplay_menu_browser.js", "gameplay_menu_browser.wasm", "gameplay_menu_browser.data")}
    require(artifacts["gameplay_menu_browser.wasm"] == identity["wasm_sha256"], "Wasm changed during preparation")
    check_source()
    output.mkdir(exist_ok=False)
    (output / TABLE_NAME).write_bytes(table)
    receipt = {
        "schema": "melee-web-rollback-staging-linked-table-receipt-v1",
        "profile": PROFILE,
        "sourceSha": source_sha,
        "sourceStateSha256": manifest_sha256,
        "configurationIdentitySha256": manifest["configuration_identity_sha256"],
        "dependenciesSha256": identity["dependencies_lock_sha256"],
        "policySha256": policy_sha256,
        "tableSha256": sha256(table),
        "rowCount": len(rows),
        "artifacts": artifacts,
    }
    (output / "linked-statics-receipt.json").write_bytes(canonical(receipt))
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-sha", required=True)
    parser.add_argument("--manifest-sha256", required=True)
    parser.add_argument("--policy-sha256", required=True)
    args = parser.parse_args()
    try:
        receipt = prepare(args.source_root, args.build, args.policy, args.output,
                          source_sha=args.source_sha, manifest_sha256=args.manifest_sha256,
                          policy_sha256=args.policy_sha256)
    except (OSError, KeyError, ValueError, ManifestError, StageError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"Rollback staging table refused: {exc}\n")
    print(json.dumps(receipt, sort_keys=True))


if __name__ == "__main__":
    main()
