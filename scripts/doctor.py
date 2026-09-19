#!/usr/bin/env python3
"""Inspect the local Melee Web development environment without changing it.

The doctor does not bootstrap, build, extract, or launch anything. It reports
what is present and prints the next command for checks that need attention.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
from typing import Any

from bootstrap import read_lock, verify_repository
from build_public import BuildError, RUNTIME_IDENTITY_SCHEMA, _read_runtime_identity, _validate_runtime_provenance
from check_gameplay import node_runtime
from extract_disc_file import DiscFormatError, DiscImage


ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "melee-web-doctor-v1"
FIGHTER_ASSETS = (
    "PlCo.dat", "PlMr.dat", "PlMrNr.dat", "PlMrAJ.dat", "GrNLa.dat",
    "ItCo.usd", "EfMrData.dat", "EfCoData.dat", "PdPm.dat", "LbRb.dat",
    "sislib_font.bin",
)
DISC_ASSETS = tuple(name for name in FIGHTER_ASSETS if name != "sislib_font.bin")


def _run(command: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    """Run a bounded read-only command used for version or Git inspection."""
    try:
        env = dict(os.environ)
        env["GIT_OPTIONAL_LOCKS"] = "0"
        return subprocess.run(command, cwd=cwd, text=True, capture_output=True,
                              check=False, timeout=10, env=env)
    except (OSError, subprocess.TimeoutExpired):
        return subprocess.CompletedProcess(command, 1, "", "unavailable")


def _git(root: Path, *args: str) -> str | None:
    result = _run(["git", *args], root)
    return result.stdout.strip() if result.returncode == 0 else None


def _regular(path: Path) -> bool:
    return not path.is_symlink() and path.is_file()


def _rel(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except (OSError, ValueError):
        return path.name or str(path)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _tool_version(path: Path | None) -> str | None:
    if path is None:
        return None
    result = _run([str(path), "--version"])
    if result.returncode:
        return None
    return (result.stdout or result.stderr).splitlines()[0].strip() if (result.stdout or result.stderr) else None


def inspect_checkout(root: Path) -> dict[str, Any]:
    top = _git(root, "rev-parse", "--show-toplevel")
    if top is None or Path(top).resolve() != root.resolve():
        return {"status": "error", "head": None, "dirty": None,
                "message": "The project root is not a standalone Git checkout."}
    head = _git(root, "rev-parse", "HEAD")
    status = _git(root, "status", "--porcelain", "--untracked-files=all")
    if head is None or status is None:
        return {"status": "error", "head": head, "dirty": None,
                "message": "Git could not report the checkout state."}
    return {"status": "dirty" if status else "ok", "head": head,
            "dirty": bool(status),
            "message": "Checkout has uncommitted changes." if status else "Checkout is clean."}


def _dependency(root: Path, name: str, spec: dict[str, Any]) -> dict[str, Any]:
    path = root / ".deps" / name
    record: dict[str, Any] = {
        "path": _rel(path, root), "url": spec.get("url"),
        "pinned_commit": spec.get("commit"), "head": None,
        "dirty": None, "status": "missing",
    }
    if (root / ".deps").is_symlink():
        record["status"] = "invalid"
        record["message"] = ".deps may not be a symlink."
        return record
    if path.is_symlink():
        record["status"] = "invalid"
        record["message"] = "Dependency checkout may not be a symlink."
        return record
    if not path.is_dir():
        record["message"] = "Dependency checkout is absent."
        return record
    record["head"] = _git(path, "rev-parse", "HEAD")
    try:
        verify_repository(path, spec["commit"])
    except (OSError, ValueError, subprocess.SubprocessError):
        if record["head"] is None:
            record["status"] = "invalid"
            record["message"] = "Dependency path is not a Git checkout."
        else:
            record["status"] = "mismatch"
            record["message"] = "Dependency HEAD differs from the lock file."
        return record
    dirty = _git(path, "status", "--porcelain", "--untracked-files=all")
    if dirty is None:
        record["status"] = "invalid"
        record["message"] = "Git could not inspect the dependency checkout."
    elif dirty:
        record.update(status="dirty", dirty=True,
                      message="Dependency checkout contains local changes.")
    else:
        record.update(status="ok", dirty=False, message="Pinned checkout is clean.")
    return record


def _reviewed_patch_state(repository: Path, patch: Path) -> tuple[str, str]:
    """Compare a reviewed patch using Git reads only.

    bootstrap.patch_state uses an isolated index, but Git may still write
    patch blobs into the dependency object database. The doctor must be safe
    to run against an owned checkout, so this equivalent check uses only
    GIT_OPTIONAL_LOCKS=0 reads and rejects staged/untracked changes.
    """
    if not _regular(patch):
        return "missing", "Reviewed patch is missing."
    staged = _git(repository, "diff", "--cached", "--name-only")
    status = _git(repository, "status", "--porcelain", "--untracked-files=all")
    if staged is None or status is None:
        return "error", "Git could not inspect the reviewed patch state."
    if staged:
        return "error", "Dependency checkout has staged changes."
    # Reproduce bootstrap.patch_state's exact working-tree comparison, but
    # place both its temporary index and any blobs created by `git add` in a
    # temporary object store. The dependency's index and object database stay
    # untouched; untracked additions remain part of the reviewed diff.
    try:
        object_dir = Path(_git(repository, "rev-parse", "--git-path", "objects") or "")
        if not object_dir.is_absolute():
            object_dir = (repository / object_dir).resolve()
        with tempfile.TemporaryDirectory(prefix="melee-doctor-git-") as temporary:
            temporary_path = Path(temporary)
            isolated_objects = temporary_path / "objects"
            isolated_objects.mkdir()
            environment = dict(os.environ)
            environment.update(
                GIT_OPTIONAL_LOCKS="0",
                GIT_INDEX_FILE=str(temporary_path / "index"),
                GIT_OBJECT_DIRECTORY=str(isolated_objects),
                GIT_ALTERNATE_OBJECT_DIRECTORIES=str(object_dir),
            )
            def git_read(*args: str) -> subprocess.CompletedProcess[bytes]:
                return subprocess.run(["git", *args], cwd=repository, env=environment,
                                      capture_output=True, check=False, timeout=10)
            if git_read("read-tree", "HEAD").returncode:
                return "error", "Git could not prepare the isolated patch index."
            if git_read("add", "--all", "--", ".").returncode:
                return "error", "Git could not inspect the dependency working tree."
            actual = git_read("diff", "--no-ext-diff", "--no-color", "--cached", "--binary", "HEAD")
            if actual.returncode:
                return "error", "Git could not read the isolated dependency diff."
            expected = patch.read_bytes()
    except (OSError, subprocess.SubprocessError):
        return "error", "Git could not inspect the reviewed patch state."
    if actual.stdout == expected:
        return "applied", "Reviewed patch is applied exactly."
    if not actual.stdout:
        return "clean", "Dependency checkout is unpatched; bootstrap is required."
    return "error", "Dependency changes differ from the reviewed patch."


def inspect_dependencies(root: Path, lock: dict[str, Any] | None) -> dict[str, Any]:
    if lock is None:
        return {"status": "error", "repositories": {}, "reviewed_patches": {},
                "message": "The dependency lock could not be read."}
    repositories = {name: _dependency(root, name, spec)
                   for name, spec in lock["repositories"].items()}
    patches: dict[str, Any] = {}
    aurora_patch = root / "patches/aurora-browser.patch"
    melee_patch = root / "patches/melee-gameplay.patch"
    patches["aurora-browser"] = {"path": _rel(aurora_patch, root),
                                  "status": "present" if _regular(aurora_patch) else "missing",
                                  "state": None}
    patches["melee-gameplay"] = {"path": _rel(melee_patch, root),
                                  "status": "present" if _regular(melee_patch) else "missing",
                                  "state": None}
    aurora = root / ".deps/aurora"
    if patches["aurora-browser"]["status"] == "present" and aurora.is_dir() and not aurora.is_symlink():
        try:
            state, message = _reviewed_patch_state(aurora, aurora_patch)
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            state, message = "error", str(error)
        patches["aurora-browser"]["state"] = state
        patches["aurora-browser"]["applied"] = state == "applied"
        patches["aurora-browser"]["message"] = message
    else:
        patches["aurora-browser"]["message"] = "Aurora checkout is unavailable for patch verification."
    patches["melee-gameplay"]["message"] = (
        "Reviewed gameplay patch is present; it is applied to build/gameplay-source."
        if patches["melee-gameplay"]["status"] == "present"
        else "Reviewed gameplay patch is missing."
    )
    repos_ok = all(value["status"] == "ok" for value in repositories.values())
    # Aurora's reviewed patch intentionally makes its checkout dirty. That is
    # the one dependency state accepted by bootstrap; patch_state is the gate.
    aurora_ok = patches["aurora-browser"].get("state") == "applied"
    repos_ok = repos_ok or (
        repositories.get("aurora", {}).get("status") == "dirty"
        and all(value["status"] == "ok" for name, value in repositories.items() if name != "aurora")
        and aurora_ok
    )
    ready = repos_ok and aurora_ok and patches["melee-gameplay"]["status"] == "present"
    return {
        "status": "ok" if ready else "error",
        "emscripten": lock.get("emscripten"),
        "python_build_packages": lock.get("python_build_packages", []),
        "reference_tools": {name: {"commit": spec.get("commit"), "version": spec.get("version")}
                            for name, spec in lock.get("reference_tools", {}).items()},
        "repositories": repositories, "reviewed_patches": patches,
        "message": "Pinned repositories and reviewed patches are ready." if ready
                   else "Pinned dependencies or reviewed patches are not ready.",
    }


def _pinned_versions(lock: dict[str, Any] | None) -> dict[str, str]:
    result: dict[str, str] = {}
    for package in (lock or {}).get("python_build_packages", []):
        if isinstance(package, str) and "==" in package:
            name, version = package.split("==", 1)
            result[name.lower()] = version
    return result


def _tool(root: Path, name: str, lock: dict[str, Any] | None) -> dict[str, Any]:
    bins = root / ".venv" / ("Scripts" if os.name == "nt" else "bin")
    project = bins / (name + (".exe" if os.name == "nt" else ""))
    system_name = shutil.which(name)
    system = Path(system_name) if system_name else None
    project_version = _tool_version(project) if _regular(project) else None
    system_version = _tool_version(system) if system else None
    record: dict[str, Any] = {
        "required": True, "project_path": _rel(project, root),
        "project_available": project_version is not None,
        "system_available": system_version is not None,
        "project_version": project_version, "system_version": system_version,
        "path": _rel(project if project_version else system, root) if (project_version or system) else None,
        "available": bool(project_version or system_version),
        "status": "missing", "expected_version": _pinned_versions(lock).get(name),
    }
    if not project_version:
        record["status"] = "fallback" if system_version else "missing"
        record["message"] = ("Global tool is visible, but build.py requires the project-local pinned tool."
                             if system_version else "Project-local pinned tool is unavailable.")
        record["version"] = system_version
        return record
    actual = project_version
    if name == "cmake":
        match = re.search(r"version\s+([0-9]+(?:\.[0-9]+){1,2})", actual)
        actual = match.group(1) if match else actual
    elif name == "ninja":
        match = re.match(r"([0-9]+(?:\.[0-9]+){1,2})", actual)
        actual = match.group(1) if match else actual
    record["version"] = actual
    record["version_matches_lock"] = record["expected_version"] in (None, actual)
    record["status"] = "ok" if record["version_matches_lock"] else "mismatch"
    return record


def _playwright(root: Path, requested: Path | None) -> dict[str, Any]:
    if requested is not None:
        requested = requested.expanduser()
        if (requested.is_symlink() or not requested.is_dir() or
                not _regular(requested / "index.mjs") or not _regular(requested / "package.json")):
            return {"required": True, "status": "error", "package_available": False,
                    "package_path": _rel(requested, root), "version": None,
                    "browser_available": False,
                    "message": "Requested Playwright package directory is missing or invalid."}
    candidates = [requested] if requested else []
    if os.environ.get("MELEE_PLAYWRIGHT_DIR"):
        candidates.append(Path(os.environ["MELEE_PLAYWRIGHT_DIR"]).expanduser())
    candidates.extend((root / "node_modules/playwright", root / "work/deploy-tools/node_modules/playwright"))
    package = next((path for path in candidates if path.is_dir() and not path.is_symlink()
                    and _regular(path / "index.mjs") and _regular(path / "package.json")), None)
    record: dict[str, Any] = {
        "required": False, "status": "available" if package else "unavailable",
        "package_available": package is not None,
        "package_path": _rel(package, root) if package else None,
        "version": None, "browser_available": False,
        "message": "No Playwright package directory was found; browser checks are optional."
                   if package is None else "Playwright package is available; no browser was launched.",
    }
    if package:
        try:
            data = json.loads((package / "package.json").read_text(encoding="utf-8"))
            record["version"] = data.get("version") if isinstance(data, dict) else None
        except (OSError, UnicodeDecodeError, json.JSONDecodeError):
            record["message"] = "Playwright package metadata is unreadable."
    browser_paths = []
    if os.environ.get("MELEE_BROWSER_PATH"):
        browser_paths.append(Path(os.environ["MELEE_BROWSER_PATH"]).expanduser())
    browser_paths.extend(Path(value) for name in ("google-chrome", "chromium", "chromium-browser", "chrome")
                         if (value := shutil.which(name)))
    if sys.platform == "darwin":
        browser_paths.append(Path("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"))
    browser = next((path for path in browser_paths if _regular(path)), None)
    record["browser_available"] = browser is not None
    record["browser_path"] = _rel(browser, root) if browser else None
    if package and browser is None:
        record["message"] = "Playwright is available; no browser executable was found without launching one."
    return record


def inspect_tools(root: Path, lock: dict[str, Any] | None, playwright: Path | None = None) -> dict[str, Any]:
    tools = {name: _tool(root, name, lock) for name in ("cmake", "ninja")}
    try:
        node = node_runtime(root)
    except (OSError, ValueError, SyntaxError):
        node = None
    node_record = _tool(root, "node", lock)
    node_version = _tool_version(node) if node else None
    node_record.update(project_available=node_version is not None,
                       project_path=_rel(node, root) if node else None,
                       project_version=node_version)
    if node and node_version:
        node_record.update(path=_rel(node, root), available=True, status="ok",
                           version=node_version,
                           message="Project Emscripten Node is configured.")
    else:
        node_record["status"] = "missing"
        node_record["message"] = "Project Emscripten Node is unavailable or not executable."
    tools["node"] = node_record
    tools["python"] = {
        "required": True, "status": "ok", "available": True,
        "path": Path(sys.executable).name, "version": sys.version.split()[0],
    }
    tools["browser_automation"] = _playwright(root, playwright)
    failures = [name for name in ("cmake", "ninja", "node") if tools[name]["status"] != "ok"]
    return {"status": "ok" if not failures else "error", "tools": tools,
            "required_failures": failures}


def inspect_assets(root: Path, assets: Path | None = None) -> dict[str, Any]:
    requested = assets.expanduser() if assets else root / "assets-local"
    if requested.is_symlink() or not requested.is_dir():
        return {"status": "unavailable", "path": _rel(requested, root), "available": [],
                "missing": list(FIGHTER_ASSETS), "invalid": [],
                "requested": assets is not None,
                "message": "No extracted asset directory is available; this does not establish that an owned disc is absent."}
    directory = requested / "next-gate" if assets is None and (requested / "next-gate").is_dir() else requested
    available, missing, invalid = [], [], []
    for name in FIGHTER_ASSETS:
        path = directory / name
        if _regular(path):
            available.append(name)
        elif path.exists() or path.is_symlink():
            invalid.append(name)
        else:
            missing.append(name)
    state = "ready" if not missing and not invalid else "partial" if available else "unavailable"
    return {"status": state, "path": _rel(directory, root), "available": available,
            "missing": missing, "invalid": invalid, "requested": assets is not None,
            "message": "Extracted asset set is available." if state == "ready" else "Some optional extracted assets are unavailable."}


def inspect_disc(root: Path, disc: Path | None) -> dict[str, Any]:
    if disc is None:
        return {"requested": False, "status": "unavailable", "available": False,
                "proof_of_absence": False, "path": None,
                "message": "No --disc was supplied; disc availability is unknown."}
    path = disc.expanduser()
    result: dict[str, Any] = {"requested": True, "status": "unavailable", "available": False,
                              "proof_of_absence": False, "path": _rel(path, root),
                              "format": None, "required_files": {}}
    if path.is_symlink() or not path.is_file():
        result["message"] = "Requested disc path is missing or not a regular file."
        return result
    try:
        with DiscImage(path) as image:
            files = image.files()
            matches = {name: [item for item in files if item == name or Path(item).name == name]
                       for name in DISC_ASSETS}
            result.update(format="CISO" if image._mapping is not None else "raw",
                          available=True, file_count=len(files),
                          required_files={name: {"available": bool(paths), "matches": paths}
                                          for name, paths in matches.items()})
            result["status"] = "ready" if all(matches.values()) else "partial"
            result["message"] = ("Owned GALE01 revision 2 disc is readable." if result["status"] == "ready"
                                  else "Disc is valid but does not contain every runtime file needed by the fighter gate.")
    except (OSError, DiscFormatError, ValueError, struct.error) as error:
        result["message"] = f"Disc validation failed: {error}"
    return result


def _identity_path(root: Path, value: object) -> Path | None:
    if not isinstance(value, str) or not value or "\\" in value:
        return None
    path = Path(value)
    if path.is_absolute() or path.as_posix() != value or any(part in ("", ".", "..") for part in path.parts):
        return None
    result = root / path
    try:
        return result if result.resolve().is_relative_to(root.resolve()) else None
    except (OSError, AttributeError):
        return None


def inspect_build(root: Path, build: Path | None) -> dict[str, Any]:
    if build is None:
        return {"requested": False, "status": "not_requested", "staleness": "unknown", "path": None}
    requested = build.expanduser()
    report: dict[str, Any] = {"requested": True, "path": _rel(requested, root),
                              "status": "missing", "staleness": "unknown", "identity": None}
    if requested.is_symlink() or not requested.exists():
        report["message"] = "Requested build path is missing or a symlink."
        return report
    identity_path = requested if requested.is_file() else requested.parent / "runtime-public-identity.json"
    if requested.is_dir() and not identity_path.is_file():
        identity_path = root / "build/runtime-public-identity.json"
    if not identity_path.is_file() or identity_path.is_symlink():
        artifact_root = requested.parent if requested.is_file() else requested
        report.update(artifact_root=_rel(artifact_root, root), identity_path=_rel(identity_path, root),
                      configured=_regular(artifact_root / "CMakeCache.txt"), status="unverifiable",
                      message="No producer identity sidecar is available; build staleness is unknown.")
        return report
    try:
        identity = json.loads(identity_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        report.update(status="invalid", message=f"Producer identity is unreadable: {error}")
        return report
    if not isinstance(identity, dict):
        report.update(status="invalid", message="Producer identity is not a JSON object.")
        return report
    report["identity"] = {key: identity.get(key) for key in ("schema", "target", "configuration", "artifact_root")}
    artifact_root = _identity_path(root, identity.get("artifact_root"))
    if artifact_root is None:
        report.update(status="unknown", message="Producer identity artifact root is invalid; staleness is unknown.")
        return report
    report.update(artifact_root=_rel(artifact_root, root), identity_path=_rel(identity_path, root),
                  configured=_regular(artifact_root / "CMakeCache.txt"))
    if requested.is_dir() and artifact_root.resolve() != requested.resolve():
        report.update(status="mismatch", message="Producer identity belongs to a different artifact root; staleness is unknown.")
        return report
    if identity.get("schema") != RUNTIME_IDENTITY_SCHEMA:
        report.update(status="unknown", message="Producer identity schema is unsupported; staleness is unknown.")
        return report
    # The producer helpers verify every source file/tree, prepared gameplay
    # patch, tool hash, pipeline seed, and runtime artifact. A commit-only or
    # partial identity intentionally remains unknown here.
    if root.resolve() != ROOT.resolve():
        report.update(status="unknown", message="Build identity cannot be checked against this checkout; staleness is unknown.")
        return report
    try:
        canonical, _files, _runtime_hash, canonical_bytes = _read_runtime_identity(artifact_root)
        if canonical != identity or canonical_bytes != identity_path.read_bytes():
            report.update(status="unknown", message="Requested identity does not match the canonical producer sidecar; staleness is unknown.")
            return report
        _validate_runtime_provenance(canonical)
    except BuildError as error:
        message = str(error)
        stale = "differs from current" in message or "differs from the current" in message
        report.update(status="stale" if stale else "unknown", staleness="stale" if stale else "unknown",
                      message=message if stale else f"Build identity could not be fully verified; staleness is unknown ({message}).")
        return report
    report.update(status="ready", staleness="current", message="Producer identity and all recorded provenance match this checkout.")
    return report


def _commands(assets: dict[str, Any], disc: Path | None, build: Path | None,
              disc_available: bool = False) -> list[str]:
    commands: list[str] = []
    if assets.get("requested") and (assets.get("missing") or assets.get("invalid")):
        if disc is None or not disc_available:
            commands.append("Provide an owned GALE01 revision 2 disc, then rerun: python3 scripts/doctor.py --disc /path/to/owned-disc")
        else:
            image = shlex.quote(str(disc.expanduser()))
            for name in assets.get("missing", []):
                commands.append((f"python3 scripts/extract_font_atlas.py {image} --output assets-local/next-gate/{name}"
                                 if name == "sislib_font.bin" else
                                 f"python3 scripts/extract_disc_file.py {image} {name} --output assets-local/next-gate/{name}"))
    if build is not None:
        name = build.expanduser().name
        commands.append("python3 scripts/build.py --target runtime-public --configuration Release"
                        if name == "browser-public-release" else
                        "python3 scripts/build.py --target runtime --configuration Release"
                        if name in {"browser-release", "browser"} else "python3 scripts/build.py")
    return commands


def run_doctor(root: Path = ROOT, *, disc: Path | None = None, build: Path | None = None,
               assets: Path | None = None, playwright: Path | None = None) -> dict[str, Any]:
    root = Path(root).resolve()
    failures: list[dict[str, str]] = []
    warnings: list[dict[str, str]] = []
    checkout = inspect_checkout(root)
    if checkout["status"] == "error":
        failures.append({"check": "checkout", "message": checkout["message"]})
    elif checkout.get("dirty"):
        warnings.append({"check": "checkout", "message": "Checkout is dirty; the doctor did not modify it."})
    lock: dict[str, Any] | None = None
    try:
        lock = read_lock(root)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        failures.append({"check": "dependency_lock", "message": f"Dependency lock is invalid: {error}"})
    dependencies = inspect_dependencies(root, lock)
    if dependencies["status"] != "ok":
        failures.append({"check": "dependencies", "message": dependencies["message"]})
    tools = inspect_tools(root, lock, playwright)
    if tools["status"] != "ok":
        failures.append({"check": "toolchain", "message": "Project-local pinned CMake, Ninja, and Emscripten Node are not ready."})
    disc_report = inspect_disc(root, disc)
    if disc is not None and not disc_report["available"]:
        failures.append({"check": "disc", "message": disc_report["message"]})
    assets_report = inspect_assets(root, assets)
    if assets is not None and assets_report["status"] != "ready":
        failures.append({"check": "assets", "message": "Requested extracted asset directory is not complete."})
    build_report = inspect_build(root, build)
    if build is not None and build_report["status"] != "ready":
        failures.append({"check": "build", "message": build_report["message"]})
    browser = tools["tools"]["browser_automation"]
    if browser["status"] != "available":
        if playwright is not None:
            failures.append({"check": "browser_automation", "message": browser["message"]})
        else:
            warnings.append({"check": "browser_automation", "message": browser["message"]})
    rebuild = build if build_report["status"] in {"missing", "invalid", "mismatch", "stale"} else None
    commands = _commands(assets_report, disc, rebuild, bool(disc_report["available"]))
    if build_report["status"] in {"unknown", "unverifiable"}:
        commands.append("Inspect the producer receipt; public Release identity uses build/runtime-public-identity.json. For development captures, follow docs/HITCH_CAPTURE.md.")
    if dependencies["status"] != "ok" or tools["status"] != "ok":
        commands.insert(0, "python3 scripts/bootstrap.py")
    if disc is not None and not disc_report["available"]:
        commands.append("Rerun with an owned GALE01 revision 2 ISO, GCM, or Wii-style CISO.")
    if browser["status"] != "available":
        commands.append("MELEE_PLAYWRIGHT_DIR=/path/to/playwright python3 scripts/doctor.py")
    return {
        "schema": SCHEMA, "ok": not failures, "status": "pass" if not failures else "fail", "root": ".",
        "checkout": checkout, "dependencies": dependencies, "tools": tools,
        "disc": disc_report, "assets": assets_report, "build": build_report,
        "failures": failures, "warnings": warnings, "next_commands": list(dict.fromkeys(commands)),
    }


def _plain(report: dict[str, Any]) -> str:
    lines = [f"Melee Web doctor: {report['status'].upper()}"]
    checkout = report["checkout"]
    state = "dirty" if checkout.get("dirty") else "clean" if checkout.get("dirty") is False else "unavailable"
    lines.append(f"Checkout: HEAD {checkout.get('head') or 'unknown'}; {state}")
    dependencies = report["dependencies"]
    lines.append(f"Dependencies: {dependencies['status']}")
    for name, value in dependencies.get("repositories", {}).items():
        lines.append(f"  {name}: {value['status']} ({value.get('head') or value.get('pinned_commit') or 'unknown'})")
    for name, value in dependencies.get("reviewed_patches", {}).items():
        lines.append(f"  patch {name}: {value.get('state') or value['status']}")
    lines.append(f"Toolchain: {report['tools']['status']}")
    for name, value in report["tools"]["tools"].items():
        lines.append(f"  {name}: {value['status']}" + (f" ({value['version']})" if value.get("version") else ""))
    lines.append(f"Disc: {report['disc']['status']} ({report['disc']['message']})")
    assets = report["assets"]
    lines.append(f"Assets: {assets['status']} ({len(assets.get('available', []))}/{len(FIGHTER_ASSETS)} expected files)")
    build = report["build"]
    lines.append("Build: not requested" if not build["requested"] else
                 f"Build: {build['status']}; staleness {build['staleness']} ({build.get('message', '')})")
    if report["failures"]:
        lines.append("Failures:")
        lines.extend(f"  - {failure['message']}" for failure in report["failures"])
    if report["next_commands"]:
        lines.append("Next commands:")
        lines.extend(f"  {command}" for command in report["next_commands"])
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="emit a machine-readable JSON report")
    parser.add_argument("--disc", type=Path, help="optional owned GALE01 revision 2 ISO, GCM, or CISO")
    parser.add_argument("--build", type=Path, help="optional existing build directory or identity path")
    parser.add_argument("--assets", type=Path, help="optional extracted asset directory (default: assets-local)")
    parser.add_argument("--playwright", type=Path, help="optional installed Playwright package directory")
    args = parser.parse_args(argv)
    report = run_doctor(disc=args.disc, build=args.build, assets=args.assets, playwright=args.playwright)
    print(json.dumps(report, indent=2, sort_keys=True) if args.json else _plain(report))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
