#!/usr/bin/env python3
"""Emit a provenance-bound inventory of generated-source C static bytes.

This is deliberately not a complete game-state or rollback snapshot. It records
only retained mutable input sections that the pinned LLD link map attributes to
generated source C translation units. Every other retained mutable section is
listed as excluded with its reason.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shlex
import subprocess
import sys
from collections import defaultdict
from pathlib import Path, PurePosixPath
from typing import Any


SCHEMA = "melee-web-source-state-manifest-v1"
TARGET = "gameplay_slippi_synctest_browser"
MAP_HEADER = "Addr Off Size Out In Symbol"
MUTABLE_SECTION_CLASSES = (
    "data", "bss", "sdata", "sbss", "tdata", "tbss", "common", "scommon", "noinit",
)
_MUTABLE_SECTION_ALTERNATION = "|".join(MUTABLE_SECTION_CLASSES)
MUTABLE_SECTION = re.compile(rf"^\.(?:{_MUTABLE_SECTION_ALTERNATION})(?:\.|$)")
MUTABLE_INPUT_SECTION_MARKER = re.compile(
    rf":\(\.(?:{_MUTABLE_SECTION_ALTERNATION})(?:\.|\)|$)"
)
MAP_COLUMNS = re.compile(
    r"^\s*(?P<address>[0-9a-fA-F]+|-)\s+"
    r"(?P<offset>[0-9a-fA-F]+|-)\s+"
    r"(?P<size>[0-9a-fA-F]+|-)\s+(?P<rest>.*?)\s*$"
)
INPUT_SECTION = re.compile(r"^(?P<object>.+):\((?P<section>[^()]*)\)$")
ARCHIVE_MEMBER = re.compile(r"^(?P<archive>.+\.a)\((?P<member>[^()]*)\)$")


class ManifestError(ValueError):
    pass


def sha256(path: Path) -> str:
    if path.is_symlink() or not path.is_file():
        raise ManifestError(f"expected a regular non-symlink file: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _parse_map(path: Path) -> list[dict[str, Any]]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError) as error:
        raise ManifestError(f"cannot read link map {path}: {error}") from error

    if sum(tuple(line.split()) == tuple(MAP_HEADER.split()) for line in lines) != 1:
        raise ManifestError("unsupported or malformed LLD map: expected one standard header")

    rows: list[dict[str, Any]] = []
    exact_rows: set[tuple[int, int, str, str, int]] = set()
    section_owners: set[tuple[str, str]] = set()
    for line_number, line in enumerate(lines, start=1):
        if ":(" not in line:
            continue
        match = MAP_COLUMNS.fullmatch(line)
        if not match:
            if MUTABLE_INPUT_SECTION_MARKER.search(line):
                raise ManifestError(f"malformed mutable input-section row at {path}:{line_number}")
            continue
        input_match = INPUT_SECTION.fullmatch(match.group("rest"))
        if not input_match:
            if MUTABLE_INPUT_SECTION_MARKER.search(line):
                raise ManifestError(f"malformed mutable input-section row at {path}:{line_number}")
            continue
        section = input_match.group("section")
        if not MUTABLE_SECTION.match(section):
            continue

        address_text = match.group("address")
        size_text = match.group("size")
        offset_text = match.group("offset")
        if address_text == "-" or size_text == "-" or offset_text == "-":
            raise ManifestError(f"mutable input section lacks numeric VMA/offset/size at {path}:{line_number}")
        address = int(address_text, 16)
        size = int(size_text, 16)
        offset = int(offset_text, 16)
        if address > 0xFFFFFFFF or size > 0x100000000 - address:
            raise ManifestError(f"mutable input section lies outside wasm32 memory at {path}:{line_number}")
        object_name = input_match.group("object").strip()
        if not object_name or not section:
            raise ManifestError(f"empty object or section at {path}:{line_number}")
        key = (address, size, object_name, section, offset)
        if key in exact_rows:
            raise ManifestError(f"duplicate mutable input section at {path}:{line_number}: {object_name}:{section}")
        owner_key = (object_name, section)
        if owner_key in section_owners:
            raise ManifestError(f"repeated mutable input section at {path}:{line_number}: {object_name}:{section}")
        exact_rows.add(key)
        section_owners.add(owner_key)
        rows.append({
            "address": address,
            "offset": offset,
            "size": size,
            "object": object_name,
            "section": section,
            "map_line": line_number,
        })

    if not rows:
        raise ManifestError("link map contains no retained mutable input-section rows")
    _validate_non_overlapping(rows)
    return rows


def _validate_non_overlapping(rows: list[dict[str, Any]]) -> None:
    nonempty = sorted((row for row in rows if row["size"]), key=lambda row: (row["address"], row["size"]))
    previous: dict[str, Any] | None = None
    for row in nonempty:
        if previous is not None and row["address"] < previous["address"] + previous["size"]:
            raise ManifestError(
                "overlapping mutable input sections: "
                f"{previous['object']}:{previous['section']} and {row['object']}:{row['section']}"
            )
        previous = row


def _read_producer_sources(path: Path) -> dict[str, set[Path]]:
    producers: dict[str, set[Path]] = defaultdict(set)
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError) as error:
        raise ManifestError(f"cannot read CMake producer source list {path}: {error}") from error
    for line_number, line in enumerate(lines, start=1):
        if not line:
            continue
        target, separator, source = line.partition("\t")
        if not separator or not target or not source:
            raise ManifestError(f"malformed producer source record at {path}:{line_number}")
        producers[target].add(Path(source).resolve())
    if not producers:
        raise ManifestError("CMake producer source list is empty")
    expected = {"gameplay_runtime", "hsd_native_runtime", "fighter_source_runtime"}
    unexpected = set(producers) - expected
    if unexpected:
        raise ManifestError(f"unexpected producer targets: {sorted(unexpected)}")
    missing = expected - set(producers)
    if missing:
        raise ManifestError(f"missing source producer targets: {sorted(missing)}")
    return producers


def _compile_output(entry: dict[str, Any], database: Path) -> Path:
    directory = Path(entry.get("directory", database.parent))
    if not directory.is_absolute():
        directory = (database.parent / directory).resolve()
    output = entry.get("output")
    if output:
        output_path = Path(output)
    else:
        arguments = entry.get("arguments")
        if arguments is None:
            command = entry.get("command")
            if not isinstance(command, str):
                raise ManifestError("compile command has neither output nor command arguments")
            arguments = shlex.split(command)
        try:
            output_path = Path(arguments[arguments.index("-o") + 1])
        except (ValueError, IndexError) as error:
            raise ManifestError("compile command has no parseable -o object output") from error
    if not output_path.is_absolute():
        output_path = directory / output_path
    return output_path.resolve()


def _read_compile_commands(path: Path) -> dict[Path, set[Path]]:
    try:
        entries = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ManifestError(f"cannot read compile commands {path}: {error}") from error
    if not isinstance(entries, list):
        raise ManifestError("compile_commands.json must contain an array")
    outputs: dict[Path, set[Path]] = defaultdict(set)
    for entry in entries:
        if not isinstance(entry, dict) or not isinstance(entry.get("file"), str):
            raise ManifestError("malformed compile command entry")
        source = Path(entry["file"])
        directory = Path(entry.get("directory", path.parent))
        if not directory.is_absolute():
            directory = (path.parent / directory).resolve()
        if not source.is_absolute():
            source = directory / source
        output = _compile_output(entry, path)
        outputs[source.resolve()].add(output)
    return outputs


def _archive_owner(object_name: str) -> tuple[str, str] | None:
    match = ARCHIVE_MEMBER.fullmatch(object_name)
    if not match:
        return None
    return Path(match.group("archive")).name, match.group("member")


def _source_for_input(
    object_name: str,
    producer_sources: dict[str, set[Path]],
    compile_outputs: dict[Path, set[Path]],
) -> tuple[str | None, Path | None, str | None]:
    archive = _archive_owner(object_name)
    if archive is None:
        return None, None, "not-a-known-source-archive-member"
    archive_name, member = archive
    target_by_archive = {f"lib{target}.a": target for target in producer_sources}
    target = target_by_archive.get(archive_name)
    if target is None:
        return None, None, "archive-outside-generated-source-producers"
    candidate_sources: set[Path] = set()
    member_path = PurePosixPath(member.replace("\\", "/"))
    for source in producer_sources[target]:
        for output in compile_outputs.get(source, set()):
            output_posix = PurePosixPath(output.as_posix())
            if member_path == output_posix or member_path.name == output_posix.name:
                candidate_sources.add(source)
    if not candidate_sources:
        raise ManifestError(f"cannot resolve {object_name} through {target} CMake sources/compile_commands")
    if len(candidate_sources) != 1:
        raise ManifestError(
            f"ambiguous archive member {object_name}: "
            + ", ".join(sorted(source.as_posix() for source in candidate_sources))
        )
    return target, next(iter(candidate_sources)), None


def _display_path(path: Path, repository_root: Path) -> str:
    try:
        return path.relative_to(repository_root).as_posix()
    except ValueError:
        return path.as_posix()


def _source_relative(source: Path, source_root: Path) -> str | None:
    try:
        return source.relative_to(source_root).as_posix()
    except ValueError:
        return None


def _classify_rows(
    rows: list[dict[str, Any]],
    producer_sources: dict[str, set[Path]],
    compile_outputs: dict[Path, set[Path]],
    source_root: Path,
    repository_root: Path,
) -> list[dict[str, Any]]:
    classified: list[dict[str, Any]] = []
    for row in rows:
        target, source, reason = _source_for_input(row["object"], producer_sources, compile_outputs)
        source_relative = _source_relative(source, source_root) if source is not None else None
        source_c = source is not None and source.suffix.lower() == ".c"
        included = source_relative is not None and source_c
        if included:
            decision = "source-c-static-candidate"
            exclusion_reason = None
        else:
            decision = "excluded"
            if source_relative is not None and not source_c:
                exclusion_reason = "generated-source-non-c-translation-unit"
            elif source is not None:
                exclusion_reason = "project-or-external-c-source-outside-generated-source-root"
            else:
                exclusion_reason = reason or "source-provenance-unavailable"
        record = {
            "address": row["address"],
            "size": row["size"],
            "input_section": row["section"],
            "input_object": row["object"],
            "producer_target": target,
            "source": source_relative if source_relative is not None else (
                _display_path(source, repository_root) if source is not None else None
            ),
            "decision": decision,
            "exclusion_reason": exclusion_reason,
        }
        classified.append(record)
    return sorted(classified, key=lambda row: (row["address"], row["size"], row["input_object"], row["input_section"]))


def _git_output(source_root: Path, *arguments: str) -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(source_root), *arguments],
            text=True,
            stderr=subprocess.PIPE,
        ).strip()
    except (OSError, subprocess.CalledProcessError) as error:
        raise ManifestError(f"cannot inspect generated source checkout {source_root}: {error}") from error


def _read_melee_pin(repository_root: Path) -> str:
    lock_path = repository_root / "dependencies.lock.json"
    try:
        lock = json.loads(lock_path.read_text(encoding="utf-8"))
        commit = lock["repositories"]["melee"]["commit"]
    except (OSError, UnicodeError, json.JSONDecodeError, KeyError, TypeError) as error:
        raise ManifestError(f"cannot read pinned Melee source identity from {lock_path}: {error}") from error
    if not isinstance(commit, str) or not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ManifestError("dependencies.lock.json has an invalid Melee commit")
    return commit


def _producer_source_hashes(
    producer_sources: dict[str, set[Path]],
) -> dict[str, str]:
    hashes: dict[str, str] = {}
    for target in sorted(producer_sources):
        for source in sorted(producer_sources[target]):
            if not source.is_file():
                raise ManifestError(f"CMake producer source is missing: {source}")
            hashes[source.as_posix()] = sha256(source)
    return hashes


def build_manifest(args: argparse.Namespace) -> dict[str, Any]:
    if args.target != TARGET:
        raise ManifestError(f"manifest is private to {TARGET}")
    map_path = Path(args.link_map).resolve()
    wasm_path = Path(args.wasm).resolve()
    compile_commands_path = Path(args.compile_commands).resolve()
    producer_sources_path = Path(args.producer_sources).resolve()
    source_root = Path(args.source_root).resolve()
    repository_root = Path(args.repository_root).resolve()
    cmake_cache_path = Path(args.cmake_cache).resolve()
    build_graph_value = args.build_graph or str(Path(args.compile_commands).resolve().parent / "build.ninja")
    build_graph_path = Path(build_graph_value).resolve()
    version_path = Path(args.emscripten_version_file).resolve()
    if not source_root.is_dir():
        raise ManifestError(f"generated source checkout root is missing: {source_root}")
    if not _source_relative(source_root, repository_root):
        raise ManifestError("generated source checkout root must live inside the repository checkout")

    locked_melee_commit = _read_melee_pin(repository_root)
    actual_melee_commit = _git_output(source_root, "rev-parse", "HEAD")
    if actual_melee_commit != locked_melee_commit:
        raise ManifestError(
            "generated source checkout does not match dependencies.lock.json: "
            f"expected {locked_melee_commit}, got {actual_melee_commit}"
        )
    generated_patch_path = source_root / ".git/melee-web-gameplay.patch"
    if not generated_patch_path.is_file():
        raise ManifestError(f"generated source checkout has no applied-patch identity: {generated_patch_path}")

    rows = _parse_map(map_path)
    producers = _read_producer_sources(producer_sources_path)
    compile_outputs = _read_compile_commands(compile_commands_path)
    classified = _classify_rows(rows, producers, compile_outputs, source_root, repository_root)
    included = [row for row in classified if row["decision"] == "source-c-static-candidate"]
    if not included:
        raise ManifestError("link map has no mutable sections proven to come from generated source C")

    try:
        emscripten_version = version_path.read_text(encoding="utf-8").strip().strip('"')
    except (OSError, UnicodeError) as error:
        raise ManifestError(f"cannot read Emscripten version {version_path}: {error}") from error
    if not emscripten_version:
        raise ManifestError("Emscripten version file is empty")

    artifact_hashes = {
        "wasm_sha256": sha256(wasm_path),
        "link_map_sha256": sha256(map_path),
    }
    producer_source_hashes = _producer_source_hashes(producers)
    source_patches = {
        "melee-gameplay.patch": sha256(repository_root / "patches/melee-gameplay.patch"),
        "melee-profile-hooks.patch": sha256(
            repository_root / "src/slippi_profile/patches/melee-profile-hooks.patch"
        ),
        "generated-composed-patch": sha256(generated_patch_path),
    }
    identity_inputs = {
        "schema": SCHEMA,
        "target": TARGET,
        "cmake_build_type": args.build_type,
        "emscripten_version": emscripten_version,
        "source_root": _display_path(source_root, repository_root),
        "locked_melee_commit": locked_melee_commit,
        "actual_melee_commit": actual_melee_commit,
        "source_patch_sha256": source_patches,
        "producer_source_sha256": producer_source_hashes,
        "dependencies_lock_sha256": sha256(repository_root / "dependencies.lock.json"),
        "cmake_cache_sha256": sha256(cmake_cache_path),
        "build_graph_sha256": sha256(build_graph_path),
        "compile_commands_sha256": sha256(compile_commands_path),
        "producer_sources_sha256": sha256(producer_sources_path),
        "manifest_script_sha256": sha256(repository_root / "scripts/source_state_manifest.py"),
        "cmake_source_sha256": {
            "CMakeLists.txt": sha256(repository_root / "CMakeLists.txt"),
            "cmake/FighterRuntime.cmake": sha256(repository_root / "cmake/FighterRuntime.cmake"),
        },
        **artifact_hashes,
    }
    canonical_identity = json.dumps(identity_inputs, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return {
        "schema": SCHEMA,
        "target": TARGET,
        "complete_game_state": False,
        "scope": "retained mutable input-section bytes from generated source C translation units only",
        "address_model": "wasm32 linear-memory VMA from pinned wasm-ld map; post-link VMA checked by optimized sentinel fixture",
        "configuration_identity_sha256": hashlib.sha256(canonical_identity).hexdigest(),
        "configuration_identity": identity_inputs,
        "counts": {
            "mutable_input_sections": len(classified),
            "source_c_candidates": len(included),
            "excluded_mutable_input_sections": len(classified) - len(included),
        },
        "mutable_input_sections": classified,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", required=True)
    parser.add_argument("--link-map", required=True)
    parser.add_argument("--wasm", required=True)
    parser.add_argument("--compile-commands", required=True)
    parser.add_argument("--producer-sources", required=True)
    parser.add_argument("--source-root", required=True)
    parser.add_argument("--repository-root", required=True)
    parser.add_argument("--cmake-cache", required=True)
    parser.add_argument("--build-graph")
    parser.add_argument("--emscripten-version-file", required=True)
    parser.add_argument("--build-type", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args(argv)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.unlink(missing_ok=True)
    try:
        manifest = build_manifest(args)
        temporary = output.with_suffix(output.suffix + ".tmp")
        temporary.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        temporary.replace(output)
    except (ManifestError, OSError) as error:
        output.with_suffix(output.suffix + ".tmp").unlink(missing_ok=True)
        print(f"source_state_manifest: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
