#!/usr/bin/env python3
"""Build the passive reference-capture Dolphin from a clean pinned checkout.

The source checkout passed with ``--source-dir`` is never modified.  A fresh
ignored worktree is created, the tracked patch series is applied, and the
observer sources are overlaid there before CMake runs.  This keeps the GPL
corresponding source and the exact downstream change reviewable in Git while
leaving the official pinned checkout usable by the existing retail harness.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
PINNED_COMMIT = "c77bbaa0f372c3f72281602a8b087206706542cb"
PATCH_DIR = ROOT / "reference-capture" / "dolphin" / "patches"
SOURCE_OVERLAY = ROOT / "reference-capture" / "dolphin" / "source"


def run(command: list[str], *, cwd: Path | None = None) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=cwd, check=True)


def git(source: Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(source), *args], text=True).strip()


def _git_with_index(source: Path, index: Path, *args: str) -> str:
    environment = os.environ.copy()
    environment["GIT_INDEX_FILE"] = str(index)
    return subprocess.check_output(["git", "-C", str(source), *args],
                                   env=environment, text=True).strip()


def _index_snapshot(source: Path, index: Path | None = None) -> str:
    if index is None:
        return git(source, "ls-files", "-s")
    return _git_with_index(source, index, "ls-files", "-s")


def _expected_index_snapshots(source: Path, patches: list[Path]) -> list[str]:
    """Return the tracked index after each clean patch-series prefix.

    Applying patches to a throwaway index checks the actual staged tree rather
    than trusting local marker files.  The source checkout and worktree are
    never changed by this operation.
    """
    with tempfile.TemporaryDirectory(prefix="reference-dolphin-index-") as temporary:
        index = Path(temporary) / "index"
        _git_with_index(source, index, "read-tree", "HEAD")
        snapshots = [_index_snapshot(source, index)]
        environment = os.environ.copy()
        environment["GIT_INDEX_FILE"] = str(index)
        for patch in patches:
            subprocess.run(["git", "-C", str(source), "apply", "--cached", str(patch)],
                           env=environment, check=True)
            snapshots.append(_index_snapshot(source, index))
        return snapshots


def _untracked_paths(source: Path) -> set[str]:
    paths: set[str] = set()
    for flags in ((), ("--ignored",)):
        output = subprocess.check_output(["git", "-C", str(source), "ls-files", "--others",
                                          "--exclude-standard", *flags, "-z"])
        paths.update(os.fsdecode(path) for path in output.split(b"\0") if path)
    return paths


def _overlay_targets(source_overlay: Path, work: Path) -> tuple[dict[str, Path], dict[str, Path]]:
    targets: dict[str, Path] = {}
    for file in sorted(source_overlay.rglob("*")):
        if file.is_dir() and not file.is_symlink():
            continue
        if file.is_symlink() or not file.is_file():
            raise SystemExit(f"observer overlay contains non-regular file: {file}")
        relative = file.relative_to(source_overlay)
        targets[str((Path("Source/Core/Core") / relative).as_posix())] = file
    return {path: work / path for path in targets}, targets


def verify_source_composition(work: Path, patches: list[Path], source_overlay: Path,
                              *, require_complete: bool) -> int:
    """Verify that ``work`` is exactly a clean pinned patch prefix plus overlay.

    Patch markers are only a cache of progress.  The staged index is compared
    with trees reconstructed from HEAD and every patch, while the worktree,
    submodules, and untracked files are checked independently.
    """
    snapshots = _expected_index_snapshots(work, patches)
    actual = _index_snapshot(work)
    try:
        prefix = snapshots.index(actual)
    except ValueError as error:
        raise SystemExit(f"build source has unexplained staged edits: {work}") from error
    if require_complete and prefix != len(patches):
        raise SystemExit(f"build source is missing the complete patch series: {work}")

    # This catches edits inside initialized submodules as well as ordinary
    # unstaged tracked edits.  The expected patch edits are staged.
    result = subprocess.run(["git", "-C", str(work), "diff", "--quiet",
                             "--ignore-submodules=none"], check=False)
    if result.returncode:
        raise SystemExit(f"build source has unexplained worktree edits: {work}")

    marker_root = work / ".mwrc"
    expected_markers = {patch.name: sha256(patch) for patch in patches[:prefix]}
    actual_markers: dict[str, str] = {}
    if marker_root.exists():
        if not marker_root.is_dir() or marker_root.is_symlink():
            raise SystemExit(f"invalid patch marker directory: {marker_root}")
        for marker in sorted(marker_root.iterdir()):
            if not marker.is_file() or marker.is_symlink() or marker.name not in {
                    patch.name for patch in patches}:
                raise SystemExit(f"unexplained patch marker: {marker}")
            actual_markers[marker.name] = marker.read_text(encoding="utf-8").strip()
    if set(actual_markers) - set(expected_markers):
        raise SystemExit(f"patch marker is ahead of the staged source: {marker_root}")
    for name, value in actual_markers.items():
        if value != expected_markers[name]:
            raise SystemExit(f"stale patch marker in build source: {marker_root / name}")

    overlay_paths, overlay_sources = _overlay_targets(source_overlay, work)
    allowed_untracked = set(overlay_paths) | {f".mwrc/{name}" for name in expected_markers}
    for path in _untracked_paths(work):
        if path not in allowed_untracked:
            raise SystemExit(f"unexplained untracked build-source edit: {work / path}")
    if require_complete:
        submodules = git(work, "submodule", "status", "--recursive")
        if any(line.startswith(("-", "+", "U")) for line in submodules.splitlines()):
            raise SystemExit(f"build source has missing or mismatched submodules: {work}")

    for relative, target in overlay_paths.items():
        source_file = overlay_sources[relative]
        if target.is_symlink() or target.exists():
            if target.is_symlink() or not target.is_file() or sha256(target) != sha256(source_file):
                raise SystemExit(f"observer overlay differs in build source: {target}")
        elif require_complete:
            raise SystemExit(f"observer overlay is missing from build source: {target}")
    return prefix


def _atomic_write(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary_name: str | None = None
    try:
        descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.",
                                                      dir=path.parent)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_name, path)
        temporary_name = None
    finally:
        if temporary_name is not None:
            Path(temporary_name).unlink(missing_ok=True)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def bundle_inventory(bundle: Path) -> list[dict[str, object]]:
    """Record every packaged file and internal symlink for runtime admission."""
    entries: list[dict[str, object]] = []
    for path in sorted(bundle.rglob("*")):
        relative = str(path.relative_to(bundle))
        if path.is_symlink():
            target = os.readlink(path)
            resolved = (path.parent / target).resolve()
            if bundle not in resolved.parents and resolved != bundle:
                raise SystemExit(f"bundle contains external symlink: {path} -> {target}")
            entries.append({"path": relative, "kind": "symlink", "target": target})
        elif path.is_file():
            entries.append({"path": relative, "kind": "file", "size": path.stat().st_size,
                            "sha256": sha256(path)})
    return entries


def _macho_dependencies(path: Path) -> list[str]:
    result = subprocess.run(["otool", "-L", str(path)], text=True,
                            capture_output=True, check=False)
    if result.returncode:
        return []
    values = [line.strip().split(" (", 1)[0] for line in result.stdout.splitlines()[1:]
              if line.strip() and not line.strip().endswith(":")]
    # otool -L prints a dylib's install name as the first entry; it is not a
    # dependency and commonly uses @rpath/lib*.dylib.
    identity_lines = subprocess.run(["otool", "-D", str(path)], text=True,
                                     capture_output=True, check=False).stdout.splitlines()
    # Fat Mach-O files repeat the install name once for each architecture.
    identities = {line.strip() for line in identity_lines
                  if line.strip() and not line.strip().endswith(":")}
    return [value for value in values if value not in identities]


def runtime_inventory(binary: Path, bundle: Path) -> list[dict[str, object]]:
    """Hash non-system Mach-O dependencies actually used by the packaged app."""
    initial_owners = [binary]
    initial_owners.extend(path for path in bundle.rglob("*")
                          if path.is_file() and path.suffix in {".dylib", ".so"})

    def load_rpaths(owner: Path) -> list[Path]:
        load = subprocess.run(["otool", "-l", str(owner)], text=True,
                              capture_output=True, check=False)
        values: list[Path] = []
        lines = load.stdout.splitlines()
        for index, line in enumerate(lines):
            if line.strip() == "cmd LC_RPATH" and index + 2 < len(lines):
                raw = lines[index + 2].strip()
                if raw.startswith("path "):
                    values.append(Path(raw.split(" ", 2)[1]))
        return values

    global_rpaths = load_rpaths(binary)
    entries: dict[str, dict[str, object]] = {}
    queue = list(initial_owners)
    seen_owners: set[Path] = set()
    while queue:
        owner = queue.pop(0).resolve()
        if owner in seen_owners:
            continue
        seen_owners.add(owner)
        owner_rpaths = load_rpaths(owner)
        for load_name in _macho_dependencies(owner):
            candidates: list[Path] = []
            if load_name.startswith("/"):
                candidates.append(Path(load_name))
            elif load_name.startswith("@loader_path/"):
                candidates.append(owner.parent / load_name.removeprefix("@loader_path/"))
            elif load_name.startswith("@executable_path/"):
                candidates.append(binary.parent / load_name.removeprefix("@executable_path/"))
            elif load_name.startswith("@rpath/"):
                suffix = load_name.removeprefix("@rpath/")
                for root in [*owner_rpaths, *global_rpaths]:
                    root_name = str(root)
                    if root_name.startswith("@loader_path/"):
                        root_path = owner.parent / root_name.removeprefix("@loader_path/")
                    elif root_name.startswith("@executable_path/"):
                        root_path = binary.parent / root_name.removeprefix("@executable_path/")
                    else:
                        root_path = root
                    candidates.append(root_path / suffix)
            if not candidates:
                raise SystemExit(f"cannot resolve Mach-O dependency {load_name} of {owner}")
            resolved = next((candidate.resolve() for candidate in candidates if candidate.exists()), None)
            system_managed = load_name.startswith("/System/") or load_name.startswith("/usr/lib/")
            if resolved is None and not system_managed:
                raise SystemExit(f"missing non-system Mach-O dependency {load_name} of {owner}")
            key = load_name if resolved is None else str(resolved)
            item = entries.setdefault(key, {"load_name": load_name, "load_names": [load_name],
                                            "resolved_path": str(resolved) if resolved else None,
                                            "system_managed": system_managed})
            if load_name not in item["load_names"]:
                item["load_names"].append(load_name)
            if resolved is not None and not system_managed:
                item["sha256"] = sha256(resolved)
                item["size"] = resolved.stat().st_size
                if resolved.suffix in {".dylib", ".so"}:
                    queue.append(resolved)
    return [entries[key] for key in sorted(entries)]


def copy_overlay(source: Path, destination: Path) -> None:
    for file in source.rglob("*"):
        if file.is_dir() and not file.is_symlink():
            continue
        if file.is_symlink() or not file.is_file():
            raise SystemExit(f"observer overlay contains non-regular file: {file}")
        target = destination / file.relative_to(source)
        if target.exists():
            if target.is_symlink() or not target.is_file() or sha256(target) != sha256(file):
                raise SystemExit(f"observer overlay differs in build source: {target}")
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, target)


def archive_provenance(archive_root: Path, binary_sha: str, manifest: Path,
                       source_overlay: Path, patch_dir: Path, *,
                       manifest_bytes: bytes | None = None) -> Path:
    """Keep the small corresponding-source receipt for this exact binary.

    The large ignored CMake/source trees remain in their caller-selected
    unique directories.  This archive is deliberately limited to the receipt,
    tracked patch series, observer overlay, parser, and build helper so a
    later build cannot silently replace the source identity of a completed
    capture bundle.
    """
    archive = archive_root / binary_sha
    files: list[tuple[Path, Path, bytes | None]] = [
        (manifest, archive / manifest.name, manifest_bytes),
        (Path(__file__), archive / Path(__file__).name, None),
    ]
    files.extend((patch, archive / "patches" / patch.name, None)
                 for patch in sorted(patch_dir.glob("*.patch")))
    for file in sorted(source_overlay.rglob("*")):
        if file.is_dir() and not file.is_symlink():
            continue
        if file.is_symlink() or not file.is_file():
            raise SystemExit(f"observer overlay contains non-regular file: {file}")
        files.append((file, archive / "source" / file.relative_to(source_overlay), None))
    stream_parser = ROOT / "reference-capture" / "dolphin" / "reference_observer_stream.py"
    files.append((stream_parser, archive / stream_parser.name, None))

    def source_sha(source_file: Path, payload: bytes | None) -> str:
        return (hashlib.sha256(payload).hexdigest() if payload is not None
                else sha256(source_file))

    # Preflight every destination before creating or changing the archive, so
    # a collision cannot leave a half-updated corresponding-source tree.
    for source_file, destination, payload in files:
        if destination.exists() and sha256(destination) != source_sha(source_file, payload):
            raise SystemExit(f"archive provenance collision: {destination}")
    archive.mkdir(parents=True, exist_ok=True)
    for source_file, destination, payload in files:
        if not destination.exists():
            destination.parent.mkdir(parents=True, exist_ok=True)
            if payload is None:
                shutil.copy2(source_file, destination)
            else:
                destination.write_bytes(payload)
    return archive


def publish_build_receipt(value: dict[str, object], manifest: Path, archive_root: Path,
                          source_overlay: Path, patch_dir: Path) -> Path:
    """Archive and then atomically publish one deterministic build receipt."""
    value = dict(value)
    archive = archive_root / str(value["binary_sha256"])
    value["provenance_archive"] = str(archive)
    payload = (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")
    archive_provenance(archive_root, str(value["binary_sha256"]), manifest,
                       source_overlay, patch_dir, manifest_bytes=payload)
    _atomic_write(manifest, payload)
    return archive


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--source-dir", type=Path, default=ROOT / ".deps" / "reference-dolphin",
                        help="clean pinned Dolphin checkout (never modified)")
    result.add_argument("--work-dir", type=Path,
                        default=ROOT / "work" / "reference-dolphin-source")
    result.add_argument("--build-dir", type=Path,
                        default=ROOT / "work" / "reference-dolphin-build")
    result.add_argument("--output", type=Path, default=None,
                        help="built app/binary to record (defaults to CMake's Binaries output)")
    result.add_argument("--generator", default="Unix Makefiles")
    result.add_argument("--target", default="build_final_bundle",
                        help="CMake target (build_final_bundle for a macOS app)")
    result.add_argument("--jobs", type=int, default=None)
    result.add_argument("--headless", action="store_true",
                        help="build DolphinNoGUI instead of the double-clickable Qt app")
    result.add_argument("--skip-submodules", action="store_true",
                        help="only for a source tree whose submodules are already initialized")
    result.add_argument("--manifest", type=Path, default=None)
    result.add_argument("--archive-root", type=Path,
                        default=ROOT / "work" / "reference-capture-validation" /
                        "observer-builds",
                        help="archive corresponding source by completed binary SHA")
    return result


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    source = args.source_dir.expanduser().resolve()
    work = args.work_dir.expanduser().resolve()
    build = args.build_dir.expanduser().resolve()
    output = (args.output.expanduser().resolve() if args.output else
              build / "Binaries" / ("dolphin-emu-nogui" if args.headless else "Dolphin.app"))
    if work == source or source in work.parents or work in source.parents:
        raise SystemExit("build worktree must be separate from the pinned source checkout")
    if build == source or source in build.parents:
        raise SystemExit("build output must be outside the pinned source checkout")
    if git(source, "rev-parse", "HEAD") != PINNED_COMMIT:
        raise SystemExit(f"source checkout is not pinned to {PINNED_COMMIT}")
    if git(source, "status", "--porcelain"):
        raise SystemExit(f"refusing dirty pinned source checkout: {source}")
    if work.exists():
        if git(work, "rev-parse", "HEAD") != PINNED_COMMIT:
            raise SystemExit(f"existing build source is not pinned: {work}")
    else:
        work.parent.mkdir(parents=True, exist_ok=True)
        try:
            run(["git", "clone", "--shared", str(source), str(work)])
        except subprocess.CalledProcessError:
            # A filtered/prompted source checkout can lack an object needed by
            # --shared. Re-fetch the pinned revision from its recorded origin
            # into the ignored build source instead of touching that checkout.
            if work.exists():
                shutil.rmtree(work)
            origin = git(source, "config", "--get", "remote.origin.url")
            run(["git", "clone", "--filter=blob:none", origin, str(work)])
        run(["git", "checkout", "--detach", PINNED_COMMIT], cwd=work)
    patches = sorted(PATCH_DIR.glob("*.patch"))
    prefix = verify_source_composition(work, patches, SOURCE_OVERLAY / "Core", require_complete=False)
    if not args.skip_submodules:
        run(["git", "submodule", "update", "--init", "--recursive"], cwd=work)
    for index, patch in enumerate(patches):
        if index >= prefix:
            run(["git", "apply", "--index", str(patch)], cwd=work)
        marker = work / ".mwrc" / patch.name
        marker.parent.mkdir(parents=True, exist_ok=True)
        marker.write_text(sha256(patch) + "\n", encoding="utf-8")
    # Dolphin keeps its core sources one level below Source/Core/Core.  Keep
    # the overlay rooted at source/Core so corresponding-source paths and the
    # generated CMake target agree exactly.
    copy_overlay(SOURCE_OVERLAY / "Core", work / "Source/Core/Core")
    verify_source_composition(work, patches, SOURCE_OVERLAY / "Core", require_complete=True)
    build.parent.mkdir(parents=True, exist_ok=True)
    cmake = ["cmake", "-S", str(work), "-B", str(build), "-G", args.generator,
             "-DCMAKE_BUILD_TYPE=Release", "-DENABLE_AUTOUPDATE=OFF"]
    if args.headless:
        cmake.extend(["-DENABLE_HEADLESS=ON", "-DENABLE_QT=OFF", "-DENABLE_NOGUI=ON"])
        if args.target == "build_final_bundle":
            args.target = "dolphin-nogui"
    else:
        cmake.extend(["-DENABLE_HEADLESS=OFF", "-DENABLE_QT=ON", "-DENABLE_NOGUI=OFF"])
    run(cmake)
    command = ["cmake", "--build", str(build), "--target", args.target]
    if args.jobs:
        if args.jobs < 1:
            raise SystemExit("--jobs must be positive")
        command.extend(["--parallel", str(args.jobs)])
    run(command)
    verify_source_composition(work, patches, SOURCE_OVERLAY / "Core", require_complete=True)
    manifest = (args.manifest or (output.parent / "reference-dolphin-build.json")).expanduser().resolve()
    manifest.parent.mkdir(parents=True, exist_ok=True)
    binary = output / "Contents" / "MacOS" / "Dolphin" if output.suffix == ".app" else output
    if not binary.is_file():
        raise SystemExit(f"build completed without the requested binary: {binary}")
    overlay_hashes = {
        str(file.relative_to(SOURCE_OVERLAY)): sha256(file)
        for file in sorted(SOURCE_OVERLAY.rglob("*")) if file.is_file()
    }
    composed_diff = subprocess.check_output(["git", "diff", "--binary", "HEAD"], cwd=work)
    compiler = args.__dict__.get("compiler") or ""
    if not compiler:
        compiler = subprocess.run(["c++", "--version"], text=True, capture_output=True,
                                  check=False).stdout.splitlines()[0] if shutil.which("c++") else "unknown"
    cmake_version = subprocess.run(["cmake", "--version"], text=True, capture_output=True,
                                   check=False).stdout.splitlines()[0]
    observer_identity = {
        "game_revision": "GALE01r2",
        "dol_sha1": "08e0bf20134dfcb260699671004527b2d6bb1a45",
        "dol_sha256": "dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646",
        "cpu": "JITARM64",
        "writes_guest_memory": False,
    }
    value = {
        "schema": "melee-web-reference-dolphin-build",
        "version": 1,
        "dolphin_commit": PINNED_COMMIT,
        "source": str(work),
        "build": str(build),
        "target": args.target,
        "configuration": "Release",
        "cmake_command": cmake,
        "build_command": command,
        "cmake_version": cmake_version,
        "compiler": compiler,
        "dependencies": {
            "CMAKE_PREFIX_PATH": os.environ.get("CMAKE_PREFIX_PATH", ""),
            "Qt6_DIR": os.environ.get("Qt6_DIR", ""),
            "Qt5_DIR": os.environ.get("Qt5_DIR", ""),
        },
        "observer_patch_sha256": [sha256(patch) for patch in sorted(PATCH_DIR.glob("*.patch"))],
        "observer_source_overlay_sha256": overlay_hashes,
        "composed_source_diff_sha256": hashlib.sha256(composed_diff).hexdigest(),
        "observer_identity": observer_identity,
        "observer_identity_sha256": hashlib.sha256(
            json.dumps(observer_identity, sort_keys=True, separators=(",", ":")).encode()).hexdigest(),
        "binary": str(binary),
        "binary_sha256": sha256(binary),
        "bundle_inventory": bundle_inventory(output) if output.suffix == ".app" else [],
        "runtime_dependencies": runtime_inventory(binary, output) if output.suffix == ".app" else [],
        "writes_guest_memory": False,
        "cpu": "JITARM64",
        "input_recording_version": 1,
    }
    publish_build_receipt(value, manifest, args.archive_root.expanduser().resolve(),
                          SOURCE_OVERLAY, PATCH_DIR)
    print(f"Built passive reference Dolphin; manifest: {manifest}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode) from error
