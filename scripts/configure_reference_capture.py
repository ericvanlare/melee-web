#!/usr/bin/env python3
"""Provision private capture settings from explicit, already-owned inputs.

Run once after building the reviewed Dolphin observer and before installation.
The app thereafter discovers this configuration without a terminal. This never
copies the game image, extracted game files, or prepared memory card.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import tempfile
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from reference_capture_environment import (SCHEMA, DOLPHIN_REVISION, file_inventory,
    sha256, support_root, verify_disc, read_settings)
from build_reference_dolphin import bundle_inventory, runtime_inventory

PREPARED_GCI = "f64c9e07e436221ffc76acac7116a70167b2a690a59d5c56c2469c6fb5f2a1e6"
PREPARED_SRAM = "3ebf0d88061ea04d1f757894ee36e27b3e10bb9102b1ae961a7c5c644e4d59db"
INSTALL_SCHEMA = "webmelee-reference-dolphin-install-v1"


def _tree_inventory(root):
    """Hash a corresponding-source tree without following private escapes."""
    root = Path(root)
    if root.is_symlink() or not root.is_dir():
        raise ValueError("The corresponding-source archive must be an ordinary directory")
    forbidden_parts = {".git", ".deps", "captures", "private", "secrets", "states",
                       "configuration", "config", "memorycards", "save-states"}
    forbidden_names = {"config.json", "dolphin.ini", "gcpadnew.ini", "memorycard.raw",
                       "sram.raw"}
    forbidden_suffixes = {".ciso", ".dol", ".elf", ".gci", ".gcm", ".iso", ".raw",
                          ".rvz", ".sav", ".slp"}
    result = {}
    for entry in sorted(root.rglob("*")):
        relative = entry.relative_to(root)
        if entry.is_symlink():
            raise ValueError(f"The corresponding-source archive contains a symbolic link: {relative}")
        if any(part.lower() in forbidden_parts for part in relative.parts):
            raise ValueError(f"The corresponding-source archive contains private data: {relative}")
        if entry.is_dir():
            continue
        if not entry.is_file():
            raise ValueError(f"The corresponding-source archive contains a special file: {relative}")
        if entry.name.lower() in forbidden_names or entry.suffix.lower() in forbidden_suffixes:
            raise ValueError(f"The corresponding-source archive contains game data: {relative}")
        result[relative.as_posix()] = {"bytes": entry.stat().st_size, "sha256": sha256(entry)}
    if not result:
        raise ValueError("The corresponding-source archive is empty")
    return result


def _runtime_identity(entries):
    """Keep runtime dependency identity without leaking the build-dir path."""
    identity = []
    for entry in entries:
        if not isinstance(entry, dict) or not isinstance(entry.get("load_name"), str):
            raise ValueError("The Dolphin runtime receipt contains an invalid dependency")
        value = {"load_name": entry["load_name"]}
        for key in ("load_names", "sha256", "size", "system_managed"):
            if key in entry:
                value[key] = entry[key]
        identity.append(value)
    return identity


def _write_json_private(path, value):
    """Write a private JSON file with a durable, mode-0600 replacement."""
    path = Path(path)
    with tempfile.NamedTemporaryFile(mode="w", dir=path.parent, prefix=".provision-",
                                     delete=False) as stream:
        temporary = Path(stream.name)
        os.fchmod(stream.fileno(), 0o600)
        json.dump(value, stream, sort_keys=True, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def _installed_descriptor(version_root, *, expected_hash=None):
    version_root = Path(version_root)
    descriptor_path = version_root / "installed.json"
    if descriptor_path.is_symlink() or not descriptor_path.is_file():
        raise ValueError("The installed Dolphin version is missing its descriptor")
    try:
        descriptor = json.loads(descriptor_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ValueError("The installed Dolphin descriptor is invalid") from error
    required = {"schema", "version", "binary_sha256", "app", "executable",
                "receipt", "provenance", "receipt_sha256", "bundle_inventory",
                "runtime_dependencies", "provenance_inventory"}
    if (not isinstance(descriptor, dict) or set(descriptor) != required or
            descriptor["schema"] != INSTALL_SCHEMA or descriptor["version"] != 1):
        raise ValueError("The installed Dolphin descriptor is unsupported")
    binary_hash = descriptor["binary_sha256"]
    if not isinstance(binary_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", binary_hash):
        raise ValueError("The installed Dolphin descriptor has an invalid binary hash")
    if expected_hash is not None and binary_hash != expected_hash:
        raise ValueError("The installed Dolphin descriptor has the wrong binary hash")
    for key in ("app", "executable", "receipt", "provenance"):
        if not isinstance(descriptor[key], str) or Path(descriptor[key]).is_absolute():
            raise ValueError("The installed Dolphin descriptor contains an absolute path")
    return descriptor


def _validate_installed_dolphin(version_root, *, expected_hash=None):
    """Validate a previously published version without consulting its source path."""
    version_root = Path(version_root)
    if version_root.is_symlink() or not version_root.is_dir():
        raise ValueError("The installed Dolphin version is not an ordinary directory")
    version_root = version_root.resolve()
    descriptor = _installed_descriptor(version_root, expected_hash=expected_hash)
    app = version_root / descriptor["app"]
    executable = version_root / descriptor["executable"]
    receipt = version_root / descriptor["receipt"]
    provenance = version_root / descriptor["provenance"]
    for path, label in ((app, "Dolphin app"), (executable, "Dolphin executable"),
                        (receipt, "Dolphin receipt"), (provenance, "source archive")):
        if path.is_symlink() or not path.exists():
            raise ValueError(f"The installed {label} is missing or is a symbolic link")
        if version_root not in path.resolve().parents:
            raise ValueError(f"The installed {label} escapes its version directory")
    if not app.is_dir() or not executable.is_file() or not provenance.is_dir():
        raise ValueError("The installed Dolphin version has an invalid layout")
    if sha256(executable) != descriptor["binary_sha256"]:
        raise ValueError("The installed Dolphin binary has drifted")
    actual_bundle = bundle_inventory(app)
    if actual_bundle != descriptor["bundle_inventory"]:
        raise ValueError("The installed Dolphin bundle has drifted")
    actual_runtime = runtime_inventory(executable, app)
    if _runtime_identity(actual_runtime) != descriptor["runtime_dependencies"]:
        raise ValueError("The installed Dolphin runtime libraries have drifted")
    if sha256(receipt) != descriptor["receipt_sha256"]:
        raise ValueError("The installed Dolphin build receipt has drifted")
    try:
        receipt_value = json.loads(receipt.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ValueError("The installed Dolphin build receipt is invalid") from error
    if (receipt_value.get("schema") != "melee-web-reference-dolphin-build" or
            receipt_value.get("binary_sha256") != descriptor["binary_sha256"]):
        raise ValueError("The installed Dolphin build receipt does not bind its binary")
    if _tree_inventory(provenance) != descriptor["provenance_inventory"]:
        raise ValueError("The installed corresponding-source archive has drifted")
    return descriptor


def _install_dolphin(*, binary, build, build_manifest, root):
    """Publish a verified, relocatable Dolphin version under the private root."""
    binary = Path(binary).resolve(strict=True)
    root = Path(root).resolve()
    binary_hash = build.get("binary_sha256")
    if not isinstance(binary_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", binary_hash):
        raise ValueError("The Dolphin build receipt has no valid binary hash")
    archive_value = build.get("provenance_archive")
    if not isinstance(archive_value, str) or not archive_value:
        raise ValueError("The Dolphin build receipt has no corresponding-source archive")
    archive = Path(archive_value).expanduser().resolve(strict=True)
    if not archive.is_dir():
        raise ValueError("The corresponding-source archive is not a directory")
    source_receipt = archive / Path(build_manifest).name
    if (not source_receipt.is_file() or
            source_receipt.read_bytes() != Path(build_manifest).read_bytes()):
        raise ValueError("The corresponding-source archive does not contain the exact build receipt")
    provenance_inventory = _tree_inventory(archive)

    dolphin_root = root / "Dolphin"
    dolphin_root.mkdir(mode=0o700, parents=True, exist_ok=True)
    version_root = dolphin_root / binary_hash
    if version_root.exists():
        _validate_installed_dolphin(version_root, expected_hash=binary_hash)
        return version_root, False

    staging = Path(tempfile.mkdtemp(prefix=f".dolphin-{binary_hash[:12]}-",
                                     dir=str(dolphin_root)))
    published = False
    try:
        staged_app = staging / "Dolphin.app"
        shutil.copytree(binary.parents[2], staged_app, symlinks=True)
        shutil.copy2(build_manifest, staging / "dolphin-build.json")
        shutil.copytree(archive, staging / "provenance", symlinks=False)
        staged_bundle_inventory = bundle_inventory(staged_app)
        if staged_bundle_inventory != build["bundle_inventory"]:
            raise ValueError("The relocated Dolphin bundle does not match its build receipt")
        staged_runtime = runtime_inventory(
            staging / "Dolphin.app/Contents/MacOS/Dolphin", staged_app)
        runtime_identity = _runtime_identity(staged_runtime)
        if runtime_identity != _runtime_identity(build["runtime_dependencies"]):
            raise ValueError("The relocated Dolphin runtime does not match its build receipt")
        staged_provenance_inventory = _tree_inventory(staging / "provenance")
        if staged_provenance_inventory != provenance_inventory:
            raise ValueError("The relocated corresponding-source archive does not match its source")
        descriptor = {
            "schema": INSTALL_SCHEMA,
            "version": 1,
            "binary_sha256": binary_hash,
            "app": "Dolphin.app",
            "executable": "Dolphin.app/Contents/MacOS/Dolphin",
            "receipt": "dolphin-build.json",
            "provenance": "provenance",
            "receipt_sha256": sha256(staging / "dolphin-build.json"),
            "bundle_inventory": staged_bundle_inventory,
            "runtime_dependencies": runtime_identity,
            "provenance_inventory": staged_provenance_inventory,
        }
        if sha256(staging / descriptor["executable"]) != binary_hash:
            raise ValueError("The relocated Dolphin binary does not match its build receipt")
        _write_json_private(staging / "installed.json", descriptor)
        _validate_installed_dolphin(staging, expected_hash=binary_hash)
        try:
            staging.replace(version_root)
        except FileExistsError as error:
            raise ValueError("A Dolphin version appeared during installation; refusing overwrite") from error
        published = True
        _validate_installed_dolphin(version_root, expected_hash=binary_hash)
        return version_root, True
    finally:
        if not published:
            shutil.rmtree(staging, ignore_errors=True)


def configure(*, disc, dol, build_manifest, fixture_gc, root, refresh_build=False,
              install_dolphin=False):
    paths = [Path(value).expanduser().resolve(strict=True)
             for value in (disc, dol, build_manifest, fixture_gc)]
    disc, dol, build_manifest, fixture_gc = paths
    build = json.loads(build_manifest.read_text())
    if (build.get("schema") != "melee-web-reference-dolphin-build" or
            build.get("dolphin_commit") != DOLPHIN_REVISION or build.get("cpu") != "JITARM64"):
        raise ValueError("A reviewed passive Dolphin build receipt is required")
    binary = Path(build["binary"]).resolve(strict=True)
    if sha256(binary) != build.get("binary_sha256"):
        raise ValueError("Dolphin binary does not match its build receipt")
    observer_root = ROOT / "reference-capture/dolphin"
    overlay = {p.relative_to(observer_root / "source").as_posix(): sha256(p)
               for p in sorted((observer_root / "source").rglob("*")) if p.is_file()}
    patches = [sha256(p) for p in sorted((observer_root / "patches").glob("*.patch"))]
    if (overlay != build.get("observer_source_overlay_sha256") or
            patches != build.get("observer_patch_sha256")):
        raise ValueError("Dolphin was not built from the current reviewed observer source")
    if bundle_inventory(binary.parents[2]) != build.get("bundle_inventory"):
        raise ValueError("Dolphin bundle does not match its build receipt")
    if (not build.get("runtime_dependencies") or
            runtime_inventory(binary, binary.parents[2]) != build["runtime_dependencies"]):
        raise ValueError("Dolphin runtime libraries do not match the build receipt")
    fixture = file_inventory(fixture_gc)
    if (fixture.get("USA/Card A/01-GALE-SuperSmashBros0110290334.gci") != PREPARED_GCI or
            fixture.get("SRAM.raw") != PREPARED_SRAM):
        raise ValueError("The independently verified private prepared fixture is required")
    disc_hash = sha256(disc)
    verify_disc(disc, dol, disc_hash)
    root = Path(root).expanduser().resolve()
    root.mkdir(mode=0o700, parents=True, exist_ok=True)
    settings_path = root / "environment.json"
    previous = read_settings(settings_path) if settings_path.exists() else None
    if previous is not None and not refresh_build:
        raise ValueError("Existing private settings are preserved; use the app to configure the controller")
    if refresh_build and previous is None:
        raise ValueError("No existing environment to update")
    profile = root / "Configuration"
    profile.mkdir(mode=0o700, exist_ok=True)
    if previous is not None:
        expected_paths = {"disc": str(disc), "dol": str(dol),
                          "fixture_gc": str(fixture_gc), "profile": str(profile)}
        if (any(previous["paths"][key] != value for key, value in expected_paths.items()) or
                previous["hashes"]["profile"] != file_inventory(profile) or
                previous["hashes"]["fixture_gc"] != fixture or
                previous["hashes"]["disc_image_sha256"] != disc_hash):
            raise ValueError("Build refresh cannot adopt changed private inputs or configuration")
    elif any(profile.iterdir()):
        raise ValueError("Existing configuration is preserved; review it before provisioning")
    installed_root = None
    created_install = False
    if install_dolphin:
        installed_root, created_install = _install_dolphin(
            binary=binary, build=build, build_manifest=build_manifest, root=root)
        binary = installed_root / "Dolphin.app/Contents/MacOS/Dolphin"
        installed = _installed_descriptor(installed_root, expected_hash=build["binary_sha256"])
        bundle_hashes = installed["bundle_inventory"]
        runtime_hashes = runtime_inventory(binary, installed_root / "Dolphin.app")
    else:
        bundle_hashes = build["bundle_inventory"]
        runtime_hashes = build["runtime_dependencies"]
    if previous is None:
        # Normal keyboard input permits UI/controller setup, but does not satisfy
        # physical-controller readiness. CPU and unused physical ports are absent.
        (profile / "Dolphin.ini").write_text(
            "[Core]\nCPUCore = 4\nCPUThread = False\nEnableCheats = False\n"
            "EnableCustomRTC = True\nCustomRTCValue = 1704067200\nEmulationSpeed = 1.0\n"
            "SIDevice0 = 6\nSIDevice1 = 0\nSIDevice2 = 0\nSIDevice3 = 0\n"
            "SlotA = 8\nSlotB = 0\n[Input]\nBackgroundInput = True\n"
            "[Analytics]\nEnabled = False\nPermissionAsked = True\n")
        (profile / "GCPadNew.ini").write_text(
            "[GCPad1]\nDevice = Quartz/0/Keyboard & Mouse\n"
            "Buttons/A = `X`\nButtons/B = `Z`\nButtons/X = `C`\nButtons/Y = `S`\n"
            "Buttons/Start = `Return`\nMain Stick/Up = `Up Arrow`\n"
            "Main Stick/Down = `Down Arrow`\nMain Stick/Left = `Left Arrow`\n"
            "Main Stick/Right = `Right Arrow`\n")
    observer_files = {}
    for source in sorted(observer_root.rglob("*")):
        if source.is_file() and source.suffix in (".cpp", ".h", ".patch", ".py"):
            observer_files[source.relative_to(observer_root).as_posix()] = sha256(source)
    observer_id = hashlib.sha256(json.dumps(observer_files, sort_keys=True,
                                           separators=(",", ":")).encode()).hexdigest()
    value = {"schema": SCHEMA, "version": 1,
        "paths": {"disc": str(disc), "dol": str(dol), "dolphin": str(binary),
                  "fixture_gc": str(fixture_gc), "profile": str(profile)},
        "hashes": {"disc_image_sha256": disc_hash, "dolphin_binary_sha256": sha256(binary),
                   "dolphin_bundle": bundle_hashes,
                   "dolphin_runtime_dependencies": runtime_hashes,
                   "profile": file_inventory(profile), "fixture_gc": fixture},
        "dolphin_revision": DOLPHIN_REVISION, "observer_identity": observer_id,
        "controller": {"backend": "keyboard", "device": "Keyboard", "port": 1},
        "timing_policy": {"cpu": "JITARM64", "dual_core": False, "speed": 1.0,
                          "rtc": 1704067200}}
    if previous is not None:
        value["controller"] = previous["controller"]
        archived = root / "BuildHistory" / previous["hashes"]["dolphin_binary_sha256"]
        archived.mkdir(mode=0o700, parents=True, exist_ok=True)
        for name in ("environment.json", "dolphin-build.json"):
            source = root / name
            target = archived / name
            if not target.exists():
                with target.open("x") as stream:
                    os.fchmod(stream.fileno(), 0o600)
                    stream.write(source.read_text())
    old_settings = settings_path.read_bytes() if settings_path.exists() else None
    receipt_path = root / "dolphin-build.json"
    old_receipt = receipt_path.read_bytes() if receipt_path.exists() else None
    try:
        _write_private(settings_path, value)
        # Build provenance stays private because it binds corresponding source and
        # compiler locations. It contains hashes and paths, never game/save bytes.
        _write_private(receipt_path, build)
    except Exception:
        # Keep a failed refresh from leaving settings and receipt at different versions.
        if old_settings is None:
            settings_path.unlink(missing_ok=True)
        else:
            settings_path.write_bytes(old_settings)
        if old_receipt is None:
            receipt_path.unlink(missing_ok=True)
        else:
            receipt_path.write_bytes(old_receipt)
        if created_install and installed_root is not None:
            shutil.rmtree(installed_root, ignore_errors=True)
        raise
    return settings_path


def _write_private(path, value):
    with tempfile.NamedTemporaryFile(mode="w", dir=path.parent, prefix=".provision-",
                                     delete=False) as stream:
        temporary = Path(stream.name)
        os.fchmod(stream.fileno(), 0o600)
        json.dump(value, stream, sort_keys=True, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("disc", "dol", "build-manifest", "fixture-gc"):
        parser.add_argument("--" + flag, type=Path, required=True)
    parser.add_argument("--root", type=Path, default=support_root())
    parser.add_argument("--refresh-build", action="store_true",
                        help="Adopt a new reviewed build while preserving private inputs and controller settings")
    parser.add_argument("--install-dolphin", action="store_true",
                        help="Copy the verified Dolphin app and corresponding source archive into the private support root")
    args = parser.parse_args()
    from install_reference_capture import installation_guard
    with installation_guard(args.root):
        print(configure(disc=args.disc, dol=args.dol, build_manifest=args.build_manifest,
                        fixture_gc=args.fixture_gc, root=args.root, refresh_build=args.refresh_build,
                        install_dolphin=args.install_dolphin))


if __name__ == "__main__":
    main()
