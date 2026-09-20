#!/usr/bin/env python3
"""Build the small, static WebMelee public shell.

The maintenance profile reads exactly eight text templates and hashes its two
shared assets.  The player profile takes an explicit, producer-built runtime
directory and copies only the reviewed player graph into a versioned path.  It
never discovers a runtime by walking the repository.
The manifest is written beside that tree so it can describe every deployed
byte without needing a recursive self-hash convention.
"""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = ROOT / "web" / "public"
LEGAL_NOTICE_SOURCE = ROOT / "docs" / "licenses" / "runtime-third-party.txt"
LEGAL_NOTICE_OUTPUT = "licenses/runtime-third-party.txt"
PLAYER_SOURCE = ROOT / "web" / "player"
SOURCE_ALLOWLIST = (
    "index.html",
    "site.css",
    "site.js",
    "terms.html",
    "privacy.html",
    "copyright.html",
    "notices.html",
    "404.html",
)
PLAYER_SOURCE_ALLOWLIST = ("index.html", "player.css", "player-shell.mjs")
PLAYER_RUNTIME_FILES = (
    "melee-runtime.mjs",
    "runtime-assets.mjs",
    "disc-image.mjs",
    "prototype-keyboard-layouts.mjs",
    "controller-input.mjs",
    "controller-panel.mjs",
    "controller-panel.css",
    "controller-settings.mjs",
    "controller-settings.css",
    "gameplay_public.js",
    "gameplay_public.wasm",
)
PLAYER_ALLOWED_ARTIFACTS = frozenset(PLAYER_RUNTIME_FILES) | {"gameplay_public.data"}
PLAYER_SOURCE_RUNTIME_FILES = (
    "melee-runtime.mjs",
    "runtime-assets.mjs",
    "disc-image.mjs",
    "prototype-keyboard-layouts.mjs",
    "controller-input.mjs",
    "controller-panel.mjs",
    "controller-panel.css",
    "controller-settings.mjs",
    "controller-settings.css",
)
RUNTIME_IDENTITY_SCHEMA = "melee-web-runtime-public-build-v2"
RUNTIME_IDENTITY_NAME = "runtime-public-identity.json"
RUNTIME_SOURCE_FILES = (
    "CMakeLists.txt",
    "cmake/FighterRuntime.cmake",
    "patches/melee-gameplay.patch",
    "src/gameplay_menu_browser.cpp",
    "src/gameplay_asset_manifest.cpp",
    "src/gameplay_asset_manifest.hpp",
    "src/runtime_asset_scope.hpp",
    "src/gameplay_menu_world.cpp",
    "src/gameplay_match_session.cpp",
    "src/gameplay_audio.c",
    "src/gameplay_audio_bank.cpp",
    "src/browser_input.cpp",
    "src/browser_input.h",
    "src/browser_controllers.cpp",
    "src/browser_controllers.h",
    "scripts/bootstrap.py",
    "scripts/build.py",
    "scripts/gameplay_bool.py",
    "scripts/gameplay_sources.py",
    "scripts/generate_common_schema.py",
    "scripts/generate_fighter_registry.py",
    "scripts/materialize_pipeline_cache.py",
    "web/initial_pipeline_cache.db.gz.b64",
    "patches/aurora-browser.patch",
    "dependencies.lock.json",
    "tests/native_menu_alarm_unavailable.c",
    "tests/native_menu_fighter_input.c",
    "tests/native_menu_stage_input.c",
)
RUNTIME_REQUIRED_EXPORTS = (
    "_main", "_malloc", "_free", "_melee_web_native_menu_file",
    "_melee_web_native_menu_prepare", "_melee_web_native_menu_launch",
    "_melee_web_native_menu_unload", "_melee_web_native_menu_pause",
    "_melee_web_native_menu_message", "_melee_web_native_menu_running",
    "_melee_web_native_menu_phase", "_melee_web_native_menu_cache_idle",
    "_melee_web_input_set_activity", "_melee_web_input_set_keyboard",
    "_melee_web_input_set_keyboard_port", "_melee_web_input_set_keyboard_layout",
)
RUNTIME_FORBIDDEN_EXPORTS = frozenset({
    "_melee_web_native_menu_replay", "_melee_web_native_menu_replay_cursor",
    "_melee_web_native_menu_confirm_check", "_melee_web_native_menu_pad_sample",
    "_melee_web_native_menu_pad_sample_full", "_melee_web_native_menu_player_state",
    "_melee_web_native_menu_drive_fighter", "_melee_web_native_menu_drive_stage",
    "_melee_web_native_menu_stock_check", "_melee_web_native_menu_stock_check_ready",
    "_melee_web_native_menu_diagnostics", "_melee_web_native_menu_memory",
    "_melee_web_css_observe", "_melee_web_sss_observe", "_melee_web_input_message",
})
PREPARED_GAMEPLAY_PATH = "build/gameplay-source"
PREPARED_GAMEPLAY_PATCHES = {
    "composed_patch": "build/gameplay-source/.git/melee-web-composed.patch",
    "reviewed_patch": "build/gameplay-source/.git/melee-web-gameplay.patch",
}
RUNTIME_TOOLCHAIN_PATHS = frozenset({
    ".deps/emsdk/.emscripten",
    ".deps/emsdk/upstream/emscripten/emcc",
    ".deps/emsdk/upstream/emscripten/emscripten-version.txt",
    ".venv/bin/cmake",
    ".venv/bin/ninja",
})
RUNTIME_ARTIFACT_ROOTS = frozenset({"build/browser-public-release",
                                    "build/browser-public-selective-release"})
PIPELINE_SEED_PATHS = {
    "source": "web/initial_pipeline_cache.db.gz.b64",
    "materialized": "build/browser-public-release/initial_pipeline_cache.db",
}
HTML_INPUTS = (
    "index.html",
    "terms.html",
    "privacy.html",
    "copyright.html",
    "notices.html",
    "404.html",
)
HTML_OUTPUTS = frozenset(HTML_INPUTS)
MAX_FILE_BYTES = 25 * 1024 * 1024
MAX_TOTAL_BYTES = 25 * 1024 * 1024
# Cloudflare Pages enforces a 25 MiB maximum for each published file. Keep a
# larger bound only while reading a producer input so the final inventory can
# reject it deterministically before publication.
RUNTIME_INPUT_MAX_FILE_BYTES = 256 * 1024 * 1024
RUNTIME_MAX_FILE_BYTES = MAX_FILE_BYTES
RUNTIME_MAX_TOTAL_BYTES = 512 * 1024 * 1024
SCHEMA = "melee-web-public-release-v1"

STYLE_TOKEN = "{{STYLE_URL}}"
SCRIPT_TOKEN = "{{SCRIPT_URL}}"
CONTACT_TOKEN = "{{CONTACT_EMAIL}}"
OPERATOR_TOKEN = "{{OPERATOR_NAME}}"

CSP = (
    "default-src 'none'; script-src 'self'; style-src 'self'; img-src data:; "
    "connect-src 'none'; font-src 'none'; object-src 'none'; base-uri 'none'; "
    "form-action 'none'; frame-ancestors 'none'; worker-src 'none'; manifest-src 'none'"
)
PLAYER_CSP = (
    "default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self'; "
    "img-src data:; connect-src 'self'; font-src 'none'; object-src 'none'; base-uri 'none'; "
    "form-action 'none'; frame-ancestors 'none'; worker-src 'self'; media-src 'self'; manifest-src 'none'"
)
PERMISSIONS = "fullscreen=(self), camera=(), microphone=(), geolocation=(), payment=(), usb=()"

# These signatures cover formats prohibited by the public release policy.
# They are checked in content,
# rather than inferred from a file extension that a caller can rename.
MAGIC_SIGNATURES: tuple[tuple[str, bytes], ...] = (
    ("CISO disc image", b"CISO"),
    ("RVZ disc image", b"RVZ"),
    ("WBFS disc image", b"WBFS"),
    ("WebAssembly", b"\x00asm"),
    ("ELF executable", b"\x7fELF"),
    ("DOS executable", b"MZ"),
    ("ZIP/archive", b"PK"),
    ("RAR archive", b"Rar!\x1a\x07"),
    ("AR archive", b"!<arch>\n"),
    ("Mach-O executable", b"\xcf\xfa\xed\xfe"),
    ("Java class", b"\xca\xfe\xba\xbe"),
    ("SQLite database", b"SQLite format 3\x00"),
    ("gzip archive", b"\x1f\x8b"),
    ("bzip archive", b"BZh"),
    ("XZ archive", b"\xfd7zXZ\x00"),
    ("7z archive", b"7z\xbc\xaf\x27\x1c"),
    ("PDF document", b"%PDF-"),
    ("PNG image", b"\x89PNG\r\n\x1a\n"),
    ("JPEG image", b"\xff\xd8\xff"),
    ("GIF image", b"GIF8"),
    ("RIFF media", b"RIFF"),
    ("Ogg media", b"OggS"),
    ("ID3 audio", b"ID3"),
    ("FLAC audio", b"fLaC"),
    ("Matroska/WebM media", b"\x1a\x45\xdf\xa3"),
)

