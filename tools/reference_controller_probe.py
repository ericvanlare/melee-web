"""Pinned SDL controller discovery without display-name guesses or PAD input."""
from __future__ import annotations

import hashlib
import configparser
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import uuid

from build_reference_dolphin import runtime_inventory

DOLPHIN_REVISION = "c77bbaa0f372c3f72281602a8b087206706542cb"
SDL_REVISION = "5848e584a1b606de26e3dbd1c7e4ecbc34f807a6"
BUILD_SCHEMA = "webmelee-controller-probe-build"


def _error(message):
    from reference_capture_environment import EnvironmentError
    return EnvironmentError(message)


def _hash(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def _runtime_identity(rows):
    return [{key: row[key] for key in
             ("load_name", "load_names", "sha256", "size", "system_managed") if key in row}
            for row in rows]


def _receipt(path):
    if path.is_symlink() or not path.is_file():
        raise _error("Controller discovery receipt is missing")
    value = json.loads(path.read_text())
    if (not isinstance(value, dict) or value.get("schema") != BUILD_SCHEMA or value.get("version") != 1 or
            value.get("dolphin_revision") != DOLPHIN_REVISION or
            value.get("sdl_revision") != SDL_REVISION):
        raise _error("Controller discovery source identity is not pinned")
    return value


def verify_probe(settings):
    descriptor = settings.get("controller_probe")
    required = {"binary", "binary_sha256", "receipt", "receipt_sha256"}
    if not isinstance(descriptor, dict) or set(descriptor) != required:
        raise _error("Install the pinned SDL controller discovery helper before capturing")
    binary, receipt = Path(descriptor["binary"]), Path(descriptor["receipt"])
    root = Path(settings["paths"]["profile"]).parent / "ControllerProbes"
    expected = root / descriptor["binary_sha256"]
    if (binary != expected / "controller-probe" or receipt != expected / "build.json" or
            binary.is_symlink() or expected.is_symlink() or root.is_symlink()):
        raise _error("Controller discovery must use its isolated installed version")
    if not binary.is_file() or _hash(binary) != descriptor["binary_sha256"]:
        raise _error("Controller discovery binary drift")
    if not receipt.is_file() or _hash(receipt) != descriptor["receipt_sha256"]:
        raise _error("Controller discovery receipt drift")
    build = _receipt(receipt)
    if build.get("binary", {}).get("sha256") != descriptor["binary_sha256"]:
        raise _error("Controller discovery binary and receipt disagree")
    try:
        actual = runtime_inventory(binary, expected)
    except SystemExit as error:
        raise _error("Controller discovery runtime verification failed") from error
    if not actual or _runtime_identity(actual) != _runtime_identity(build.get("runtime_dependencies", [])):
        raise _error("Controller discovery runtime library drift")
    return binary


def decode_controllers(payload, hardware, probe_sha256):
    if (not isinstance(payload, dict) or payload.get("schema") != "webmelee-controller-probe" or
            payload.get("version") != 1 or payload.get("status") != "ok" or
            not isinstance(payload.get("devices"), list) or
            len(payload["devices"]) > 256):
        raise _error("Malformed SDL controller discovery response")
    result, qualifiers = [], set()
    for row in payload["devices"]:
        if not isinstance(row, dict):
            raise _error("Malformed SDL controller identity")
        name, index = row.get("name"), row.get("index")
        vid, pid = row.get("vendor_id"), row.get("product_id")
        if (not isinstance(name, str) or not name or len(name) > 1024 or
                type(index) is not int or index < 0 or index > 255 or
                type(vid) is not int or not 0 <= vid <= 65535 or
                type(pid) is not int or not 0 <= pid <= 65535 or
                type(row.get("virtual")) is not bool or (name, index) in qualifiers):
            raise _error("Malformed or duplicate SDL controller identity")
        qualifiers.add((name, index))
        matches = [device for device in hardware if
                   vid != 0 and pid != 0 and device.get("vendor_id") == vid and
                   device.get("product_id") == pid and not row["virtual"]]
        result.append({"source": "SDL", "name": name, "index": index,
                       "vendor_id": vid, "product_id": pid,
                       "hardware": matches[0] if len(matches) == 1 else None,
                       "probe_sha256": probe_sha256, "sdl_revision": SDL_REVISION})
    return result


def enumerate_controllers(settings, hardware):
    binary = verify_probe(settings)
    environment = {key: value for key, value in os.environ.items() if not key.startswith("SDL_")}
    try:
        response = subprocess.run([str(binary), "--config", str(Path(settings["paths"]["profile"]) / "Dolphin.ini")],
                                  env=environment, capture_output=True, text=True, timeout=10, check=True)
        if len(response.stdout) > 1024 * 1024:
            raise _error("Oversized SDL controller discovery response")
        payload = json.loads(response.stdout)
    except (OSError, subprocess.SubprocessError, ValueError) as error:
        raise _error("SDL controller discovery failed; close controller settings and rescan") from error
    rows = decode_controllers(payload, hardware, settings["controller_probe"]["binary_sha256"])
    for row in rows:
        row["probe_receipt_sha256"] = settings["controller_probe"]["receipt_sha256"]
    return rows


def install_probe(manifest, root):
    """Publish a new immutable helper version and preserve the operator's mapping."""
    from reference_capture_environment import read_settings
    from configure_reference_capture import _write_private
    root, manifest = Path(root), Path(manifest).resolve()
    settings_path = root / "environment.json"
    settings = read_settings(settings_path)
    build = _receipt(manifest)
    source = Path(build["binary"]["path"])
    digest = build["binary"]["sha256"]
    if source.is_symlink() or not source.is_file() or _hash(source) != digest:
        raise _error("Controller discovery build binary drift")
    versions = root / "ControllerProbes"
    versions.mkdir(mode=0o700, exist_ok=True)
    destination = versions / digest
    descriptor = {"binary": str(destination / "controller-probe"), "binary_sha256": digest,
                  "receipt": str(destination / "build.json"), "receipt_sha256": _hash(manifest)}
    if not destination.exists():
        staging = Path(tempfile.mkdtemp(prefix=".install-", dir=versions))
        try:
            shutil.copyfile(source, staging / "controller-probe")
            (staging / "controller-probe").chmod(0o755)
            shutil.copyfile(manifest, staging / "build.json")
            (staging / "build.json").chmod(0o600)
            os.replace(staging, destination)
        finally:
            if staging.exists(): shutil.rmtree(staging)
    updated = dict(settings, controller_probe=descriptor)
    if settings["controller"]["backend"] == "SDL":
        # Version 0.1.0 retained only the name. Recover the selected logical
        # controller index from the operator's actual, unchanged Dolphin map.
        pad = configparser.ConfigParser(interpolation=None)
        pad.read(Path(settings["paths"]["profile"]) / "GCPadNew.ini")
        qualifier = pad.get("GCPad1", "Device", fallback="").split("/", 2)
        if (len(qualifier) != 3 or qualifier[0] != "SDL" or
                not qualifier[1].isdecimal() or qualifier[2] != settings["controller"]["device"]):
            raise _error("Saved Dolphin controller selection does not match the capture settings")
        updated["controller"] = dict(settings["controller"], index=int(qualifier[1]))
    verify_probe(updated)
    backup = versions / f"settings-before-{uuid.uuid4().hex}.json"
    _write_private(backup, settings)
    _write_private(settings_path, updated)
    return descriptor
