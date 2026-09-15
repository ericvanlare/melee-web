#!/usr/bin/env python3
"""Fail-closed audit for a WebMelee public shell bundle.

The audit does not trust a build log or a manifest alone.  It inventories the
output tree, validates every byte and filename, recomputes hashes and checks
the shell's small capability surface before accepting the sidecar manifest.
"""

from __future__ import annotations

import argparse
import html
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Any, Iterable

ROOT = Path(__file__).resolve().parents[1]

try:
    from build_public import (
        CSP,
        CONTACT_TOKEN,
        FORBIDDEN_TEXT_PATTERNS,
        HTML_INPUTS,
        HTML_OUTPUTS,
        MAGIC_SIGNATURES,
        MAX_FILE_BYTES,
        MAX_TOTAL_BYTES,
        OPERATOR_TOKEN,
        PERMISSIONS,
        SCHEMA,
        SCRIPT_TOKEN,
        SOURCE_ALLOWLIST,
        STYLE_TOKEN,
        BuildError,
        DEFAULT_SOURCE,
        LEGAL_NOTICE_OUTPUT,
        _read_legal_notice,
        PLAYER_SOURCE,
        PLAYER_RUNTIME_FILES,
        PLAYER_SOURCE_RUNTIME_FILES,
        RUNTIME_MAX_FILE_BYTES,
        RUNTIME_MAX_TOTAL_BYTES,
        RUNTIME_IDENTITY_SCHEMA,
        RUNTIME_IDENTITY_NAME,
        RUNTIME_SOURCE_FILES,
        RUNTIME_REQUIRED_EXPORTS,
        RUNTIME_FORBIDDEN_EXPORTS,
        PREPARED_GAMEPLAY_PATH,
        PREPARED_GAMEPLAY_PATCHES,
        RUNTIME_TOOLCHAIN_PATHS,
        PIPELINE_SEED_PATHS,
        RUNTIME_ARTIFACT_ROOTS,
        _config,
        _headers,
        _redirects,
        _robots,
        _validate_magic,
        _validate_source,
        _replace_html,
        _identity_artifacts,
        _parse_wasm_exports,
        _validate_player_source,
        _validate_runtime_graph,
        _identity_repo_path,
        _runtime_graph_hash,
        _validate_audio_policy,
    )
except ImportError as exc:  # pragma: no cover - only relevant to direct misuse
    raise SystemExit(f"audit_public must run with scripts/ on PYTHONPATH: {exc}") from exc


ASSET_RE = re.compile(r"^assets/site\.([0-9a-f]{16})\.(css|js)$")
RUNTIME_RE = re.compile(r"^runtime/([0-9a-f]{16})/(.+)$")
OUTPUT_ROOT_FILES = frozenset(HTML_OUTPUTS | {"_headers", "robots.txt"})
FORBIDDEN_HTML_CODE = re.compile(
    r"(?:<\s*(?:iframe|object|embed|form|input|meta[^>]+http-equiv\s*=)|"
    r"\bon[a-z]+\s*=|(?:window\.)?location\.(?:assign|replace)|history\.(?:push|replace)State)",
    re.I,
)


class AuditError(ValueError):
    """A user-facing fail-closed audit error."""


def _fail(message: str) -> None:
    raise AuditError(message)


def _identity_path(value: object, label: str) -> Path:
    try:
        return _identity_repo_path(value, label)
    except BuildError as exc:
        _fail(str(exc))
    raise AuditError("unreachable")


def _tree_hash(path: Path) -> tuple[int, str]:
    digest = hashlib.sha256()
    files = 0
    for child in sorted(path.rglob("*")):
        if ".git" in child.relative_to(path).parts:
            continue
        if child.is_symlink():
            _fail(f"source inventory contains a symlink: {child}")
        if not child.is_file():
            continue
        digest.update(child.relative_to(ROOT).as_posix().encode("utf-8"))
        digest.update(b"\0")
        try:
            digest.update(child.read_bytes())
        except OSError as exc:
            _fail(f"cannot read source inventory file {child}: {exc}")
        files += 1
    return files, digest.hexdigest()


def _is_symlink(path: Path) -> bool:
    try:
        return path.is_symlink()
    except OSError as exc:
        _fail(f"cannot inspect path {path}: {exc}")
    return False