# These expressions describe executable or embedded content.  The legal shell
# is allowed to say that storage, disc selection, or a future runtime is out of
# scope; those words in prose do not make a prohibited capability.  The same
# checks are applied to generated files and to audit input.
FORBIDDEN_TEXT_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("file input", re.compile(r"(?:<input\b|type\s*=\s*[\"']file|\.files\b|showOpenFilePicker|FileReader)", re.I)),
    ("persistent browser storage API", re.compile(r"\b(?:localStorage|sessionStorage|indexedDB|opfs)\s*[.\[]", re.I)),
    ("network API", re.compile(r"\b(?:fetch|XMLHttpRequest|WebSocket|EventSource)\s*[(.]", re.I)),
    ("runtime/importer code", re.compile(r"(?:\b(?:importDisc|WebAssembly|wasm|runtime)\s*[(.\"']|(?:from|import)\s*[\"'][^\"']*(?:runtime|wasm))", re.I)),
    ("private path", re.compile(r"(?:/Users/|/home/|[A-Za-z]:[\\/]Users[\\/]|assets-local|\.deps(?:/|\\\\)|file://|~[/\\])", re.I)),
    ("secret material", re.compile(r"(?:BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY|AKIA[0-9A-Z]{16}|(?:api[_-]?key|secret|password|authorization)\s*[:=])", re.I)),
    ("diagnostic code", re.compile(r"(?:\bdebugger\b|\bconsole\s*\.|\btrace\s*[(])", re.I)),
)
NOTICE_PRIVATE_PATH_PATTERN = re.compile(
    r"(?:/Users/|/home/|[A-Za-z]:[\\/]Users[\\/]|assets-local|file://|~[/\\])", re.I
)


class BuildError(ValueError):
    """A user-facing fail-closed packaging error."""


def _display(path: Path) -> str:
    return path.as_posix()


def _is_symlink(path: Path) -> bool:
    try:
        return path.is_symlink()
    except OSError as exc:
        raise BuildError(f"cannot inspect path {path}: {exc}") from exc


def _validate_directory(path: Path, label: str) -> None:
    if _is_symlink(path):
        raise BuildError(f"{label} may not be a symlink: {_display(path)}")
    if not path.is_dir():
        raise BuildError(f"{label} is not a directory: {_display(path)}")


def _validate_size(path: Path, size: int | None = None) -> int:
    try:
        actual = path.stat().st_size if size is None else size
    except OSError as exc:
        raise BuildError(f"cannot stat {_display(path)}: {exc}") from exc
    if actual > MAX_FILE_BYTES:
        raise BuildError(f"file exceeds 25 MiB limit: {_display(path)} ({actual} bytes)")
    return actual


def _validate_magic(data: bytes, label: str) -> None:
    for name, signature in MAGIC_SIGNATURES:
        if data.startswith(signature):
            raise BuildError(f"{name} signature rejected in {label}")
    # ISO-9660 primary volume descriptors are at a fixed offset.  Also catch a
    # renamed/truncated descriptor if the identifying bytes are retained.
    if len(data) >= 0x8006 and data[0x8001:0x8006] == b"CD001":
        raise BuildError(f"ISO-9660 disc signature rejected in {label}")
    if b"CD001" in data:
        raise BuildError(f"disc signature rejected in {label}")
    if re.fullmatch(rb"[A-Z0-9]{4}[0-9]{2}", data[:6] or b""):
        raise BuildError(f"GameCube disc header rejected in {label}")
    if b"\x00" in data:
        raise BuildError(f"binary content rejected in {label}")
    for name, pattern in FORBIDDEN_TEXT_PATTERNS:
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise BuildError(f"non-UTF-8/binary content rejected in {label}") from exc
        if pattern.search(text):
            raise BuildError(f"{name} text rejected in {label}")
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise BuildError(f"non-UTF-8 content rejected in {label}") from exc
    if any(ord(char) < 0x09 or 0x0e <= ord(char) < 0x20 for char in text):
        raise BuildError(f"control character rejected in {label}")


def _read_text(path: Path, label: str) -> bytes:
    if _is_symlink(path):
        raise BuildError(f"symlink input rejected: {_display(path)}")
    if not path.is_file():
        raise BuildError(f"input is not a regular file: {_display(path)}")
    size = _validate_size(path)
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise BuildError(f"cannot read {_display(path)}: {exc}") from exc
    if len(data) != size:
        raise BuildError(f"input changed while reading: {_display(path)}")
    _validate_magic(data, label)
    return data


def _validate_source(source: Path) -> dict[str, bytes]:
    _validate_directory(source, "source directory")
    try:
        entries = list(source.iterdir())
    except OSError as exc:
        raise BuildError(f"cannot inspect source directory {_display(source)}: {exc}") from exc
    allowed = set(SOURCE_ALLOWLIST)
    for entry in entries:
        if entry.name not in allowed or entry.name in (".", ".."):
            raise BuildError(f"unauthorized source path: {_display(entry)}")
        if _is_symlink(entry):
            raise BuildError(f"symlink source entry rejected: {_display(entry)}")
        if entry.is_dir():
            raise BuildError(f"source entry must be a regular file: {_display(entry)}")
    result: dict[str, bytes] = {}
    for name in SOURCE_ALLOWLIST:
        path = source / name
        if not path.exists():
            raise BuildError(f"missing required source input: {_display(path)}")
        result[name] = _read_text(path, name)
    required_tokens = {
        "index.html": (STYLE_TOKEN, SCRIPT_TOKEN),
        "terms.html": (STYLE_TOKEN, CONTACT_TOKEN, OPERATOR_TOKEN),
        "privacy.html": (STYLE_TOKEN, CONTACT_TOKEN, OPERATOR_TOKEN),
        "copyright.html": (STYLE_TOKEN, CONTACT_TOKEN, OPERATOR_TOKEN),
        "notices.html": (STYLE_TOKEN,),
        "404.html": (STYLE_TOKEN,),
    }
    for name in HTML_INPUTS:
        text = result[name].decode("utf-8")
        required = required_tokens[name]
        missing = [token for token in required if token not in text]
        if missing:
            raise BuildError(f"{name} is missing placeholder(s): {', '.join(missing)}")
        if re.search(r"<style\b|\bon[a-z]+\s*=", text, re.I):
            raise BuildError(f"inline style or event handler rejected in {name}")
    if 'href="/licenses/runtime-third-party.txt"' not in result["notices.html"].decode("utf-8"):
        raise BuildError("notices.html must link the complete runtime third-party notice")
    css = result["site.css"].decode("utf-8")
    js = result["site.js"].decode("utf-8")
    if "requestFullscreen" not in js or "fullscreenchange" not in js:
        raise BuildError("site.js must implement the fullscreen control")
    if re.search(r"\b(?:requestAnimationFrame|setTimeout|setInterval)\s*[(]", js):
        raise BuildError("non-fullscreen scheduler code rejected in site.js")
    if not css.strip():
        raise BuildError("site.css may not be empty")
    return result


def _read_legal_notice() -> bytes:
    """Read the source-bound full runtime notice copied into every profile."""
    if _is_symlink(LEGAL_NOTICE_SOURCE):
        raise BuildError(f"symlink input rejected: {_display(LEGAL_NOTICE_SOURCE)}")
    if not LEGAL_NOTICE_SOURCE.is_file():
        raise BuildError(f"input is not a regular file: {_display(LEGAL_NOTICE_SOURCE)}")
    size = _validate_size(LEGAL_NOTICE_SOURCE)
    try:
        data = LEGAL_NOTICE_SOURCE.read_bytes()
    except OSError as exc:
        raise BuildError(f"cannot read {_display(LEGAL_NOTICE_SOURCE)}: {exc}") from exc
    if len(data) != size:
        raise BuildError(f"input changed while reading: {_display(LEGAL_NOTICE_SOURCE)}")
    for name, signature in MAGIC_SIGNATURES:
        if data.startswith(signature):
            raise BuildError(f"{name} signature rejected in {LEGAL_NOTICE_OUTPUT}")
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise BuildError(f"non-UTF-8 content rejected in {LEGAL_NOTICE_OUTPUT}") from exc
    if "\x00" in text or any(ord(char) < 0x09 or 0x0e <= ord(char) < 0x20 for char in text):
        raise BuildError(f"control character rejected in {LEGAL_NOTICE_OUTPUT}")
    # Notices are prose and may mention runtime APIs, Wasm, or storage terms.
    # Keep only the content-safety checks relevant to a distributed text file.
    for name, pattern in (("private path", NOTICE_PRIVATE_PATH_PATTERN),
                          ("secret material", dict(FORBIDDEN_TEXT_PATTERNS)["secret material"])):
        if pattern.search(text):
            raise BuildError(f"{name} text rejected in {LEGAL_NOTICE_OUTPUT}")
    return data


