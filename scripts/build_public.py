#!/usr/bin/env python3
"""Build the small, static WebMelee public shell.

This packager intentionally has no knowledge of the native/browser runtime.  It
reads exactly eight text templates, hashes the two shared assets, substitutes
the four public configuration placeholders, and writes a fresh deploy tree.
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
import sys
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = ROOT / "web" / "public"
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
    css = result["site.css"].decode("utf-8")
    js = result["site.js"].decode("utf-8")
    if "requestFullscreen" not in js or "fullscreenchange" not in js:
        raise BuildError("site.js must implement the fullscreen control")
    if re.search(r"\b(?:requestAnimationFrame|setTimeout|setInterval)\s*[(]", js):
        raise BuildError("non-fullscreen scheduler code rejected in site.js")
    if not css.strip():
        raise BuildError("site.css may not be empty")
    return result


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


def _headers(mode: str, index_production: bool = False) -> str:
    lines = [
        "/*",
        f"  Content-Security-Policy: {CSP}",
        f"  Permissions-Policy: {PERMISSIONS}",
        "  X-Content-Type-Options: nosniff",
        "  Referrer-Policy: no-referrer",
        "  X-Frame-Options: DENY",
    ]
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


def _file_records(output: Path) -> list[dict[str, int | str]]:
    records: list[dict[str, int | str]] = []
    total = 0
    for path in sorted(output.rglob("*")):
        rel = path.relative_to(output).as_posix()
        if path.is_dir() and not path.is_symlink():
            continue
        if path.is_symlink() or not path.is_file():
            raise BuildError(f"generated output contains non-file entry: {rel}")
        data = path.read_bytes()
        size = _validate_size(path, len(data))
        total += size
        records.append({"path": rel, "size": size, "sha256": hashlib.sha256(data).hexdigest()})
    if total > MAX_TOTAL_BYTES:
        raise BuildError(f"deployed bundle exceeds 25 MiB total limit: {total} bytes")
    return records


def build(
    source: Path | str = DEFAULT_SOURCE,
    output: Path | str | None = None,
    mode: str = "preview",
    operator: str | None = None,
    contact: str | None = None,
    manifest: Path | str | None = None,
    index_production: bool = False,
) -> Path:
    """Build a fresh public bundle and return its output directory."""
    if output is None:
        raise BuildError("--output is required; choose a fresh deploy directory")
    source = Path(source)
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
    source_bytes = _validate_source(source)
    operator, contact = _config(mode, operator, contact)
    output.mkdir()
    try:
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
        for name in HTML_INPUTS:
            _write_new(output / name, _replace_html(source_bytes[name], operator, contact, css_url, js_url, mode))
        _write_new(output / "_headers", _headers(mode, index_production).encode("utf-8"))
        redirects = _redirects(mode)
        if redirects is not None:
            _write_new(output / "_redirects", redirects.encode("utf-8"))
        _write_new(output / "robots.txt", _robots(mode, index_production).encode("utf-8"))
        records = _file_records(output)
        manifest_value = {
            "schema": SCHEMA,
            "mode": mode,
            "draft_preview": mode == "preview",
            "index_production": bool(index_production) if mode == "production" else False,
            "operator": operator,
            "contact": contact,
            "files": records,
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
        )
    except (BuildError, OSError, ValueError) as exc:
        print(f"public build rejected: {exc}", file=sys.stderr)
        return 2
    print(f"public {args.mode} bundle: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
