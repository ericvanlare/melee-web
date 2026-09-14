"""Private, fail-closed environment verification for the retail capture app.

Settings contain local paths and belong outside the repository. No verifier
changes the disc, prepared save, Dolphin installation, or global profile.
"""
from __future__ import annotations

import configparser
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from extract_disc_file import DiscImage
from build_reference_dolphin import bundle_inventory, runtime_inventory

SCHEMA = "webmelee-reference-environment"
DOL_SHA1 = "08e0bf20134dfcb260699671004527b2d6bb1a45"
DOL_SHA256 = "dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646"
DOLPHIN_REVISION = "c77bbaa0f372c3f72281602a8b087206706542cb"


class EnvironmentError(ValueError):
    pass


def support_root() -> Path:
    return Path.home() / "Library/Application Support/WebMelee Reference Capture"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def json_sha256(value) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True,
                                    separators=(",", ":")).encode()).hexdigest()


def file_inventory(root: Path) -> dict:
    """Bind all relative regular files; symlinks cannot redirect a profile."""
    if root.is_symlink() or not root.is_dir():
        raise EnvironmentError("Expected an ordinary private directory")
    result = {}
    for entry in sorted(root.rglob("*")):
        if entry.is_symlink():
            raise EnvironmentError("A private input directory contains a symbolic link")
        if entry.is_file():
            result[entry.relative_to(root).as_posix()] = sha256(entry)
        elif not entry.is_dir():
            raise EnvironmentError("A private input directory contains a special file")
    return result


def read_settings(path: Path) -> dict:
    if path.is_symlink() or not path.is_file():
        raise EnvironmentError("The local environment has not been configured")
    settings = json.loads(path.read_text())
    expected = {"schema", "version", "paths", "hashes", "dolphin_revision",
                "observer_identity", "controller", "timing_policy"}
    if (not isinstance(settings, dict) or not expected <= set(settings) or
            set(settings) - expected - {"controller_probe"}):
        raise EnvironmentError("Unsupported local environment settings")
    if settings["schema"] != SCHEMA or settings["version"] != 1:
        raise EnvironmentError("Unsupported local environment version")
    if set(settings["paths"]) != {"disc", "dol", "dolphin", "fixture_gc", "profile"}:
        raise EnvironmentError("Missing or unknown private input paths")
    for value in settings["paths"].values():
        if not isinstance(value, str) or not Path(value).is_absolute():
            raise EnvironmentError("Private input paths must be absolute")
    if settings["dolphin_revision"] != DOLPHIN_REVISION:
        raise EnvironmentError("Dolphin source revision is not the reviewed reference")
    if settings["timing_policy"] != {"cpu": "JITARM64", "dual_core": False,
                                      "speed": 1.0, "rtc": 1704067200}:
        raise EnvironmentError("Unsupported retail timing policy")
    return settings


def verify_disc(disc: Path, dol: Path, expected_disc_hash: str) -> dict:
    if sha256(dol) != DOL_SHA256:
        raise EnvironmentError("The reference executable identity was rejected")
    size = dol.stat().st_size
    if not 0x100 <= size <= 8 * 1024 * 1024:
        raise EnvironmentError("Invalid reference executable size")
    with DiscImage(disc) as image:
        offset = struct.unpack(">I", image.read(0x420, 4))[0]
        if offset < 0x440:
            raise EnvironmentError("Invalid executable location in disc")
        payload = image.read(offset, size)
        embedded = hashlib.sha256(payload).hexdigest()
        embedded_sha1 = hashlib.sha1(payload).hexdigest()
        if embedded != DOL_SHA256 or embedded_sha1 != DOL_SHA1:
            raise EnvironmentError("The disc contains a different executable")
    digest = sha256(disc)
    if digest != expected_disc_hash:
        raise EnvironmentError("The configured disc identity has changed")
    return {"game_id": "GALE01", "revision": 2, "image_sha256": digest,
            "dol_sha1": embedded_sha1, "dol_sha256": embedded,
            "image_bytes": disc.stat().st_size}