def _read_player_text(path: Path, label: str) -> bytes:
    """Read a player source text file without applying shell-only checks."""
    if _is_symlink(path):
        raise BuildError(f"symlink input rejected: {_display(path)}")
    if not path.is_file():
        raise BuildError(f"input is not a regular file: {_display(path)}")
    size = _validate_size(path)
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise BuildError(f"cannot read {_display(path)}: {exc}") from exc
    if len(data) != size:
        raise BuildError(f"input changed while reading: {_display(path)}")
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise BuildError(f"non-UTF-8 content rejected in {label}") from exc
    if "\x00" in text or any(ord(char) < 0x09 or 0x0e <= ord(char) < 0x20 for char in text):
        raise BuildError(f"control character rejected in {label}")
    # Player source may import/fetch the reviewed runtime and offer local disc
    # selection. Keep the source boundary free of credentials, local paths,
    # and debug/evidence sinks.
    for name, pattern in FORBIDDEN_TEXT_PATTERNS:
        if name in {"network API", "runtime/importer code", "file input", "persistent browser storage API"}:
            continue
        if pattern.search(text):
            raise BuildError(f"{name} text rejected in {label}")
    return data


def _validate_player_source(source: Path) -> dict[str, bytes]:
    _validate_directory(source, "player source directory")
    try:
        entries = list(source.iterdir())
    except OSError as exc:
        raise BuildError(f"cannot inspect player source directory {_display(source)}: {exc}") from exc
    allowed = set(PLAYER_SOURCE_ALLOWLIST)
    for entry in entries:
        if entry.name not in allowed or entry.name in (".", ".."):
            raise BuildError(f"unauthorized player source path: {_display(entry)}")
        if _is_symlink(entry) or entry.is_dir():
            raise BuildError(f"player source entry must be a regular file: {_display(entry)}")
    result: dict[str, bytes] = {}
    for name in PLAYER_SOURCE_ALLOWLIST:
        path = source / name
        if not path.exists():
            raise BuildError(f"missing required player source input: {_display(path)}")
        result[name] = _read_player_text(path, name)
    html_text = result["index.html"].decode("utf-8")
    missing = [token for token in (STYLE_TOKEN, SCRIPT_TOKEN) if token not in html_text]
    if missing:
        raise BuildError(f"player index.html is missing placeholder(s): {', '.join(missing)}")
    if not re.search(r"<script\b[^>]*\btype\s*=\s*[\"']module[\"'][^>]*>", html_text, re.I):
        raise BuildError("player index.html must use a module script")
    if re.search(r"<style\b|\bon[a-z]+\s*=", html_text, re.I):
        raise BuildError("inline style or event handler rejected in player index.html")
    css_text = result["player.css"].decode("utf-8")
    if not css_text.strip() or re.search(r"url\s*[(]", css_text, re.I):
        raise BuildError("player.css must be non-empty and contain no external or embedded URLs")
    shell = result["player-shell.mjs"].decode("utf-8")
    for import_path in ("../melee-runtime.mjs", "../controller-settings.mjs"):
        if not re.search(rf"(?:from|import)\s*[\"']{re.escape(import_path)}[\"']", shell):
            raise BuildError(f"player-shell.mjs is missing reviewed import {import_path}")
    return result


def _identity_path(runtime_dir: Path) -> Path:
    candidates = [runtime_dir.parent / RUNTIME_IDENTITY_NAME, ROOT / "build" / RUNTIME_IDENTITY_NAME]
    for candidate in candidates:
        if candidate.is_file() and not _is_symlink(candidate):
            return candidate
    raise BuildError(
        "runtime-dir must be the producer artifact root and its sibling must contain "
        f"{RUNTIME_IDENTITY_NAME}"
    )


def _unique_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate runtime identity key: {key}")
        result[key] = value
    return result


def _identity_artifacts(value: object, artifact_root: str | None = None) -> dict[str, dict[str, object]]:
    """Normalize the producer's root-relative artifact records."""
    raw = value.get("artifacts") if isinstance(value, dict) else None
    result: dict[str, dict[str, object]] = {}
    if not isinstance(raw, list):
        raise BuildError("runtime identity artifacts must be a list")
    if artifact_root is None:
        artifact_root = value.get("artifact_root") if isinstance(value, dict) else None
    if not isinstance(artifact_root, str) or not artifact_root or artifact_root.startswith(("/", "\\")):
        raise BuildError("runtime identity artifact_root is invalid")
    root_path = Path(artifact_root)
    for item in raw:
        if not isinstance(item, dict) or set(item) != {"path", "bytes", "sha256"}:
            raise BuildError("runtime identity artifact records must contain path, bytes and sha256")
        path = item["path"]
        if (not isinstance(path, str) or "\\" in path or path.startswith("/")
                or Path(path).as_posix() != path
                or any(part in ("", ".", "..") for part in Path(path).parts)):
            raise BuildError(f"runtime identity contains an unauthorized artifact path: {path!r}")
        try:
            local_path = Path(path).relative_to(root_path).as_posix()
        except ValueError as exc:
            raise BuildError(f"runtime artifact path is outside artifact_root: {path!r}") from exc
        if "/" in local_path or local_path not in PLAYER_ALLOWED_ARTIFACTS:
            raise BuildError(f"runtime identity contains an unauthorized artifact path: {path!r}")
        if local_path in result:
            raise BuildError(f"runtime identity has a duplicate artifact: {path!r}")
        sha256 = item["sha256"]
        size = item["bytes"]
        if not isinstance(sha256, str) or not re.fullmatch(r"[0-9a-f]{64}", sha256):
            raise BuildError(f"runtime identity has an invalid SHA-256 for {local_path!r}")
        if not isinstance(size, int) or isinstance(size, bool) or size < 1:
            raise BuildError(f"runtime identity must include a positive byte count for {local_path!r}")
        result[local_path] = {"sha256": sha256, "size": size}
    return result


def _parse_wasm_exports(data: bytes, label: str) -> list[str]:
    if not data.startswith(b"\x00asm\x01\x00\x00\x00"):
        raise BuildError(f"runtime Wasm is not a version 1 module: {label}")
    offset = 8

    def read_u32() -> int:
        nonlocal offset
        value = 0
        shift = 0
        while offset < len(data) and shift < 35:
            byte = data[offset]
            offset += 1
            value |= (byte & 0x7f) << shift
            if not byte & 0x80:
                return value
            shift += 7
        raise BuildError(f"malformed Wasm length in {label}")

    def read_name() -> str:
        length = read_u32()
        nonlocal offset
        end = offset + length
        if end > len(data):
            raise BuildError(f"truncated Wasm export in {label}")
        try:
            value = data[offset:end].decode("utf-8")
        except UnicodeDecodeError as exc:
            raise BuildError(f"invalid Wasm export name in {label}") from exc
        offset = end
        return value

    exports: list[dict[str, int | str]] = []
    while offset < len(data):
        section = data[offset]
        offset += 1
        length = read_u32()
        end = offset + length
        if end > len(data):
            raise BuildError(f"truncated Wasm section in {label}")
        if section == 7:
            count = read_u32()
            for _ in range(count):
                name = read_name()
                if offset >= end:
                    raise BuildError(f"truncated Wasm export kind in {label}")
                kind = data[offset]
                offset += 1
                index = read_u32()
                exports.append({"name": name, "kind": kind, "index": index})
            if offset != end:
                raise BuildError(f"malformed Wasm export section in {label}")
        offset = end
    if len(exports) != len({item["name"] for item in exports}):
        raise BuildError(f"duplicate Wasm exports in {label}")
    return exports


