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
        _config,
        _headers,
        _redirects,
        _robots,
        _validate_magic,
        _validate_source,
        _replace_html,
    )
except ImportError as exc:  # pragma: no cover - only relevant to direct misuse
    raise SystemExit(f"audit_public must run with scripts/ on PYTHONPATH: {exc}") from exc


ASSET_RE = re.compile(r"^assets/site\.([0-9a-f]{16})\.(css|js)$")
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
    allowed = {"schema", "mode", "draft_preview", "index_production", "operator", "contact", "files"}
    if set(value) != allowed:
        _fail("manifest has unexpected or missing top-level fields")
    return value


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate manifest key: {key}")
        result[key] = value
    return result


def _output_files(output: Path) -> tuple[set[str], int]:
    if _is_symlink(output) or not output.is_dir():
        _fail(f"output must be a real directory: {output}")
    paths: set[str] = set()
    total = 0
    for path in sorted(output.rglob("*")):
        rel = path.relative_to(output).as_posix()
        if path.is_dir() and not path.is_symlink():
            if rel != "assets":
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
        if size > MAX_FILE_BYTES:
            _fail(f"file exceeds 25 MiB limit: {rel} ({size} bytes)")
        total += size
        if total > MAX_TOTAL_BYTES:
            _fail("deployed bundle exceeds 25 MiB total limit")
    return paths, total


def _expected_paths(paths: set[str], mode: str) -> tuple[set[str], str, str]:
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
    expected = set(OUTPUT_ROOT_FILES) | {css_path, js_path}
    if mode == "production":
        expected.add("_redirects")
    if paths != expected:
        extras = sorted(paths - expected)
        missing = sorted(expected - paths)
        _fail(f"output paths do not match fixed allowlist; extras={extras}, missing={missing}")
    return expected, css_path, js_path


def _read_output(output: Path, rel: str) -> bytes:
    path = output / rel
    if _is_symlink(path) or not path.is_file():
        _fail(f"output path is not a regular file: {rel}")
    try:
        data = path.read_bytes()
    except OSError as exc:
        _fail(f"cannot read output file {rel}: {exc}")
    try:
        _validate_magic(data, rel)
    except BuildError as exc:
        _fail(str(exc))
    return data


def _validate_html(data: bytes, rel: str, css_path: str, js_path: str, operator: str, contact: str, mode: str) -> None:
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        _fail(f"HTML is not UTF-8: {rel}: {exc}")
    if any(token in text for token in (STYLE_TOKEN, SCRIPT_TOKEN, CONTACT_TOKEN, OPERATOR_TOKEN)):
        _fail(f"unresolved placeholder in {rel}")
    if FORBIDDEN_HTML_CODE.search(text) or re.search(r"(?:href|src)\s*=\s*[\"']\s*javascript:", text, re.I):
        _fail(f"embedded form, navigation script, iframe or event handler in {rel}")
    if f"/assets/{css_path.split('/', 1)[1]}" not in text:
        _fail(f"{rel} does not reference the hashed stylesheet")
    scripts = re.findall(r"<script\b([^>]*)>", text, re.I)
    if rel == "index.html":
        if len(scripts) != 1 or f"/assets/{js_path.split('/', 1)[1]}" not in scripts[0]:
            _fail("index.html does not reference only the hashed fullscreen script")
    elif scripts:
        _fail(f"unexpected script element in {rel}")
    if rel in {"terms.html", "privacy.html", "copyright.html"}:
        if html.escape(operator, quote=True) not in text or html.escape(contact, quote=True) not in text:
            _fail(f"configured operator/contact is missing from {rel}")
    if mode == "preview" and "PUBLIC PREVIEW" not in text.upper():
        _fail(f"preview marker is missing from {rel}")
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


def _validate_headers(data: bytes, mode: str, index_production: bool) -> None:
    expected = _headers(mode, index_production).encode("utf-8")
    if data != expected:
        _fail("_headers does not match the required CSP/Permissions-Policy policy")
    text = data.decode("utf-8")
    if re.search(r"Cross-Origin-(?:Opener|Embedder)-Policy|Cross-Origin-Resource-Policy|COOP|COEP", text, re.I):
        _fail("COOP/COEP header is not permitted in the public shell")
    for directive in ("default-src 'none'", "script-src 'self'", "style-src 'self'", "connect-src 'none'", "worker-src 'none'"):
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


def _records(output: Path, paths: Iterable[str]) -> list[dict[str, int | str]]:
    records: list[dict[str, int | str]] = []
    for rel in sorted(paths):
        data = _read_output(output, rel)
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
    paths, total = _output_files(output)
    expected, css_path, js_path = _expected_paths(paths, mode)
    records = _records(output, expected)
    manifest_records = value.get("files")
    if not isinstance(manifest_records, list) or manifest_records != records:
        _fail("manifest file inventory or hash does not match deployed bytes")
    for rel in expected:
        data = _read_output(output, rel)
        if rel.endswith(".html"):
            _validate_html(data, rel, css_path, js_path, operator, contact, mode)
        elif rel == js_path:
            _validate_js(data)
        elif rel == css_path:
            try:
                css_text = data.decode("utf-8")
            except UnicodeDecodeError as exc:
                _fail(f"stylesheet is not UTF-8: {exc}")
            if re.search(r"url\s*[(]", css_text, re.I):
                _fail("stylesheet contains an external or embedded asset URL")
        elif rel == "_headers":
            _validate_headers(data, mode, index_production)
        elif rel == "_redirects":
            _validate_redirects(data, mode)
        elif rel == "robots.txt":
            if data != _robots(mode, index_production).encode("utf-8"):
                _fail("robots.txt does not match the release indexing policy")
    # The hash names must be derived from the bytes, not merely self-consistent
    # with the manifest.
    css_data = _read_output(output, css_path)
    js_data = _read_output(output, js_path)
    if css_path != f"assets/site.{hashlib.sha256(css_data).hexdigest()[:16]}.css":
        _fail("stylesheet filename hash does not match its bytes")
    if js_path != f"assets/site.{hashlib.sha256(js_data).hexdigest()[:16]}.js":
        _fail("script filename hash does not match its bytes")
    # A rewritten manifest is not authority to change an approved page. Bind
    # the candidate to this checkout's reviewed source, including prose.
    source = _validate_source(DEFAULT_SOURCE)
    if css_data != source["site.css"] or js_data != source["site.js"]:
        _fail("asset bytes differ from the current approved source")
    for rel in HTML_INPUTS:
        expected_html = _replace_html(source[rel], operator, contact,
                                      "/" + css_path, "/" + js_path, mode)
        if _read_output(output, rel) != expected_html:
            _fail(f"page differs from the current approved source: {rel}")
    return {"schema": SCHEMA, "mode": mode, "files": records, "bytes": total}


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="deployed bundle directory")
    parser.add_argument("--manifest", type=Path, required=True, help="manifest sidecar outside output")
    parser.add_argument("--mode", choices=("preview", "production"))
    return parser


def main(argv: Iterable[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        result = audit(args.output, args.manifest, args.mode)
    except (AuditError, BuildError, OSError, ValueError) as exc:
        print(f"public audit rejected: {exc}", file=sys.stderr)
        return 2
    print(f"public audit passed: {result['mode']} ({len(result['files'])} files, {result['bytes']} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