def _safe_rel(value: Any) -> str:
    if not isinstance(value, str) or not value or "\x00" in value:
        _fail("manifest contains an invalid file path")
    if "\\" in value or value.startswith("/") or value.startswith("."):
        _fail(f"manifest contains an unsafe file path: {value!r}")
    path = Path(value)
    if any(part in ("", ".", "..") for part in path.parts):
        _fail(f"manifest contains an unsafe file path: {value!r}")
    if path.as_posix() != value:
        _fail(f"manifest path is not canonical: {value!r}")
    return value


def _load_manifest(path: Path) -> dict[str, Any]:
    if _is_symlink(path) or not path.is_file():
        _fail(f"manifest must be a regular file: {path}")
    try:
        raw = path.read_bytes()
    except OSError as exc:
        _fail(f"cannot read manifest: {exc}")
    if len(raw) > MAX_FILE_BYTES:
        _fail("manifest exceeds 25 MiB limit")
    try:
        text = raw.decode("utf-8")
        value = json.loads(text, object_pairs_hook=_unique_object)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        _fail(f"manifest is not valid UTF-8 JSON: {exc}")
    if not isinstance(value, dict):
        _fail("manifest root must be an object")
    allowed = {"schema", "profile", "mode", "draft_preview", "index_production", "operator", "contact", "files", "runtime"}
    if set(value) not in (allowed - {"runtime"}, allowed):
        _fail("manifest has unexpected or missing top-level fields")
    if "runtime" not in value:
        value["runtime"] = None
    return value


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate manifest key: {key}")
        result[key] = value
    return result


def _output_files(output: Path, profile: str = "maintenance") -> tuple[set[str], int]:
    if _is_symlink(output) or not output.is_dir():
        _fail(f"output must be a real directory: {output}")
    paths: set[str] = set()
    total = 0
    total_limit = RUNTIME_MAX_TOTAL_BYTES if profile == "player" else MAX_TOTAL_BYTES
    for path in sorted(output.rglob("*")):
        rel = path.relative_to(output).as_posix()
        if path.is_dir() and not path.is_symlink():
            if profile == "maintenance" and rel not in {"assets", "licenses"}:
                _fail(f"unauthorized output directory: {rel}")
            if profile == "player" and (rel not in {"assets", "licenses", "runtime"}
                                         and not re.fullmatch(r"runtime/[0-9a-f]{16}(?:/player)?", rel)):
                _fail(f"unauthorized output directory: {rel}")
            continue
        if _is_symlink(path) or not path.is_file():
            _fail(f"symlink or non-file output entry rejected: {rel}")
        _safe_rel(rel)
        if rel in paths:
            _fail(f"duplicate output path: {rel}")
        paths.add(rel)
        try:
            size = path.stat().st_size
        except OSError as exc:
            _fail(f"cannot stat output file {rel}: {exc}")
        file_limit = RUNTIME_MAX_FILE_BYTES if profile == "player" and rel.startswith("runtime/") else MAX_FILE_BYTES
        if size > file_limit:
            _fail(f"file exceeds {file_limit // (1024 * 1024)} MiB limit: {rel} ({size} bytes)")
        total += size
        if total > total_limit:
            _fail(f"deployed bundle exceeds {total_limit // (1024 * 1024)} MiB total limit")
    return paths, total


