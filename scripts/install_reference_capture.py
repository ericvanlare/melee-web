#!/usr/bin/env python3
"""Build and install the native WebMelee reference-capture application.

The installer has a deliberately small packaging boundary.  It compiles the
AppKit front end, copies the Python supervisor and its source-only resources,
and records the exact Python executable used to run it.  Game discs, Dolphin
state, controller payloads and the user's private configuration live outside
the app bundle and are never discovered by walking the source tree.
"""

from __future__ import annotations

import argparse
from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
import platform
import plistlib
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Iterable, Sequence


ROOT = Path(__file__).resolve().parents[1]
APP_NAME = "WebMelee Reference Capture.app"
EXECUTABLE_NAME = "WebMelee Reference Capture"
IDENTITY_SCHEMA = "webmelee-reference-capture-identity-v1"
PYTHON_SCHEMA = "webmelee-reference-capture-python-v1"
RUNTIME_SCHEMA = "webmelee-reference-capture-runtime-v1"
NOTICE_NAME = "DOLPHIN-NOTICE.txt"

# Payloads and local state are intentionally excluded even if a caller points
# --runtime-root at a broad checkout.  The list is path based in addition to
# the extension checks, so an accidentally renamed disc image is still not
# copied into a distributable app.
FORBIDDEN_DIRECTORY_NAMES = {
    ".deps",
    ".git",
    "assets-local",
    "captures",
    "private",
    "secrets",
    "save-states",
    "states",
}
FORBIDDEN_FILE_NAMES = {
    "config.json",
    "dolphin.ini",
    "memorycard.raw",
    "memorycard_a.raw",
    "memorycard_b.raw",
}
FORBIDDEN_SUFFIXES = {
    ".ciso",
    ".dol",
    ".elf",
    ".gcm",
    ".iso",
    ".raw",
    ".rvz",
    ".slp",
    ".dtm",
    ".mwri",
    ".gci",
    ".sav",
}


