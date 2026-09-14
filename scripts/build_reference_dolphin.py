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


ROOT = Path(__file__).resolve().parents[1]
PINNED_COMMIT = "c77bbaa0f372c3f72281602a8b087206706542cb"
PATCH_DIR = ROOT / "reference-capture" / "dolphin" / "patches"
SOURCE_OVERLAY = ROOT / "reference-capture" / "dolphin" / "source"


def run(command: list[str], *, cwd: Path | None = None) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=cwd, check=True)


def git(source: Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(source), *args], text=True).strip()


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
        if not file.is_file():
            continue
        target = destination / file.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, target)


def archive_provenance(archive_root: Path, binary_sha: str, manifest: Path,
                       source_overlay: Path, patch_dir: Path) -> Path:
    """Keep the small corresponding-source receipt for this exact binary.

    The large ignored CMake/source trees remain in their caller-selected
    unique directories.  This archive is deliberately limited to the receipt,
    tracked patch series, observer overlay, parser, and build helper so a
    later build cannot silently replace the source identity of a completed
    capture bundle.
    """
    archive = archive_root / binary_sha
    archive.mkdir(parents=True, exist_ok=True)
    files: list[tuple[Path, Path]] = [(manifest, archive / manifest.name),
                                      (Path(__file__), archive / Path(__file__).name)]
    files.extend((patch, archive / "patches" / patch.name)
                 for patch in sorted(patch_dir.glob("*.patch")))
    files.extend((file, archive / "source" / file.relative_to(source_overlay))
                 for file in sorted(source_overlay.rglob("*")) if file.is_file())
    stream_parser = ROOT / "reference-capture" / "dolphin" / "reference_observer_stream.py"
    files.append((stream_parser, archive / stream_parser.name))
    for source_file, destination in files:
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.exists() and sha256(destination) != sha256(source_file):
            raise SystemExit(f"archive provenance collision: {destination}")
        if not destination.exists():
            shutil.copy2(source_file, destination)
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
    if not args.skip_submodules:
        run(["git", "submodule", "update", "--init", "--recursive"], cwd=work)
    for patch in sorted(PATCH_DIR.glob("*.patch")):
        marker = work / ".mwrc" / patch.name
        expected_marker = sha256(patch)
        if marker.exists():
            if marker.read_text(encoding="utf-8").strip() != expected_marker:
                raise SystemExit(f"stale patch marker in build source: {marker}")
            continue
        run(["git", "apply", "--index", str(patch)], cwd=work)
        marker.parent.mkdir(parents=True, exist_ok=True)
        marker.write_text(expected_marker + "\n", encoding="utf-8")
    # Dolphin keeps its core sources one level below Source/Core/Core.  Keep
    # the overlay rooted at source/Core so corresponding-source paths and the
    # generated CMake target agree exactly.
    copy_overlay(SOURCE_OVERLAY / "Core", work / "Source/Core/Core")
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
    manifest.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    archive = archive_provenance(args.archive_root.expanduser().resolve(), value["binary_sha256"],
                                 manifest, SOURCE_OVERLAY, PATCH_DIR)
    value["provenance_archive"] = str(archive)
    manifest.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    # Keep the archived receipt self-contained after adding its own location.
    shutil.copy2(manifest, archive / manifest.name)
    print(f"Built passive reference Dolphin; manifest: {manifest}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode) from error
