#!/usr/bin/env python3
"""Audit a ccache 4.9 result cache without executing cached output.

The decoder is ccache itself: ``--inspect`` reads result/manifest metadata and
``--extract-result`` writes decoded members into a private temporary directory.
The cache's configuration is never used. This is a bounded review tool, not a
ccache format reimplementation.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import selectors
import stat
import subprocess
import sys
import tempfile
import time
from collections import Counter
from dataclasses import dataclass
from typing import Iterable

try:
    import resource
except ImportError:  # pragma: no cover - Windows has no resource module.
    resource = None

try:
    from scripts import check_repository_content as guard
except ImportError:  # Executed directly with scripts/ as sys.path[0].
    import check_repository_content as guard


SCHEMA = "melee-web-compiler-cache-audit-v1"
SCANNER_VERSION = "1"
MAX_ENTRY_BYTES = 64 * 1024 * 1024
MAX_COMMAND_OUTPUT_BYTES = 4 * 1024 * 1024
MAX_DECODED_FILE_BYTES = 16 * 1024 * 1024
MAX_DECODED_TOTAL_BYTES = 64 * 1024 * 1024
MAX_DECODED_FILES = 32
MAX_METADATA_FILE_BYTES = 1024 * 1024
COMMAND_TIMEOUT_SECONDS = 15
RESOURCE_MEMORY_BYTES = 256 * 1024 * 1024
RESOURCE_FILE_BYTES = MAX_DECODED_TOTAL_BYTES
HEX = re.compile(r"^[0-9a-f]$")
DIGEST = re.compile(r"^[0-9a-f]+$")
ASSET_INPUT = re.compile(rb"(?i)(?:assets-local|captures)[/\\][^\s\"'<>]+")
WASM_MAGIC = b"\x00asm"
ELF_MAGIC = b"\x7fELF"
RUNNER_USERS = {"runner", "runneradmin", "actions", "github", "github-actions"}


@dataclass
class Finding:
    rule: str
    severity: str
    identity: bytes | str | None = None
    relative_path: str | None = None

    def as_dict(self) -> dict:
        result = {"rule": self.rule, "severity": self.severity}
        if self.relative_path is not None:
            result["path_sha256"] = hashlib.sha256(self.relative_path.encode("utf-8")).hexdigest()
        if self.identity is not None:
            value = self.identity if isinstance(self.identity, bytes) else self.identity.encode("utf-8")
            result["identity_sha256"] = hashlib.sha256(value).hexdigest()
        return result


@dataclass
class CommandResult:
    returncode: int | None
    stdout: bytes
    stderr: bytes
    timed_out: bool = False
    output_limited: bool = False


def _resource_limits() -> None:
    """Apply limits in the ccache child before it reads or decodes an entry."""
    if resource is None:
        return
    limits = (
        (resource.RLIMIT_CPU, (COMMAND_TIMEOUT_SECONDS, COMMAND_TIMEOUT_SECONDS + 1)),
        (resource.RLIMIT_AS, (RESOURCE_MEMORY_BYTES, RESOURCE_MEMORY_BYTES)),
        (resource.RLIMIT_FSIZE, (RESOURCE_FILE_BYTES, RESOURCE_FILE_BYTES)),
        (resource.RLIMIT_NOFILE, (64, 64)),
    )
    for kind, values in limits:
        try:
            resource.setrlimit(kind, values)
        except (OSError, ValueError):
            pass


def _kill_process(process: subprocess.Popen) -> None:
    try:
        process.kill()
    except ProcessLookupError:
        pass


def _run_bounded(command: list[str], *, cwd: Path, env: dict[str, str]) -> CommandResult:
    process = subprocess.Popen(
        command,
        cwd=cwd,
        env=env,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        preexec_fn=_resource_limits if resource is not None else None,
        start_new_session=True,
    )
    assert process.stdout is not None and process.stderr is not None
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ, "stdout")
    selector.register(process.stderr, selectors.EVENT_READ, "stderr")
    chunks = {"stdout": bytearray(), "stderr": bytearray()}
    timed_out = False
    output_limited = False
    deadline = time.monotonic() + COMMAND_TIMEOUT_SECONDS
    while selector.get_map():
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            timed_out = True
            _kill_process(process)
            break
        for key, _ in selector.select(remaining):
            data = os.read(key.fileobj.fileno(), 64 * 1024)
            if not data:
                selector.unregister(key.fileobj)
                continue
            destination = chunks[key.data]
            destination.extend(data)
            if len(destination) > MAX_COMMAND_OUTPUT_BYTES:
                output_limited = True
                _kill_process(process)
                break
        if timed_out or output_limited:
            break
    if timed_out or output_limited:
        for stream in (process.stdout, process.stderr):
            try:
                selector.unregister(stream)
            except KeyError:
                pass
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            _kill_process(process)
            process.wait(timeout=2)
    else:
        process.wait(timeout=2)
    selector.close()
    process.stdout.close()
    process.stderr.close()
    return CommandResult(
        process.returncode,
        bytes(chunks["stdout"]),
        bytes(chunks["stderr"]),
        timed_out=timed_out,
        output_limited=output_limited,
    )


def _safe_hash_file(path: Path, *, limit: int = MAX_ENTRY_BYTES) -> tuple[str, int, bool]:
    digest = hashlib.sha256()
    total = 0
    with path.open("rb") as stream:
        while True:
            block = stream.read(1024 * 1024)
            if not block:
                break
            total += len(block)
            if total > limit:
                return digest.hexdigest(), total, True
            digest.update(block)
    return digest.hexdigest(), total, False


def _safe_relative(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def _finding(rule: str, severity: str, *, identity=None, relative_path=None) -> Finding:
    return Finding(rule, severity, identity=identity, relative_path=relative_path)


def _scan_bytes(
    data: bytes,
    *,
    context: str,
    relative_path: str,
    findings: list[Finding],
    counters: Counter,
) -> None:
    for rule, pattern in guard.SECRET_RULES.items():
        for match in pattern.finditer(data):
            token = match.group(1) if rule == "sensitive_assignment" else match.group()
            findings.append(_finding(rule, "review", identity=token, relative_path=relative_path))
            counters[rule] += 1
    for match in guard.PERSONAL_PATH.finditer(data):
        # The shared guard matches the complete path prefix and has no capture
        # group. Extract only the username for runner-path classification.
        user = match.group().split(b"/")[2].decode("ascii", errors="replace").lower()
        if user in RUNNER_USERS:
            counters["runner_path"] += 1
        else:
            findings.append(_finding("personal_path", "review", identity=match.group(), relative_path=relative_path))
            counters["nonrunner_personal_path"] += 1
    if context in {"metadata", "depfile"}:
        for match in ASSET_INPUT.finditer(data):
            findings.append(_finding("asset_or_capture_input", "review", identity=match.group(), relative_path=relative_path))
            counters["asset_or_capture_input"] += 1


def _read_bounded(path: Path, *, limit: int = MAX_DECODED_FILE_BYTES) -> tuple[bytes, bool]:
    size = path.stat().st_size
    if size > limit:
        return b"", True
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    return data[:limit], len(data) > limit


def _read_uleb(data: bytes, offset: int) -> tuple[int, int] | None:
    value = 0
    shift = 0
    while offset < len(data) and shift <= 63:
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return value, offset
        shift += 7
    return None


def _wasm_type(data: bytes) -> str | None:
    if not data.startswith(WASM_MAGIC):
        return None
    if len(data) < 8 or data[4:8] != b"\x01\x00\x00\x00":
        return "wasm_invalid"
    offset = 8
    linking = False
    while offset < len(data):
        section_id = data[offset]
        offset += 1
        if section_id > 12:
            return "wasm_invalid"
        parsed = _read_uleb(data, offset)
        if parsed is None:
            return "wasm_invalid"
        section_size, offset = parsed
        end = offset + section_size
        if end > len(data):
            return "wasm_invalid"
        if section_id == 0:
            name = _read_uleb(data, offset)
            if name is None:
                return "wasm_invalid"
            name_size, name_offset = name
            if name_offset + name_size > end:
                return "wasm_invalid"
            if data[name_offset:name_offset + name_size] == b"linking":
                linking = True
        offset = end
    return "wasm_relocatable" if linking else "wasm_nonrelocatable"


def _elf_type(data: bytes) -> str | None:
    if not data.startswith(ELF_MAGIC):
        return None
    if len(data) < 18 or data[4] not in (1, 2) or data[5] not in (1, 2):
        return "elf_invalid"
    endian = "little" if data[5] == 1 else "big"
    e_type = int.from_bytes(data[16:18], endian)
    return "elf_relocatable" if e_type == 1 else "elf_nonrelocatable"


def _output_type(path: Path, data: bytes) -> str:
    suffix = path.name.removeprefix("ccache-result")
    if suffix == ".o":
        return _wasm_type(data) or _elf_type(data) or "object_unrecognized"
    if suffix == ".d":
        return "depfile"
    if suffix in {".stderr", ".stdout", ".dia"}:
        return "diagnostic"
    return "unexpected"


def _cache_env(cache_dir: Path, temporary_dir: Path) -> dict[str, str]:
    env = {key: value for key, value in os.environ.items() if not key.startswith("CCACHE_")}
    env.update(
        CCACHE_CONFIGPATH=os.devnull,
        CCACHE_DIR=str(cache_dir),
        CCACHE_TEMPDIR=str(temporary_dir),
    )
    return env


def _entry_kind(path: Path, fanout_parts: tuple[str, ...]) -> str | None:
    """Recognize a v4 local filename after 2, 3, or 4 fanout levels.

    Ccache's 20-byte digest is rendered as 40 lowercase hex characters. The
    fanout characters are directory names, so the filename contains the
    remaining digest characters plus the R/M type suffix.
    """
    name = path.name
    if not name or name[-1] not in "RM":
        return None
    digest_part = name[:-1]
    if len(digest_part) != 40 - len(fanout_parts) or not digest_part:
        return None
    if not all(HEX.fullmatch(part) for part in fanout_parts) or not DIGEST.fullmatch(digest_part):
        return None
    return "result" if name[-1] == "R" else "manifest"


def _is_hex_dir(part: str) -> bool:
    return len(part) == 1 and HEX.fullmatch(part) is not None


def _scan_metadata_file(
    path: Path,
    *,
    cache_dir: Path,
    findings: list[Finding],
    counters: Counter,
    metadata_files: list[dict],
) -> None:
    relative_path = _safe_relative(path, cache_dir)
    digest, size, exceeded = _safe_hash_file(path, limit=MAX_METADATA_FILE_BYTES)
    metadata_record = {"relative_path": relative_path, "size": size}
    if exceeded:
        metadata_record["hash_status"] = "unavailable"
    else:
        metadata_record["sha256"] = digest
    metadata_files.append(metadata_record)
    if exceeded:
        findings.append(_finding("metadata_file_size_limit", "incomplete", identity=relative_path))
        return
    data, exceeded = _read_bounded(path, limit=MAX_METADATA_FILE_BYTES)
    if exceeded:
        findings.append(_finding("metadata_file_size_limit", "incomplete", identity=relative_path))
        return
    _scan_bytes(data, context="metadata", relative_path=relative_path, findings=findings, counters=counters)


def _classify_tree(
    cache_dir: Path,
    findings: list[Finding],
    counters: Counter,
    metadata_files: list[dict],
) -> list[tuple[Path, str]]:
    entries: list[tuple[Path, str]] = []
    for current, dirnames, filenames in os.walk(cache_dir, topdown=True, followlinks=False):
        current_path = Path(current)
        rel_current = PurePosixPath(current_path.relative_to(cache_dir).as_posix())
        if rel_current == PurePosixPath("."):
            rel_parts: tuple[str, ...] = ()
        else:
            rel_parts = rel_current.parts
        kept_dirs = []
        for name in sorted(dirnames):
            path = current_path / name
            rel = path.relative_to(cache_dir).as_posix()
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode):
                findings.append(_finding("symlink_entry", "incomplete", identity=rel))
                continue
            if not stat.S_ISDIR(mode):
                findings.append(_finding("non_directory_entry", "incomplete", identity=rel))
                continue
            parts = tuple(path.relative_to(cache_dir).parts)
            allowed = name in {"tmp", "lock"} and len(parts) == 1
            allowed = allowed or (1 <= len(parts) <= 4 and all(_is_hex_dir(part) for part in parts))
            if not allowed:
                findings.append(_finding("unknown_directory", "incomplete", identity=rel))
                continue
            kept_dirs.append(name)
        dirnames[:] = kept_dirs
        for name in sorted(filenames):
            path = current_path / name
            rel = path.relative_to(cache_dir).as_posix()
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode):
                findings.append(_finding("symlink_entry", "incomplete", identity=rel))
                continue
            if not stat.S_ISREG(mode):
                findings.append(_finding("non_regular_entry", "incomplete", identity=rel))
                continue
            parts = tuple(path.relative_to(cache_dir).parts)
            entry_parent_depth = len(parts) - 1
            fanout_parts = parts[:-1]
            kind = _entry_kind(path, fanout_parts)
            valid_entry = (
                kind is not None
                and 2 <= entry_parent_depth <= 4
                and all(_is_hex_dir(part) for part in parts[:-1])
            )
            if valid_entry:
                if path.stat().st_size == 0:
                    findings.append(_finding("empty_cache_entry", "incomplete", identity=rel))
                elif path.stat().st_size > MAX_ENTRY_BYTES:
                    findings.append(_finding("cache_entry_size_limit", "incomplete", identity=rel))
                else:
                    entries.append((path, kind))
                continue
            if name in {"CACHEDIR.TAG", "stats"} and all(_is_hex_dir(part) for part in parts[:-1]):
                _scan_metadata_file(
                    path,
                    cache_dir=cache_dir,
                    findings=findings,
                    counters=counters,
                    metadata_files=metadata_files,
                )
                continue
            if name == "ccache.conf" and parts == ("ccache.conf",):
                _scan_metadata_file(
                    path,
                    cache_dir=cache_dir,
                    findings=findings,
                    counters=counters,
                    metadata_files=metadata_files,
                )
                continue
            findings.append(_finding("unknown_cache_file", "incomplete", identity=rel))
    return entries


def _version(ccache: str, cache_dir: Path, temporary_dir: Path) -> tuple[str | None, Finding | None]:
    result = _run_bounded(
        [ccache, "--config-path", os.devnull, "--version"],
        cwd=temporary_dir,
        env=_cache_env(cache_dir, temporary_dir),
    )
    if result.timed_out:
        return None, _finding("ccache_version_timeout", "incomplete")
    if result.output_limited:
        return None, _finding("ccache_version_output_limit", "incomplete")
    text = result.stdout.decode("utf-8", errors="replace")
    match = re.search(r"ccache version ([0-9]+\.[0-9]+(?:\.[0-9]+)?)", text)
    if result.returncode != 0 or match is None:
        return None, _finding("ccache_version_unavailable", "incomplete")
    version = match.group(1)
    if not version.startswith("4.9"):
        return version, _finding("unsupported_ccache_version", "incomplete", identity=version)
    return version, None


def _inspect_entry(
    path: Path,
    kind: str,
    *,
    cache_dir: Path,
    temporary_dir: Path,
    ccache: str,
    findings: list[Finding],
    counters: Counter,
    decoded_type_counts: Counter,
) -> dict:
    relative_path = _safe_relative(path, cache_dir)
    entry_hash, entry_size, exceeded = _safe_hash_file(path)
    record = {
        "kind": kind,
        "relative_path": relative_path,
        "entry_sha256": entry_hash,
        "entry_size": entry_size,
        "inspect": {},
    }
    if exceeded:
        findings.append(_finding("cache_entry_size_limit", "incomplete", identity=relative_path))
        return record
    inspect_result = _run_bounded(
        [ccache, "--config-path", os.devnull, "--inspect", str(path)],
        cwd=temporary_dir,
        env=_cache_env(cache_dir, temporary_dir),
    )
    metadata = inspect_result.stdout
    record["inspect"] = {
        "returncode": inspect_result.returncode,
        "timed_out": inspect_result.timed_out,
        "output_limited": inspect_result.output_limited,
        "output_bytes": len(metadata),
        "output_sha256": hashlib.sha256(metadata).hexdigest(),
    }
    _scan_bytes(metadata, context="metadata", relative_path=relative_path, findings=findings, counters=counters)
    if inspect_result.timed_out:
        findings.append(_finding("inspect_timeout", "incomplete", identity=relative_path))
        return record
    if inspect_result.output_limited:
        findings.append(_finding("inspect_output_limit", "incomplete", identity=relative_path))
        return record
    if inspect_result.returncode != 0 or not metadata:
        findings.append(_finding("inspect_failed", "incomplete", identity=relative_path))
        return record
    if kind != "result":
        return record
    # Keep each decoder invocation in its own directory. This prevents files
    # from one result from being mistaken for members of the next result.
    extract_dir = Path(tempfile.mkdtemp(prefix="extract-", dir=temporary_dir))
    extract_result = _run_bounded(
        [ccache, "--config-path", os.devnull, "--extract-result", str(path)],
        cwd=extract_dir,
        env=_cache_env(cache_dir, extract_dir),
    )
    if extract_result.timed_out:
        findings.append(_finding("extract_timeout", "incomplete", identity=relative_path))
        return record
    if extract_result.output_limited:
        findings.append(_finding("extract_output_limit", "incomplete", identity=relative_path))
        return record
    if extract_result.returncode != 0:
        findings.append(_finding("extract_failed", "incomplete", identity=relative_path))
        return record
    decoded = []
    files = sorted(extract_dir.iterdir())
    total_bytes = 0
    if not files:
        findings.append(_finding("empty_decode", "incomplete", identity=relative_path))
    if len(files) > MAX_DECODED_FILES:
        findings.append(_finding("decoded_file_count_limit", "incomplete", identity=relative_path))
    for output in files[:MAX_DECODED_FILES]:
        mode = output.lstat().st_mode
        if not stat.S_ISREG(mode):
            findings.append(_finding("decoded_non_regular", "incomplete", identity=output.name))
            continue
        data, exceeded = _read_bounded(output)
        size = output.stat().st_size
        total_bytes += size
        member = {"name": output.name, "size": size}
        if exceeded:
            member["hash_status"] = "unavailable"
            findings.append(_finding("decoded_file_size_limit", "incomplete", identity=output.name))
            decoded.append(member)
            continue
        member["sha256"] = hashlib.sha256(data).hexdigest()
        output_type = _output_type(output, data)
        member["type"] = output_type
        decoded_type_counts[output_type] += 1
        decoded.append(member)
        context = "depfile" if output_type == "depfile" else "diagnostic" if output_type == "diagnostic" else "object"
        _scan_bytes(data, context=context, relative_path=relative_path, findings=findings, counters=counters)
        if output_type in {"unexpected", "wasm_invalid", "wasm_nonrelocatable", "object_unrecognized", "elf_invalid", "elf_nonrelocatable"}:
            findings.append(_finding("unexpected_decoded_member_type", "review", identity=output_type, relative_path=relative_path))
    if total_bytes > MAX_DECODED_TOTAL_BYTES:
        findings.append(_finding("decoded_total_size_limit", "incomplete", identity=relative_path))
    record["decoded"] = decoded
    return record


def audit(cache_dir: str | Path, *, ccache: str | Path = "ccache") -> dict:
    """Audit a ccache cache and return a sanitized JSON-compatible report."""
    cache_input = Path(cache_dir)
    # Check the user-supplied path before resolving it. Resolving first would
    # turn a symlinked cache root into an apparently safe directory.
    cache_is_symlink = False
    try:
        cache_is_symlink = cache_input.is_symlink()
    except OSError:
        cache_is_symlink = True
    cache_path = cache_input.resolve(strict=False)
    findings: list[Finding] = []
    counters: Counter = Counter()
    decoded_type_counts: Counter = Counter()
    report = {
        "schema": SCHEMA,
        "scanner_version": SCANNER_VERSION,
        "scanner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "cache_dir_sha256": hashlib.sha256(str(cache_path).encode("utf-8")).hexdigest(),
        "bounds": {
            "max_entry_bytes": MAX_ENTRY_BYTES,
            "max_command_output_bytes": MAX_COMMAND_OUTPUT_BYTES,
            "max_decoded_file_bytes": MAX_DECODED_FILE_BYTES,
            "max_decoded_total_bytes": MAX_DECODED_TOTAL_BYTES,
            "max_decoded_files": MAX_DECODED_FILES,
            "max_metadata_file_bytes": MAX_METADATA_FILE_BYTES,
            "command_timeout_seconds": COMMAND_TIMEOUT_SECONDS,
            "resource_memory_bytes": RESOURCE_MEMORY_BYTES,
            "resource_file_bytes": RESOURCE_FILE_BYTES,
        },
        "ccache": {"name": Path(str(ccache)).name},
        "cache": {"regular_files": 0, "entries": 0, "result_entries": 0, "manifest_entries": 0},
        "metadata_files": [],
        "entries": [],
        "findings": [],
        "scan_counts": {},
        "decoded_type_counts": {},
        "notes": [
            "Ccache was invoked with CCACHE_CONFIGPATH=/dev/null and no cache configuration was trusted.",
            "Cached output was decoded only into a private temporary directory and never executed.",
        ],
    }
    try:
        if cache_is_symlink or not cache_path.exists() or not cache_path.is_dir():
            findings.append(_finding("missing_or_unsafe_cache_dir", "incomplete"))
            report["status"] = "incomplete"
            report["findings"] = [item.as_dict() for item in findings]
            return report
        with tempfile.TemporaryDirectory(prefix="melee-ccache-audit-") as temporary:
            temporary_path = Path(temporary)
            version, version_finding = _version(str(ccache), cache_path, temporary_path)
            report["ccache"]["version"] = version
            if version_finding is not None:
                findings.append(version_finding)
            entries = _classify_tree(cache_path, findings, counters, report["metadata_files"])
            report["cache"]["regular_files"] = sum(1 for path in cache_path.rglob("*") if path.is_file())
            report["cache"]["entries"] = len(entries)
            report["cache"]["result_entries"] = sum(kind == "result" for _, kind in entries)
            report["cache"]["manifest_entries"] = sum(kind == "manifest" for _, kind in entries)
            if not entries:
                findings.append(_finding("empty_cache", "incomplete"))
            if version_finding is None:
                for path, kind in entries:
                    record = _inspect_entry(
                        path,
                        kind,
                        cache_dir=cache_path,
                        temporary_dir=temporary_path,
                        ccache=str(ccache),
                        findings=findings,
                        counters=counters,
                        decoded_type_counts=decoded_type_counts,
                    )
                    report["entries"].append(record)
    except (OSError, subprocess.SubprocessError, ValueError) as error:
        findings.append(_finding("audit_incomplete", "incomplete", identity=type(error).__name__))
    report["scan_counts"] = dict(sorted(counters.items()))
    report["decoded_type_counts"] = dict(sorted(decoded_type_counts.items()))
    report["findings"] = [item.as_dict() for item in findings]
    report["status"] = "incomplete" if any(item.severity == "incomplete" for item in findings) else "review" if findings else "passed"
    return report


def _write_exclusive(path: Path, report: dict) -> None:
    encoded = (json.dumps(report, indent=2, sort_keys=True) + "\n").encode("utf-8")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    descriptor = os.open(path, flags, 0o600)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(encoded)
    except BaseException:
        try:
            os.close(descriptor)
        except OSError:
            pass
        raise


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--ccache", default="ccache")
    args = parser.parse_args(argv)
    report = audit(args.cache_dir, ccache=args.ccache)
    try:
        _write_exclusive(args.output, report)
    except OSError:
        return 2
    # The full receipt is in the requested output file. Keep CI logs to a
    # bounded summary so no metadata or path-bearing detail is echoed there.
    print(json.dumps({
        "status": report["status"],
        "entries": report["cache"]["entries"],
        "regular_files": report["cache"]["regular_files"],
        "finding_count": len(report["findings"]),
        "decoded_type_counts": report["decoded_type_counts"],
    }, sort_keys=True))
    return {"passed": 0, "review": 1, "incomplete": 2}[report["status"]]


if __name__ == "__main__":
    raise SystemExit(main())