def _read_runtime_identity(runtime_dir: Path) -> tuple[dict[str, object], dict[str, bytes], str, bytes]:
    _validate_directory(runtime_dir, "runtime directory")
    identity_path = _identity_path(runtime_dir)
    try:
        identity_bytes = identity_path.read_bytes()
        value = json.loads(identity_bytes.decode("utf-8"), object_pairs_hook=_unique_object)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
        raise BuildError(f"runtime identity is not valid UTF-8 JSON: {_display(identity_path)}: {exc}") from exc
    if not isinstance(value, dict) or value.get("schema") != RUNTIME_IDENTITY_SCHEMA:
        raise BuildError(f"unsupported runtime identity schema in {_display(identity_path)}")
    required_identity = {
        "schema", "target", "configuration", "artifact_root", "artifacts", "wasm_exports",
        "source_inputs", "toolchain", "pipeline_seed", "upload_convention",
        "audio_policy", "audio_graph",
    }
    if set(value) != required_identity:
        raise BuildError("runtime identity has unexpected or missing producer fields")
    if value.get("target") != "runtime-public" or value.get("configuration") != "Release":
        raise BuildError("runtime identity is not the Release runtime-public target")
    if not isinstance(value.get("artifact_root"), str) or value["artifact_root"] not in RUNTIME_ARTIFACT_ROOTS:
        raise BuildError("runtime identity artifact_root is not the reviewed public Release output")
    _validate_audio_policy(value)
    convention = value.get("upload_convention")
    if (not isinstance(convention, dict)
            or convention.get("identity_path") != "build/runtime-public-identity.json"
            or convention.get("identity_is_outside_artifact_root") is not True):
        raise BuildError("runtime identity upload convention is not the reviewed producer output")
    artifacts = _identity_artifacts(value)
    required = {"gameplay_public.js", "gameplay_public.wasm", "gameplay_public.data"}
    if set(artifacts) != required:
        missing = sorted(required - set(artifacts))
        extras = sorted(set(artifacts) - required)
        details = []
        if missing:
            details.append(f"missing: {', '.join(missing)}")
        if extras:
            details.append(f"unexpected: {', '.join(extras)}")
        raise BuildError(f"runtime identity artifact set is not the reviewed producer output ({'; '.join(details)})")
    files: dict[str, bytes] = {}
    for rel, record in artifacts.items():
        path = runtime_dir / rel
        if _is_symlink(path) or not path.is_file():
            raise BuildError(f"runtime artifact is missing or not a regular file: {_display(path)}")
        try:
            data = path.read_bytes()
        except OSError as exc:
            raise BuildError(f"cannot read runtime artifact {_display(path)}: {exc}") from exc
        if len(data) > RUNTIME_INPUT_MAX_FILE_BYTES:
            raise BuildError(f"runtime input exceeds 256 MiB pre-read limit: {rel}")
        if len(data) != record["size"] or hashlib.sha256(data).hexdigest() != record["sha256"]:
            raise BuildError(f"runtime artifact identity mismatch: {rel}")
        files[rel] = data
    wasm_exports = value.get("wasm_exports")
    if not isinstance(wasm_exports, dict) or set(wasm_exports) != {"required", "functions", "all", "javascript_bindings", "forbidden_absent"}:
        raise BuildError("runtime identity must contain the producer Wasm export record")
    actual_export_records = _parse_wasm_exports(files["gameplay_public.wasm"], "gameplay_public.wasm")
    actual_exports = [item["name"] for item in actual_export_records]
    actual_functions = [item["name"] for item in actual_export_records if item["kind"] == 0]
    all_exports = wasm_exports.get("all")
    if not isinstance(all_exports, list) or all_exports != actual_export_records:
        raise BuildError("runtime identity Wasm exports do not match gameplay_public.wasm")
    if wasm_exports.get("required") != list(RUNTIME_REQUIRED_EXPORTS):
        raise BuildError("runtime identity required Wasm export list is not the reviewed API")
    if wasm_exports.get("functions") != actual_functions:
        raise BuildError("runtime identity Wasm function list does not match gameplay_public.wasm")
    if wasm_exports.get("forbidden_absent") != sorted(RUNTIME_FORBIDDEN_EXPORTS):
        raise BuildError("runtime identity forbidden Wasm export list is not the reviewed policy")
    if set(actual_exports) & RUNTIME_FORBIDDEN_EXPORTS:
        raise BuildError("runtime identity Wasm exports do not match gameplay_public.wasm")
    bindings = wasm_exports.get("javascript_bindings")
    if (not isinstance(bindings, dict) or set(bindings) != set(RUNTIME_REQUIRED_EXPORTS)
            or any(not isinstance(name, str) for name in bindings.values())):
        raise BuildError("runtime identity JavaScript binding map is incomplete")
    function_names = {item["name"] for item in actual_export_records if item["kind"] == 0}
    if set(bindings.values()) - function_names:
        raise BuildError("runtime identity JavaScript binding is absent from gameplay_public.wasm")
    runtime_hash = value.get("runtime_hash", value.get("hash"))
    if runtime_hash is None:
        canonical = json.dumps(artifacts, sort_keys=True, separators=(",", ":")).encode("utf-8")
        runtime_hash = hashlib.sha256(canonical).hexdigest()
    if not isinstance(runtime_hash, str) or not re.fullmatch(r"[0-9a-f]{16,64}", runtime_hash):
        raise BuildError("runtime identity runtime_hash must be 16-64 lowercase hexadecimal characters")
    return value, files, runtime_hash[:16], identity_bytes


