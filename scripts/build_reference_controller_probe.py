#!/usr/bin/env python3
"""Build the isolated pinned SDL controller identity probe.

This script reuses only the static SDL/libusb artifacts from the already-built
reference Dolphin tree. It never configures or rebuilds Dolphin itself.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

from build_reference_dolphin import runtime_inventory


ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIR = ROOT / "work/reference-dolphin-source-v2"
BUILD_DIR = ROOT / "work/reference-dolphin-build-v2"
PROBE_SOURCE = ROOT / "reference-capture/controller-probe"
DEFAULT_OUTPUT = ROOT / "work/reference-controller-probe-v1"
DOLPHIN_REVISION = "c77bbaa0f372c3f72281602a8b087206706542cb"
SDL_REVISION = "5848e584a1b606de26e3dbd1c7e4ecbc34f807a6"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def sha256_tree(root: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(path for path in root.rglob("*") if path.is_file()):
        digest.update(path.relative_to(root).as_posix().encode())
        digest.update(b"\0")
        with path.open("rb") as stream:
            while block := stream.read(1024 * 1024):
                digest.update(block)
        digest.update(b"\0")
    return digest.hexdigest()


def git(source: Path, *args: str) -> str:
    result = subprocess.run(["git", "-C", str(source), *args], check=True,
                            capture_output=True, text=True)
    return result.stdout.strip()


def require_file(path: Path, label: str) -> Path:
    if path.is_symlink() or not path.is_file():
        raise SystemExit(f"missing {label}: {path}")
    return path


def cmake_cache_value(cache: Path, key: str) -> str:
    prefix = f"{key}:"
    for line in cache.read_text(encoding="utf-8").splitlines():
        if line.startswith(prefix):
            return line.split("=", 1)[1]
    return ""


def cmake_version() -> str:
    result = subprocess.run(["cmake", "--version"], check=True,
                            capture_output=True, text=True)
    return result.stdout.splitlines()[0] if result.stdout else ""


def cmake_compiler_value(cmake_build: Path, key: str) -> str:
    files = sorted(cmake_build.glob("CMakeFiles/*/CMakeCXXCompiler.cmake"))
    if not files:
        return ""
    match = re.search(rf'set\(CMAKE_CXX_{re.escape(key)} "([^"]*)"\)',
                      files[-1].read_text(encoding="utf-8"))
    return match.group(1) if match else ""


def compiler_version(path: Path) -> str:
    result = subprocess.run([str(path), "--version"], check=True,
                            capture_output=True, text=True)
    return result.stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--source", type=Path, default=SOURCE_DIR,
                        help="pinned reference Dolphin source checkout")
    parser.add_argument("--build", type=Path, default=BUILD_DIR,
                        help="existing reference Dolphin build tree")
    parser.add_argument("--generator", default=None,
                        help="optional CMake generator, for example Ninja")
    args = parser.parse_args()

    source = args.source.resolve()
    build = args.build.resolve()
    output = args.output_root.resolve()
    sdl_source = source / "Externals/SDL/SDL"
    sdl_build = build / "Externals/SDL/SDL"
    libusb_source = source / "Externals/libusb/libusb"
    sdl_library = require_file(sdl_build / "libSDL3.a", "pinned libSDL3.a")
    libusb_library = require_file(build / "Externals/libusb/libusb.a",
                                  "pinned bundled libusb.a")
    headers = require_file(sdl_source / "include/SDL3/SDL.h", "pinned SDL headers")
    require_file(sdl_build / "SDL3staticTargets.cmake", "SDL static target metadata")
    require_file(sdl_build / "SDL3headersTargets.cmake", "SDL header target metadata")
    require_file(PROBE_SOURCE / "CMakeLists.txt", "probe CMakeLists.txt")
    require_file(PROBE_SOURCE / "main.cpp", "probe source")

    sdl_status = git(sdl_source, "status", "--porcelain")
    if sdl_status:
        raise SystemExit("pinned SDL checkout is dirty; refusing to build probe")
    sdl_commit = git(sdl_source, "rev-parse", "HEAD")
    dolphin_commit = git(source, "rev-parse", "HEAD")
    if dolphin_commit != DOLPHIN_REVISION:
        raise SystemExit(f"Dolphin source is not pinned to {DOLPHIN_REVISION}")
    if sdl_commit != SDL_REVISION:
        raise SystemExit(f"SDL source is not pinned to {SDL_REVISION}")
    output.mkdir(mode=0o700, parents=True, exist_ok=True)
    cmake_build = output / "cmake-build"
    cmake_args = ["cmake", "-S", str(PROBE_SOURCE), "-B", str(cmake_build),
                  "-DCMAKE_BUILD_TYPE=Release",
                  f"-DSDL3_SOURCE_DIR={sdl_source}",
                  f"-DSDL3_BUILD_DIR={sdl_build}",
                  f"-DLIBUSB_SOURCE_DIR={libusb_source}",
                  f"-DSDL3_LIBRARY={sdl_library}",
                  f"-DLIBUSB_LIBRARY={libusb_library}"]
    if args.generator:
        cmake_args[1:1] = ["-G", args.generator]
    subprocess.run(cmake_args, check=True)
    subprocess.run(["cmake", "--build", str(cmake_build), "--target",
                    "webmelee-controller-probe", "--config", "Release"], check=True)

    executable = cmake_build / "bin/webmelee-controller-probe"
    require_file(executable, "built controller probe")
    cache = cmake_build / "CMakeCache.txt"
    require_file(cache, "CMake cache")
    compiler_path = Path(cmake_cache_value(cache, "CMAKE_CXX_COMPILER"))
    require_file(compiler_path, "C++ compiler")
    probe_files = {
        name: {"path": str(PROBE_SOURCE / name),
               "sha256": sha256_file(PROBE_SOURCE / name)}
        for name in ("main.cpp", "CMakeLists.txt", "README.md")
    }
    helper_path = Path(__file__).resolve()
    manifest = {
        "schema": "webmelee-controller-probe-build",
        "version": 1,
        "dolphin_revision": dolphin_commit,
        "sdl_revision": sdl_commit,
        "sdl_version": "3.4.4",
        "sdl_library_sha256": sha256_file(sdl_library),
        "sdl_header_tree_sha256": sha256_tree(sdl_source / "include/SDL3"),
        "sdl_generated_header_tree_sha256": sha256_tree(sdl_build / "include-revision/SDL3"),
        "libusb_library_sha256": sha256_file(libusb_library),
        "binary": {"path": str(executable), "sha256": sha256_file(executable)},
        # Keep this identical to the app builder's Mach-O inventory. Static
        # SDL/libusb inputs are recorded above; they are not runtime loads.
        "runtime_dependencies": runtime_inventory(executable, output),
        "source_build": {
            "dolphin_source_commit": dolphin_commit,
            "sdl_source_commit": sdl_commit,
            "sdl_library_sha256": sha256_file(sdl_library),
            "sdl_header_tree_sha256": sha256_tree(sdl_source / "include/SDL3"),
            "sdl_generated_header_tree_sha256": sha256_tree(sdl_build / "include-revision/SDL3"),
            "sdl_static_target_metadata_sha256": sha256_file(
                sdl_build / "SDL3staticTargets.cmake"),
            "sdl_headers_target_metadata_sha256": sha256_file(
                sdl_build / "SDL3headersTargets.cmake"),
            "sdl_config_sha256": sha256_file(sdl_build / "SDL3Config.cmake"),
            "libusb_library_sha256": sha256_file(libusb_library),
        },
        "build_provenance": {
            "probe_sources": probe_files,
            "builder": {"path": str(helper_path), "sha256": sha256_file(helper_path)},
            "cmake": {"version": cmake_version()},
            "compiler": {
                "path": str(compiler_path),
                "sha256": sha256_file(compiler_path),
                "id": cmake_compiler_value(cmake_build, "COMPILER_ID"),
                "version": cmake_compiler_value(cmake_build, "COMPILER_VERSION"),
                "version_output": compiler_version(compiler_path),
                "flags": {
                    "CMAKE_CXX_FLAGS": cmake_cache_value(cache, "CMAKE_CXX_FLAGS"),
                    "CMAKE_CXX_FLAGS_RELEASE": cmake_cache_value(
                        cache, "CMAKE_CXX_FLAGS_RELEASE"),
                },
            },
            "cmake_options": {
                "build_type": "Release",
                "generator": args.generator or "default",
                "sdl_source": str(sdl_source),
                "sdl_build": str(sdl_build),
                "libusb_source": str(libusb_source),
            },
        },
        "invocation": "webmelee-controller-probe --config <Dolphin.ini>",
    }
    (output / "build-manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(executable)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode) from error