def _walk_plist(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from _walk_plist(child)
    elif isinstance(value, list):
        for child in value:
            yield from _walk_plist(child)


def physical_devices(settings: dict | None = None) -> list[dict]:
    """Observe HID game devices and Nintendo USB adapters, without serials."""
    devices = {}
    for klass in ("IOHIDDevice", "IOUSBHostDevice"):
        raw = subprocess.check_output(["/usr/sbin/ioreg", "-a", "-r", "-c", klass],
                                      timeout=10)
        # ioreg exits successfully with no bytes when a class has no devices.
        if not raw.strip():
            continue
        for row in _walk_plist(plistlib.loads(raw)):
            vendor = row.get("VendorID", row.get("idVendor"))
            product = row.get("ProductID", row.get("idProduct"))
            adapter = (vendor, product) == (0x057e, 0x0337)
            if not adapter and not (row.get("PrimaryUsagePage") == 1 and
                                     row.get("PrimaryUsage") in (4, 5)):
                continue
            name = row.get("Product", row.get("USB Product Name", "Game controller"))
            key = (str(name), vendor, product)
            devices[key] = {"name": str(name), "vendor_id": vendor,
                            "product_id": product, "adapter": adapter,
                            "transport": str(row.get("Transport", "USB" if adapter else "unknown"))}
    hardware = sorted(devices.values(), key=lambda d: d["name"])
    if settings is not None and settings["controller"]["backend"] == "SDL":
        # SDL may assign a controller database name different from IOHID's
        # product name. Enumerate through the same pinned SDL implementation;
        # never infer aliases from spelling or accept an unrelated gamepad.
        from reference_controller_probe import enumerate_controllers
        return enumerate_controllers(settings, hardware)
    return hardware


def configured_controller(settings: dict, devices: list[dict]) -> dict:
    controller = settings["controller"]
    required = {"backend", "device", "port"}
    if (not isinstance(controller, dict) or not required <= set(controller) or
            set(controller) - required - {"index"}):
        raise EnvironmentError("Unsupported controller configuration")
    if controller["port"] != 1:
        raise EnvironmentError("This release supports the human controller on port 1")
    backend, name = controller["backend"], controller["device"]
    if backend == "adapter":
        matches = [d for d in devices if d["adapter"]]
    elif backend == "SDL":
        index = controller.get("index", 0)
        if isinstance(index, bool) or not isinstance(index, int) or index < 0:
            raise EnvironmentError("Invalid SDL controller index")
        matches = [d for d in devices if d.get("source") == "SDL" and
                   d["name"] == name and d.get("index") == index and
                   d.get("hardware")]
    elif backend in ("keyboard", "unconfigured"):
        matches = []
    else:
        raise EnvironmentError("Unsupported physical controller backend")
    if len(matches) > 1:
        raise EnvironmentError("Ambiguous configured controller identity")
    return {"state": "connected" if matches else "unavailable", "configured": controller,
            "devices": devices, "selected": matches[0] if matches else None,
            "physical_session_validated": False}


def verify_environment(settings: dict, root: Path, *, progress=lambda *_: None,
                       devices: list[dict] | None = None) -> dict:
    paths = {key: Path(value) for key, value in settings["paths"].items()}
    profile = paths["profile"]
    # A caller cannot select Dolphin's implicit/global user folder, even if its
    # current hashes happen to match. The supervisor always passes -u as well.
    if profile.resolve() != (root / "Configuration").resolve() or profile.is_symlink():
        raise EnvironmentError("Dolphin profile must be the application's isolated configuration")
    progress("verifying", "Checking the pinned Dolphin and observer")
    hashes = settings["hashes"]
    if sha256(paths["dolphin"]) != hashes["dolphin_binary_sha256"]:
        raise EnvironmentError("Dolphin binary drift: reinstall the reviewed observer build")
    dolphin_bundle = paths["dolphin"].parents[2]
    try:
        actual_bundle = bundle_inventory(dolphin_bundle)
    except SystemExit as error:
        raise EnvironmentError("Dolphin bundle verification failed: " + str(error)) from error
    if dolphin_bundle.suffix != ".app" or actual_bundle != hashes.get("dolphin_bundle"):
        raise EnvironmentError("Dolphin bundle drift: reinstall the reviewed observer build")
    runtime_dependencies = hashes.get("dolphin_runtime_dependencies")
    try:
        actual_runtime = runtime_inventory(paths["dolphin"], dolphin_bundle)
    except SystemExit as error:
        raise EnvironmentError("Dolphin runtime verification failed: " + str(error)) from error
    if not runtime_dependencies or actual_runtime != runtime_dependencies:
        raise EnvironmentError("Dolphin runtime library drift: rebuild the reviewed observer")
    if not re.fullmatch(r"[0-9a-f]{64}", settings["observer_identity"]):
        raise EnvironmentError("Missing observer identity")
    actual_profile = file_inventory(profile)
    if actual_profile != hashes["profile"]:
        raise EnvironmentError("Dolphin configuration changed; review it through Configure Controller")
    progress("verifying", "Checking the private prepared save. Allow macOS folder access if prompted.")
    fixture = file_inventory(paths["fixture_gc"])
    if not fixture or fixture != hashes["fixture_gc"]:
        raise EnvironmentError("The private prepared unlock fixture changed")
    progress("verifying", "Verifying the owned GALE01 1.02 disc")
    disc = verify_disc(paths["disc"], paths["dol"], hashes["disc_image_sha256"])
    progress("verifying", "Checking the configured physical controller")
    controller = configured_controller(settings, physical_devices(settings) if devices is None else devices)
    return {"disc": disc, "dolphin": {"source_revision": DOLPHIN_REVISION,
            "binary_sha256": hashes["dolphin_binary_sha256"],
            "observer_sha256": settings["observer_identity"],
            "bundle_inventory_sha256": json_sha256(actual_bundle),
            "runtime_dependencies_sha256": json_sha256(actual_runtime),
            "runtime_libraries": [{"name": Path(row.get("load_name", "")).name,
                                   "sha256": row.get("sha256"),
                                   "system_managed": row.get("system_managed", False)}
                                  for row in actual_runtime]},
            "controller": controller, "configuration_sha256": actual_profile,
            "prepared_fixture_sha256": fixture, "prepared_fixture_kind": "private prepared unlock fixture",
            "timing_policy": settings["timing_policy"], "locale": "en_US.UTF-8",
            "os": {"system": os.uname().sysname, "release": os.uname().release,
                   "machine": os.uname().machine}}