def _expected_paths(paths: set[str], mode: str, profile: str,
                    runtime: dict[str, Any] | None = None) -> tuple[set[str], str, str, str]:
    if profile == "player":
        if not isinstance(runtime, dict) or not isinstance(runtime.get("path"), str):
            _fail("player manifest runtime metadata is missing")
        runtime_path = runtime["path"]
        if not re.fullmatch(r"runtime/[0-9a-f]{16}", runtime_path):
            _fail("player runtime path must be runtime/<16 lowercase hex characters>")
        required = set(HTML_OUTPUTS) | {"_headers", "robots.txt", LEGAL_NOTICE_OUTPUT}
        if mode == "production":
            required.add("_redirects")
        runtime_files = runtime.get("identity", {}).get("artifacts") if isinstance(runtime.get("identity"), dict) else None
        try:
            artifact_map = _identity_artifacts(runtime.get("identity"))
        except BuildError as exc:
            _fail(str(exc))
        runtime_rel = {f"{runtime_path}/{rel}" for rel in artifact_map}
        runtime_rel.update({f"{runtime_path}/{rel}" for rel in PLAYER_SOURCE_RUNTIME_FILES})
        runtime_rel.update({f"{runtime_path}/player/player.css", f"{runtime_path}/player/player-shell.mjs"})
        legal_source = _validate_source(DEFAULT_SOURCE)
        legal_css_path = f"assets/site.{hashlib.sha256(legal_source['site.css']).hexdigest()[:16]}.css"
        expected = required | runtime_rel | {legal_css_path}
        if paths != expected:
            extras = sorted(paths - expected)
            missing = sorted(expected - paths)
            _fail(f"player output paths do not match fixed allowlist; extras={extras}, missing={missing}")
        return expected, f"{runtime_path}/player/player.css", f"{runtime_path}/player/player-shell.mjs", legal_css_path
    if not HTML_OUTPUTS.issubset(paths):
        _fail("one or more required HTML pages are missing")
    assets = sorted(path for path in paths if path.startswith("assets/"))
    matches = [ASSET_RE.fullmatch(path) for path in assets]
    if any(match is None for match in matches):
        _fail("unauthorized asset path or extension")
    css = [match for match in matches if match and match.group(2) == "css"]
    js = [match for match in matches if match and match.group(2) == "js"]
    if len(css) != 1 or len(js) != 1 or len(assets) != 2:
        _fail("bundle must contain exactly one hashed CSS and one hashed JS asset")
    css_path = next(path for path, match in zip(assets, matches) if match and match.group(2) == "css")
    js_path = next(path for path, match in zip(assets, matches) if match and match.group(2) == "js")
    expected = set(OUTPUT_ROOT_FILES) | {css_path, js_path, LEGAL_NOTICE_OUTPUT}
    if mode == "production":
        expected.add("_redirects")
    if paths != expected:
        extras = sorted(paths - expected)
        missing = sorted(expected - paths)
        _fail(f"output paths do not match fixed allowlist; extras={extras}, missing={missing}")
    return expected, css_path, js_path, css_path


def _read_output(output: Path, rel: str, profile: str = "maintenance") -> bytes:
    path = output / rel
    if _is_symlink(path) or not path.is_file():
        _fail(f"output path is not a regular file: {rel}")
    try:
        data = path.read_bytes()
    except OSError as exc:
        _fail(f"cannot read output file {rel}: {exc}")
    if rel != LEGAL_NOTICE_OUTPUT and (
            profile == "maintenance"
            or not rel.startswith("runtime/") and not (profile == "player" and rel == "index.html")):
        try:
            _validate_magic(data, rel)
        except BuildError as exc:
            _fail(str(exc))
    return data


def _validate_html(data: bytes, rel: str, css_path: str, js_path: str, operator: str, contact: str, mode: str,
                   legal_css_path: str | None = None,
                   profile: str = "maintenance") -> None:
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        _fail(f"HTML is not UTF-8: {rel}: {exc}")
    if any(token in text for token in (STYLE_TOKEN, SCRIPT_TOKEN, CONTACT_TOKEN, OPERATOR_TOKEN)):
        _fail(f"unresolved placeholder in {rel}")
    forbidden_html = FORBIDDEN_HTML_CODE
    if profile == "player":
        forbidden_html = re.compile(
            r"(?:<\s*(?:iframe|object|embed|form|meta[^>]+http-equiv\s*=)|"
            r"\bon[a-z]+\s*=|(?:window\.)?location\.(?:assign|replace)|history\.(?:push|replace)State)", re.I
        )
    if forbidden_html.search(text) or re.search(r"(?:href|src)\s*=\s*[\"']\s*javascript:", text, re.I):
        _fail(f"embedded form, navigation script, iframe or event handler in {rel}")
    css_url = "/" + (legal_css_path if profile == "player" and rel != "index.html" else css_path)
    if css_url not in text:
        _fail(f"{rel} does not reference the hashed stylesheet")
    scripts = re.findall(r"<script\b([^>]*)>", text, re.I)
    if rel == "index.html":
        if len(scripts) != 1 or ("/" + js_path) not in scripts[0]:
            _fail("index.html does not reference only the reviewed player script")
        if profile == "player" and not re.search(r"\btype\s*=\s*[\"']module[\"']", scripts[0], re.I):
            _fail("player index.html must use a module script")
    elif scripts:
        _fail(f"unexpected script element in {rel}")
    if rel in {"terms.html", "privacy.html", "copyright.html"}:
        if html.escape(operator, quote=True) not in text or html.escape(contact, quote=True) not in text:
            _fail(f"configured operator/contact is missing from {rel}")
    if f'<html lang="en" data-environment="{mode}">' not in text:
        _fail(f"deployment environment marker is missing from {rel}")
    if mode == "preview" and '<title>[staging] ' not in text:
        _fail(f"staging title is missing from {rel}")
    if mode == "production" and "DRAFT PREVIEW" in text.upper():
        _fail(f"draft preview marker present in production page: {rel}")
    if rel == "404.html":
        if "Page not found" not in text or "index.html" in text or "<script" in text.lower():
            _fail("404.html is not a strict non-SPA page")


