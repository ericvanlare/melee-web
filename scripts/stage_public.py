#!/usr/bin/env python3
"""Prepare and audit the isolated WebMelee Pages staging package.

This module deliberately stops before deployment.  It binds a package to one
clean, exact source commit, audits the production/player base with the
checked-in public auditor, preserves its bytes by default, and writes a portable
receipt beside (never inside) the upload directory. A small deterministic label
overlay is opt-in.

The ``reuse`` path is for an already-built, already-frozen candidate.  It does
not trust a manifest as provenance: the source auditor still runs from the
exact checkout and must have the producer identity/provenance files that the
manifest references.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Any, Iterable


STAGING_PROJECT = "webmelee-staging"
STAGING_BRANCH = "staging"
STAGING_URL = "https://webmelee-staging.pages.dev"
STAGING_PAGES_HOST = "webmelee-staging.pages.dev"

# These identify the initial frozen PR15 base.  They are reference values, not
# defaults: every CLI invocation must provide a full source and base digest.
INITIAL_SOURCE_SHA = "08b02660fd2a09120342ffaf116e36fe8656af23"
INITIAL_BASE_MANIFEST_SHA256 = "59725af22d375101ece877fec9fbe052caff81051db72c0ab93432e6bf19f27e"
INITIAL_RUNTIME_HASH = "68c94e397d6e7c5c"
INITIAL_IDENTITY_SHA256 = "c2b2404ad9a6a5d495c204f05368d9cfb5ec0ee7a07a937c57014984df9fed88"

OVERLAY_VERSION = "melee-web-staging-overlay-v1"
OVERLAY_SPEC = (
    "prefix the player index title with [staging]; replace its edition with "
    "staging alpha · no audio; replace production Pages host names in "
    "_headers; use a staging-only _redirects comment"
)
OVERLAY_SPEC_SHA256 = hashlib.sha256(OVERLAY_SPEC.encode("utf-8")).hexdigest()
EXACT_SPEC = "copy the audited production/player base without a staging overlay"
EXACT_SPEC_SHA256 = hashlib.sha256(EXACT_SPEC.encode("utf-8")).hexdigest()
EXACT_VERSION = "melee-web-staging-exact-v1"

CPU_MARKERS = (b"CPU_ADDRESS_AUDIT", b"melee-web-native-cpu-address-diagnostic")
REQUIRED_PUBLIC_DEFINE = b"MELEE_WEB_PUBLIC_RUNTIME"
OPERATOR = "NaiadAI, LLC"
CONTACT = "legal@webmelee.gg"
HTML_FILES = ("index.html", "terms.html", "privacy.html", "copyright.html", "notices.html", "404.html")
OVERLAY_CHANGED_PATHS = frozenset(("index.html", "_headers", "_redirects"))
PLAYER_RUNTIME_PREFIX = "runtime/"

STAGING_REDIRECTS = (
    "# Staging only: no host redirects are configured in this Pages project.\n"
    "# Verify the immutable deployment before attaching staging.webmelee.gg.\n"
    "# Production project webmelee and its canonical redirects are out of scope.\n"
)
RECEIPT_SCHEMA = "melee-web-staging-receipt-v1"
PUBLIC_MANIFEST_SCHEMA = "melee-web-public-release-v1"
RECEIPT_KEYS = frozenset({
    "schema", "project", "branch", "stable_url", "source_sha",
    "base_manifest_sha256", "overlay_manifest_sha256", "overlay_version",
    "overlay_spec_sha256", "staging_mode", "profile", "mode", "index_production",
    "operator", "contact", "runtime", "native_artifacts", "audio_policy",
    "changed_files",
})
PRIVATE_PATH_RE = re.compile(
    r"(?:/(?:Users|home)/|[A-Za-z]:[\\/](?:Users|home)[\\/]|"
    r"(?:^|[\\/])(?:assets-local|\.deps|\.venv)(?:[\\/]|$)|"
    r"file://|(?:^|[\\/])~(?:[\\/]|$))",
    re.IGNORECASE | re.VERBOSE,
)
SHA1_RE = re.compile(r"^[0-9a-f]{40}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


class StageError(ValueError):
    """A user-facing fail-closed staging preparation error."""


def _display(path: Path) -> str:
    return path.as_posix()


def _is_symlink(path: Path) -> bool:
    try:
        return path.is_symlink()
    except OSError as exc:
        raise StageError(f"cannot inspect path {_display(path)}: {exc}") from exc


def _require_directory(path: Path, label: str) -> None:
    if _is_symlink(path) or not path.is_dir():
        raise StageError(f"{label} must be a real directory: {_display(path)}")


def _require_file(path: Path, label: str) -> None:
    if _is_symlink(path) or not path.is_file():
        raise StageError(f"{label} must be a regular file: {_display(path)}")


def _inside(path: Path, parent: Path) -> bool:
    try:
        return path.resolve(strict=False).is_relative_to(parent.resolve(strict=False))
    except OSError as exc:
        raise StageError(f"cannot resolve path {_display(path)}: {exc}") from exc


def _safe_relative(value: object, label: str = "path") -> str:
    if not isinstance(value, str) or not value or "\x00" in value:
        raise StageError(f"{label} is not a valid relative path")
    if value.startswith("/") or "\\" in value:
        raise StageError(f"{label} is not a canonical relative path: {value!r}")
    path = Path(value)
    if path.as_posix() != value or any(part in ("", ".", "..") for part in path.parts):
        raise StageError(f"{label} is not a canonical relative path: {value!r}")
    return value


def _new_external_directory(path: Path, source_root: Path, label: str) -> None:
    if _is_symlink(path) or path.exists():
        raise StageError(f"{label} must be a new directory: {_display(path)}")
    if _inside(path, source_root):
        raise StageError(f"{label} must be outside the source checkout: {_display(path)}")
    if not path.parent.is_dir():
        raise StageError(f"{label} parent must already exist: {_display(path.parent)}")


def _new_sidecar(path: Path, output: Path, label: str) -> None:
    if _is_symlink(path) or path.exists():
        raise StageError(f"{label} must be a new regular file: {_display(path)}")
    if _inside(path, output):
        raise StageError(f"{label} must be outside the upload directory: {_display(path)}")
    if not path.parent.is_dir():
        raise StageError(f"{label} parent must already exist: {_display(path.parent)}")


def _canonical_json_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise StageError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _load_json(path: Path, label: str) -> tuple[dict[str, Any], bytes]:
    _require_file(path, label)
    try:
        raw = path.read_bytes()
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_canonical_json_object)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
        raise StageError(f"{label} is not valid UTF-8 JSON: {_display(path)}: {exc}") from exc
    if not isinstance(value, dict):
        raise StageError(f"{label} root must be an object")
    return value, raw


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _sha256_file(path: Path, label: str) -> str:
    _require_file(path, label)
    try:
        return _sha256_bytes(path.read_bytes())
    except OSError as exc:
        raise StageError(f"cannot read {label}: {_display(path)}: {exc}") from exc


def _canonical_json_bytes(value: dict[str, Any]) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")


def _run(command: list[str], cwd: Path) -> None:
    try:
        subprocess.run(command, cwd=cwd, check=True)
    except OSError as exc:
        raise StageError(f"cannot run command in {_display(cwd)}: {exc}") from exc
    except subprocess.CalledProcessError as exc:
        raise StageError(f"command failed with exit {exc.returncode}: {command[0]}") from exc


def _capture(command: list[str], cwd: Path) -> str:
    try:
        result = subprocess.run(command, cwd=cwd, check=True, text=True,
                               capture_output=True)
    except OSError as exc:
        raise StageError(f"cannot inspect source checkout: {exc}") from exc
    except subprocess.CalledProcessError as exc:
        raise StageError(f"source checkout command failed with exit {exc.returncode}") from exc
    return result.stdout


def _validate_source_sha(source_root: Path, sha: str) -> None:
    _require_directory(source_root, "source root")
    if not isinstance(sha, str) or not SHA1_RE.fullmatch(sha.strip()):
        raise StageError("--sha must be the full 40-character lowercase commit SHA")
    sha = sha.strip()
    actual = _capture(["git", "rev-parse", "HEAD"], source_root).strip()
    if actual != sha:
        raise StageError(f"source HEAD is {actual}, expected exact SHA {sha}")
    status = _capture(["git", "status", "--porcelain=v1", "--untracked-files=all"], source_root)
    if status.strip():
        raise StageError("source checkout is not clean; refusing staging preparation")


def _validate_pr15_guard(source_root: Path) -> None:
    packager = source_root / "scripts" / "build_public.py"
    cmake = source_root / "cmake" / "FighterRuntime.cmake"
    cpu = source_root / "src" / "gameplay_cpu_observation.c"
    _require_file(packager, "public packager")
    _require_file(cmake, "public CMake file")
    _require_file(cpu, "CPU observation source")
    try:
        packager_bytes = packager.read_bytes()
        cmake_bytes = cmake.read_bytes()
        cpu_bytes = cpu.read_bytes()
    except OSError as exc:
        raise StageError(f"cannot read PR15 source guard: {exc}") from exc
    missing = [marker.decode("ascii") for marker in CPU_MARKERS if marker not in packager_bytes]
    if missing:
        raise StageError("public packager is missing CPU diagnostic rejection markers: " + ", ".join(missing))
    if REQUIRED_PUBLIC_DEFINE not in cmake_bytes:
        raise StageError("public CMake target is missing MELEE_WEB_PUBLIC_RUNTIME")
    if (b"#ifndef MELEE_WEB_PUBLIC_RUNTIME" not in cpu_bytes
            or REQUIRED_PUBLIC_DEFINE not in cpu_bytes):
        raise StageError("CPU observation source is missing the public-runtime compile guard")


def _walk_files(root: Path, label: str) -> dict[str, bytes]:
    _require_directory(root, label)
    files: dict[str, bytes] = {}
    try:
        paths = sorted(root.rglob("*"), key=lambda item: item.as_posix())
    except OSError as exc:
        raise StageError(f"cannot inspect {label}: {exc}") from exc
    for path in paths:
        rel = _safe_relative(path.relative_to(root).as_posix(), f"{label} path")
        if _is_symlink(path):
            raise StageError(f"symlink rejected in {label}: {rel}")
        if path.is_dir():
            continue
        if not path.is_file():
            raise StageError(f"non-file entry rejected in {label}: {rel}")
        try:
            files[rel] = path.read_bytes()
        except OSError as exc:
            raise StageError(f"cannot read {label} file {rel}: {exc}") from exc
    return files


def _walk_directories(root: Path, label: str) -> set[str]:
    """Return the relative directory inventory, rejecting links and escapes."""
    _require_directory(root, label)
    directories: set[str] = set()
    try:
        paths = sorted(root.rglob("*"), key=lambda item: item.as_posix())
    except OSError as exc:
        raise StageError(f"cannot inspect {label}: {exc}") from exc
    for path in paths:
        rel = _safe_relative(path.relative_to(root).as_posix(), f"{label} path")
        if _is_symlink(path):
            raise StageError(f"symlink rejected in {label}: {rel}")
        if path.is_dir():
            directories.add(rel)
    return directories


def _records(files: dict[str, bytes]) -> list[dict[str, int | str]]:
    return [
        {"path": rel, "size": len(data), "sha256": _sha256_bytes(data)}
        for rel, data in sorted(files.items())
    ]


def _manifest_contract(value: dict[str, Any]) -> dict[str, Any]:
    if value.get("schema") != PUBLIC_MANIFEST_SCHEMA:
        raise StageError("base manifest has an unsupported public release schema")
    if value.get("profile") != "player":
        raise StageError("base manifest must be the player profile")
    if value.get("mode") != "production" or value.get("draft_preview") is not False:
        raise StageError("base manifest must be production mode, never preview mode")
    if value.get("index_production") is not False:
        raise StageError("staging base must remain noindex (index_production=false)")
    if value.get("operator") != OPERATOR or value.get("contact") != CONTACT:
        raise StageError("base manifest operator/contact facts are not the approved public values")
    runtime = value.get("runtime")
    if not isinstance(runtime, dict):
        raise StageError("base manifest runtime metadata is missing")
    if (not isinstance(runtime.get("path"), str)
            or not re.fullmatch(r"runtime/[0-9a-f]{16}", runtime["path"])
            or runtime.get("hash") != runtime["path"].rsplit("/", 1)[1]):
        raise StageError("base manifest runtime hash/path is invalid")
    if not isinstance(runtime.get("identity_sha256"), str) or not SHA256_RE.fullmatch(runtime["identity_sha256"]):
        raise StageError("base manifest runtime identity digest is invalid")
    identity = runtime.get("identity")
    if not isinstance(identity, dict):
        raise StageError("base manifest producer identity is missing")
    if identity.get("target") != "runtime-public" or identity.get("configuration") != "Release":
        raise StageError("base producer identity is not the Release runtime-public target")
    policy = identity.get("audio_policy")
    expected_policy = {
        "mode": "disabled",
        "pcm_output": False,
        "dsp_resampler": False,
        "dsp_coefficients_required": False,
    }
    if policy != expected_policy:
        raise StageError("base producer identity is not the silent public audio policy")
    return {"runtime": runtime, "identity": identity, "audio_policy": expected_policy}


def _validate_manifest_inventory(output: Path, manifest: dict[str, Any], label: str) -> dict[str, bytes]:
    files = _walk_files(output, label)
    expected = manifest.get("files")
    if not isinstance(expected, list):
        raise StageError(f"{label} manifest files must be a list")
    records = _records(files)
    if expected != records:
        raise StageError(f"{label} manifest inventory does not match its bytes")
    return files


def _source_audit(source_root: Path, output: Path, manifest: Path) -> None:
    auditor = source_root / "scripts" / "audit_public.py"
    _require_file(auditor, "source public auditor")
    _run([
        sys.executable, str(auditor),
        "--profile", "player", "--mode", "production",
        "--output", str(output), "--manifest", str(manifest),
    ], source_root)


def _overlay_html(data: bytes, rel: str) -> bytes:
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise StageError(f"base HTML is not UTF-8: {rel}") from exc
    title_matches = list(re.finditer(r"<title>(.*?)</title>", text, re.IGNORECASE | re.DOTALL))
    if len(title_matches) != 1:
        raise StageError(f"base HTML must contain exactly one title: {rel}")
    match = title_matches[0]
    if match.group(1).startswith("[staging] "):
        raise StageError(f"base HTML title is not a production title: {rel}")
    text = text[:match.start(1)] + "[staging] " + match.group(1) + text[match.end(1):]
    if rel == "index.html":
        needle = '<span id="edition">alpha · no audio</span>'
        if text.count(needle) != 1:
            raise StageError("base player edition marker is not the reviewed alpha text")
        text = text.replace(needle, '<span id="edition">staging alpha · no audio</span>', 1)
    return text.encode("utf-8")


def _overlay_headers(data: bytes) -> bytes:
    old = b"webmelee.pages.dev"
    new = STAGING_PAGES_HOST.encode("ascii")
    if old not in data:
        raise StageError("base _headers has no production Pages host rule to scope to staging")
    result = data.replace(old, new)
    if b"webmelee.pages.dev" in result or new not in result:
        raise StageError("staging _headers host rewrite failed")
    if b"X-Robots-Tag: noindex" not in result:
        raise StageError("staging _headers lost the noindex policy")
    return result


def _write_package(files: dict[str, bytes], output: Path) -> dict[str, bytes]:
    """Write a fresh package from already-audited bytes."""
    _require_directory(output, "staging output")
    for rel, data in sorted(files.items()):
        _safe_relative(rel, "staging output path")
        destination = output / rel
        if _is_symlink(destination) or not destination.parent.resolve().is_relative_to(output.resolve()):
            raise StageError(f"staging package path escapes output: {rel}")
        if not destination.parent.exists():
            destination.parent.mkdir(parents=True)
        if destination.exists():
            raise StageError(f"staging output path already exists: {rel}")
        try:
            with destination.open("xb") as handle:
                handle.write(data)
        except OSError as exc:
            raise StageError(f"cannot write staging output {rel}: {exc}") from exc
    return files


def _apply_overlay(base: dict[str, bytes], output: Path) -> dict[str, bytes]:
    generated = dict(base)
    for rel, transform in (("index.html", _overlay_html), ("_headers", _overlay_headers)):
        if rel not in generated:
            raise StageError(f"base package is missing {rel}")
        generated[rel] = transform(generated[rel], rel) if rel == "index.html" else transform(generated[rel])
    if "_redirects" not in generated:
        raise StageError("base package is missing _redirects")
    generated["_redirects"] = STAGING_REDIRECTS.encode("utf-8")
    return _write_package(generated, output)


def _changed_files(base: dict[str, bytes], overlay: dict[str, bytes]) -> list[dict[str, int | str]]:
    if set(base) != set(overlay):
        missing = sorted(set(base) - set(overlay))
        extra = sorted(set(overlay) - set(base))
        raise StageError(f"staging overlay changed inventory: missing={missing}, extra={extra}")
    changed = []
    for rel in sorted(base):
        if base[rel] == overlay[rel]:
            continue
        if rel not in OVERLAY_CHANGED_PATHS:
            raise StageError(f"staging overlay changed an unauthorized path: {rel}")
        changed.append({
            "path": rel,
            "base_size": len(base[rel]),
            "base_sha256": _sha256_bytes(base[rel]),
            "overlay_size": len(overlay[rel]),
            "overlay_sha256": _sha256_bytes(overlay[rel]),
        })
    expected = sorted(OVERLAY_CHANGED_PATHS)
    actual = [item["path"] for item in changed]
    if actual != expected:
        raise StageError(f"staging overlay changed files differ from the allowlist: {actual}")
    return changed


def _native_artifacts(identity: dict[str, Any]) -> list[dict[str, int | str]]:
    raw = identity.get("artifacts")
    if not isinstance(raw, list):
        raise StageError("producer identity native artifacts are missing")
    result = []
    for item in raw:
        if not isinstance(item, dict) or set(item) != {"path", "bytes", "sha256"}:
            raise StageError("producer identity native artifact record is invalid")
        path = _safe_relative(item["path"], "native artifact path")
        size = item["bytes"]
        sha = item["sha256"]
        if not isinstance(size, int) or isinstance(size, bool) or size < 1 or not isinstance(sha, str) or not SHA256_RE.fullmatch(sha):
            raise StageError(f"producer identity native artifact record is invalid: {path}")
        result.append({"path": path, "bytes": size, "sha256": sha})
    return result


def _portable(value: object, location: str = "receipt") -> None:
    if isinstance(value, str):
        if PRIVATE_PATH_RE.search(value):
            raise StageError(f"private path is forbidden in portable receipt: {location}")
        return
    if isinstance(value, dict):
        for key, child in value.items():
            _portable(child, f"{location}.{key}")
    elif isinstance(value, list):
        for index, child in enumerate(value):
            _portable(child, f"{location}[{index}]")


def _result(
    source_sha: str,
    base_manifest_sha256: str,
    overlay_manifest_sha256: str,
    base: dict[str, Any],
    overlay: dict[str, bytes],
    changed: list[dict[str, int | str]],
    staging_mode: str,
) -> dict[str, Any]:
    runtime = base["runtime"]
    identity = base["identity"]
    return {
        "source_sha": source_sha,
        "base_manifest_sha256": base_manifest_sha256,
        "overlay_manifest_sha256": overlay_manifest_sha256,
        "staging_mode": staging_mode,
        "profile": "player",
        "mode": "production",
        "index_production": False,
        "operator": OPERATOR,
        "contact": CONTACT,
        "runtime": {
            "path": runtime["path"],
            "hash": runtime["hash"],
            "identity_sha256": runtime.get("identity_sha256"),
        },
        "native_artifacts": _native_artifacts(identity),
        "audio_policy": copy.deepcopy(base["audio_policy"]),
        "changed_files": changed,
    }


def _receipt_payload(audit_result: dict[str, Any]) -> dict[str, Any]:
    staging_mode = audit_result["staging_mode"]
    if staging_mode == "exact":
        overlay_version, overlay_spec_sha256 = EXACT_VERSION, EXACT_SPEC_SHA256
    elif staging_mode == "label":
        overlay_version, overlay_spec_sha256 = OVERLAY_VERSION, OVERLAY_SPEC_SHA256
    else:
        raise StageError("unsupported staging mode in audit result")
    receipt = {
        "schema": RECEIPT_SCHEMA,
        "project": STAGING_PROJECT,
        "branch": STAGING_BRANCH,
        "stable_url": STAGING_URL,
        "source_sha": audit_result["source_sha"],
        "base_manifest_sha256": audit_result["base_manifest_sha256"],
        "overlay_manifest_sha256": audit_result["overlay_manifest_sha256"],
        "overlay_version": overlay_version,
        "overlay_spec_sha256": overlay_spec_sha256,
        "staging_mode": staging_mode,
        "profile": audit_result["profile"],
        "mode": audit_result["mode"],
        "index_production": audit_result["index_production"],
        "operator": audit_result["operator"],
        "contact": audit_result["contact"],
        "runtime": audit_result["runtime"],
        "native_artifacts": audit_result["native_artifacts"],
        "audio_policy": audit_result["audio_policy"],
        "changed_files": audit_result["changed_files"],
    }
    _portable(receipt)
    return receipt


def _write_json_new(path: Path, value: dict[str, Any], label: str) -> bytes:
    encoded = _canonical_json_bytes(value)
    return _write_bytes_new(path, encoded, label)


def _write_bytes_new(path: Path, encoded: bytes, label: str) -> bytes:
    if _is_symlink(path) or path.exists():
        raise StageError(f"{label} already exists: {_display(path)}")
    if not path.parent.is_dir():
        raise StageError(f"{label} parent does not exist: {_display(path.parent)}")
    try:
        with path.open("xb") as handle:
            handle.write(encoded)
    except OSError as exc:
        raise StageError(f"cannot write {label}: {exc}") from exc
    return encoded


def _validate_receipt_value(value: dict[str, Any], *, source_sha: str | None = None,
                            base_manifest_sha256: str | None = None,
                            overlay_manifest_sha256: str | None = None) -> dict[str, Any]:
    _portable(value)
    if set(value) != RECEIPT_KEYS:
        raise StageError("staging receipt has unexpected or missing fields")
    if value.get("schema") != RECEIPT_SCHEMA:
        raise StageError("unsupported staging receipt schema")
    if value.get("project") != STAGING_PROJECT or value.get("branch") != STAGING_BRANCH:
        raise StageError("staging receipt is bound to the wrong Pages project or branch")
    if value.get("stable_url") != STAGING_URL:
        raise StageError("staging receipt stable URL is invalid")
    actual_source = value.get("source_sha")
    if not isinstance(actual_source, str) or not SHA1_RE.fullmatch(actual_source):
        raise StageError("staging receipt source SHA is invalid")
    if source_sha is not None and actual_source != source_sha:
        raise StageError("staging receipt source SHA differs from the requested source")
    for key, expected in (("base_manifest_sha256", base_manifest_sha256),
                          ("overlay_manifest_sha256", overlay_manifest_sha256)):
        actual = value.get(key)
        if not isinstance(actual, str) or not SHA256_RE.fullmatch(actual):
            raise StageError(f"staging receipt {key} is invalid")
        if expected is not None and actual != expected:
            raise StageError(f"staging receipt {key} differs from the expected digest")
    staging_mode = value.get("staging_mode")
    if staging_mode == "exact":
        expected_overlay = (EXACT_VERSION, EXACT_SPEC_SHA256)
    elif staging_mode == "label":
        expected_overlay = (OVERLAY_VERSION, OVERLAY_SPEC_SHA256)
    else:
        raise StageError("staging receipt mode is invalid")
    if (value.get("overlay_version"), value.get("overlay_spec_sha256")) != expected_overlay:
        raise StageError("staging receipt overlay identity is invalid")
    if staging_mode == "exact" and value.get("overlay_manifest_sha256") != value.get("base_manifest_sha256"):
        raise StageError("exact staging receipt must preserve the base manifest digest")
    if staging_mode == "label" and value.get("overlay_manifest_sha256") == value.get("base_manifest_sha256"):
        raise StageError("label staging receipt must have a distinct overlay manifest digest")
    if (value.get("profile"), value.get("mode"), value.get("index_production")) != ("player", "production", False):
        raise StageError("staging receipt release contract is invalid")
    if value.get("operator") != OPERATOR or value.get("contact") != CONTACT:
        raise StageError("staging receipt operator/contact facts are invalid")
    runtime = value.get("runtime")
    if (not isinstance(runtime, dict) or set(runtime) != {"path", "hash", "identity_sha256"}
            or not isinstance(runtime.get("path"), str)
            or not re.fullmatch(r"runtime/[0-9a-f]{16}", runtime["path"])
            or runtime.get("hash") != runtime["path"].rsplit("/", 1)[1]
            or not isinstance(runtime.get("identity_sha256"), str)
            or not SHA256_RE.fullmatch(runtime["identity_sha256"])):
        raise StageError("staging receipt runtime identity is invalid")
    policy = value.get("audio_policy")
    if policy != {
        "mode": "disabled", "pcm_output": False,
        "dsp_resampler": False, "dsp_coefficients_required": False,
    }:
        raise StageError("staging receipt audio policy is not silent")
    native = value.get("native_artifacts")
    if not isinstance(native, list):
        raise StageError("staging receipt native artifact list is invalid")
    for item in native:
        if not isinstance(item, dict) or set(item) != {"path", "bytes", "sha256"}:
            raise StageError("staging receipt native artifact record is invalid")
        _safe_relative(item["path"], "receipt native artifact path")
        if (not isinstance(item["bytes"], int) or isinstance(item["bytes"], bool) or item["bytes"] < 1
                or not isinstance(item["sha256"], str) or not SHA256_RE.fullmatch(item["sha256"])):
            raise StageError("staging receipt native artifact hash/size is invalid")
    changed = value.get("changed_files")
    expected_changed = [] if staging_mode == "exact" else sorted(OVERLAY_CHANGED_PATHS)
    if not isinstance(changed, list) or [item.get("path") for item in changed if isinstance(item, dict)] != expected_changed:
        raise StageError("staging receipt changed-file list is invalid")
    for item in changed:
        if not isinstance(item, dict) or set(item) != {
            "path", "base_size", "base_sha256", "overlay_size", "overlay_sha256",
        }:
            raise StageError("staging receipt changed-file record is invalid")
        path = _safe_relative(item["path"], "receipt changed-file path")
        if path not in OVERLAY_CHANGED_PATHS:
            raise StageError(f"receipt changed-file path is not allowlisted: {path}")
        if (not isinstance(item["base_size"], int) or isinstance(item["base_size"], bool) or item["base_size"] < 0
                or not isinstance(item["overlay_size"], int) or isinstance(item["overlay_size"], bool) or item["overlay_size"] < 0
                or not isinstance(item["base_sha256"], str) or not SHA256_RE.fullmatch(item["base_sha256"])
                or not isinstance(item["overlay_sha256"], str) or not SHA256_RE.fullmatch(item["overlay_sha256"])):
            raise StageError("receipt changed-file hash/size is invalid")
    return value


def validate_receipt(receipt: Path | str | dict[str, Any], *, source_sha: str | None = None,
                     base_manifest_sha256: str | None = None,
                     overlay_manifest_sha256: str | None = None) -> dict[str, Any]:
    """Validate a portable staging receipt and return its parsed object."""
    if isinstance(receipt, dict):
        value = receipt
    else:
        value, _ = _load_json(Path(receipt), "staging receipt")
    return _validate_receipt_value(value, source_sha=source_sha,
                                   base_manifest_sha256=base_manifest_sha256,
                                   overlay_manifest_sha256=overlay_manifest_sha256)


def audit_staging(source_root: Path | str, sha: str, base_output: Path | str,
                  base_manifest: Path | str, base_manifest_sha256: str,
                  output: Path | str, manifest: Path | str, *,
                  label_staging: bool | None = None) -> dict[str, Any]:
    """Source-audit the base and validate an exact or label staging package.

    The function never writes to ``source_root``, ``base_output`` or
    ``base_manifest``.  It invokes only the checked-in auditor, which is a
    read-only operation.  ``output`` and ``manifest`` must already contain the
    package and its sidecar.  If ``label_staging`` is supplied, it must agree
    with the mode inferred from the bytes; callers may omit it when validating
    an existing package.
    """
    source_root = Path(source_root)
    base_output = Path(base_output)
    base_manifest = Path(base_manifest)
    output = Path(output)
    manifest = Path(manifest)
    if not isinstance(base_manifest_sha256, str) or not SHA256_RE.fullmatch(base_manifest_sha256):
        raise StageError("base manifest SHA-256 must be full lowercase hexadecimal")
    _validate_source_sha(source_root, sha)
    _validate_pr15_guard(source_root)
    _require_directory(base_output, "base output")
    _require_directory(output, "staging output")
    base_value, base_raw = _load_json(base_manifest, "base manifest")
    if _sha256_bytes(base_raw) != base_manifest_sha256:
        raise StageError("base manifest SHA-256 does not match the pinned digest")
    base_contract = _manifest_contract(base_value)
    _source_audit(source_root, base_output, base_manifest)
    base_files = _validate_manifest_inventory(base_output, base_value, "base output")
    overlay_value, overlay_raw = _load_json(manifest, "staging manifest")
    overlay_contract = _manifest_contract(overlay_value)
    if set(overlay_value) != set(base_value):
        raise StageError("staging manifest metadata differs from the base manifest schema")
    for key in base_value:
        if key != "files" and overlay_value.get(key) != base_value.get(key):
            raise StageError(f"staging manifest changed protected metadata: {key}")
    if overlay_value.get("runtime") != base_value.get("runtime"):
        raise StageError("staging overlay changed producer/runtime identity metadata")
    overlay_files = _validate_manifest_inventory(output, overlay_value, "staging output")
    base_directories = _walk_directories(base_output, "base output")
    overlay_directories = _walk_directories(output, "staging output")
    if overlay_value.get("files") != _records(overlay_files):
        raise StageError("staging manifest inventory is not canonical")
    if base_contract["audio_policy"] != overlay_contract["audio_policy"]:
        raise StageError("staging overlay changed the audio policy")
    inventories_equal = base_files == overlay_files and base_directories == overlay_directories
    exact = inventories_equal and overlay_raw == base_raw
    staging_mode = "exact" if exact else "label"
    if label_staging is not None and bool(label_staging) != (staging_mode == "label"):
        raise StageError("requested staging mode differs from the package bytes")
    same_output = output.resolve(strict=False) == base_output.resolve(strict=False)
    overlaps_output = _inside(output, base_output) or _inside(base_output, output)
    same_manifest = manifest.resolve(strict=False) == base_manifest.resolve(strict=False)
    if staging_mode == "label":
        if overlay_directories != base_directories:
            raise StageError("label staging changed the directory inventory")
        if _inside(output, source_root):
            raise StageError("label staging output must be outside the source checkout")
        if overlaps_output or same_manifest:
            raise StageError("label staging output and base inputs must be separate")
    elif (overlaps_output and not same_output) or (_inside(output, source_root) and not same_output):
        raise StageError("exact staging output may be in the source checkout only when it is the audited base")
    if _inside(manifest, output):
        raise StageError("staging manifest must be outside the upload directory")
    if staging_mode == "exact":
        return _result(sha, base_manifest_sha256, base_manifest_sha256,
                       base_contract, overlay_files, [], staging_mode)
    if overlay_raw != _canonical_json_bytes(overlay_value):
        raise StageError("staging manifest is not canonical JSON")
    required = {"index.html", "_headers", "_redirects"}
    if not required.issubset(base_files) or not required.issubset(overlay_files):
        raise StageError("label staging package is missing an overlay-controlled file")
    changed = _changed_files(base_files, overlay_files)
    # Regenerate each permitted transformation from the audited base.  The
    # changed-file list and sidecar inventory alone cannot prove that a file
    # contains the exact reviewed overlay.
    if overlay_files["index.html"] != _overlay_html(base_files["index.html"], "index.html"):
        raise StageError("staging index.html overlay is not deterministic")
    if overlay_files["_headers"] != _overlay_headers(base_files["_headers"]):
        raise StageError("staging _headers overlay is not deterministic")
    for rel in sorted(set(base_files) - OVERLAY_CHANGED_PATHS):
        if base_files[rel] != overlay_files[rel]:
            raise StageError(f"staging overlay changed a protected path: {rel}")
    if "staging alpha · no audio".encode("utf-8") not in overlay_files["index.html"]:
        raise StageError("staging player edition marker is missing")
    if b"webmelee.pages.dev" in overlay_files["_headers"] or STAGING_PAGES_HOST.encode("ascii") not in overlay_files["_headers"]:
        raise StageError("staging _headers is not scoped to webmelee-staging.pages.dev")
    if overlay_files["_redirects"] != STAGING_REDIRECTS.encode("utf-8"):
        raise StageError("staging _redirects is not the comments-only staging policy")
    overlay_manifest_sha256 = _sha256_bytes(overlay_raw)
    return _result(sha, base_manifest_sha256, overlay_manifest_sha256,
                   base_contract, overlay_files, changed, staging_mode)


def _fresh_base_paths(source_root: Path, sha: str) -> tuple[Path, Path]:
    build_root = source_root / "build"
    if build_root.is_symlink():
        raise StageError("source build directory may not be a symlink")
    build_root.mkdir(parents=True, exist_ok=True)
    for index in range(1000):
        suffix = "" if index == 0 else f"-{index}"
        output = build_root / f"staging-base-{sha[:16]}{suffix}"
        manifest = output.with_name(output.name + ".manifest.json")
        if not output.exists() and not manifest.exists():
            return output, manifest
    raise StageError("could not allocate a fresh ignored base output")


def _build_base(source_root: Path, sha: str) -> tuple[Path, Path]:
    base_output, base_manifest = _fresh_base_paths(source_root, sha)
    build_script = source_root / "scripts" / "build.py"
    packager = source_root / "scripts" / "build_public.py"
    _require_file(build_script, "runtime build script")
    _require_file(packager, "public packager")
    _run([
        sys.executable, str(build_script),
        "--target", "runtime-public", "--configuration", "Release",
    ], source_root)
    _run([
        sys.executable, str(packager),
        "--profile", "player", "--mode", "production",
        "--operator", OPERATOR, "--contact", CONTACT,
        "--runtime-dir", str(source_root / "build" / "browser-public-release"),
        "--output", str(base_output), "--manifest", str(base_manifest),
    ], source_root)
    return base_output, base_manifest


def prepare_staging(source_root: Path | str, sha: str, output: Path | str,
                    receipt: Path | str, *, reuse_output: Path | str | None = None,
                    reuse_manifest: Path | str | None = None,
                    base_manifest_sha256: str | None = None,
                    label_staging: bool = False) -> dict[str, Any]:
    """Prepare a fresh staging package and portable receipt.

    Exact byte-identical mode is the default; pass ``label_staging=True`` to
    request the deterministic three-file label overlay.

    Supplying any reuse option requires all three reuse options.  The output
    and receipt are always fresh; reuse inputs are read-only.  Failed fresh
    outputs and generated base artifacts are retained for diagnosis.
    """
    source_root = Path(source_root)
    output = Path(output)
    receipt = Path(receipt)
    reuse_values = (reuse_output, reuse_manifest, base_manifest_sha256)
    if any(value is not None for value in reuse_values) and not all(value is not None for value in reuse_values):
        raise StageError("--reuse-output, --reuse-manifest and --base-manifest-sha256 are all-or-none")
    _validate_source_sha(source_root, sha)
    _validate_pr15_guard(source_root)
    _new_external_directory(output, source_root, "staging output")
    manifest = output.with_name(output.name + ".manifest.json")
    if receipt.resolve(strict=False) == manifest.resolve(strict=False):
        raise StageError("staging receipt and manifest must be separate sidecar files")
    _new_sidecar(manifest, output, "staging manifest")
    _new_sidecar(receipt, output, "staging receipt")
    base_output: Path
    base_manifest: Path
    if all(value is not None for value in reuse_values):
        base_output = Path(reuse_output)  # type: ignore[arg-type]
        base_manifest = Path(reuse_manifest)  # type: ignore[arg-type]
        _require_directory(base_output, "reused base output")
        _require_file(base_manifest, "reused base manifest")
        if _inside(output, base_output) or _inside(base_output, output):
            raise StageError("staging output and reused base output must be separate")
    else:
        base_output, base_manifest = _build_base(source_root, sha)
        base_manifest_sha256 = _sha256_file(base_manifest, "generated base manifest")
    assert base_manifest_sha256 is not None
    base_value, base_raw = _load_json(base_manifest, "base manifest")
    if _sha256_bytes(base_raw) != base_manifest_sha256:
        raise StageError("base manifest SHA-256 does not match the requested digest")
    _manifest_contract(base_value)
    # Run the immutable base audit before copying it.  audit_staging repeats
    # this check after the overlay so callers cannot skip source provenance.
    _source_audit(source_root, base_output, base_manifest)
    base_files = _validate_manifest_inventory(base_output, base_value, "base output")
    output.mkdir()
    overlay_files = (
        _apply_overlay(base_files, output)
        if label_staging else _write_package(dict(base_files), output)
    )
    overlay_value = copy.deepcopy(base_value)
    if label_staging:
        overlay_value["files"] = _records(overlay_files)
        _write_json_new(manifest, overlay_value, "staging manifest")
    else:
        _write_bytes_new(manifest, base_raw, "staging manifest")
    result = audit_staging(source_root, sha, base_output, base_manifest,
                           base_manifest_sha256, output, manifest,
                           label_staging=label_staging)
    receipt_value = _receipt_payload(result)
    _write_json_new(receipt, receipt_value, "staging receipt")
    validate_receipt(receipt, source_sha=sha,
                     base_manifest_sha256=base_manifest_sha256,
                     overlay_manifest_sha256=result["overlay_manifest_sha256"])
    result.update({"output": output, "manifest": manifest, "receipt": receipt,
                   "receipt_value": receipt_value})
    return result


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--sha", required=True, help="full 40-character source commit SHA")
    parser.add_argument("--output", type=Path, required=True, help="new external upload directory")
    parser.add_argument("--receipt", type=Path, required=True, help="new receipt path outside the upload directory")
    parser.add_argument("--reuse-output", type=Path)
    parser.add_argument("--reuse-manifest", type=Path)
    parser.add_argument("--base-manifest-sha256")
    parser.add_argument("--label-staging", action="store_true",
                        help="opt into the three-file [staging] label overlay")
    return parser


def main(argv: Iterable[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        result = prepare_staging(
            args.source_root, args.sha, args.output, args.receipt,
            reuse_output=args.reuse_output,
            reuse_manifest=args.reuse_manifest,
            base_manifest_sha256=args.base_manifest_sha256,
            label_staging=args.label_staging,
        )
    except (StageError, OSError, ValueError) as exc:
        print(f"public staging preparation rejected: {exc}", file=sys.stderr)
        return 2
    print(json.dumps({
        "output": str(result["output"]),
        "manifest": str(result["manifest"]),
        "receipt": str(result["receipt"]),
        "source_sha": result["source_sha"],
        "base_manifest_sha256": result["base_manifest_sha256"],
        "overlay_manifest_sha256": result["overlay_manifest_sha256"],
        "staging_mode": result["staging_mode"],
        "changed_files": [item["path"] for item in result["changed_files"]],
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