class InstallError(RuntimeError):
    """A user-facing packaging or installation failure."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def resolve_python(python: str | os.PathLike[str] | None) -> Path:
    candidate = Path(python).expanduser() if python else None
    if candidate is None:
        found = shutil.which("python3")
        if not found:
            raise InstallError("No python3 executable was found; pass --python explicitly.")
        candidate = Path(found)
    candidate = candidate.resolve()
    if not candidate.is_file() or not os.access(candidate, os.X_OK):
        raise InstallError(f"Python executable is not runnable: {candidate}")
    return candidate


def python_metadata(python: Path) -> dict[str, str]:
    try:
        result = subprocess.run(
            [str(python), "--version"],
            check=True,
            capture_output=True,
            text=True,
            timeout=10,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        raise InstallError(f"Could not inspect the selected Python executable: {exc}") from exc
    version = (result.stdout or result.stderr).strip()
    if not version.startswith("Python "):
        raise InstallError(f"The selected executable did not report a Python version: {version!r}")
    return {
        "schema": PYTHON_SCHEMA,
        "path": str(python),
        "version": version.removeprefix("Python ").strip(),
        "sha256": sha256_file(python),
    }


def _reject_runtime_path(relative: Path) -> None:
    parts = {part.lower() for part in relative.parts}
    if parts & FORBIDDEN_DIRECTORY_NAMES:
        raise InstallError(f"Runtime payload includes private or game data: {relative}")
    name = relative.name.lower()
    if name in FORBIDDEN_FILE_NAMES or any(name.endswith(suffix) for suffix in FORBIDDEN_SUFFIXES):
        raise InstallError(f"Runtime payload includes a forbidden game/local file: {relative}")


def runtime_files(runtime_root: Path) -> list[Path]:
    runtime_root = runtime_root.expanduser().resolve()
    if not runtime_root.is_dir():
        raise InstallError(f"Runtime root is not a directory: {runtime_root}")
    files: list[Path] = []
    for path in sorted(runtime_root.rglob("*")):
        relative = path.relative_to(runtime_root)
        _reject_runtime_path(relative)
        if path.is_symlink():
            raise InstallError(f"Runtime symlinks are not allowed in an app bundle: {relative}")
        if path.is_dir():
            continue
        if not path.is_file():
            raise InstallError(f"Runtime entry is not a regular file: {relative}")
        if path.name == "__pycache__" or path.suffix == ".pyc":
            continue
        files.append(path)
    required = runtime_root / "scripts/reference_capture_app.py"
    if not required.is_file():
        raise InstallError("Runtime root must contain scripts/reference_capture_app.py")
    return files


def copy_runtime(runtime_root: Path, destination: Path) -> list[dict[str, str | int]]:
    records: list[dict[str, str | int]] = []
    source_root = runtime_root.expanduser().resolve()
    for source in runtime_files(source_root):
        relative = source.relative_to(source_root)
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        records.append({
            "path": str(relative),
            "sha256": sha256_file(target),
            "bytes": target.stat().st_size,
        })
    return records


def materialize_source_runtime(source_root: Path, destination: Path) -> Path:
    """Make the app's source-only Python runtime without executing scripts.

    Only the Python modules that can be imported by the supervisor are copied.
    In particular this does not invoke historical capture scripts, discover
    user paths, or copy files from assets-local/.  An explicit --runtime-root
    remains available for release builds that have a smaller reviewed set.
    """
    destination.mkdir(parents=True, exist_ok=True)
    roots = (source_root / "scripts", source_root / "tools", source_root / "reference-capture/dolphin")
    for root in roots:
        if not root.is_dir():
            continue
        relative_root = root.relative_to(source_root)
        for source in sorted(root.rglob("*.py")):
            if source.is_symlink():
                raise InstallError(f"Source runtime symlinks are not allowed: {source}")
            target = destination / relative_root / source.relative_to(root)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    required = destination / "scripts/reference_capture_app.py"
    if not required.is_file():
        raise InstallError("The source checkout does not contain scripts/reference_capture_app.py")
    return destination


def _run_checked(command: Sequence[str], *, cwd: Path | None = None) -> None:
    try:
        subprocess.run(command, cwd=cwd, check=True)
    except OSError as exc:
        raise InstallError(f"Could not run {command[0]}: {exc}") from exc
    except subprocess.CalledProcessError as exc:
        raise InstallError(f"Build command failed with exit code {exc.returncode}: {' '.join(command)}") from exc


def find_swiftc(explicit: str | None) -> tuple[str, str | None]:
    if explicit:
        return explicit, None
    if sys.platform != "darwin":
        found = shutil.which("swiftc")
        if not found:
            raise InstallError("AppKit compilation requires macOS and swiftc.")
        return found, None
    xcrun = shutil.which("xcrun")
    if not xcrun:
        raise InstallError("Xcode command line tools are required to compile the AppKit app.")
    try:
        swiftc = subprocess.run([xcrun, "--sdk", "macosx", "--find", "swiftc"], check=True, capture_output=True, text=True).stdout.strip()
        sdk = subprocess.run([xcrun, "--sdk", "macosx", "--show-sdk-path"], check=True, capture_output=True, text=True).stdout.strip()
    except subprocess.SubprocessError as exc:
        raise InstallError(f"Could not locate the macOS SDK: {exc}") from exc
    if not swiftc or not sdk:
        raise InstallError("xcrun did not return a Swift compiler and macOS SDK.")
    return swiftc, sdk


def compile_app(source: Path, destination: Path, *, swiftc: str | None, sdk: str | None, architectures: Sequence[str]) -> None:
    compiler, located_sdk = find_swiftc(swiftc)
    sdk = sdk or located_sdk
    if not source.is_file():
        raise InstallError(f"App source is missing: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    outputs: list[Path] = []
    for architecture in architectures:
        output = destination.with_name(f"{destination.name}.{architecture}")
        command = [compiler, "-O", "-parse-as-library", "-module-name", "ReferenceCapture", "-target", f"{architecture}-apple-macosx12.0"]
        if sdk:
            command.extend(["-sdk", sdk])
        command.extend(["-framework", "AppKit", "-framework", "CryptoKit", str(source), "-o", str(output)])
        _run_checked(command)
        outputs.append(output)
    if len(outputs) == 1:
        outputs[0].replace(destination)
    else:
        lipo = shutil.which("lipo")
        if not lipo:
            raise InstallError("A universal build requires lipo.")
        _run_checked([lipo, "-create", *map(str, outputs), "-output", str(destination)])
        for output in outputs:
            output.unlink(missing_ok=True)
    destination.chmod(0o755)


def _plist() -> bytes:
    return plistlib.dumps({
        "CFBundleDisplayName": "WebMelee Reference Capture",
        "CFBundleExecutable": EXECUTABLE_NAME,
        "CFBundleIdentifier": "org.webmelee.reference-capture",
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": "WebMelee Reference Capture",
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": "0.2.0",
        "CFBundleVersion": "3",
        "NSDocumentsFolderUsageDescription": "Read the already-configured private prepared save fixture without copying or changing it.",
        "LSMinimumSystemVersion": "12.0",
        "NSHighResolutionCapable": True,
        "NSHumanReadableCopyright": "WebMelee reference tooling; see the bundled notices.",
    })


def dolphin_notice() -> str:
    return (
        "WebMelee Reference Capture can use Dolphin Emulator as a separate, user-selected "
        "original-game reference process. No game disc, memory card, save state, or Dolphin "
        "binary is bundled by this installer.\n\n"
        "Dolphin Emulator is licensed under GPL-2.0-or-later. Source code and license: "
        "https://github.com/dolphin-emu/dolphin\n"
        "This notice is provided for the application integration boundary.\n"
    )


def create_bundle(staging_app: Path, *, runtime_root: Path, python: Path, swift_source: Path,
                  swiftc: str | None, sdk: str | None, architectures: Sequence[str]) -> None:
    contents = staging_app / "Contents"
    macos = contents / "MacOS"
    resources = contents / "Resources"
    macos.mkdir(parents=True, exist_ok=True)
    resources.mkdir(parents=True, exist_ok=True)
    executable = macos / EXECUTABLE_NAME
    compile_app(swift_source, executable, swiftc=swiftc, sdk=sdk, architectures=architectures)
    (contents / "Info.plist").write_bytes(_plist())
    runtime_records = copy_runtime(runtime_root, resources / "runtime")
    python_manifest = resources / "python-runtime.json"
    python_manifest.write_text(
        json.dumps(python_metadata(python), indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    (resources / NOTICE_NAME).write_text(dolphin_notice(), encoding="utf-8")
    identity = {
        "schema": IDENTITY_SCHEMA,
        "application": "WebMelee Reference Capture",
        "version": "0.2.0",
        "app_code_sha256": sha256_file(executable),
        "python_runtime_sha256": sha256_file(python_manifest),
        "runtime_schema": RUNTIME_SCHEMA,
        "runtime_files": runtime_records,
        "generated_at": int(time.time()),
    }
    (resources / "identity.json").write_text(json.dumps(identity, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def create_finder_alias(app_path: Path, desktop: Path | None = None) -> bool:
    """Create a real Finder alias on Desktop, never a symlink.

    The compiled app uses Foundation bookmark data, so installation does not
    require Finder automation permission.  On macOS an existing item is
    replaced only when it is already an alias; a user's ordinary file is left
    untouched and reported as an installation error.
    """
    if sys.platform != "darwin":
        return False
    desktop = desktop or (Path.home() / "Desktop")
    desktop.mkdir(parents=True, exist_ok=True)
    executable = app_path / "Contents" / "MacOS" / EXECUTABLE_NAME
    if not executable.is_file():
        raise InstallError("The installed application executable is missing; cannot create its alias.")
    alias_path = desktop / APP_NAME
    try:
        subprocess.run([str(executable), "--create-desktop-alias", str(alias_path)],
                       check=True, capture_output=True, text=True)
    except subprocess.CalledProcessError as exc:
        detail = (exc.stderr or exc.stdout or "").strip()
        raise InstallError(f"Could not create the Desktop Finder alias{': ' + detail if detail else ''}") from exc
    except OSError as exc:
        raise InstallError(f"Could not create the Desktop Finder alias: {exc}") from exc
    return True


def relaunch_app(app_path: Path) -> bool:
    """Ask LaunchServices to focus/relaunch one installed app instance."""
    if sys.platform != "darwin":
        return False
    opener = shutil.which("open")
    if not opener:
        raise InstallError("open is required to relaunch the installed application.")
    try:
        # The installation lock requires the prior application to be closed.
        # Do not use open -n, which can create duplicate windows/backends.
        subprocess.run([opener, str(app_path)], check=True, capture_output=True, text=True)
    except subprocess.CalledProcessError as exc:
        detail = (exc.stderr or exc.stdout or "").strip()
        raise InstallError(f"Could not relaunch the installed application{': ' + detail if detail else ''}") from exc
    return True


def atomic_install(staging_app: Path, destination: Path) -> Path:
    """Atomically replace destination and restore the old app on failure."""
    destination = destination.expanduser().resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    backup: Path | None = None
    had_previous = destination.exists()
    if had_previous:
        backup = destination.with_name(f".{destination.name}.previous-{os.getpid()}")
        if backup.exists():
            shutil.rmtree(backup)
        os.replace(destination, backup)
    try:
        os.replace(staging_app, destination)
        if not destination.is_dir() or not (destination / "Contents/Info.plist").is_file():
            raise InstallError("The installed application bundle is incomplete.")
    except Exception:
        if destination.exists():
            shutil.rmtree(destination)
        if backup is not None and backup.exists():
            os.replace(backup, destination)
        elif not had_previous:
            destination.unlink(missing_ok=True)
        raise
    if backup is not None and backup.exists():
        shutil.rmtree(backup)
    return destination


@contextmanager
def installation_guard(root: Path | None = None):
    root = root or Path.home() / "Library/Application Support/WebMelee Reference Capture"
    root.mkdir(mode=0o700, parents=True, exist_ok=True)
    with (root / "application.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise InstallError("Quit WebMelee Reference Capture before installing an update. "
                               "An open capture must finish or be stopped first.") from error
        try:
            yield
        finally:
            fcntl.flock(lock, fcntl.LOCK_UN)


def install(*, destination: Path, runtime_root: Path | None, python: Path, swift_source: Path,
            swiftc: str | None = None, sdk: str | None = None,
            architectures: Sequence[str] = ("arm64",), create_alias: bool = True,
            relaunch: bool = False) -> Path:
    destination = destination.expanduser().resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary_parent = Path(tempfile.mkdtemp(prefix=".webmelee-reference-capture-", dir=str(destination.parent)))
    staging_app = temporary_parent / APP_NAME
    source_runtime = temporary_parent / "source-runtime"
    try:
        if runtime_root is None:
            runtime_root = materialize_source_runtime(ROOT, source_runtime)
        create_bundle(staging_app, runtime_root=runtime_root, python=python, swift_source=swift_source,
                      swiftc=swiftc, sdk=sdk, architectures=architectures)
        with installation_guard():
            installed = atomic_install(staging_app, destination)
    finally:
        shutil.rmtree(temporary_parent, ignore_errors=True)
    if create_alias:
        create_finder_alias(installed)
    if relaunch:
        relaunch_app(installed)
    return installed


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-root", type=Path, help="Reviewed source-only runtime directory containing scripts/reference_capture_app.py (defaults to scripts/, tools/ and reference-capture/dolphin/ in this checkout)")
    parser.add_argument("--python", dest="python_path", help="Exact Python executable to pin in the app bundle")
    parser.add_argument("--destination", type=Path, help="Installed .app path (defaults to ~/Applications/WebMelee Reference Capture.app)")
    parser.add_argument("--swiftc", help="Swift compiler override for build/test hosts")
    parser.add_argument("--sdk", help="macOS SDK path override")
    parser.add_argument("--arch", action="append", dest="architectures", choices=("arm64", "x86_64"), help="Build architecture; repeat for a universal app")
    parser.add_argument("--skip-alias", action="store_true", help="Do not create the normal Desktop Finder alias")
    parser.add_argument("--relaunch", action="store_true", help="Ask LaunchServices to relaunch/focus the installed single app instance")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    python = resolve_python(args.python_path)
    destination = args.destination or (Path.home() / "Applications" / APP_NAME)
    architectures = tuple(args.architectures or (("arm64", "x86_64") if sys.platform == "darwin" and platform.machine() == "arm64" else ("x86_64",)))
    try:
        installed = install(
            destination=destination,
            runtime_root=args.runtime_root,
            python=python,
            swift_source=ROOT / "reference-capture/app/ReferenceCaptureApp.swift",
            swiftc=args.swiftc,
            sdk=args.sdk,
            architectures=architectures,
            create_alias=not args.skip_alias,
            relaunch=args.relaunch,
        )
    except InstallError as exc:
        print(f"install-reference-capture: error: {exc}", file=sys.stderr)
        return 2
    print(f"Installed {installed}")
    if not args.skip_alias and sys.platform == "darwin":
        print(f"Desktop Finder alias: {Path.home() / 'Desktop' / APP_NAME}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