def _validate_js(data: bytes) -> None:
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        _fail(f"fullscreen script is not UTF-8: {exc}")
    if "requestFullscreen" not in text or "exitFullscreen" not in text or "fullscreenchange" not in text:
        _fail("site.js does not provide the fullscreen control")
    forbidden = re.compile(
        r"(?:\b(?:import|export)\b|\b(?:fetch|XMLHttpRequest|WebSocket|EventSource)\s*[(.]|"
        r"\b(?:localStorage|sessionStorage|indexedDB|caches|navigator\.storage)\b|"
        r"\b(?:FileReader|WebAssembly|requestAnimationFrame|setTimeout|setInterval|console\s*\.|debugger)\b|"
        r"(?:<\s*input|\.files\b|showOpenFilePicker))",
        re.I,
    )
    if forbidden.search(text):
        _fail("fullscreen script contains runtime, storage, file, network or diagnostic code")
    if re.search(r"\b(?:window\.)?location\b|\bhistory\b|\bdocument\.cookie\b", text, re.I):
        _fail("fullscreen script contains navigation or cookie state")
    if re.search(r"\b(?:document|window)\.(?:createElement|write|open|fetch)\b", text, re.I):
        _fail("fullscreen script contains DOM construction or network code outside the control")


def _validate_headers(data: bytes, mode: str, index_production: bool, profile: str = "maintenance") -> None:
    expected = _headers(mode, index_production, profile).encode("utf-8")
    if data != expected:
        _fail("_headers does not match the required CSP/Permissions-Policy policy")
    text = data.decode("utf-8")
    has_isolation = all(
        header in text for header in (
            "Cross-Origin-Opener-Policy: same-origin",
            "Cross-Origin-Embedder-Policy: require-corp",
            "Cross-Origin-Resource-Policy: same-origin",
        )
    )
    if profile == "maintenance" and re.search(r"Cross-Origin-(?:Opener|Embedder)-Policy|Cross-Origin-Resource-Policy|COOP|COEP", text, re.I):
        _fail("COOP/COEP header is not permitted in the public shell")
    if profile == "player" and not has_isolation:
        _fail("player headers must require same-origin cross-origin isolation")
    directives = ("default-src 'none'", "script-src 'self'", "style-src 'self'", "connect-src 'none'", "worker-src 'none'")
    if profile == "player":
        directives = ("default-src 'none'", "script-src 'self'", "style-src 'self'", "connect-src 'self'", "worker-src 'self'")
    for directive in directives:
        if directive not in text:
            _fail(f"required CSP directive missing: {directive}")
    if "camera=()" not in text or "microphone=()" not in text or "geolocation=()" not in text or "payment=()" not in text or "usb=()" not in text:
        _fail("camera/microphone/geolocation/payment/usb permissions must be disabled")


def _validate_redirects(data: bytes, mode: str) -> None:
    expected = _redirects(mode)
    if expected is None:
        _fail("preview output must not contain _redirects")
    if data != expected.encode("utf-8"):
        _fail("_redirects contains unsupported host rules or a SPA fallback")
    if re.search(r"\s(?:200|/index\.html)", data.decode("utf-8"), re.I):
        _fail("SPA fallback is forbidden")


