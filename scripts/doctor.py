#!/usr/bin/env python3
"""Read-only, task-scoped environment checks for Melee Web."""

from __future__ import annotations

import argparse
import contextlib
import json
import os
import re
import shutil
import struct
import subprocess
from pathlib import Path
from typing import Any

from bootstrap import read_lock, verify_repository
from check_gameplay import node_runtime
from extract_disc_file import DiscFormatError, DiscImage


ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "melee-web-doctor-v1"


def _run(command: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    env = dict(os.environ, GIT_OPTIONAL_LOCKS="0")
    try:
        return subprocess.run(command, cwd=cwd, text=True, capture_output=True,
                              check=False, timeout=15, env=env)
    except (OSError, subprocess.TimeoutExpired):
        return subprocess.CompletedProcess(command, 1, "", "unavailable")


def _git(root: Path, *args: str) -> str | None:
    result = _run(["git", *args], root)
    return result.stdout.strip() if result.returncode == 0 else None


def _rel(path: Path | None, root: Path) -> str | None:
    if path is None:
        return None
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except (OSError, ValueError):
        return path.name or str(path)


def _regular(path: Path) -> bool:
    return not path.is_symlink() and path.is_file()


def _executable(path: Path) -> bool:
    return _regular(path) and os.access(path, os.X_OK)


def _version(path: Path | None) -> str | None:
    if path is None or not _executable(path):
        return None
    result = _run([str(path), "--version"])
    if result.returncode:
        return None
    output = result.stdout or result.stderr
    return output.splitlines()[0].strip() if output else None


@contextlib.contextmanager
def _git_read_environment():
    previous = os.environ.get("GIT_OPTIONAL_LOCKS")
    os.environ["GIT_OPTIONAL_LOCKS"] = "0"
    try:
        yield
    finally:
        if previous is None:
            os.environ.pop("GIT_OPTIONAL_LOCKS", None)
        else:
            os.environ["GIT_OPTIONAL_LOCKS"] = previous


def _versions(lock: dict[str, Any] | None) -> dict[str, str]:
    versions: dict[str, str] = {}
    for package in (lock or {}).get("python_build_packages", []):
        if isinstance(package, str) and "==" in package:
            name, version = package.split("==", 1)
            versions[name.lower()] = version
    return versions


def _tool(root: Path, name: str, expected: str | None) -> dict[str, Any]:
    folder = "Scripts" if os.name == "nt" else "bin"
    suffix = ".exe" if os.name == "nt" else ""
    path = root / ".venv" / folder / f"{name}{suffix}"
    value = _version(path)
    record: dict[str, Any] = {
        "path": _rel(path, root), "expected_version": expected,
        "version": value, "available": value is not None,
        "project_available": value is not None,
    }
    if value is None:
        record.update(status="missing", message=f"Project-local {name} is missing or not executable.")
        return record
    match = re.search(r"\d+(?:\.\d+){1,2}", value)
    actual = match.group(0) if match else value
    record["version"] = actual
    record["version_matches_lock"] = expected is None or actual == expected
    record["status"] = "ok" if record["version_matches_lock"] else "mismatch"
    record["message"] = (f"Project-local {name} is ready."
                         if record["status"] == "ok"
                         else f"Project-local {name} is {actual}; lock requires {expected}.")
    return record


def _node(root: Path, task: str) -> dict[str, Any]:
    if task == "browser":
        path = shutil.which("node")
        if path:
            configured = Path(path).resolve()
            value = _version(configured)
            if value:
                return {"path": _rel(configured, root), "absolute_path": str(configured),
                        "project_available": True, "available": True, "version": value,
                        "status": "ok", "message": "PATH Node is ready for browser checks."}
    try:
        configured = Path(node_runtime(root))
    except (OSError, ValueError, SyntaxError, subprocess.SubprocessError) as error:
        return {"path": None, "project_available": False, "available": False,
                "status": "missing", "message": f"Configured Emscripten Node is unavailable: {error}"}
    value = _version(configured)
    record: dict[str, Any] = {"path": _rel(configured, root), "absolute_path": str(configured),
                              "project_available": value is not None,
                              "available": value is not None, "version": value}
    record.update(status="ok" if value else "missing",
                  message="Configured Emscripten Node is ready."
                  if value else "Configured Emscripten Node is missing or not executable.")
    return record


def _resolve_browser(root: Path, node: dict[str, Any], requested: Path | None) -> dict[str, Any]:
    if node.get("status") != "ok":
        return {"status": "not_run", "message": "Playwright resolver was not run because Node is unavailable."}
    script = root / "scripts/browser_tools.mjs"
    node_path = Path(node.get("absolute_path") or node["path"])
    if not node_path.is_absolute():
        node_path = root / node_path
    command = [str(node_path), str(script), "--json"]
    if requested is not None:
        command.extend(("--playwright", str(requested.expanduser())))
    result = _run(command, root)
    if result.returncode:
        message = (result.stderr or result.stdout or "Playwright resolver failed").strip()
        return {"status": "error", "message": message}
    try:
        resolved = json.loads(result.stdout)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        return {"status": "error", "message": f"Playwright resolver returned invalid JSON: {error}"}
    browser = resolved.get("browser") if isinstance(resolved, dict) else None
    package = resolved.get("playwrightPath") if isinstance(resolved, dict) else None
    valid_browser = (isinstance(browser, dict) and browser.get("channel") == "chrome") or (
        isinstance(browser, dict) and isinstance(browser.get("executablePath"), str)
        and bool(browser["executablePath"]))
    if not isinstance(package, str) or not package or not valid_browser:
        return {"status": "error",
                "message": "Playwright resolver returned no usable playwrightPath and Chrome browser configuration."}
    return {"status": "ok", "playwrightPath": package, "browser": browser,
            "message": "Playwright and a Chrome browser are configured."}


def inspect_tools(root: Path, lock: dict[str, Any] | None, task: str = "build",
                  playwright: Path | None = None) -> dict[str, Any]:
    tools: dict[str, Any] = {"node": _node(root, task)}
    if task == "build":
        versions = _versions(lock)
        tools["cmake"] = _tool(root, "cmake", versions.get("cmake"))
        tools["ninja"] = _tool(root, "ninja", versions.get("ninja"))
        failures = [name for name in ("cmake", "ninja", "node")
                    if tools[name]["status"] != "ok"]
        return {"status": "ok" if not failures else "error", "tools": tools,
                "required_failures": failures}
    if task != "browser":
        raise ValueError(f"Unsupported doctor task: {task}")
    browser = _resolve_browser(root, tools["node"], playwright)
    return {"status": "ok" if tools["node"]["status"] == "ok" and browser["status"] == "ok" else "error",
            "tools": tools, "browser": browser,
            "required_failures": (["node"] if tools["node"]["status"] != "ok" else []) +
            (["browser"] if browser["status"] != "ok" else [])}


def inspect_dependencies(root: Path, lock: dict[str, Any] | None) -> dict[str, Any]:
    if lock is None:
        return {"status": "error", "repositories": {}, "reviewed_patches": {},
                "message": "Dependency lock is unavailable; bootstrap is authoritative."}
    repositories: dict[str, Any] = {}
    deps = root / ".deps"
    for name, spec in lock.get("repositories", {}).items():
        path = deps / name
        record: dict[str, Any] = {"path": _rel(path, root), "pinned_commit": spec.get("commit"),
                                  "head": _git(path, "rev-parse", "HEAD") if path.is_dir() else None}
        try:
            with _git_read_environment():
                verify_repository(path, spec["commit"])
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            record["status"] = ("mismatch" if record["head"] else
                                 "invalid" if path.exists() else "missing")
            record["message"] = str(error)
        else:
            record.update(status="ok", message="Pinned repository HEAD matches the lock.")
        repositories[name] = record
    patches: dict[str, Any] = {}
    for name, relative in (("aurora-browser", "patches/aurora-browser.patch"),
                           ("melee-gameplay", "patches/melee-gameplay.patch")):
        path = root / relative
        present = _regular(path)
        patches[name] = {"path": relative, "status": "present" if present else "missing",
                         "application": "not_checked",
                         "message": ("Patch file is present; application/readiness is not checked here."
                                     if present else "Reviewed patch file is missing.")}
    repositories_ok = all(value["status"] == "ok" for value in repositories.values())
    patches_ok = all(value["status"] == "present" for value in patches.values())
    ready = repositories_ok and patches_ok
    return {"status": "ok" if ready else "error", "repositories": repositories,
            "reviewed_patches": patches,
            "message": ("Pinned repositories and reviewed patch files are present; bootstrap remains authoritative for application/readiness."
                         if ready else "Pinned repositories or reviewed patch files are not ready; run bootstrap for detailed diagnostics.")}


def inspect_disc(root: Path, disc: Path | None) -> dict[str, Any]:
    if disc is None:
        return {"requested": False, "status": "not_requested", "available": False,
                "path": None, "message": "No --disc was supplied."}
    path = disc.expanduser()
    report: dict[str, Any] = {"requested": True, "status": "error", "available": False,
                              "path": _rel(path, root), "format": None}
    if path.is_symlink() or not path.is_file():
        report["message"] = "Requested disc path is missing or not a regular file."
        return report
    try:
        with DiscImage(path) as image:
            report.update(status="ready", available=True,
                          format="CISO" if image._mapping is not None else "raw",
                          file_count=len(image.files()),
                          message="Readable compatible GALE01 revision 2 disc.")
    except (OSError, DiscFormatError, ValueError, struct.error) as error:
        report["message"] = f"Disc validation failed: {error}"
    return report


def inspect_build(root: Path, build: Path | None) -> dict[str, Any]:
    if build is None:
        return {"requested": False, "status": "not_requested", "freshness": "unknown", "path": None}
    path = build.expanduser()
    if path.is_symlink() or not path.is_dir():
        return {"requested": True, "status": "missing", "freshness": "unknown",
                "path": _rel(path, root),
                "message": "Requested build directory is missing or not a directory."}
    return {"requested": True, "status": "present", "freshness": "unknown",
            "path": _rel(path, root),
            "message": "Build directory is present; freshness is unknown. See docs/HITCH_CAPTURE.md."}


def run_doctor(root: Path = ROOT, *, task: str = "build", disc: Path | None = None,
               build: Path | None = None, playwright: Path | None = None) -> dict[str, Any]:
    if task not in {"build", "browser"}:
        raise ValueError(f"Unsupported doctor task: {task}")
    root = Path(root).resolve()
    failures: list[dict[str, str]] = []
    dependencies: dict[str, Any] = {"status": "not_checked", "repositories": {}, "reviewed_patches": {}}
    lock: dict[str, Any] | None = None
    if task == "build":
        try:
            lock = read_lock(root)
        except (OSError, ValueError, json.JSONDecodeError) as error:
            failures.append({"check": "dependency_lock", "message": f"Dependency lock is invalid: {error}"})
        dependencies = inspect_dependencies(root, lock)
        if dependencies["status"] != "ok":
            failures.append({"check": "dependencies", "message": dependencies["message"]})
    tools = inspect_tools(root, lock, task, playwright if task == "browser" else None)
    if tools["status"] != "ok":
        for name in tools["required_failures"]:
            value = tools["tools"].get(name, tools.get("browser", {}))
            failures.append({"check": name, "message": value.get("message", "Check failed.")})
    disc_report = inspect_disc(root, disc)
    if disc is not None and disc_report["status"] != "ready":
        failures.append({"check": "disc", "message": disc_report["message"]})
    build_report = inspect_build(root, build)
    if build is not None and build_report["status"] != "present":
        failures.append({"check": "build", "message": build_report["message"]})
    next_commands: list[str] = []
    if task == "build" and (dependencies["status"] != "ok" or tools["status"] != "ok"):
        next_commands.append("python3 scripts/bootstrap.py")
    if build_report["requested"] and build_report["status"] == "present":
        next_commands.append("Read docs/HITCH_CAPTURE.md for development build freshness checks.")
    if disc is not None and disc_report["status"] != "ready":
        next_commands.append("Provide a readable GALE01 revision 2 disc and rerun with --disc PATH.")
    browser = tools.get("browser", {"status": "not_checked"})
    return {"schema": SCHEMA, "task": task, "ok": not failures,
            "status": "pass" if not failures else "fail", "root": ".",
            "dependencies": dependencies, "tools": tools, "browser": browser,
            "disc": disc_report, "build": build_report,
            "failures": failures, "next_commands": list(dict.fromkeys(next_commands))}


def _plain(report: dict[str, Any]) -> str:
    lines = [f"Melee Web doctor ({report['task']}): {'PASS' if report['ok'] else 'FAIL'}"]
    checks = [("dependencies", report["dependencies"]), ("toolchain", report["tools"]),
              ("browser", report["browser"]), ("disc", report["disc"]), ("build", report["build"])]
    for name, value in checks:
        if value.get("status") in {"not_checked", "not_requested"}:
            continue
        detail = value.get("message", "")
        lines.append(f"{name}: {value.get('status')}" + (f" — {detail}" if detail else ""))
    if report["failures"]:
        lines.append("Issues:")
        lines.extend(f"  - {item['message']}" for item in report["failures"])
    if report["next_commands"]:
        lines.append("Next:")
        lines.extend(f"  {command}" for command in report["next_commands"])
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--task", choices=("build", "browser"), default="build")
    parser.add_argument("--json", action="store_true", help="emit a machine-readable JSON report")
    parser.add_argument("--disc", type=Path, help="optional owned GALE01 revision 2 ISO, GCM, or CISO")
    parser.add_argument("--build", type=Path, help="optional existing build directory; freshness is unknown")
    parser.add_argument("--playwright", type=Path, help="Playwright package directory for the browser task")
    args = parser.parse_args(argv)
    report = run_doctor(task=args.task, disc=args.disc, build=args.build, playwright=args.playwright)
    print(json.dumps(report, indent=2, sort_keys=True) if args.json else _plain(report))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