def _validate_audio_policy(identity: dict[str, object]) -> None:
    """Require the producer's source-bound proof that the alpha has no audio."""
    artifact_root = identity.get("artifact_root")
    if not isinstance(artifact_root, str) or artifact_root not in RUNTIME_ARTIFACT_ROOTS:
        raise BuildError("runtime identity artifact_root is not the reviewed public Release output")
    policy = identity.get("audio_policy")
    expected_policy = {
        "mode": "disabled",
        "pcm_output": False,
        "dsp_resampler": False,
        "dsp_coefficients_required": False,
    }
    if policy != expected_policy:
        raise BuildError("runtime identity audio_policy is not the reviewed disabled policy")
    proof = identity.get("audio_graph")
    if not isinstance(proof, dict):
        raise BuildError("runtime identity audio_graph proof is missing")
    if proof.get("schema") != "melee-web-public-audio-graph-v2":
        raise BuildError("runtime identity audio_graph proof schema is unsupported")
    if set(proof) != {"schema", "target", "excluded_inputs", "ninja", "compile_commands", "depfile", "ninja_deps", "checks"}:
        raise BuildError("runtime identity audio_graph proof has unexpected or missing fields")
    if proof.get("target") != "gameplay_public" or proof.get("excluded_inputs") != [
        "src/gameplay_audio_resample.c", "src/gameplay_audio_resample.h"
    ]:
        raise BuildError("runtime identity audio_graph does not exclude the reviewed GPL resampler inputs")

    def require_hash(value: object, label: str) -> str:
        if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value):
            raise BuildError(f"runtime identity audio_graph {label} hash is invalid")
        return value

    ninja = proof.get("ninja")
    if not isinstance(ninja, dict) or set(ninja) != {
        "path", "target_statement_sha256", "source_archive_statement_sha256",
        "asset_archive_statement_sha256", "target_inputs", "source_archive_inputs", "asset_archive_inputs",
    } or ninja.get("path") != f"{artifact_root}/build.ninja":
        raise BuildError("runtime identity audio_graph Ninja evidence is incomplete")
    ninja_path = _identity_repo_path(ninja["path"], "audio graph Ninja")
    if _is_symlink(ninja_path) or not ninja_path.is_file():
        raise BuildError("runtime identity audio_graph Ninja file is missing")
    try:
        ninja_text = ninja_path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as exc:
        raise BuildError("runtime identity audio_graph Ninja file is unreadable") from exc

    def ninja_block(output: str) -> list[str]:
        lines = ninja_text.splitlines()
        for index, line in enumerate(lines):
            if not line.startswith(f"build {output}:"):
                continue
            block = [line]
            cursor = index + 1
            while cursor < len(lines) and (lines[cursor].startswith("  ") or not lines[cursor]):
                block.append(lines[cursor])
                cursor += 1
            return block
        raise BuildError(f"runtime identity audio_graph Ninja statement is missing: {output}")

    def statement_hash(output: str) -> tuple[str, list[str]]:
        block = ninja_block(output)
        tokens = [token for token in " ".join(block).split()
                  if token.startswith("CMakeFiles/") or token.startswith("lib")]
        return hashlib.sha256("\n".join(block).encode()).hexdigest(), tokens

    nonpublic_archive = re.compile(r"(?:^|/)libfighter_(?:source|asset)_runtime\.a:?$")
    for output, hash_key, inputs_key in (
        ("gameplay_public.js", "target_statement_sha256", "target_inputs"),
        ("libfighter_source_runtime_public.a", "source_archive_statement_sha256", "source_archive_inputs"),
        ("libfighter_asset_runtime_public.a", "asset_archive_statement_sha256", "asset_archive_inputs"),
    ):
        actual_hash, actual_inputs = statement_hash(output)
        if require_hash(ninja.get(hash_key), f"{hash_key}") != actual_hash or ninja.get(inputs_key) != actual_inputs:
            raise BuildError(f"runtime identity audio_graph Ninja evidence is stale: {output}")
        if any("gameplay_audio_resample." in item for item in actual_inputs):
            raise BuildError("runtime identity audio_graph Ninja graph references the GPL resampler")
        if any(nonpublic_archive.fullmatch(item) for item in actual_inputs):
            raise BuildError("runtime identity audio_graph public target closure references a non-public fighter archive")

    commands = proof.get("compile_commands")
    if not isinstance(commands, dict) or set(commands) != {
        "path", "public_audio_command_sha256", "public_audio_object", "public_resampler_compile_commands",
    } or commands.get("path") != f"{artifact_root}/compile_commands.json":
        raise BuildError("runtime identity audio_graph compile command evidence is incomplete")
    compile_path = _identity_repo_path(commands["path"], "audio graph compile commands")
    if _is_symlink(compile_path) or not compile_path.is_file():
        raise BuildError("runtime identity audio_graph compile command database is missing")
    try:
        compile_entries = json.loads(compile_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise BuildError("runtime identity audio_graph compile command database is unreadable") from exc
    if not isinstance(compile_entries, list):
        raise BuildError("runtime identity audio_graph compile command database is invalid")
    public_audio = [entry for entry in compile_entries if isinstance(entry, dict)
                    and isinstance(entry.get("command"), str)
                    and "fighter_source_runtime_public.dir" in entry["command"]
                    and isinstance(entry.get("file"), str) and entry["file"].endswith("/src/gameplay_audio.c")]
    public_resampler = [entry for entry in compile_entries if isinstance(entry, dict)
                        and isinstance(entry.get("command"), str)
                        and "fighter_source_runtime_public.dir" in entry["command"]
                        and "gameplay_audio_resample.c" in str(entry.get("file", ""))]
    if len(public_audio) != 1 or public_resampler or commands.get("public_resampler_compile_commands") != 0:
        raise BuildError("runtime identity audio_graph compile commands do not prove the silent audio unit")
    audio_command = public_audio[0].get("command")
    if not isinstance(audio_command, str) or "MELEE_WEB_PUBLIC_AUDIO_DISABLED" not in audio_command:
        raise BuildError("runtime identity audio_graph silent policy compile definition is missing")
    if require_hash(commands.get("public_audio_command_sha256"), "public audio command") != hashlib.sha256(audio_command.encode()).hexdigest():
        raise BuildError("runtime identity audio_graph public audio compile command evidence is stale")
    if commands.get("public_audio_object") != "CMakeFiles/fighter_source_runtime_public.dir/src/gameplay_audio.c.o":
        raise BuildError("runtime identity audio_graph public audio object is invalid")

    depfile = proof.get("depfile")
    if not isinstance(depfile, dict) or set(depfile) != {"path", "present", "resampler_header_referenced"}:
        raise BuildError("runtime identity audio_graph depfile evidence is incomplete")
    if depfile.get("path") != f"{artifact_root}/CMakeFiles/fighter_source_runtime_public.dir/src/gameplay_audio.c.o.d":
        raise BuildError("runtime identity audio_graph depfile path is invalid")
    depfile_path = _identity_repo_path(depfile["path"], "audio graph depfile")
    if _is_symlink(depfile_path):
        raise BuildError("runtime identity audio_graph depfile may not be a symlink")
    depfile_text = ""
    if depfile_path.is_file():
        try:
            depfile_text = depfile_path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as exc:
            raise BuildError("runtime identity audio_graph depfile is unreadable") from exc
    elif depfile_path.exists():
        raise BuildError("runtime identity audio_graph depfile is not a regular file")
    if depfile.get("present") is not bool(depfile_text) or depfile.get("resampler_header_referenced") is not False:
        raise BuildError("runtime identity audio_graph depfile evidence is stale")
    if "gameplay_audio_resample.h" in depfile_text:
        raise BuildError("runtime identity audio_graph depfile references the GPL resampler header")

    ninja_deps = proof.get("ninja_deps")
    if not isinstance(ninja_deps, dict) or set(ninja_deps) != {
        "object", "sha256", "resampler_header_referenced",
    } or ninja_deps.get("object") != "CMakeFiles/fighter_source_runtime_public.dir/src/gameplay_audio.c.o":
        raise BuildError("runtime identity audio_graph Ninja dependency evidence is incomplete")
    ninja_path = _identity_repo_path(".venv/bin/ninja", "project Ninja")
    if _is_symlink(ninja_path) or not ninja_path.is_file():
        raise BuildError("runtime identity audio_graph project Ninja is missing")
    try:
        deps_result = subprocess.run(
            [str(ninja_path), "-C", artifact_root, "-t", "deps", ninja_deps["object"]],
            cwd=ROOT, text=True, capture_output=True, check=False,
        )
    except OSError as exc:
        raise BuildError("runtime identity audio_graph Ninja dependency query failed") from exc
    deps_text = deps_result.stdout
    object_header = re.compile(
        rf"^{re.escape(ninja_deps['object'])}: #deps ([1-9][0-9]*), deps mtime [0-9]+ \(VALID\)$",
        re.MULTILINE,
    )
    if deps_result.returncode != 0 or not deps_text.strip() or not object_header.search(deps_text):
        raise BuildError("runtime identity audio_graph Ninja dependency database is unavailable")
    if not isinstance(ninja_deps.get("sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", ninja_deps["sha256"]):
        raise BuildError("runtime identity audio_graph Ninja dependency hash is invalid")
    if hashlib.sha256(deps_text.encode()).hexdigest() != ninja_deps["sha256"]:
        raise BuildError("runtime identity audio_graph Ninja dependency evidence is stale")
    if ninja_deps.get("resampler_header_referenced") is not False or "gameplay_audio_resample.h" in deps_text:
        raise BuildError("runtime identity audio_graph Ninja dependencies reference the GPL resampler header")
    dependency_paths = deps_text.splitlines()[1:]
    if not any(path.rstrip().endswith("/src/gameplay_audio.c") for path in dependency_paths):
        raise BuildError("runtime identity audio_graph Ninja dependencies omit gameplay_audio.c")
    if not any(path.rstrip().endswith("/src/gameplay_audio_silent_clock.h") for path in dependency_paths):
        raise BuildError("runtime identity audio_graph Ninja dependencies omit the silent clock header")

    checks = proof.get("checks")
    expected_checks = {
        "resampler_c_in_public_ninja_graph": False,
        "resampler_h_in_public_ninja_graph": False,
        "resampler_c_in_public_compile_commands": False,
        "resampler_h_in_public_depfile": False,
        "resampler_h_in_public_ninja_deps": False,
    }
    if checks != expected_checks:
        raise BuildError("runtime identity audio_graph checks do not prove exclusion")


def _validate_runtime_provenance(identity: dict[str, object]) -> None:
    source_inputs = identity.get("source_inputs")
    if not isinstance(source_inputs, dict):
        raise BuildError("runtime identity source_inputs record is missing")
    source_hashes = source_inputs.get("files_sha256")
    if not isinstance(source_hashes, dict) or set(source_hashes) != set(RUNTIME_SOURCE_FILES):
        raise BuildError("runtime identity source fingerprint set is incomplete")
    for rel in RUNTIME_SOURCE_FILES:
        expected = source_hashes.get(rel)
        path = _identity_repo_path(rel, "source")
        if (path.is_symlink() or not path.is_file() or not isinstance(expected, str)
                or not re.fullmatch(r"[0-9a-f]{64}", expected)):
            raise BuildError(f"runtime identity source fingerprint is invalid: {rel}")
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise BuildError(f"runtime identity source fingerprint differs from current checkout: {rel}")
    trees = source_inputs.get("trees")
    if not isinstance(trees, dict) or set(trees) != {"src", "cmake"}:
        raise BuildError("runtime identity source tree fingerprint set is incomplete")
    for name in ("src", "cmake"):
        record = trees.get(name)
        path = _identity_repo_path(name, f"{name} tree")
        if not isinstance(record, dict) or record.get("path") != name or not isinstance(record.get("files"), int) or not isinstance(record.get("sha256"), str):
            raise BuildError(f"runtime identity source tree fingerprint is invalid: {name}")
        if path.is_symlink() or not path.is_dir() or _tree_hash(path) != (record["files"], record["sha256"]):
            raise BuildError(f"runtime identity source tree differs from current checkout: {name}")
    prepared = source_inputs.get("prepared_gameplay")
    if not isinstance(prepared, dict):
        raise BuildError("runtime identity prepared gameplay source record is missing")
    prepared_path = prepared.get("path")
    if prepared_path != PREPARED_GAMEPLAY_PATH or not isinstance(prepared.get("pinned_commit"), str):
        raise BuildError("runtime identity prepared gameplay source record is invalid")
    generated = _identity_repo_path(prepared_path, "prepared gameplay source")
    if not generated.is_dir() or _is_symlink(generated):
        raise BuildError("runtime identity prepared gameplay source directory is missing")
    try:
        commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=generated, text=True).strip()
        diff = subprocess.check_output(["git", "diff", "--binary", "HEAD"], cwd=generated)
    except (OSError, subprocess.CalledProcessError) as exc:
        raise BuildError(f"cannot inspect prepared gameplay source: {exc}") from exc
    if commit != prepared["pinned_commit"] or hashlib.sha256(diff).hexdigest() != prepared.get("working_tree_diff_sha256"):
        raise BuildError("runtime identity prepared gameplay source differs from current checkout")
    for key in ("composed_patch", "reviewed_patch"):
        part = prepared.get(key)
        if (not isinstance(part, dict) or part.get("path") != PREPARED_GAMEPLAY_PATCHES[key]
                or not isinstance(part.get("sha256"), str)):
            raise BuildError(f"runtime identity prepared gameplay {key} record is invalid")
        patch_path = _identity_repo_path(part["path"], f"prepared gameplay {key}")
        if (patch_path.is_symlink() or not patch_path.is_file()
                or hashlib.sha256(patch_path.read_bytes()).hexdigest() != part["sha256"]):
            raise BuildError(f"runtime identity prepared gameplay {key} differs from current checkout")
    tree = prepared.get("tree")
    if (not isinstance(tree, dict) or tree.get("path") != prepared_path
            or _tree_hash(generated) != (tree.get("files"), tree.get("sha256"))):
        raise BuildError("runtime identity prepared gameplay tree differs from current checkout")
    toolchain = identity.get("toolchain")
    if not isinstance(toolchain, dict) or not isinstance(toolchain.get("sha256"), dict):
        raise BuildError("runtime identity toolchain fingerprint set is missing")
    tool_hashes = toolchain["sha256"]
    if set(tool_hashes) != RUNTIME_TOOLCHAIN_PATHS:
        raise BuildError("runtime identity toolchain fingerprint set is not the reviewed toolset")
    for rel, expected in tool_hashes.items():
        if not isinstance(rel, str) or not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected):
            raise BuildError("runtime identity toolchain fingerprint is invalid")
        path = _identity_repo_path(rel, "toolchain")
        if (path.is_symlink() or not path.is_file()
                or hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise BuildError(f"runtime identity toolchain fingerprint differs from current tools: {rel}")
    seed = identity.get("pipeline_seed")
    if not isinstance(seed, dict):
        raise BuildError("runtime identity pipeline seed record is missing")
    seed_paths = dict(PIPELINE_SEED_PATHS, materialized=f"{identity['artifact_root']}/initial_pipeline_cache.db")
    for key in ("source", "materialized"):
        part = seed.get(key)
        if (not isinstance(part, dict) or part.get("path") != seed_paths[key]
                or not isinstance(part.get("sha256"), str)):
            raise BuildError(f"runtime identity pipeline seed {key} record is invalid")
        path = _identity_repo_path(part["path"], f"pipeline seed {key}")
        if (path.is_symlink() or not path.is_file()
                or hashlib.sha256(path.read_bytes()).hexdigest() != part["sha256"]):
            raise BuildError(f"runtime identity pipeline seed differs from current files: {part['path']}")
    materialized = seed["materialized"]
    expected_seed = seed.get("expected_sha256")
    if not isinstance(expected_seed, str) or expected_seed != materialized.get("sha256"):
        raise BuildError("runtime identity pipeline seed expected digest is inconsistent")
    data_records = [item for item in identity.get("artifacts", [])
                    if isinstance(item, dict) and isinstance(item.get("path"), str)
                    and (item["path"] == "gameplay_public.data" or item["path"].endswith("/gameplay_public.data"))]
    if len(data_records) != 1 or data_records[0].get("sha256") != expected_seed:
        raise BuildError("runtime data is not bound to the reviewed pipeline seed")


def _tree_hash(path: Path) -> tuple[int, str]:
    digest = hashlib.sha256()
    files = 0
    for child in sorted(path.rglob("*")):
        if ".git" in child.relative_to(path).parts:
            continue
        if child.is_symlink():
            raise BuildError(f"source inventory contains a symlink: {child}")
        if not child.is_file():
            continue
        digest.update(child.relative_to(ROOT).as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(child.read_bytes())
        files += 1
    return files, digest.hexdigest()


def _identity_repo_path(value: object, label: str) -> Path:
    """Resolve a producer path only when it is canonical and inside ROOT."""
    if not isinstance(value, str) or not value or "\\" in value:
        raise BuildError(f"runtime identity {label} path is invalid")
    path = Path(value)
    if path.is_absolute() or path.as_posix() != value or any(part in ("", ".", "..") for part in path.parts):
        raise BuildError(f"runtime identity {label} path is not canonical and repo-relative")
    root = ROOT.resolve()
    candidate = ROOT / path
    try:
        if not candidate.resolve().is_relative_to(root):
            raise BuildError(f"runtime identity {label} path escapes the reviewed checkout")
    except OSError as exc:
        raise BuildError(f"cannot resolve runtime identity {label} path: {exc}") from exc
    return candidate


def _runtime_graph_hash(files: dict[str, bytes]) -> str:
    """Hash the complete immutable loader group, including entry assets."""
    records = [
        {"path": path, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        for path, data in sorted(files.items())
    ]
    canonical = json.dumps(records, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(canonical).hexdigest()[:16]


def _validate_runtime_graph(files: dict[str, bytes]) -> None:
    """Validate the small public loader graph and reject evidence/upload code."""
    forbidden_modules = {
        "dsp-coefficients.mjs", "audio-worklet.js", "audio-ring.mjs",
        "runtime-audio-assets.mjs", "runtime-audio.mjs",
    }
    if forbidden_modules.intersection(files):
        raise BuildError("public runtime graph contains a development audio module")
    for rel, data in files.items():
        # Export checks alone cannot detect dormant diagnostics retained by
        # internal replay references in a shared native archive.
        if any(marker in data for marker in (
            b"CPU_ADDRESS_AUDIT", b"melee-web-native-cpu-address-diagnostic",
        )):
            raise BuildError(f"CPU address diagnostic rejected in public runtime: {rel}")
        if rel.endswith((".mjs", ".js")):
            try:
                text = data.decode("utf-8")
            except UnicodeDecodeError as exc:
                raise BuildError(f"runtime JavaScript is not UTF-8: {rel}") from exc
            if "\x00" in text or any(ord(char) < 0x09 or 0x0e <= ord(char) < 0x20 for char in text):
                raise BuildError(f"control character rejected in runtime JavaScript: {rel}")
            for name, pattern in FORBIDDEN_TEXT_PATTERNS:
                if name in {"network API", "runtime/importer code", "file input", "persistent browser storage API"}:
                    continue
                if rel == "gameplay_public.js" and name == "diagnostic code":
                    continue
                matches = list(pattern.finditer(text))
                if name == "private path" and rel == "gameplay_public.js":
                    # Emscripten emits this fixed virtual HOME in its standard
                    # runtime boilerplate. Permit only the exact literal and
                    # only when it is not extended into another path.
                    allowed_home = "/home/web_user"
                    matches = [match for match in matches
                               if not (text.startswith(allowed_home, match.start())
                                       and (match.start() + len(allowed_home) == len(text)
                                            or text[match.start() + len(allowed_home)] in "\t\n\r \"'`),;:{}[]"))]
                if matches:
                    raise BuildError(f"{name} text rejected in runtime JavaScript: {rel}")
            if re.search(r"(?:sendBeacon|FormData|/upload(?:/|\b)|__melee_evidence|evidence\.json)", text, re.I):
                raise BuildError(f"upload/evidence code rejected in runtime JavaScript: {rel}")
            if re.search(r"https?://|(?:from|import)\s*[\"'](?:https?:|//)", text, re.I):
                raise BuildError(f"external runtime URL rejected in runtime JavaScript: {rel}")
            if re.search(r"(?:dsp-coefficients|audio-worklet|audio-ring|runtime-audio)", text, re.I):
                raise BuildError(f"development audio module reference rejected in runtime JavaScript: {rel}")
    required_imports = {
        "melee-runtime.mjs": ("./runtime-assets.mjs", "./gameplay_public.js", "./controller-input.mjs"),
        "runtime-assets.mjs": ("./disc-image.mjs",),
        "controller-settings.mjs": ("./prototype-keyboard-layouts.mjs", "./controller-panel.mjs", "./controller-settings.css"),
    }
    for rel, imports in required_imports.items():
        text = files[rel].decode("utf-8")
        for import_path in imports:
            if import_path not in text:
                raise BuildError(f"runtime graph is missing {import_path} from {rel}")
    if b"gameplay_public.wasm" not in files["gameplay_public.js"]:
        raise BuildError("gameplay_public.js does not bind gameplay_public.wasm")
    # A runtime data file is only admitted when declared by the producer.  It
    # may be binary, but it cannot be a renamed disc or archive.
    for rel, data in files.items():
        if rel.endswith(".data"):
            for name, signature in MAGIC_SIGNATURES:
                if name in {"WebAssembly", "SQLite database"}:
                    continue
                if data.startswith(signature):
                    raise BuildError(f"{name} signature rejected in runtime data: {rel}")
            if len(data) >= 0x8006 and data[0x8001:0x8006] == b"CD001":
                raise BuildError(f"ISO-9660 disc signature rejected in runtime data: {rel}")
            if b"CD001" in data:
                raise BuildError(f"disc signature rejected in runtime data: {rel}")


def _validate_value(value: str, label: str, *, email: bool = False) -> str:
    if not isinstance(value, str) or not value.strip():
        raise BuildError(f"{label} must be a non-empty text value")
    value = value.strip()
    if len(value) > 200 or any(ord(c) < 0x20 for c in value):
        raise BuildError(f"{label} contains invalid control characters or is too long")
    for name, pattern in FORBIDDEN_TEXT_PATTERNS:
        if pattern.search(value):
            raise BuildError(f"{name} text rejected in {label}")
    if re.search(r"\b\d{12,}\b", value):
        raise BuildError(f"account-like identifier rejected in {label}")
    if email:
        if not re.fullmatch(r"[^@\s<>]+@[^@\s<>]+\.[^@\s<>]+", value):
            raise BuildError(f"invalid contact email: {value!r}")
    elif any(char in value for char in "<>\""):
        raise BuildError(f"invalid characters in {label}")
    return value


def _config(mode: str, operator: str | None, contact: str | None) -> tuple[str, str]:
    if mode not in ("preview", "production"):
        raise BuildError("mode must be preview or production")
    if mode == "production" and (operator is None or contact is None):
        raise BuildError("production requires explicit --operator and --contact configuration")
    operator = operator or "DRAFT PREVIEW — OPERATOR NOT SET"
    contact = contact or "preview-contact@example.invalid"
    operator = _validate_value(operator, "operator")
    contact = _validate_value(contact, "contact", email=True)
    if mode == "preview":
        # The banner in every page is the conspicuous marker; defaults remain
        # invalid for production use and make accidental publishing obvious.
        return operator, contact
    if operator.upper().startswith("DRAFT PREVIEW") or contact.endswith(".invalid"):
        raise BuildError("production operator/contact may not use draft preview placeholders")
    return operator, contact


def _safe_manifest_path(output: Path, manifest: Path | None) -> Path:
    if manifest is None:
        manifest = output.parent / f"{output.name}.manifest.json"
    if _is_symlink(manifest):
        raise BuildError(f"manifest path may not be a symlink: {_display(manifest)}")
    try:
        if manifest.resolve().is_relative_to(output.resolve()):
            raise BuildError("manifest must be outside deploy output directory")
    except OSError as exc:
        raise BuildError(f"cannot inspect manifest path: {exc}") from exc
    if manifest.exists():
        raise BuildError(f"manifest already exists; deterministic build requires a fresh path: {_display(manifest)}")
    return manifest


def _write_new(path: Path, data: bytes) -> None:
    if _is_symlink(path) or path.exists():
        raise BuildError(f"refusing to overwrite generated path: {_display(path)}")
    try:
        with path.open("xb") as handle:
            handle.write(data)
    except OSError as exc:
        raise BuildError(f"cannot write {_display(path)}: {exc}") from exc


def _headers(mode: str, index_production: bool = False, profile: str = "maintenance") -> str:
    if profile not in ("maintenance", "player"):
        raise BuildError("profile must be maintenance or player")
    csp = PLAYER_CSP if profile == "player" else CSP
    lines = [
        "/*",
        f"  Content-Security-Policy: {csp}",
        f"  Permissions-Policy: {PERMISSIONS}",
        "  X-Content-Type-Options: nosniff",
        "  Referrer-Policy: no-referrer",
        "  X-Frame-Options: DENY",
    ]
    if profile == "player":
        lines.extend(
            (
                "  Cross-Origin-Opener-Policy: same-origin",
                "  Cross-Origin-Embedder-Policy: require-corp",
                "  Cross-Origin-Resource-Policy: same-origin",
            )
        )
    if mode == "preview" or not index_production:
        lines.extend(("  X-Robots-Tag: noindex, nofollow, noarchive",))
    if mode == "production":
        # Cloudflare Pages supports absolute URL patterns.  Keep the deployed
        # apex indexable while preventing the Pages preview hostname from
        # becoming a second indexed copy.
        lines.extend(
            (
                "https://webmelee.pages.dev/*",
                "  X-Robots-Tag: noindex, nofollow, noarchive",
                "https://:version.webmelee.pages.dev/*",
                "  X-Robots-Tag: noindex, nofollow, noarchive",
            )
        )
    lines.extend(("/assets/*", "  Cache-Control: public, max-age=31536000, immutable"))
    # The legal notice is deliberately an unversioned, source-bound text
    # asset.  Browsers must revalidate it so a corrected applicable notice is
    # visible without waiting for an immutable cache entry to expire.
    lines.extend(("/licenses/*", "  Cache-Control: public, max-age=0, must-revalidate"))
    if profile == "player":
        lines.extend(("/runtime/*", "  Cache-Control: public, max-age=31536000, immutable"))
    return "\n".join(lines) + "\n"


def _redirects(mode: str) -> str | None:
    if mode != "production":
        return None
    # Cloudflare Pages `_redirects` has no host/domain matching.  Keep this
    # file explicit and empty so it cannot become a SPA fallback; configure
    # exact host redirects in Cloudflare Bulk Redirects at deployment. Bulk
    # Redirects use flags for path preservation, not wildcard substitutions.
    return (
        "# Host redirects are configured with Cloudflare Bulk Redirects.\n"
        "# Sources: www.webmelee.gg/ and webmelee.pages.dev/\n"
        "# Target: https://webmelee.gg/; status: 301\n"
        "# Enable subpath matching, preserve path suffix and preserve query string.\n"
        "# Leave include subdomains disabled so preview hosts remain separate.\n"
    )


def _robots(mode: str, index_production: bool = False) -> str:
    if mode == "preview" or not index_production:
        return "# Draft preview: never index this bundle.\nUser-agent: *\nDisallow: /\n"
    return "# The apex production host is indexable; Pages preview hosts are blocked by _headers.\nUser-agent: *\nAllow: /\n"


def _replace_html(data: bytes, operator: str, contact: str, css_url: str, js_url: str, mode: str) -> bytes:
    text = data.decode("utf-8")
    replacements = {
        STYLE_TOKEN: css_url,
        SCRIPT_TOKEN: js_url,
        CONTACT_TOKEN: html.escape(contact, quote=True),
        OPERATOR_TOKEN: html.escape(operator, quote=True),
    }
    for token, value in replacements.items():
        text = text.replace(token, value)
    text = text.replace('<html lang="en">', f'<html lang="en" data-environment="{mode}">', 1)
    if mode == "preview":
        text = text.replace('<title>', '<title>[staging] ', 1)
    if any(token in text for token in replacements):
        raise BuildError("placeholder substitution failed")
    return text.encode("utf-8")


def _file_records(output: Path, profile: str = "maintenance") -> list[dict[str, int | str]]:
    records: list[dict[str, int | str]] = []
    total = 0
    for path in sorted(output.rglob("*")):
        rel = path.relative_to(output).as_posix()
        if path.is_dir() and not path.is_symlink():
            continue
        if path.is_symlink() or not path.is_file():
            raise BuildError(f"generated output contains non-file entry: {rel}")
        data = path.read_bytes()
        if profile == "player":
            size = _validate_size(path, len(data))
            if size > RUNTIME_MAX_FILE_BYTES:
                raise BuildError(f"runtime file exceeds 25 MiB limit: {rel} ({size} bytes)")
        else:
            size = _validate_size(path, len(data))
        total += size
        records.append({"path": rel, "size": size, "sha256": hashlib.sha256(data).hexdigest()})
    total_limit = RUNTIME_MAX_TOTAL_BYTES if profile == "player" else MAX_TOTAL_BYTES
    if total > total_limit:
        raise BuildError(f"deployed bundle exceeds {total_limit // (1024 * 1024)} MiB total limit: {total} bytes")
    return records


def build(
    source: Path | str = DEFAULT_SOURCE,
    output: Path | str | None = None,
    mode: str = "preview",
    operator: str | None = None,
    contact: str | None = None,
    manifest: Path | str | None = None,
    index_production: bool = False,
    profile: str = "maintenance",
    runtime_dir: Path | str | None = None,
) -> Path:
    """Build a fresh public bundle and return its output directory."""
    if output is None:
        raise BuildError("--output is required; choose a fresh deploy directory")
    if profile not in ("maintenance", "player"):
        raise BuildError("profile must be maintenance or player")
    source = Path(source)
    if profile == "player" and source == DEFAULT_SOURCE:
        source = PLAYER_SOURCE
    if profile == "player" and runtime_dir is None:
        raise BuildError("player profile requires explicit --runtime-dir")
    output = Path(output)
    if _is_symlink(output) or output.exists():
        raise BuildError(f"output must be a new directory (and may not be a symlink): {_display(output)}")
    manifest_path = _safe_manifest_path(output, Path(manifest) if manifest is not None else None)
    parent = output.parent
    # A platform's conventional temporary directory may itself be a symlink
    # (for example /tmp on macOS); only the deploy directory is required to be
    # fresh and non-symlinked.
    if not parent.is_dir():
        raise BuildError(f"output parent must be an existing directory: {_display(parent)}")
    if profile == "player":
        source_bytes = _validate_player_source(source)
        legal_bytes = _validate_source(DEFAULT_SOURCE)
        runtime_identity, native_files, runtime_hash, identity_bytes = _read_runtime_identity(Path(runtime_dir))
        _validate_runtime_provenance(runtime_identity)
        runtime_files = dict(native_files)
        source_runtime = {
            "melee-runtime.mjs": ROOT / "web" / "melee-runtime.mjs",
            "runtime-assets.mjs": ROOT / "web" / "runtime-assets.mjs",
            "disc-image.mjs": ROOT / "web" / "disc-image.mjs",
            "prototype-keyboard-layouts.mjs": ROOT / "web" / "prototype-keyboard-layouts.mjs",
            "controller-input.mjs": ROOT / "web" / "controller-input.mjs",
            "controller-panel.mjs": ROOT / "web" / "controller-panel.mjs",
            "controller-panel.css": ROOT / "web" / "controller-panel.css",
            "controller-settings.mjs": ROOT / "web" / "controller-settings.mjs",
            "controller-settings.css": ROOT / "web" / "controller-settings.css",
        }
        for rel, path in source_runtime.items():
            if _is_symlink(path) or not path.is_file():
                raise BuildError(f"reviewed player runtime source is missing: {_display(path)}")
            runtime_files[rel] = path.read_bytes()
        _validate_runtime_graph(runtime_files)
        runtime_hash = _runtime_graph_hash({
            **runtime_files,
            "player/player.css": source_bytes["player.css"],
            "player/player-shell.mjs": source_bytes["player-shell.mjs"],
        })
    else:
        source_bytes = _validate_source(source)
        legal_bytes = source_bytes
        runtime_identity = runtime_files = runtime_hash = identity_bytes = None
    legal_notice = _read_legal_notice()
    operator, contact = _config(mode, operator, contact)
    output.mkdir()
    try:
        if profile == "maintenance":
            css = source_bytes["site.css"]
            js = source_bytes["site.js"]
            css_hash = hashlib.sha256(css).hexdigest()[:16]
            js_hash = hashlib.sha256(js).hexdigest()[:16]
            css_name = f"site.{css_hash}.css"
            js_name = f"site.{js_hash}.js"
            (output / "assets").mkdir()
            _write_new(output / "assets" / css_name, css)
            _write_new(output / "assets" / js_name, js)
            css_url = f"/assets/{css_name}"
            js_url = f"/assets/{js_name}"
            html_source = source_bytes
        else:
            runtime_root = output / "runtime" / runtime_hash
            (runtime_root / "player").mkdir(parents=True)
            for rel, data in runtime_files.items():
                _write_new(runtime_root / rel, data)
            _write_new(runtime_root / "player" / "player.css", source_bytes["player.css"])
            _write_new(runtime_root / "player" / "player-shell.mjs", source_bytes["player-shell.mjs"])
            legal_css_hash = hashlib.sha256(legal_bytes["site.css"]).hexdigest()[:16]
            (output / "assets").mkdir()
            _write_new(output / "assets" / f"site.{legal_css_hash}.css", legal_bytes["site.css"])
            css_url = f"/runtime/{runtime_hash}/player/player.css"
            js_url = f"/runtime/{runtime_hash}/player/player-shell.mjs"
            legal_css_url = f"/assets/site.{legal_css_hash}.css"
            html_source = dict(legal_bytes)
            html_source["index.html"] = source_bytes["index.html"]
        for name in HTML_INPUTS:
            page_css_url = css_url if profile == "player" and name == "index.html" else (
                legal_css_url if profile == "player" else css_url
            )
            _write_new(output / name, _replace_html(html_source[name], operator, contact, page_css_url, js_url, mode))
        (output / "licenses").mkdir()
        _write_new(output / LEGAL_NOTICE_OUTPUT, legal_notice)
        _write_new(output / "_headers", _headers(mode, index_production, profile).encode("utf-8"))
        redirects = _redirects(mode)
        if redirects is not None:
            _write_new(output / "_redirects", redirects.encode("utf-8"))
        _write_new(output / "robots.txt", _robots(mode, index_production).encode("utf-8"))
        records = _file_records(output, profile)
        manifest_value = {
            "schema": SCHEMA,
            "profile": profile,
            "mode": mode,
            "draft_preview": mode == "preview",
            "index_production": bool(index_production) if mode == "production" else False,
            "operator": operator,
            "contact": contact,
            "files": records,
        }
        if profile == "player":
            manifest_value["runtime"] = {
                "path": f"runtime/{runtime_hash}",
                "hash": runtime_hash,
                "identity_sha256": hashlib.sha256(identity_bytes).hexdigest(),
                "identity": runtime_identity,
            }
        manifest_path.parent.mkdir(parents=True, exist_ok=True)
        _write_new(
            manifest_path,
            (json.dumps(manifest_value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8"),
        )
    except Exception:
        # The output was created by this invocation and contains no prior user
        # data.  Remove only this fresh tree so a failed build cannot be used.
        if output.exists() and not output.is_symlink():
            shutil.rmtree(output)
        if manifest_path.exists() and not manifest_path.is_symlink():
            manifest_path.unlink()
        raise
    return output


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--mode", choices=("preview", "production"), default="preview")
    parser.add_argument("--environment", choices=("preview", "production"), dest="mode")
    parser.add_argument("--operator")
    parser.add_argument("--contact")
    parser.add_argument("--profile", choices=("maintenance", "player"), default="maintenance")
    parser.add_argument(
        "--runtime-dir",
        type=Path,
        help="producer output containing the exact player runtime graph and build identity sidecar",
    )
    parser.add_argument(
        "--index-production",
        action="store_true",
        help="permit apex production indexing; defaults to noindex pending release review",
    )
    parser.add_argument("--production", action="store_const", const="production", dest="mode")
    return parser


def main(argv: Iterable[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        output = build(
            args.source,
            args.output,
            args.mode,
            args.operator,
            args.contact,
            args.manifest,
            args.index_production,
            args.profile,
            args.runtime_dir,
        )
    except (BuildError, OSError, ValueError) as exc:
        print(f"public build rejected: {exc}", file=sys.stderr)
        return 2
    print(f"public {args.profile} {args.mode} bundle: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