def _validate_player_runtime(output: Path, runtime: dict[str, Any], records: list[dict[str, int | str]]) -> None:
    identity = runtime.get("identity")
    if not isinstance(identity, dict) or identity.get("schema") != RUNTIME_IDENTITY_SCHEMA:
        _fail("player runtime identity schema is missing or unsupported")
    required_identity = {
        "schema", "target", "configuration", "artifact_root", "artifacts", "wasm_exports",
        "source_inputs", "toolchain", "pipeline_seed", "upload_convention",
        "audio_policy", "audio_graph",
    }
    if set(identity) != required_identity or identity.get("target") != "runtime-public" or identity.get("configuration") != "Release":
        _fail("player runtime identity does not match the producer contract")
    if not isinstance(identity.get("artifact_root"), str) or identity["artifact_root"] not in RUNTIME_ARTIFACT_ROOTS:
        _fail("player runtime identity artifact_root is not the reviewed public Release output")
    try:
        _validate_audio_policy(identity)
    except BuildError as exc:
        _fail(str(exc))
    convention = identity.get("upload_convention")
    if (not isinstance(convention, dict)
            or convention.get("identity_path") != "build/runtime-public-identity.json"
            or convention.get("identity_is_outside_artifact_root") is not True):
        _fail("player runtime identity upload convention is not the reviewed producer output")
    runtime_path = runtime.get("path")
    runtime_hash = runtime.get("hash")
    if not isinstance(runtime_path, str) or not re.fullmatch(r"runtime/[0-9a-f]{16}", runtime_path):
        _fail("player runtime path is invalid")
    if not isinstance(runtime_hash, str) or runtime_path.rsplit("/", 1)[1] != runtime_hash:
        _fail("player runtime hash does not match its immutable path")
    identity_sha = runtime.get("identity_sha256")
    if not isinstance(identity_sha, str) or not re.fullmatch(r"[0-9a-f]{64}", identity_sha):
        _fail("player runtime identity hash is missing or invalid")
    identity_path = ROOT / "build" / RUNTIME_IDENTITY_NAME
    if _is_symlink(identity_path) or not identity_path.is_file():
        _fail("player runtime producer identity sidecar is missing")
    try:
        sidecar_bytes = identity_path.read_bytes()
    except OSError as exc:
        _fail(f"cannot read player runtime producer identity sidecar: {exc}")
    if hashlib.sha256(sidecar_bytes).hexdigest() != identity_sha:
        _fail("player runtime producer identity sidecar hash differs from manifest")
    try:
        sidecar = json.loads(sidecar_bytes.decode("utf-8"), object_pairs_hook=_unique_object)
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
        _fail(f"player runtime producer identity sidecar is invalid: {exc}")
    if sidecar != identity:
        _fail("player runtime identity differs from the producer sidecar")
    try:
        artifacts = _identity_artifacts(identity)
    except BuildError as exc:
        _fail(str(exc))
    required = {"gameplay_public.js", "gameplay_public.wasm", "gameplay_public.data"}
    if not required.issubset(artifacts):
        _fail(f"player runtime identity is missing required artifacts: {', '.join(sorted(required - set(artifacts)))}")
    runtime_files: dict[str, bytes] = {}
    for rel, expected in artifacts.items():
        deployed = f"{runtime_path}/{rel}"
        data = _read_output(output, deployed, "player")
        if len(data) != expected["size"] or hashlib.sha256(data).hexdigest() != expected["sha256"]:
            _fail(f"player runtime artifact identity mismatch: {rel}")
        runtime_files[rel] = data
    source_map = {
        "melee-runtime.mjs": ROOT / "web" / "melee-runtime.mjs",
        "runtime-assets.mjs": ROOT / "web" / "runtime-assets.mjs",
        "disc-image.mjs": ROOT / "web" / "disc-image.mjs",
        "prototype-keyboard-layouts.mjs": ROOT / "web" / "prototype-keyboard-layouts.mjs",
        "controller-input.mjs": ROOT / "web" / "controller-input.mjs",
        "controller-panel.mjs": ROOT / "web" / "controller-panel.mjs",
        "controller-panel.css": ROOT / "web" / "controller-panel.css",
    }
    for rel in source_map:
        runtime_files[rel] = _read_output(output, f"{runtime_path}/{rel}", "player")
    runtime_files["player/player.css"] = _read_output(output, f"{runtime_path}/player/player.css", "player")
    runtime_files["player/player-shell.mjs"] = _read_output(output, f"{runtime_path}/player/player-shell.mjs", "player")
    try:
        _validate_runtime_graph(runtime_files)
        actual_export_records = _parse_wasm_exports(runtime_files["gameplay_public.wasm"], "gameplay_public.wasm")
    except BuildError as exc:
        _fail(str(exc))
    actual_exports = [item["name"] for item in actual_export_records]
    wasm_exports = identity.get("wasm_exports")
    if not isinstance(wasm_exports, dict) or set(wasm_exports) != {"required", "functions", "all", "javascript_bindings", "forbidden_absent"}:
        _fail("player runtime Wasm export record is missing")
    if wasm_exports.get("all") != actual_export_records:
        _fail("player runtime Wasm exports do not match gameplay_public.wasm")
    if wasm_exports.get("required") != list(RUNTIME_REQUIRED_EXPORTS):
        _fail("player runtime required Wasm exports differ from the reviewed API")
    if wasm_exports.get("functions") != [item["name"] for item in actual_export_records if item["kind"] == 0]:
        _fail("player runtime Wasm function exports do not match gameplay_public.wasm")
    if wasm_exports.get("forbidden_absent") != sorted(RUNTIME_FORBIDDEN_EXPORTS) or set(actual_exports) & RUNTIME_FORBIDDEN_EXPORTS:
        _fail("player runtime forbidden Wasm export policy failed")
    bindings = wasm_exports.get("javascript_bindings")
    if (not isinstance(bindings, dict) or set(bindings) != set(RUNTIME_REQUIRED_EXPORTS)
            or any(not isinstance(name, str) for name in bindings.values())):
        _fail("player runtime JavaScript binding map is incomplete")
    function_names = {item["name"] for item in actual_export_records if item["kind"] == 0}
    if set(bindings.values()) - function_names:
        _fail("player runtime JavaScript binding is absent from gameplay_public.wasm")
    # The loader graph is source-bound to reviewed browser modules. Only the
    # native Emscripten JS/Wasm pair (and producer-declared data) comes from
    # runtime-dir.
    for rel, source in source_map.items():
        if not source.is_file():
            _fail(f"reviewed player runtime source is missing: {source}")
        try:
            expected = source.read_bytes()
        except OSError as exc:
            _fail(f"cannot read reviewed player runtime source: {source}: {exc}")
        if runtime_files[rel] != expected:
            _fail(f"player runtime source differs from reviewed checkout: {rel}")
    if _runtime_graph_hash(runtime_files) != runtime_hash:
        _fail("player runtime hash does not cover the complete immutable loader graph")
    source_inputs = identity.get("source_inputs")
    if not isinstance(source_inputs, dict):
        _fail("player runtime source_inputs record is missing")
    source_hashes = source_inputs.get("files_sha256")
    if not isinstance(source_hashes, dict) or set(source_hashes) != set(RUNTIME_SOURCE_FILES):
        _fail("player runtime source fingerprint set is incomplete")
    for rel in RUNTIME_SOURCE_FILES:
        source = _identity_path(rel, "source")
        if source.is_symlink() or not source.is_file() or not isinstance(source_hashes[rel], str) or not re.fullmatch(r"[0-9a-f]{64}", source_hashes[rel]):
            _fail(f"player runtime source fingerprint is invalid: {rel}")
        try:
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
        except OSError as exc:
            _fail(f"cannot hash player runtime source {rel}: {exc}")
        if digest != source_hashes[rel]:
            _fail(f"player runtime source fingerprint differs from reviewed checkout: {rel}")
    trees = source_inputs.get("trees")
    if not isinstance(trees, dict) or set(trees) != {"src", "cmake"}:
        _fail("player runtime source tree fingerprint set is incomplete")
    for name in ("src", "cmake"):
        record = trees.get(name)
        path = _identity_path(name, f"{name} tree")
        if (not isinstance(record, dict) or record.get("path") != name
                or not isinstance(record.get("files"), int) or not isinstance(record.get("sha256"), str)):
            _fail(f"player runtime source tree fingerprint is invalid: {name}")
        if path.is_symlink() or not path.is_dir() or _tree_hash(path) != (record["files"], record["sha256"]):
            _fail(f"player runtime source tree differs from reviewed checkout: {name}")
    prepared = source_inputs.get("prepared_gameplay")
    if not isinstance(prepared, dict) or prepared.get("path") != PREPARED_GAMEPLAY_PATH:
        _fail("player runtime prepared gameplay source record is missing")
    generated = _identity_path(prepared["path"], "prepared gameplay source")
    if not generated.is_dir() or _is_symlink(generated) or not isinstance(prepared.get("pinned_commit"), str):
        _fail("player runtime prepared gameplay source record is invalid")
    try:
        import subprocess
        commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=generated, text=True).strip()
        diff = subprocess.check_output(["git", "diff", "--binary", "HEAD"], cwd=generated)
    except (OSError, subprocess.CalledProcessError) as exc:
        _fail(f"cannot inspect prepared gameplay source: {exc}")
    if commit != prepared["pinned_commit"] or hashlib.sha256(diff).hexdigest() != prepared.get("working_tree_diff_sha256"):
        _fail("player runtime prepared gameplay source differs from reviewed checkout")
    for key in ("composed_patch", "reviewed_patch"):
        part = prepared.get(key)
        if (not isinstance(part, dict) or part.get("path") != PREPARED_GAMEPLAY_PATCHES[key]
                or not isinstance(part.get("sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", part["sha256"])):
            _fail(f"player runtime prepared gameplay {key} record is invalid")
        path = _identity_path(part["path"], f"prepared gameplay {key}")
        if path.is_symlink() or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != part["sha256"]:
            _fail(f"player runtime prepared gameplay {key} differs from reviewed checkout")
    tree = prepared.get("tree")
    if (not isinstance(tree, dict) or tree.get("path") != prepared["path"]
            or _tree_hash(generated) != (tree.get("files"), tree.get("sha256"))):
        _fail("player runtime prepared gameplay tree differs from reviewed checkout")
    toolchain = identity.get("toolchain")
    if not isinstance(toolchain, dict) or not isinstance(toolchain.get("sha256"), dict):
        _fail("player runtime toolchain identity is missing")
    tool_hashes = toolchain["sha256"]
    if set(tool_hashes) != RUNTIME_TOOLCHAIN_PATHS:
        _fail("player runtime toolchain identity is not the reviewed toolset")
    for rel, expected in tool_hashes.items():
        if not isinstance(rel, str) or not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected):
            _fail("player runtime toolchain fingerprint is invalid")
        path = _identity_path(rel, "toolchain")
        if path.is_symlink() or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            _fail(f"player runtime toolchain fingerprint differs from current tools: {rel}")
    seed = identity.get("pipeline_seed")
    if not isinstance(seed, dict) or not isinstance(seed.get("source"), dict) or not isinstance(seed.get("materialized"), dict):
        _fail("player runtime pipeline seed identity is missing")
    seed_paths = dict(PIPELINE_SEED_PATHS, materialized=f"{identity['artifact_root']}/initial_pipeline_cache.db")
    for key in ("source", "materialized"):
        part = seed[key]
        path_value, expected = part.get("path"), part.get("sha256")
        if (path_value != seed_paths[key] or not isinstance(expected, str)
                or not re.fullmatch(r"[0-9a-f]{64}", expected)):
            _fail("player runtime pipeline seed fingerprint is invalid")
        path = _identity_path(path_value, f"pipeline seed {key}")
        if path.is_symlink() or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            _fail(f"player runtime pipeline seed fingerprint differs from current files: {path_value}")
    expected_seed = seed.get("expected_sha256")
    if not isinstance(expected_seed, str) or expected_seed != seed["materialized"].get("sha256"):
        _fail("player runtime pipeline seed expected digest is inconsistent")
    data_records = [item for item in identity.get("artifacts", [])
                    if isinstance(item, dict) and isinstance(item.get("path"), str)
                    and (item["path"] == "gameplay_public.data" or item["path"].endswith("/gameplay_public.data"))]
    if len(data_records) != 1 or data_records[0].get("sha256") != expected_seed:
        _fail("player runtime data is not bound to the reviewed pipeline seed")


def _records(output: Path, paths: Iterable[str], profile: str = "maintenance") -> list[dict[str, int | str]]:
    records: list[dict[str, int | str]] = []
    for rel in sorted(paths):
        data = _read_output(output, rel, profile)
        records.append({"path": rel, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    return records


def audit(output: Path | str, manifest: Path | str, mode: str | None = None) -> dict[str, Any]:
    """Audit a bundle and return its parsed manifest on success."""
    output = Path(output)
    manifest = Path(manifest)
    try:
        if manifest.resolve().is_relative_to(output.resolve()):
            _fail("manifest must be outside deploy output directory")
    except OSError as exc:
        _fail(f"cannot inspect manifest/output paths: {exc}")
    value = _load_manifest(manifest)
    if value.get("schema") != SCHEMA:
        _fail("unsupported public release manifest schema")
    profile = value.get("profile", "maintenance")
    if profile not in ("maintenance", "player"):
        _fail("manifest profile must be maintenance or player")
    manifest_mode = value.get("mode")
    if manifest_mode not in ("preview", "production"):
        _fail("manifest mode must be preview or production")
    if mode is not None and mode != manifest_mode:
        _fail("requested audit mode does not match manifest")
    mode = manifest_mode
    draft = value.get("draft_preview")
    if draft is not (mode == "preview"):
        _fail("manifest draft_preview flag is inconsistent with mode")
    index_production = value.get("index_production")
    if not isinstance(index_production, bool) or index_production != (mode == "production" and index_production):
        _fail("manifest index_production flag is invalid")
    operator, contact = value.get("operator"), value.get("contact")
    try:
        operator, contact = _config(mode, operator, contact)
    except BuildError as exc:
        _fail(str(exc))
    if value.get("operator") != operator or value.get("contact") != contact:
        _fail("manifest operator/contact values are not canonical")
    runtime = value.get("runtime")
    if profile == "maintenance" and runtime is not None:
        _fail("maintenance manifest must not contain runtime metadata")
    if profile == "player" and runtime is None:
        _fail("player manifest must contain runtime metadata")
    paths, total = _output_files(output, profile)
    expected, css_path, js_path, legal_css_path = _expected_paths(paths, mode, profile, runtime)
    records = _records(output, expected, profile)
    manifest_records = value.get("files")
    if not isinstance(manifest_records, list) or manifest_records != records:
        _fail("manifest file inventory or hash does not match deployed bytes")
    for rel in expected:
        data = _read_output(output, rel, profile)
        if rel.endswith(".html"):
            _validate_html(data, rel, css_path, js_path, operator, contact, mode, legal_css_path, profile)
        elif rel == js_path:
            if profile == "maintenance":
                _validate_js(data)
        elif rel == css_path:
            try:
                css_text = data.decode("utf-8")
            except UnicodeDecodeError as exc:
                _fail(f"stylesheet is not UTF-8: {exc}")
            if re.search(r"url\s*[(]", css_text, re.I):
                _fail("stylesheet contains an external or embedded asset URL")
        elif rel == legal_css_path:
            try:
                css_text = data.decode("utf-8")
            except UnicodeDecodeError as exc:
                _fail(f"stylesheet is not UTF-8: {exc}")
            if re.search(r"url\s*[(]", css_text, re.I):
                _fail("stylesheet contains an external or embedded asset URL")
        elif rel == "_headers":
            _validate_headers(data, mode, index_production, profile)
        elif rel == "_redirects":
            _validate_redirects(data, mode)
        elif rel == "robots.txt":
            if data != _robots(mode, index_production).encode("utf-8"):
                _fail("robots.txt does not match the release indexing policy")
    # The hash names must be derived from the bytes, not merely self-consistent
    # with the manifest.
    css_data = _read_output(output, css_path, profile)
    js_data = _read_output(output, js_path, profile)
    legal_css_data = _read_output(output, legal_css_path, profile)
    if profile == "maintenance":
        if css_path != f"assets/site.{hashlib.sha256(css_data).hexdigest()[:16]}.css":
            _fail("stylesheet filename hash does not match its bytes")
        if js_path != f"assets/site.{hashlib.sha256(js_data).hexdigest()[:16]}.js":
            _fail("script filename hash does not match its bytes")
    # A rewritten manifest is not authority to change an approved page. Bind
    # the candidate to this checkout's reviewed source, including prose.
    source = _validate_source(DEFAULT_SOURCE)
    legal_notice = _read_legal_notice()
    if profile == "maintenance" and (css_data != source["site.css"] or js_data != source["site.js"]):
        _fail("asset bytes differ from the current approved source")
    if legal_css_data != source["site.css"]:
        _fail("legal-page stylesheet bytes differ from the current approved source")
    for rel in HTML_INPUTS:
        html_source = source[rel]
        if profile == "player" and rel == "index.html":
            player_source = _validate_player_source(PLAYER_SOURCE)
            html_source = player_source["index.html"]
        expected_css = legal_css_path if profile == "player" and rel != "index.html" else css_path
        expected_html = _replace_html(html_source, operator, contact,
                                      "/" + expected_css, "/" + js_path, mode)
        if _read_output(output, rel, profile) != expected_html:
            _fail(f"page differs from the current approved source: {rel}")
    if _read_output(output, LEGAL_NOTICE_OUTPUT, profile) != legal_notice:
        _fail("runtime third-party notice differs from the current approved source")
    if profile == "player":
        player_source = _validate_player_source(PLAYER_SOURCE)
        if css_data != player_source["player.css"] or js_data != player_source["player-shell.mjs"]:
            _fail("player entry assets differ from the current approved source")
        _validate_player_runtime(output, runtime, records)
    return {"schema": SCHEMA, "profile": profile, "mode": mode, "files": records, "bytes": total}


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="deployed bundle directory")
    parser.add_argument("--manifest", type=Path, required=True, help="manifest sidecar outside output")
    parser.add_argument("--mode", choices=("preview", "production"))
    parser.add_argument("--profile", choices=("maintenance", "player"))
    return parser


def main(argv: Iterable[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        result = audit(args.output, args.manifest, args.mode)
        if args.profile is not None and args.profile != result["profile"]:
            raise AuditError("requested audit profile does not match manifest")
    except (AuditError, BuildError, OSError, ValueError) as exc:
        print(f"public audit rejected: {exc}", file=sys.stderr)
        return 2
    print(f"public audit passed: {result['mode']} ({len(result['files'])} files, {result['bytes']} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
